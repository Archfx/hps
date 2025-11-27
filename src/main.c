#include <stdio.h>
#include <stdlib.h>

#include "../includes/types.h"
#include "../includes/hw_config.h"
#include "../includes/workload.h"
#include "../includes/scheduler.h"
#include "../includes/simulator.h"

static void print_stats(const char *label, const HwConfig *cfg,
                        const SimStats *s, int n_jobs)
{
    printf("=== %s ===\n", label);
    printf("Engines: %d | HBM: %.1f Gbps | Key Mem: %.1f MB\n",
           cfg->num_engines, cfg->hbm_bandwidth_gbps, cfg->key_mem_mb);

    printf("Jobs: %d\n", n_jobs);
    printf("Makespan: %.2f us\n", s->makespan_us);
    printf("Avg Completion: %.2f us\n", s->avg_completion_time_us);
    printf("Avg Slowdown: %.3f\n", s->avg_slowdown);
    printf("Utilization: %.3f\n", s->engine_utilization);
    printf("Fairness (Jain over tenant avg slowdown): %.4f\n\n", s->fairness);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s <hw.cfg> <workload.txt>\n", argv[0]);
        return 1;
    }

    HwConfig cfg;
    if (read_hw_config(argv[1], &cfg) != 0)
        return 1;

    TfheJob *jobs;
    int n_jobs;
    if (read_workload(argv[2], &jobs, &n_jobs) != 0)
        return 1;

    SimStats fifo_stats = run_simulation(&cfg, jobs, n_jobs,
        (SchedulerFn)pick_job_fifo);

    SimStats hps_stats = run_simulation(&cfg, jobs, n_jobs,
        (SchedulerFn)pick_job_hps);

    print_stats("FIFO Baseline", &cfg, &fifo_stats, n_jobs);
    print_stats("HPS Scheduler", &cfg, &hps_stats, n_jobs);

    free(jobs);
    return 0;
}
