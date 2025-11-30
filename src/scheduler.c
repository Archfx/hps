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

int pick_job_hps(const HwConfig *cfg, TfheJob *jobs, int n_jobs, double now_us)
{
    int best_idx = -1;
    double best_score = -DBL_MAX;

    for (int i = 0; i < n_jobs; i++) {

        // Skip jobs that cannot run logically
        if (jobs[i].remaining_bootstraps <= 0) continue;
        if (jobs[i].arrival_time_us > now_us) continue;

        /*************************************************************
         * 1. Key affinity
         *************************************************************/
        double key_aff = 1.0 / (jobs[i].key_size_mb + 1.0);

        /*************************************************************
         * 2. Noise urgency (bounded)
         *************************************************************/
        double nb = jobs[i].noise_budget;
        if (nb < 0) nb = 0;
        if (nb > 1) nb = 1;
        double noise_urg = (1.0 - nb);

        /*************************************************************
         * 3. Deadline pressure (bounded)
         *************************************************************/
        double deadline_score = 0.0;
        if (jobs[i].deadline_us > 0.0) {
            double slack = jobs[i].deadline_us - now_us;
            if (slack < 0) slack = 0;
            if (slack > 20000) slack = 20000;
            deadline_score = 1.0 - slack / (slack + 500.0);
        }

        /*************************************************************
         * 4. Tenant fairness (bounded)
         *************************************************************/
        double fairness = 1.0 / (1.0 + jobs[i].tenant_id * 0.2);

        /*************************************************************
         * 5. Bandwidth penalty (per-bootstrap)
         *************************************************************/
        double t = bootstrap_time_us(cfg, &jobs[i]);
        double bw_pen = 1.0 / (t + 1.0);

        /*************************************************************
         * Combined weighted score
         *************************************************************/
        double score =
              g_w_key_affinity  * key_aff
            + g_w_noise_urgency * noise_urg
            + g_w_deadline      * deadline_score
            + g_w_fairness      * fairness
            + g_w_bw_penalty    * bw_pen;

        if (score > best_score) {
            best_score = score;
            best_idx = i;
        }
    }

    return best_idx; 
}




// Compute per-bootstrap time
double bootstrap_time_us(const HwConfig *cfg, const TfheJob *job) {
    double bw_per_engine = cfg->hbm_bandwidth_gbps / cfg->num_engines;
    double time_us = (job->key_size_mb * 8.0 / (bw_per_engine * 1000)) * 1e6;

    if (time_us < 1.0) time_us = 1.0;
    return time_us;
}
