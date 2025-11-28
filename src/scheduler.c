#include <float.h>
#include "../includes/scheduler.h"

// FIFO scheduler
int pick_job_fifo(const HwConfig *cfg, TfheJob *jobs, int n_jobs, double now_us) {
    double best_arrival = DBL_MAX;
    int best_idx = -1;

    for (int i = 0; i < n_jobs; i++) {
        if (jobs[i].remaining_bootstraps <= 0) continue;
        if (jobs[i].arrival_time_us > now_us) continue;
        

        if (jobs[i].arrival_time_us < best_arrival) {
            best_arrival = jobs[i].arrival_time_us;
            best_idx = i;
        }
    }
    return best_idx;
}

// Hardware-parametric scheduler 
/* Tunable HPS weights (defaults chosen previously). */
static double g_w_key_affinity = 3.0;
static double g_w_noise_urgency = 4.0;
static double g_w_bw_penalty = 2.0;
static double g_w_fairness = 1.5;
static double g_w_deadline = 2.0;

void scheduler_set_weights(double w_key_affinity,
                           double w_noise_urgency,
                           double w_bw_penalty,
                           double w_fairness,
                           double w_deadline)
{
    g_w_key_affinity = w_key_affinity;
    g_w_noise_urgency = w_noise_urgency;
    g_w_bw_penalty = w_bw_penalty;
    g_w_fairness = w_fairness;
    g_w_deadline = w_deadline;
}

int pick_job_hps(const HwConfig *cfg, TfheJob *jobs, int n_jobs, double now_us) {
    int best_idx = -1;
    double best_score = -1e300;

    for (int i = 0; i < n_jobs; i++) {

        // Job not yet arrived or finished
        if (jobs[i].remaining_bootstraps <= 0) continue;
        if (jobs[i].arrival_time_us > now_us) continue;

        // ----- 1. Key-Affinity Score (approximate) -----
        double key_affinity = 1.0 / (jobs[i].key_size_mb + 1.0);

        // ----- 2. Noise-Aware Priority -----
        double noise_urgency = 1.0 / (jobs[i].noise_budget + 1e-9);

        // ----- 3. Deadline Awareness -----
        double deadline_score = 0.0;
        if (jobs[i].deadline_us > 0.0) {
            double slack = jobs[i].deadline_us - now_us;
            if (slack < 0) slack = 0.0001;
            deadline_score = 1.0 / slack;
        }

        // ----- 4. Tenant-Level Fairness -----
        double fairness = 1.0 / (jobs[i].tenant_id + 1.0);

        // ----- 5. Bandwidth Feasibility (soft check) -----
        int effective_batch = 1;
        if (cfg && cfg->batch_size > 1)
            effective_batch = cfg->batch_size < jobs[i].remaining_bootstraps ? cfg->batch_size : jobs[i].remaining_bootstraps;

        double est_bs_time_us = bootstrap_time_us(cfg, &jobs[i]) * effective_batch;
        double bw_penalty = est_bs_time_us > 0 ? (1.0 / est_bs_time_us) : 1.0;

        // ----- 6. Combine scores (use tunable weights) -----
        double score =
            g_w_key_affinity * key_affinity +
            g_w_noise_urgency * noise_urgency +
            g_w_bw_penalty * bw_penalty +
            g_w_fairness * fairness +
            g_w_deadline * deadline_score;

        if (score > best_score) {
            best_score = score;
            best_idx = i;
        }
    }

    return best_idx;  // may be -1 if no job is ready
}

// Compute per-bootstrap time
double bootstrap_time_us(const HwConfig *cfg, const TfheJob *job) {
    double bw_per_engine = cfg->hbm_bandwidth_gbps / cfg->num_engines;
    double time_us = (job->key_size_mb * 8.0 / (bw_per_engine * 1000)) * 1e6;

    if (time_us < 1.0) time_us = 1.0;
    return time_us;
}
