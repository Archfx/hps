#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "types.h"


typedef int (*SchedulerFn)(const HwConfig *, TfheJob *, int, double);

SimStats run_simulation(const HwConfig *cfg,
                        TfheJob *jobs_original,
                        int n_jobs,
                        SchedulerFn pick_job);

/* Testing helpers: scale PCIe bandwidth and cap transfer sizes (MB)
 * Call before `run_simulation` to affect subsequent runs. */
void simulator_set_pcie_scale(double scale);
void simulator_set_pcie_cap_mb(double cap_mb);
void simulator_set_show_progress(int show);
void simulator_set_csv_prefix(const char *prefix);



#endif
