#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "types.h"

typedef int (*SchedulerFn)(const HwConfig *, TfheJob *, int, double);

SimStats run_simulation(const HwConfig *cfg,
                        TfheJob *jobs_original,
                        int n_jobs,
                        SchedulerFn pick_job);

#endif
