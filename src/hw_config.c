#include <stdio.h>
#include <string.h>
#include "../includes/hw_config.h"

int read_hw_config(const char *path, HwConfig *cfg) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror("fopen hw config");
        return -1;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;

        int n = sscanf(line, "%d %lf %lf %lf %lf %lf %d",
                           &cfg->num_engines,
                           &cfg->hbm_bandwidth_gbps,
                           &cfg->key_mem_mb,
                           &cfg->pcie_bandwidth_gbps,
                           &cfg->freq_ghz,
                           &cfg->ctx_switch_overhead_us,
                           &cfg->batch_size);

            if (n < 6) {
                fprintf(stderr, "Invalid hw config line: %s\n", line);
                fclose(f);
                return -1;
            }

            if (n == 6) {
                cfg->batch_size = 1; // default
            } else if (cfg->batch_size < 1) {
                cfg->batch_size = 1;
            }

            fclose(f);
            return 0;
    }

    fclose(f);
    fprintf(stderr, "Empty hw config\n");
    return -1;
}
