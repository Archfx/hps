#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../includes/workload.h"

static int cmp_arrival(const void *a, const void *b) {
    const TfheJob *ja = a;
    const TfheJob *jb = b;
    if (ja->arrival_time_us < jb->arrival_time_us) return -1;
    if (ja->arrival_time_us > jb->arrival_time_us) return 1;
    return 0;
}

int read_workload(const char *path, TfheJob **jobs_out, int *n_jobs_out) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror("fopen workload");
        return -1;
    }

    int cap = 16, n = 0;
    TfheJob *jobs = malloc(cap * sizeof(TfheJob));

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;

        TfheJob j;
        int parsed = sscanf(line, "%d %d %lf %d %lf %lf %d %lf",
                            &j.id, &j.tenant_id, &j.arrival_time_us,
                            &j.num_bootstraps, &j.key_size_mb,
                            &j.noise_budget, &j.priority, &j.deadline_us);

        if (parsed < 7) {
            fprintf(stderr, "Invalid workload line: %s\n", line);
            free(jobs);
            fclose(f);
            return -1;
        }
        if (parsed == 7) j.deadline_us = 0;

        j.remaining_bootstraps = j.num_bootstraps;
        j.start_time_us = -1;
        j.completion_time_us = -1;
        j.started = 0;

        if (n == cap) {
            cap *= 2;
            jobs = realloc(jobs, cap * sizeof(TfheJob));
        }
        jobs[n++] = j;
    }
    fclose(f);

    qsort(jobs, n, sizeof(TfheJob), cmp_arrival);

    *jobs_out = jobs;
    *n_jobs_out = n;
    return 0;
}
