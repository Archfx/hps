#include <float.h>
#include "../includes/scheduler.h"

// FIFO scheduler
int pick_job_fifo(TfheJob *jobs, int n_jobs, double now_us) {
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
int pick_job_hps(const HwConfig *cfg, TfheJob *jobs, int n_jobs, double now_us) {
    int best_idx = -1;
    double best_score = -1e300;

    // Hardware effective per-engine bandwidth (GB/s → MB/us)
    double bw_per_engine_MB_per_us =
        (cfg->hbm_bandwidth_gbps / (double)cfg->num_engines) * 1000.0 / 8.0 / 1e6;

    for (int i = 0; i < n_jobs; i++) {

        // Job not yet arrived or finished
        if (jobs[i].remaining_bootstraps <= 0) continue;
        if (jobs[i].arrival_time_us > now_us) continue;

        // ----- 1. Key-Affinity Score (approximate) -----
        // Jobs with smaller key sizes → better locality, lower bandwidth stress
        double key_affinity = 1.0 / (jobs[i].key_size_mb + 1.0);

        // ----- 2. Noise-Aware Priority -----
        // Small noise budget → urgent (approximate: lower = more urgent)
        double noise_urgency = 1.0 / (jobs[i].noise_budget + 1e-9);

        // ----- 3. Deadline Awareness -----
        double deadline_score = 0.0;
        if (jobs[i].deadline_us > 0.0) {
            double slack = jobs[i].deadline_us - now_us;
            if (slack < 0) slack = 0.0001;
            deadline_score = 1.0 / slack;
        }

        // ----- 4. Tenant-Level Fairness -----
        // Higher tenant_id → lower priority (simple fairness proxy)
        double fairness = 1.0 / (jobs[i].tenant_id + 1.0);

        // ----- 5. Bandwidth Feasibility (soft check) -----
        // If job would take too long due to large key, penalize
        double est_bs_time_us = bootstrap_time_us(cfg, &jobs[i]);
        double bw_penalty = est_bs_time_us > 0 ? (1.0 / est_bs_time_us) : 1.0;

        // ----- 6. Combine scores -----
        double score =
            3.0 * key_affinity +      // favor key-locality
            4.0 * noise_urgency +     // noise deadline aware
            2.0 * bw_penalty +        // HBM-friendly
            1.5 * fairness +          // tenant fairness
            2.0 * deadline_score;     // soft deadlines


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
