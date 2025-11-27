#ifndef WORKLOAD_H
#define WORKLOAD_H

#include "types.h"

int read_workload(const char *path, TfheJob **jobs_out, int *n_jobs_out);

#endif
