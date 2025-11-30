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

/* ===================== SETTERS ===================== */

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


/* ====================================================
   ==================== SIMULATION ====================
   ==================================================== */

SimStats run_simulation(const HwConfig *cfg,
                        TfheJob *jobs_original,
                        int n_jobs,
                        SchedulerFn pick_job)
{
    /* --------- Clone jobs --------- */

    TfheJob *jobs = malloc(n_jobs * sizeof(TfheJob));
    for (int i = 0; i < n_jobs; i++) {
        jobs[i] = jobs_original[i];

        // initialize PCIe transfer state
        if (cfg->pcie_bandwidth_gbps <= 0.0) jobs[i].pcie_transferred = 1;
        else jobs[i].pcie_transferred = 0;
    }

    /* --------- PCIe transfer tracking --------- */

    Transfer *transfers = malloc(n_jobs * sizeof(Transfer));
    for (int t = 0; t < n_jobs; t++) {
        transfers[t].job_id = -1;
        transfers[t].remaining_bits = 0.0;
    }

    /* --------- Allocate engines + NEW LOGGING --------- */

    Engine *engines = malloc(cfg->num_engines * sizeof(Engine));
    for (int e = 0; e < cfg->num_engines; e++) {
        engines[e].job_id = -1;
        engines[e].busy_until_us = 0.0;

        // NEW: initialize engine-level logs
        engines[e].log_len = 0;
        engines[e].log_cap = 1024;
        engines[e].log = malloc(sizeof(EngineLogEntry) * engines[e].log_cap);
    }

    double now_us = 0.0;
    double total_engine_busy_us = 0.0;
    int jobs_finished = 0;

    int log_picks = getenv("HPS_LOG_PICKS") != NULL;
    const char *sched_label = "scheduler";
    if (log_picks) {
        if (pick_job == pick_job_hps) sched_label = "HPS";
        else if (pick_job == pick_job_fifo) sched_label = "FIFO";
    }

    /* ====================================================
       ==================== MAIN LOOP ====================
       ==================================================== */

    while (jobs_finished < n_jobs) {

        double next_event = DBL_MAX;

        /* ---- Next engine completion ---- */
        for (int e = 0; e < cfg->num_engines; e++) {
            if (engines[e].job_id >= 0 && engines[e].busy_until_us > now_us) {
                if (engines[e].busy_until_us < next_event)
                    next_event = engines[e].busy_until_us;
            }
        }

        /* ---- Next job arrival ---- */
        for (int j = 0; j < n_jobs; j++) {
            if (jobs[j].arrival_time_us > now_us &&
                jobs[j].arrival_time_us < next_event)
                next_event = jobs[j].arrival_time_us;
        }

        /* ---- Next PCIe transfer completion ---- */
        int active_transfers = 0;
        for (int t = 0; t < n_jobs; t++)
            if (transfers[t].job_id >= 0) active_transfers++;

        if (active_transfers > 0 && cfg->pcie_bandwidth_gbps > 0.0) {
            double eff_pcie_gbps = cfg->pcie_bandwidth_gbps * g_pcie_scale;
            double bits_per_us = (eff_pcie_gbps * 1e3) / (double)active_transfers;
            for (int t = 0; t < n_jobs; t++) {
                if (transfers[t].job_id >= 0) {
                    double finish_time =
                        now_us + transfers[t].remaining_bits / bits_per_us;
                    if (finish_time < next_event)
                        next_event = finish_time;
                }
            }
        }

        if (next_event == DBL_MAX)
            break;

        double delta = next_event - now_us;

        /* ---- Account engine busy time ---- */
        int busy_eng = 0;
        for (int e = 0; e < cfg->num_engines; e++)
            if (engines[e].job_id >= 0) busy_eng++;

        total_engine_busy_us += delta * busy_eng;
        now_us = next_event;

        /* ---- Update PCIe transfers ---- */
        if (active_transfers > 0 && cfg->pcie_bandwidth_gbps > 0.0) {
            double eff_pcie_gbps = cfg->pcie_bandwidth_gbps * g_pcie_scale;
            double bits_per_us = (eff_pcie_gbps * 1e3) / (double)active_transfers;
            double bits_dec = delta * bits_per_us;

            for (int t = 0; t < n_jobs; t++) {
                if (transfers[t].job_id >= 0) {
                    transfers[t].remaining_bits -= bits_dec;
                    if (transfers[t].remaining_bits < 1e-6)
                        transfers[t].remaining_bits = 0.0;
                }
            }
        }

        /* ---- Handle PCIe completions ---- */
        for (int t = 0; t < n_jobs; t++) {
            if (transfers[t].job_id >= 0 &&
                transfers[t].remaining_bits <= 0.0)
            {
                int j = transfers[t].job_id;
                jobs[j].pcie_transferred = 1;

                if (log_picks)
                    printf("[PCIe] done %.0f us -> job %d\n", now_us, j);

                transfers[t].job_id = -1;
            }
        }

        /* ---- Handle engine completions ---- */
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

        /* ---- Assign work (batching) ---- */
        int idle = 0;
        for (int e = 0; e < cfg->num_engines; e++)
            if (engines[e].job_id < 0) idle++;

        int attempts = 0;

        while (idle > 0) {
            if (attempts++ >= n_jobs)
                break;

            int j = pick_job(cfg, jobs, n_jobs, now_us);
            if (j < 0) break;

            if (!jobs[j].started) {
                jobs[j].started = 1;
                jobs[j].start_time_us = now_us;
            }

            /* ---- PCIe required? ---- */
            if (!jobs[j].pcie_transferred) {
                int found = 0;
                for (int t = 0; t < n_jobs; t++)
                    if (transfers[t].job_id == j)
                        found = 1;

                if (!found) {
                    for (int t = 0; t < n_jobs; t++) {
                        if (transfers[t].job_id < 0) {
                            double mb = jobs[j].key_size_mb;
                            if (g_pcie_cap_mb > 0.0 && mb > g_pcie_cap_mb)
                                mb = g_pcie_cap_mb;

                            transfers[t].job_id = j;
                            transfers[t].remaining_bits = mb * 8.0 * 1e6;
                            jobs[j].pcie_transferred = -1;
                            break;
                        }
                    }
                }
                continue;
            }

            int batch = cfg->batch_size;
            if (batch > jobs[j].remaining_bootstraps)
                batch = jobs[j].remaining_bootstraps;
            if (batch > idle)
                batch = idle;

            double t_us = bootstrap_time_us(cfg, &jobs[j]);

            /* ---- Assign engines ---- */
            for (int e = 0; e < cfg->num_engines && batch > 0; e++) {
                if (engines[e].job_id < 0) {

                    double end = now_us + t_us + cfg->ctx_switch_overhead_us;
                    engines[e].job_id = j;
                    engines[e].busy_until_us = end;

                    /* ==== NEW: LOG ENGINE SLICE ==== */
                    if (engines[e].log_len >= engines[e].log_cap) {
                        engines[e].log_cap *= 2;
                        engines[e].log = realloc(engines[e].log,
                             engines[e].log_cap * sizeof(EngineLogEntry));
                    }
                    engines[e].log[engines[e].log_len++] = (EngineLogEntry){
                        .job_id = j,
                        .start_us = now_us,
                        .end_us = end
                    };

                    idle--;
                    batch--;
                }
            }
        }
    }

    /* --------- ensure all jobs have completion time --------- */

    for (int i = 0; i < n_jobs; i++)
        if (jobs[i].completion_time_us <= 0.0)
            jobs[i].completion_time_us = now_us;

    /* --------- Compute statistics --------- */

    double first_arrival = jobs[0].arrival_time_us;
    for (int i = 1; i < n_jobs; i++)
        if (jobs[i].arrival_time_us < first_arrival)
            first_arrival = jobs[i].arrival_time_us;

    double last_finish = 0.0;
    for (int i = 0; i < n_jobs; i++)
        if (jobs[i].completion_time_us > last_finish)
            last_finish = jobs[i].completion_time_us;

    SimStats s;
    s.makespan_us = last_finish - first_arrival;

    double sum_comp = 0.0, sum_slow = 0.0;
    for (int i = 0; i < n_jobs; i++) {
        double resp = jobs[i].completion_time_us - jobs[i].arrival_time_us;
        sum_comp += resp;

        double svc = jobs[i].num_bootstraps * bootstrap_time_us(cfg, &jobs[i]);
        if (svc < 1) svc = 1;
        sum_slow += resp / svc;
    }

    s.avg_completion_time_us = sum_comp / n_jobs;
    s.avg_slowdown = sum_slow / n_jobs;
    s.engine_utilization =
        (s.makespan_us > 0 ? total_engine_busy_us / (s.makespan_us * cfg->num_engines)
                           : 0.0);

    /* --------- Compute fairness --------- */
    int max_tenant = -1;
    for (int i = 0; i < n_jobs; i++)
        if (jobs[i].tenant_id > max_tenant)
            max_tenant = jobs[i].tenant_id;

    double fairness = 1.0;
    if (max_tenant >= 0) {
        int T = max_tenant + 1;
        double *sum_slow_t = calloc(T, sizeof(double));
        int *cnt_t = calloc(T, sizeof(int));

        for (int i = 0; i < n_jobs; i++) {
            double resp = jobs[i].completion_time_us - jobs[i].arrival_time_us;
            double svc = jobs[i].num_bootstraps *
                         bootstrap_time_us(cfg, &jobs[i]);
            if (svc < 1) svc = 1;
            double slow = resp / svc;

            sum_slow_t[jobs[i].tenant_id] += slow;
            cnt_t[jobs[i].tenant_id]++;
        }

        double sum_x = 0, sum_x2 = 0;
        int present = 0;
        for (int t = 0; t < T; t++) {
            if (cnt_t[t] > 0) {
                double avg = sum_slow_t[t] / cnt_t[t];
                sum_x += avg;
                sum_x2 += avg * avg;
                present++;
            }
        }

        if (present > 1)
            fairness = (sum_x * sum_x) / (present * sum_x2);

        free(sum_slow_t);
        free(cnt_t);
    }

    s.fairness = fairness;

    /* --------- Write Logs to CSV --------- */

    if (g_csv_prefix) {
        const char *label = "sim";
        if (pick_job == pick_job_fifo) label = "fifo";
        else if (pick_job == pick_job_hps) label = "hps";

        char path_jobs[512];
        snprintf(path_jobs, sizeof(path_jobs),
                 "examples/results/%s-%s.csv", g_csv_prefix, label);

        FILE *f = fopen(path_jobs, "w");
        if (f) {
            fprintf(f, "job_id,tenant_id,arrival_us,start_us,completion_us,"
                        "num_bootstraps,key_size_mb,pcie_transferred\n");
            for (int i = 0; i < n_jobs; i++) {
                fprintf(f, "%d,%d,%.0f,%.0f,%.0f,%d,%.2f,%d\n",
                        jobs[i].id, jobs[i].tenant_id,
                        jobs[i].arrival_time_us, jobs[i].start_time_us,
                        jobs[i].completion_time_us, jobs[i].num_bootstraps,
                        jobs[i].key_size_mb, jobs[i].pcie_transferred);
            }
            fclose(f);
        }

        /* ---- NEW ENGINE LOG CSV ---- */
        char path_eng[512];
        snprintf(path_eng, sizeof(path_eng),
                 "examples/results/%s-%s-engines.csv", g_csv_prefix, label);

        FILE *ef = fopen(path_eng, "w");
        if (ef) {
            fprintf(ef, "engine,job_id,start_us,end_us\n");
            for (int e = 0; e < cfg->num_engines; e++) {
                for (int k = 0; k < engines[e].log_len; k++) {
                    EngineLogEntry *L = &engines[e].log[k];
                    fprintf(ef, "%d,%d,%.0f,%.0f\n",
                            e, L->job_id, L->start_us, L->end_us);
                }
            }
            fclose(ef);
        }
    }

    /* --------- Cleanup --------- */

    for (int e = 0; e < cfg->num_engines; e++)
        free(engines[e].log);

    free(engines);
    free(transfers);
    free(jobs);

    if (g_show_progress) printf("\n");

    return s;
}
