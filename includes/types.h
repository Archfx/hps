#ifndef TYPES_H
#define TYPES_H

typedef struct {
    int num_engines;
    double hbm_bandwidth_gbps;
    double key_mem_mb;
    double pcie_bandwidth_gbps;
    double freq_ghz;
    double ctx_switch_overhead_us;
    int batch_size;
} HwConfig;

typedef struct {
    int id;
    int tenant_id;
    double arrival_time_us;
    int num_bootstraps;

    double key_size_mb;
    double noise_budget;
    int priority;
    double deadline_us;

    int remaining_bootstraps;
    double start_time_us;
    double completion_time_us;
    int started;
    int pcie_transferred; // 0 = not transferred, -1 = transfer in-progress, 1 = transfer complete
} TfheJob;

typedef struct {
    int job_id;
    double start_us;
    double end_us;
} EngineLogEntry;

typedef struct {
    int job_id;
    double busy_until_us;

    // NEW: timeline log
    EngineLogEntry *log;
    int log_len;
    int log_cap;
} Engine;

typedef struct {
    double makespan_us;
    double avg_completion_time_us;
    double avg_slowdown;
    double engine_utilization;
    double fairness; // Jain's fairness index over per-tenant average slowdown (0..1)
} SimStats;

#endif
