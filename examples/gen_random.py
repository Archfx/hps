#!/usr/bin/env python3
"""
Random hardware config and workload generator for the hps simulator.

Creates HW configs and workload files that match the project's expected
formats so they can be directly used with `tfhe_sim`.

Usage examples:
  # generate 3 hardware configs and 2 workloads (each with ~50 jobs)
  python3 examples/gen_random.py --hw 3 --wl 2 --jobs 50

  # generate one hw config and one workload with explicit ranges
  python3 examples/gen_random.py --hw 1 --wl 1 --jobs 100 --min-eng 2 --max-eng 8

Outputs are written to `examples/hw_rand_<i>.cfg` and
`examples/wl_rand_<i>.txt`.
"""

import argparse
import random
import os
import math
from datetime import datetime


def rand_float(lo, hi, scale=1.0):
    return round(random.uniform(lo, hi) * scale, 6)


def gen_hw_config(path, num_engines_range=(1, 8), hbm_range=(128, 2048),
                  key_mem_range=(512, 8192), pcie_range=(8, 128),
                  freq_range=(0.5, 3.0), ctx_overhead_range=(0.0, 5.0),
                  batch_range=(1, 8), include_batch=True):
    num_engines = random.randint(*num_engines_range)
    hbm_bw = random.choice([128, 256, 512, 1024, 1536, 2048])
    key_mem = random.randint(*key_mem_range)
    pcie_bw = random.choice([8, 16, 32, 64, 128])
    freq = round(random.uniform(*freq_range), 2)
    ctx = round(random.uniform(*ctx_overhead_range), 2)
    batch = random.randint(*batch_range) if include_batch else None

    with open(path, 'w') as f:
        # Format: num_engines hbm_bw_gbps key_mem_mb pcie_bw_gbps freq ctx_overhead [batch_size]
        if batch is not None:
            f.write(f"{num_engines} {hbm_bw} {key_mem} {pcie_bw} {freq} {ctx} {batch}\n")
        else:
            f.write(f"{num_engines} {hbm_bw} {key_mem} {pcie_bw} {freq} {ctx}\n")

    return path


def gen_workload(path, n_jobs=100, max_arrival_us=10000, tenants=4,
                 boot_min=1, boot_max=100, key_min=1, key_max=1024,
                 noise_min=1, noise_max=100, priorities=3,
                 deadline_prob=0.2, deadline_scale=5.0):
    # Write header for readability (read_workload ignores '#' lines)
    with open(path, 'w') as f:
        f.write('# id tenant arrival_us num_boot key_size_mb noise_budget priority deadline_us\n')

        # Generate arrival times clustered in bursts to exercise scheduler
        # We'll generate arrivals using a Poisson process-like spacing
        now = 0.0
        for i in range(n_jobs):
            # Inter-arrival exponential with mean = max_arrival_us / n_jobs
            mean = max(1.0, float(max_arrival_us) / max(1.0, n_jobs))
            delta = random.expovariate(1.0 / mean)
            now += delta
            arrival = round(now, 3)

            tenant = random.randint(0, max(1, tenants - 1))
            num_boot = random.randint(boot_min, boot_max)

            # key size in MB; make distribution skewed: many small, some large
            if random.random() < 0.8:
                key_size = random.randint(key_min, min(key_min + 16, key_max))
            else:
                key_size = random.randint(min(key_max//8, key_min), key_max)

            noise = round(random.uniform(noise_min, noise_max), 3)
            priority = random.randint(0, max(0, priorities - 1))

            # Some jobs get deadlines: arrival + a multiple of expected service
            if random.random() < deadline_prob:
                # estimate service time proxy: num_boot * (key_size / 100) us (coarse)
                est_service = num_boot * max(1.0, key_size / 10.0)
                deadline = round(arrival + est_service * random.uniform(1.0, deadline_scale), 3)
            else:
                deadline = 0

            f.write(f"{i} {tenant} {arrival} {num_boot} {key_size} {noise} {priority} {deadline}\n")

    return path


def ensure_examples_dir():
    os.makedirs('examples', exist_ok=True)
    os.makedirs('examples/hw', exist_ok=True)
    os.makedirs('examples/workloads', exist_ok=True)


def main():
    parser = argparse.ArgumentParser(description='Generate random hw configs and workloads for hps')

    parser.add_argument('--hw', type=int, default=1, help='number of hw config files to generate')
    parser.add_argument('--wl', type=int, default=1, help='number of workload files to generate')
    parser.add_argument('--jobs', type=int, default=100, help='jobs per workload')
    parser.add_argument('--seed', type=int, default=None, help='random seed')
    parser.add_argument('--include-batch', action='store_true', help='include batch_size in hw configs')

    args = parser.parse_args()

    if args.seed is not None:
        random.seed(args.seed)
    else:
        random.seed()

    ensure_examples_dir()

    generated = []

    # generate hardware configs
    for i in range(args.hw):
        name = f'examples/hw/hw_rand_{i}.cfg'
        gen_hw_config(name, include_batch=args.include_batch)
        print(f'Wrote hw config: {name}')
        generated.append(name)

    # generate workloads
    for i in range(args.wl):
        name = f'examples/workloads/wl_rand_{i}.txt'
        gen_workload(name, n_jobs=args.jobs)
        print(f'Wrote workload: {name}')
        generated.append(name)

    print('\nDone. To run a generated pair:')
    if args.hw >= 1 and args.wl >= 1:
        print('  ./tfhe_sim', 'examples/hw/hw_rand_0.cfg', 'examples/workloads/wl_rand_0.txt')


if __name__ == '__main__':
    main()
