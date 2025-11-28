#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "types.h"

int pick_job_fifo(const HwConfig *cfg, TfheJob *jobs, int n_jobs, double now_us);
int pick_job_hps(const HwConfig *cfg, TfheJob *jobs, int n_jobs, double now_us);

double bootstrap_time_us(const HwConfig *cfg, const TfheJob *job);

/* Allow tuning HPS scoring weights at runtime. */
void scheduler_set_weights(double w_key_affinity,
						   double w_noise_urgency,
						   double w_bw_penalty,
						   double w_fairness,
						   double w_deadline);

#endif
