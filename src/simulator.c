#include <stdio.h>
#include <stdlib.h>
#include <float.h>
#include "../includes/simulator.h"
#include "../includes/scheduler.h"

SimStats run_simulation(const HwConfig *cfg,
                        TfheJob *jobs_original,
                        int n_jobs,
                        SchedulerFn pick_job)
{
    TfheJob *jobs = malloc(n_jobs * sizeof(TfheJob));
    for (int i = 0; i < n_jobs; i++) {
        jobs[i] = jobs_original[i];
    }

    Engine *engines = malloc(cfg->num_engines * sizeof(Engine));
    for (int e = 0; e < cfg->num_engines; e++) {
        engines[e].job_id = -1;
        engines[e].busy_until_us = 0;
    }

    double now_us = 0;
    double total_engine_busy_us = 0;
    int jobs_finished = 0;

    int log_picks = getenv("HPS_LOG_PICKS") != NULL;
    const char *sched_label = "scheduler";
    if (log_picks) {
        if (pick_job == (SchedulerFn)pick_job_hps) sched_label = "HPS";
        else if (pick_job == (SchedulerFn)pick_job_fifo) sched_label = "FIFO";
    }

    while (jobs_finished < n_jobs) {
        double next_event = DBL_MAX;

        // Next engine completion
        for (int e = 0; e < cfg->num_engines; e++) {
            if (engines[e].job_id >= 0 && engines[e].busy_until_us > now_us)
                if (engines[e].busy_until_us < next_event)
                    next_event = engines[e].busy_until_us;
        }

        // Next job arrival
        for (int j = 0; j < n_jobs; j++) {
            if (jobs[j].arrival_time_us > now_us &&
                jobs[j].arrival_time_us < next_event)
                next_event = jobs[j].arrival_time_us;
        }

        if (next_event == DBL_MAX)
            break;

        double delta = next_event - now_us;

        // Utilization
        int busy_eng = 0;
        for (int e = 0; e < cfg->num_engines; e++)
            if (engines[e].job_id >= 0) busy_eng++;

        total_engine_busy_us += delta * busy_eng;
        now_us = next_event;

        // Complete bootstrap operations
        for (int e = 0; e < cfg->num_engines; e++) {
            if (engines[e].job_id >= 0 &&
                engines[e].busy_until_us <= now_us) {

                int j = engines[e].job_id;
                jobs[j].remaining_bootstraps--;

                if (jobs[j].remaining_bootstraps == 0) {
                    jobs[j].completion_time_us = now_us;
                    jobs_finished++;
                }
                engines[e].job_id = -1;
            }
        }

        // Assign work in batches: pick a job (batch unit) and allocate up to
        // cfg->batch_size bootstraps across available idle engines.
        int idle_engines = 0;
        for (int e = 0; e < cfg->num_engines; e++) if (engines[e].job_id < 0) idle_engines++;

        // While there are idle engines, pick a job and assign up to batch_size
        while (idle_engines > 0) {
            int j = pick_job(cfg, jobs, n_jobs, now_us);
            if (j < 0) break;

            if (log_picks) {
                printf("[%s] pick at %.0f us -> job %d (rem=%d)\n",
                       sched_label, now_us, j, jobs[j].remaining_bootstraps);
            }

            if (!jobs[j].started) {
                jobs[j].started = 1;
                jobs[j].start_time_us = now_us;
            }

            int batch_len = 1;
            if (cfg && cfg->batch_size > 1)
                batch_len = cfg->batch_size < jobs[j].remaining_bootstraps ? cfg->batch_size : jobs[j].remaining_bootstraps;

            if (batch_len > idle_engines) batch_len = idle_engines;

            double t = bootstrap_time_us(cfg, &jobs[j]);

            // assign 'batch_len' idle engines to job j
            for (int e = 0; e < cfg->num_engines && batch_len > 0; e++) {
                if (engines[e].job_id < 0) {
                    engines[e].job_id = j;
                    engines[e].busy_until_us = now_us + t + cfg->ctx_switch_overhead_us;
                    batch_len--;
                    idle_engines--;
                }
            }
        }
    }

    // Compute stats
    double first_arrival = jobs[0].arrival_time_us;
    double last_finish = 0;

    for (int i = 0; i < n_jobs; i++)
        if (jobs[i].completion_time_us > last_finish)
            last_finish = jobs[i].completion_time_us;

    SimStats s;
    s.makespan_us = last_finish - first_arrival;

    double sum_comp = 0, sum_slow = 0;
    for (int i = 0; i < n_jobs; i++) {
        double resp = jobs[i].completion_time_us - jobs[i].arrival_time_us;
        sum_comp += resp;

        double svc = jobs[i].num_bootstraps * bootstrap_time_us(cfg, &jobs[i]);
        if (svc < 1) svc = 1;
        sum_slow += resp / svc;
    }

    s.avg_completion_time_us = sum_comp / n_jobs;
    s.avg_slowdown = sum_slow / n_jobs;
    s.engine_utilization = total_engine_busy_us /
                           (s.makespan_us * cfg->num_engines);

    // Compute fairness: Jain's fairness index over per-tenant average slowdown.
    // For each tenant present, compute avg slowdown (sum_slowdown_tenant / count).
    // Jain = ( (sum x_i)^2 ) / (n * sum x_i^2)
    // If only one tenant present, fairness = 1.0.
    // First find max tenant id to size arrays (tenant ids are small ints in workloads).
    int max_tenant = -1;
    for (int i = 0; i < n_jobs; i++) if (jobs[i].tenant_id > max_tenant) max_tenant = jobs[i].tenant_id;

    double fairness = 1.0;
    if (max_tenant >= 0) {
        int tcount = max_tenant + 1;
        double *sum_slow_t = malloc(tcount * sizeof(double));
        int *cnt_t = malloc(tcount * sizeof(int));
        if (sum_slow_t && cnt_t) {
            for (int t = 0; t < tcount; t++) {
                sum_slow_t[t] = 0.0;
                cnt_t[t] = 0;
            }

            for (int i = 0; i < n_jobs; i++) {
                double resp = jobs[i].completion_time_us - jobs[i].arrival_time_us;
                double svc = jobs[i].num_bootstraps * bootstrap_time_us(cfg, &jobs[i]);
                if (svc < 1) svc = 1;
                double slow = resp / svc;
                int t = jobs[i].tenant_id;
                if (t >= 0 && t < tcount) {
                    sum_slow_t[t] += slow;
                    cnt_t[t] += 1;
                }
            }

            // collect per-tenant averages for tenants with >0 jobs
            double sum_x = 0.0;
            double sum_x2 = 0.0;
            int n_present = 0;
            for (int t = 0; t < tcount; t++) {
                if (cnt_t[t] > 0) {
                    double avg = sum_slow_t[t] / (double)cnt_t[t];
                    sum_x += avg;
                    sum_x2 += avg * avg;
                    n_present++;
                }
            }

            if (n_present <= 1) fairness = 1.0;
            else {
                double numer = sum_x * sum_x;
                double denom = (double)n_present * sum_x2;
                if (denom > 0.0) fairness = numer / denom;
                else fairness = 1.0;
            }

            free(sum_slow_t);
            free(cnt_t);
        } else {
            fairness = 1.0;
            if (sum_slow_t) free(sum_slow_t);
            if (cnt_t) free(cnt_t);
        }
    }

    s.fairness = fairness;

    free(jobs);
    free(engines);

    return s;
}
