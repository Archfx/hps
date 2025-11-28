#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    if (argc < 3) {
        printf("Usage: %s [--pcie-scale SCALE] [--pcie-cap-mb CAP] [--progress] [--dump-csv PREFIX] [--hps-w1 w1 --hps-w2 w2 --hps-w3 w3 --hps-w4 w4 --hps-w5 w5] <hw.cfg> <workload.txt>\n", argv[0]);
        return 1;
    }

    double pcie_scale = 1.0;
    double pcie_cap_mb = 0.0;
    int show_progress = 0;
    const char *hw_path = NULL;
    const char *wl_path = NULL;
    const char *csv_prefix = NULL;
    double hps_w1 = -1.0, hps_w2 = -1.0, hps_w3 = -1.0, hps_w4 = -1.0, hps_w5 = -1.0;

    // simple CLI parsing
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pcie-scale") == 0 && i + 1 < argc) {
            pcie_scale = atof(argv[++i]);
        } else if (strcmp(argv[i], "--progress") == 0) {
            show_progress = 1;
        } else if (strcmp(argv[i], "--dump-csv") == 0 && i + 1 < argc) {
            csv_prefix = argv[++i];
        } else if (strcmp(argv[i], "--hps-w1") == 0 && i + 1 < argc) {
            hps_w1 = atof(argv[++i]);
        } else if (strcmp(argv[i], "--hps-w2") == 0 && i + 1 < argc) {
            hps_w2 = atof(argv[++i]);
        } else if (strcmp(argv[i], "--hps-w3") == 0 && i + 1 < argc) {
            hps_w3 = atof(argv[++i]);
        } else if (strcmp(argv[i], "--hps-w4") == 0 && i + 1 < argc) {
            hps_w4 = atof(argv[++i]);
        } else if (strcmp(argv[i], "--hps-w5") == 0 && i + 1 < argc) {
            hps_w5 = atof(argv[++i]);
        } else if (strcmp(argv[i], "--pcie-cap-mb") == 0 && i + 1 < argc) {
            pcie_cap_mb = atof(argv[++i]);
        } else if (argv[i][0] == '-') {
            printf("Unknown option: %s\n", argv[i]);
            return 1;
        } else if (!hw_path) {
            hw_path = argv[i];
        } else if (!wl_path) {
            wl_path = argv[i];
        } else {
            printf("Unexpected argument: %s\n", argv[i]);
            return 1;
        }
    }

    if (!hw_path || !wl_path) {
        printf("Usage: %s [--pcie-scale SCALE] [--pcie-cap-mb CAP] <hw.cfg> <workload.txt>\n", argv[0]);
        return 1;
    }

    HwConfig cfg;
    if (read_hw_config(hw_path, &cfg) != 0)
        return 1;

    TfheJob *jobs;
    int n_jobs;
    if (read_workload(wl_path, &jobs, &n_jobs) != 0)
        return 1;

    // apply testing knobs
    if (pcie_scale != 1.0) simulator_set_pcie_scale(pcie_scale);
    if (pcie_cap_mb > 0.0) simulator_set_pcie_cap_mb(pcie_cap_mb);
    if (show_progress) simulator_set_show_progress(1);
    if (csv_prefix) simulator_set_csv_prefix(csv_prefix);

    // Apply HPS weight overrides if provided
    if (hps_w1 >= 0.0 || hps_w2 >= 0.0 || hps_w3 >= 0.0 || hps_w4 >= 0.0 || hps_w5 >= 0.0) {
        // Use defaults for any not provided
        double w1 = hps_w1 >= 0.0 ? hps_w1 : 3.0;
        double w2 = hps_w2 >= 0.0 ? hps_w2 : 4.0;
        double w3 = hps_w3 >= 0.0 ? hps_w3 : 2.0;
        double w4 = hps_w4 >= 0.0 ? hps_w4 : 1.5;
        double w5 = hps_w5 >= 0.0 ? hps_w5 : 2.0;
        scheduler_set_weights(w1, w2, w3, w4, w5);
    }

    SimStats fifo_stats = run_simulation(&cfg, jobs, n_jobs,
        (SchedulerFn)pick_job_fifo);

    SimStats hps_stats = run_simulation(&cfg, jobs, n_jobs,
        (SchedulerFn)pick_job_hps);

    print_stats("FIFO Baseline", &cfg, &fifo_stats, n_jobs);
    print_stats("HPS Scheduler", &cfg, &hps_stats, n_jobs);

    free(jobs);
    return 0;
}
