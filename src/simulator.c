#include <stdio.h>
#include <stdlib.h>
#include <float.h>
#include <string.h>
#include "../includes/simulator.h"
#include "../includes/scheduler.h"

typedef struct {
    int job_id; // job index
    double remaining_bits; // remaining transfer size in bits
} Transfer;

// File-scope testing knobs
static double g_pcie_scale = 1.0; // multiply pcie bandwidth by this
static double g_pcie_cap_mb = 0.0; // cap per-transfer size in MB (0 = no cap)
static int g_show_progress = 0;    // whether to print progress updates
static char *g_csv_prefix = NULL;

void simulator_set_pcie_scale(double scale) {
    if (scale > 0.0) g_pcie_scale = scale;
}

void simulator_set_pcie_cap_mb(double cap_mb) {
    if (cap_mb >= 0.0) g_pcie_cap_mb = cap_mb;
}

void simulator_set_show_progress(int show) {
    g_show_progress = show ? 1 : 0;
}

void simulator_set_csv_prefix(const char *prefix) {
    if (g_csv_prefix) free(g_csv_prefix);
    if (prefix) g_csv_prefix = strdup(prefix);
    else g_csv_prefix = NULL;
}

SimStats run_simulation(const HwConfig *cfg,
                        TfheJob *jobs_original,
                        int n_jobs,
                        SchedulerFn pick_job)
{
    TfheJob *jobs = malloc(n_jobs * sizeof(TfheJob));
    for (int i = 0; i < n_jobs; i++) {
        jobs[i] = jobs_original[i];
        // initialize PCIe transfer state: if no PCIe BW configured, treat as already transferred
        if (cfg->pcie_bandwidth_gbps <= 0.0) jobs[i].pcie_transferred = 1;
        else jobs[i].pcie_transferred = 0;
    }

    // PCIe transfer tracking (contention-aware)
    Transfer *transfers = malloc(n_jobs * sizeof(Transfer));
    for (int t = 0; t < n_jobs; t++) {
        transfers[t].job_id = -1;
        transfers[t].remaining_bits = 0.0;
    }

    Engine *engines = malloc(cfg->num_engines * sizeof(Engine));
    for (int e = 0; e < cfg->num_engines; e++) {
        engines[e].job_id = -1;
        engines[e].busy_until_us = 0;
    }

    double now_us = 0;
    double total_engine_busy_us = 0;
    int jobs_finished = 0;

    /* using file-scope knobs g_pcie_scale and g_pcie_cap_mb */

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

        // Next transfer completion (contention-aware)
        int active_transfers = 0;
        for (int t = 0; t < n_jobs; t++) if (transfers[t].job_id >= 0) active_transfers++;
        if (active_transfers > 0 && cfg->pcie_bandwidth_gbps > 0.0) {
            double eff_pcie_gbps = cfg->pcie_bandwidth_gbps * g_pcie_scale;
            double bits_per_us_per_transfer = (eff_pcie_gbps * 1e3) / (double)active_transfers; // bits/us
            for (int t = 0; t < n_jobs; t++) {
                if (transfers[t].job_id >= 0) {
                    double time_to_finish = transfers[t].remaining_bits / bits_per_us_per_transfer;
                    double candidate = now_us + time_to_finish;
                    if (candidate < next_event) next_event = candidate;
                }
            }
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

        // Update PCIe transfers progress
        if (cfg->pcie_bandwidth_gbps > 0.0) {
            int active_transfers2 = 0;
            for (int t = 0; t < n_jobs; t++) if (transfers[t].job_id >= 0) active_transfers2++;
            if (active_transfers2 > 0) {
                double eff_pcie_gbps = cfg->pcie_bandwidth_gbps * g_pcie_scale;
                double bits_per_us_per_transfer = (eff_pcie_gbps * 1e3) / (double)active_transfers2; // bits/us
                double bits_decr = delta * bits_per_us_per_transfer;
                for (int t = 0; t < n_jobs; t++) {
                    if (transfers[t].job_id >= 0) {
                        transfers[t].remaining_bits -= bits_decr;
                        if (transfers[t].remaining_bits < 1e-6)
                            transfers[t].remaining_bits = 0.0;
                    }
                }
            }
        }

        // Handle transfer completions
        for (int t = 0; t < n_jobs; t++) {
            if (transfers[t].job_id >= 0 && transfers[t].remaining_bits <= 0.0) {
                int j = transfers[t].job_id;
                jobs[j].pcie_transferred = 1;
                if (log_picks)
                    printf("[PCIe] transfer complete at %.0f us -> job %d\n", now_us, j);
                transfers[t].job_id = -1;
                transfers[t].remaining_bits = 0.0;
            }
        }

        // Complete bootstrap operations
        for (int e = 0; e < cfg->num_engines; e++) {
            if (engines[e].job_id >= 0 &&
                engines[e].busy_until_us <= now_us) {

                int j = engines[e].job_id;
                jobs[j].remaining_bootstraps--;

                if (jobs[j].remaining_bootstraps == 0) {
                    jobs[j].completion_time_us = now_us;
                    jobs_finished++;
                    if (g_show_progress) {
                        double pct = (100.0 * (double)jobs_finished) / (double)n_jobs;
                        printf("\rProgress: %d/%d (%.1f%%) — now=%.0f us", jobs_finished, n_jobs, pct, now_us);
                        fflush(stdout);
                    }
                }
                engines[e].job_id = -1;
            }
        }

        // Assign work in batches: pick a job (batch unit) and allocate up to
        // cfg->batch_size bootstraps across available idle engines.
        int idle_engines = 0;
        for (int e = 0; e < cfg->num_engines; e++) if (engines[e].job_id < 0) idle_engines++;

        // *** FIX: guard against infinite loops when all candidates are blocked.
        int attempts = 0;

        // While there are idle engines, pick a job and assign up to batch_size
        while (idle_engines > 0) {
            if (attempts >= n_jobs) {
                // Tried as many picks as jobs this cycle, give up for now
                break;
            }
            attempts++;

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

            // If the job's key hasn't been transferred yet, start a PCIe transfer
            if (!jobs[j].pcie_transferred) {
                // check if a transfer is already active for this job
                int found = 0;
                for (int t = 0; t < n_jobs; t++) {
                    if (transfers[t].job_id == j) { found = 1; break; }
                }
                if (!found) {
                    // find a free transfer slot
                    for (int t = 0; t < n_jobs; t++) {
                        if (transfers[t].job_id < 0) {
                            // remaining bits = key_size_mb * 8e6 bits
                            transfers[t].job_id = j;
                            double use_mb = jobs[j].key_size_mb;
                            if (g_pcie_cap_mb > 0.0 && use_mb > g_pcie_cap_mb) use_mb = g_pcie_cap_mb;
                            transfers[t].remaining_bits = use_mb * 8.0 * 1e6;
                            jobs[j].pcie_transferred = -1; // in progress
                            if (log_picks)
                                printf("[PCIe] transfer start at %.0f us -> job %d size=%.2f MB (orig=%.2f)\n",
                                       now_us, j, use_mb, jobs[j].key_size_mb);
                            break;
                        }
                    }
                }
                // Do not assign engines to this job until transfer completes; try another job
                continue;
            }

            int batch_len = 1;
            if (cfg && cfg->batch_size > 1)
                batch_len = cfg->batch_size < jobs[j].remaining_bootstraps ?
                            cfg->batch_size : jobs[j].remaining_bootstraps;

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

    // If any job never finished (e.g., due to early termination), treat it
    // as completing at now_us to avoid negative response times.
    for (int i = 0; i < n_jobs; i++) {
        if (jobs[i].completion_time_us <= 0.0) {
            jobs[i].completion_time_us = now_us;
        }
    }

    // Compute stats
    double first_arrival = jobs[0].arrival_time_us;
    for (int i = 1; i < n_jobs; i++)
        if (jobs[i].arrival_time_us < first_arrival)
            first_arrival = jobs[i].arrival_time_us;

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

    if (s.makespan_us > 0.0)
        s.engine_utilization = total_engine_busy_us /
                               (s.makespan_us * cfg->num_engines);
    else
        s.engine_utilization = 0.0;

    // Compute fairness: Jain's fairness index over per-tenant average slowdown.
    int max_tenant = -1;
    for (int i = 0; i < n_jobs; i++)
        if (jobs[i].tenant_id > max_tenant) max_tenant = jobs[i].tenant_id;

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

    // Optionally write per-job CSV for analysis
    if (g_csv_prefix) {
        const char *label = "sim";
        if (pick_job == (SchedulerFn)pick_job_fifo) label = "fifo";
        else if (pick_job == (SchedulerFn)pick_job_hps) label = "hps";
        char path[512];
        snprintf(path, sizeof(path), "%s-%s.csv", g_csv_prefix, label);
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "job_id,tenant_id,arrival_us,start_us,completion_us,num_bootstraps,key_size_mb,pcie_transferred\n");
            for (int i = 0; i < n_jobs; i++) {
                int trans = jobs[i].pcie_transferred;
                fprintf(f, "%d,%d,%.0f,%.0f,%.0f,%d,%.2f,%d\n",
                        jobs[i].id, jobs[i].tenant_id,
                        jobs[i].arrival_time_us, jobs[i].start_time_us,
                        jobs[i].completion_time_us, jobs[i].num_bootstraps,
                        jobs[i].key_size_mb, trans);
            }
            fclose(f);
        }
    }

    free(jobs);
    free(engines);
    free(transfers);
    if (g_show_progress) printf("\n");

    return s;
}
