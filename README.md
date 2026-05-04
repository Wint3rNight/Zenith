# ZenithMemory

ZenithMemory is a C++17 allocator toolkit built to answer a practical game-engine question:

How much control and predictability can you get by matching allocator design to workload shape?

This project is intentionally not just a raw speed demo. It includes:

- Four allocator implementations with different tradeoff profiles.
- A microbenchmark for isolated behavior.
- A deterministic EngineSim workload runner for frame-oriented evidence.
- A benchmark evidence pack with raw outputs, CSV files, and environment metadata.

## Motivation

General allocators are flexible, but game/runtime systems often have stronger structure:

- per-frame scratch that can be dropped in one reset,
- fixed-size object churn (particles/components),
- variable-size resources with mixed lifetimes.

ZenithMemory exists to model that structure explicitly, then prove tradeoffs with scenario-based evidence instead of one synthetic ns/op claim.

## Allocators and Tradeoffs

| Allocator           | Alloc       | Free                 | Strength                            | Limitation                    |
| ------------------- | ----------- | -------------------- | ----------------------------------- | ----------------------------- |
| `LinearAllocator`   | O(1)        | reset-only           | Fast monotonic frame scratch        | No individual free            |
| `StackAllocator`    | O(1)        | O(1) LIFO            | Great for nested temporary scopes   | Must free in reverse order    |
| `PoolAllocator`     | O(1)        | O(1)                 | Stable fixed-size churn             | One chunk size per pool       |
| `FreeListAllocator` | O(n) search | O(1) free + coalesce | Flexible variable-size lifetime mix | More metadata/search overhead |

## Proof: Microbenchmark vs Real Workload

The project explicitly separates evidence classes:

- Microbenchmark: isolated allocator mechanics.
- Real workload simulation (EngineSim): interleaved multi-frame behavior.

See full evidence and reproducibility details in [docs/BENCHMARK_EVIDENCE.md](docs/BENCHMARK_EVIDENCE.md).

### Snapshot: Microbenchmark

From [docs/results/benchmark_micro.txt](docs/results/benchmark_micro.txt):

- Single alloc/free (100000 iterations, 64B):
  - `PoolAllocator`: 2.79 ns/op (2.26x vs malloc)
  - `LinearAllocator`: 2.81 ns/op (2.25x vs malloc)
  - `FreeListAllocator`: 7.80 ns/op (0.81x vs malloc)
- Patterned particle churn (EngineSim-inspired):
  - `PoolAllocator`: 47.71 ns/op
  - `malloc`: 58.39 ns/op
  - `FreeListAllocator`: 184.59 ns/op

Interpretation:

- Constrained allocators dominate when the pattern matches their contract.
- Flexible allocators pay overhead in tight synthetic loops.

### Snapshot: Real Workload Simulation

From [docs/results/enginesim_steady.txt](docs/results/enginesim_steady.txt), [docs/results/enginesim_burst.txt](docs/results/enginesim_burst.txt), [docs/results/enginesim_fragmentation.txt](docs/results/enginesim_fragmentation.txt), [docs/results/enginesim_recovery.txt](docs/results/enginesim_recovery.txt):

- Steady scenario: max free-list fragmentation 1.3%
- Burst scenario: max free-list fragmentation 4.2%, peak particles 25000
- Fragmentation scenario: max free-list fragmentation 14.0%
- Recovery scenario: fragmentation spikes to 52.5% mid-run, then returns to 0.0% by frame 600 after churn stops

Interpretation:

- Burst raises peak pressure (linear and pool usage) as expected.
- Free-list behavior is not a simple "slow/fast" story; it is a lifecycle story.
- In recovery, the key win is coalescing and memory restoration, not best micro ns/op.

## Why One Allocator Wins Then Loses

The same allocator can win one scenario and lose another because workload constraints change the objective function:

- Frame scratch objective: lowest overhead per transient allocation.
  - Winner: `LinearAllocator` / `StackAllocator`.
- Particle churn objective: cheap fixed-size reuse.
  - Winner: `PoolAllocator`.
- Mixed-size resource lifecycle objective: flexibility + coalescing.
  - Winner on capability: `FreeListAllocator`.
  - Loser on pure micro throughput: `FreeListAllocator`.

## Visuals

- Allocator layout overview: [docs/assets/allocator-memory-layouts.svg](docs/assets/allocator-memory-layouts.svg)
- Workload comparison visual: [docs/assets/enginesim-workload-comparison.svg](docs/assets/enginesim-workload-comparison.svg)

## Why Not VMA For This?

VMA (Vulkan Memory Allocator) is excellent for GPU memory management. ZenithMemory is intentionally different in scope:

- ZenithMemory targets CPU-side allocator patterns and interview-ready systems design clarity.
- It is dependency-light and focused on allocator fundamentals, not GPU heap orchestration.
- It makes tradeoffs and internals explicit for learning, benchmarking, and design discussion.

If your production target is Vulkan resource allocation, VMA is usually the right operational choice. This project is a focused allocator systems exercise with reproducible workload evidence.

## What I Learned

- Fast in isolation does not guarantee best behavior in mixed frame lifecycles.
- Fragmentation metrics need scenario context; single-value summaries can mislead.
- Recovery behavior is as important as degradation behavior.
- Good portfolio engineering requires reproducibility artifacts, not only charts and claims.

## Build and Run

Requirements:

- C++17 compiler
- CMake 3.16+

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/ZenithDemo
./build/ZenithBenchmark
./build/ZenithEngineSim --scenario steady --frames 600 --seed 1337 --report-every 120
ctest --test-dir build --output-on-failure
```

## Integration

### FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(
        ZenithMemory
        GIT_REPOSITORY https://github.com/YOUR_USERNAME/ZenithMemory.git
        GIT_TAG        main
)

FetchContent_MakeAvailable(ZenithMemory)
target_link_libraries(YourApp PRIVATE Zenith::Memory)
```

### add_subdirectory

```cmake
add_subdirectory(external/ZenithMemory)
target_link_libraries(YourApp PRIVATE Zenith::Memory)
```

### find_package

```bash
cmake -S . -B build
cmake --build build
cmake --install build --prefix ~/.local
```

```cmake
find_package(ZenithMemory REQUIRED)
target_link_libraries(YourApp PRIVATE Zenith::Memory)
```

## Additional Documentation

- Deep technical guide: [docs/DOCUMENTATION.md](docs/DOCUMENTATION.md)
- EngineSim design and scenario contract: [docs/ENGINESIM_DESIGN.md](docs/ENGINESIM_DESIGN.md)
- Benchmark evidence and raw artifacts map: [docs/BENCHMARK_EVIDENCE.md](docs/BENCHMARK_EVIDENCE.md)

## License

MIT
