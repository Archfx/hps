# hps: Hardware-Parametric Scheduling for TFHE Accelerators



A lightweight C-based simulator for evaluating scheduling strategies on TFHE hardware accelerators.  
It models TFHE jobs, bootstrapping engines, bandwidth limits, and key-switch overheads.
## Build
```bash
make
./tfhe_sim hw.cfg workload.txt
```

## Examples

```bash
./tfhe_sim ./examples/hw/hw1.cfg ./examples/workloads/w1.txt
```