# hps: Hardware-Parametric Scheduling for TFHE Accelerators



A lightweight C-based simulator for evaluating scheduling strategies on TFHE hardware accelerators.  
It models TFHE jobs, bootstrapping engines, bandwidth limits, and key-switch overheads.

## Build
```bash
make
./tfhe_sim hw.cfg workload.txt
```

## Examples

Run simulator with provided examples:

```bash
./tfhe_sim ./examples/hw/hw1.cfg ./examples/workloads/w1.txt
```

Generator
--------

A helper script `examples/gen_random.py` creates random hardware configs and workloads compatible with the simulator.

- Hardware config format (single line):

	`num_engines hbm_bw_gbps key_mem_mb pcie_bw_gbps freq ctx_overhead [batch_size]`

	- `batch_size` is optional; if present it controls how many bootstraps the scheduler clusters per pick. Default is `1` when absent.

- Workload format (space-separated, header included):

	`id tenant arrival_us num_boot key_size_mb noise_budget priority deadline_us`

Run the generator:

```bash
python3 examples/gen_random.py --hw 1 --wl 1 --jobs 50 --seed 123 --include-batch

# then run the generated pair
./tfhe_sim examples/hw/hw_rand_0.cfg examples/workloads/wl_rand_0.txt
```

Logging scheduler picks
-----------------------

Set the env var `HPS_LOG_PICKS=1` to print per-pick logs (scheduler, simulation time, job id, remaining bootstraps):

```bash
HPS_LOG_PICKS=1 ./tfhe_sim examples/hw/hw1.cfg examples/workloads/w1.txt
```

Plotter
--------

```shell
python3 plotter/plotter.py test_small_fifo.csv test_small_hps.csv --out-prefix test_small
```

Notes
-----

- When the system is saturated (engines fully utilized and long jobs), ordering differences between FIFO and HPS may have limited effect on makespan; use workloads with many small jobs or tighter deadlines to better exercise scheduling heuristics.

