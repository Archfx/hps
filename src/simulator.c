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

        // Assign work
        for (int e = 0; e < cfg->num_engines; e++) {
            if (engines[e].job_id >= 0) continue;

            int j = pick_job(cfg, jobs, n_jobs, now_us);
            if (j < 0) continue;

            if (!jobs[j].started) {
                jobs[j].started = 1;
                jobs[j].start_time_us = now_us;
            }

            double t = bootstrap_time_us(cfg, &jobs[j]);
            engines[e].job_id = j;
            engines[e].busy_until_us = now_us + t + cfg->ctx_switch_overhead_us;
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

    free(jobs);
    free(engines);

    return s;
}
