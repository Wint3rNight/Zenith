# Benchmark Evidence

This document is the evidence source for performance claims in this repository.

## Scope and Honesty Policy

- Microbenchmark results and workload simulation results are reported separately.
- Microbenchmark numbers are useful for understanding allocator mechanics in isolation.
- Workload simulation numbers are used for behavior claims in engine-like conditions.
- All numbers in this document come from committed artifacts under docs/results and docs/data.

## Run Environment

Source: docs/results/system_info.txt

- Date (UTC): 2026-04-17 10:33:49Z
- OS: Linux 6.19.12-1-cachyos x86_64 GNU/Linux
- CPU: AMD Ryzen 5 5600H with Radeon Graphics
- CPU topology: 6 cores / 12 threads
- CPU max MHz: 4280.9849

## Commands Used

```bash
cmake --build build

./build/ZenithBenchmark | tee ZenithMemory/docs/results/benchmark_micro.txt

./build/ZenithEngineSim --scenario steady --frames 600 --seed 1337 --report-every 120 \
  --csv ZenithMemory/docs/data/enginesim_steady.csv \
  | tee ZenithMemory/docs/results/enginesim_steady.txt

./build/ZenithEngineSim --scenario burst --frames 600 --seed 1337 --report-every 120 \
  --csv ZenithMemory/docs/data/enginesim_burst.csv \
  | tee ZenithMemory/docs/results/enginesim_burst.txt

./build/ZenithEngineSim --scenario fragmentation --frames 600 --seed 1337 --report-every 120 \
  --csv ZenithMemory/docs/data/enginesim_fragmentation.csv \
  | tee ZenithMemory/docs/results/enginesim_fragmentation.txt

./build/ZenithEngineSim --scenario recovery --frames 600 --seed 1337 --report-every 120 \
  --csv ZenithMemory/docs/data/enginesim_recovery.csv \
  | tee ZenithMemory/docs/results/enginesim_recovery.txt
```

## Microbenchmark (Synthetic)

Source: docs/results/benchmark_micro.txt

### Single alloc/free (100000 iterations, 64B, align 16)

| Allocator         | ns/op | Speed vs malloc |
| ----------------- | ----: | --------------: |
| malloc/free       |  6.32 |           1.00x |
| new/delete        | 10.20 |           0.62x |
| LinearAllocator   |  2.81 |           2.25x |
| StackAllocator    |  3.98 |           1.59x |
| PoolAllocator     |  2.79 |           2.26x |
| FreeListAllocator |  7.80 |           0.81x |

### Patterned micro scenarios (EngineSim-inspired)

- Frame scratch steady: Linear 3.22 ns/op, Stack 3.92 ns/op, FreeList 8.03 ns/op, malloc 17.07 ns/op.
- Frame scratch burst: Linear 4.52 ns/op, Stack 7.29 ns/op, FreeList 14.97 ns/op, malloc 26.04 ns/op.
- Particle churn steady: Pool 47.71 ns/op, malloc 58.39 ns/op, FreeList 184.59 ns/op.
- Particle churn burst: Pool 44.46 ns/op, malloc 56.54 ns/op, FreeList 198.71 ns/op.
- Resource blobs steady: malloc 43.46 ns/op, FreeList 663.02 ns/op.
- Resource blobs burst: malloc 45.85 ns/op, FreeList 648.14 ns/op.

Interpretation:

- Pool and linear win when allocation shape is constrained and predictable.
- Free list pays metadata/search/coalescing overhead in micro loops, especially variable-size churn.
- These micro wins/losses do not fully represent interleaved frame behavior.

## Real Workload Simulation (EngineSim)

Sources:

- docs/results/enginesim_steady.txt
- docs/results/enginesim_burst.txt
- docs/results/enginesim_fragmentation.txt
- docs/results/enginesim_recovery.txt

### Scenario summary

| Scenario      | Peak particles | Max linear peak KiB | Max free-list peak KiB | Max free-list frag % | Allocation failures |
| ------------- | -------------: | ------------------: | ---------------------: | -------------------: | ------------------: |
| steady        |          20000 |               815.6 |                  685.4 |                  1.3 |                   0 |
| burst         |          25000 |               986.5 |                 1438.1 |                  4.2 |                   0 |
| fragmentation |          12000 |               542.2 |                 4177.6 |                 14.0 |                   0 |
| recovery      |          12000 |               542.2 |                 4177.6 |                 52.5 |                   0 |

### Burst vs steady breakdown (burst scenario)

- Steady-frame average (595 frames): linear 815.6 KiB, pool 20000 chunks, free-list used 1267.2 KiB, fragmentation 2.1%.
- Burst-frame average (5 frames): linear 986.5 KiB, pool 25000 chunks, free-list used 1159.6 KiB, fragmentation 1.3%.

Why fragmentation can be lower on burst frames:

- Burst frames allocate many larger blocks that can temporarily reduce fragmentation ratio by consuming small gaps.
- Fragmentation rises again during subsequent mixed-lifetime steady frames as those blocks age and free out-of-order.

### Recovery behavior

- Fragmentation scenario stays around 10-14% once it reaches target blob pressure.
- Recovery scenario intentionally stops new blob creation after frame 300.
- Recovery temporarily spikes fragmentation (max 52.5%) as old mixed-size blocks age out.
- By frame 600, free-list used bytes reach 0.0 KiB and largest free block returns to full 8192.0 KiB.

## Why allocator winners change across scenarios

- LinearAllocator wins frame-scratch style work because reset makes individual frees unnecessary.
- PoolAllocator wins fixed-size particle churn because free list traversal is not needed.
- FreeListAllocator loses microbench throughput but is the only allocator in this project that can handle mixed-size blob lifetimes in one region.
- In recovery, FreeListAllocator demonstrates value through coalescing and memory reclamation, not raw ns/op.

## Artifact Index

- Console outputs: docs/results/benchmark_micro.txt, docs/results/enginesim_steady.txt, docs/results/enginesim_burst.txt, docs/results/enginesim_fragmentation.txt, docs/results/enginesim_recovery.txt
- CSV outputs: docs/data/enginesim_steady.csv, docs/data/enginesim_burst.csv, docs/data/enginesim_fragmentation.csv, docs/data/enginesim_recovery.csv
- Environment: docs/results/system_info.txt
