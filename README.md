# ZenithMemory

ZenithMemory is a C++17 memory allocator toolkit built around a simple engine
programming idea:

> Memory allocation should match the shape of the workload.

The project contains four custom allocators, typed allocation helpers, usage
statistics, tests, a demo executable, a microbenchmark suite, and a deterministic
engine-style simulation called EngineSim. It is meant to be small enough to read,
but complete enough to discuss real allocator tradeoffs: alignment, metadata,
fragmentation, reset behavior, fixed-size reuse, mixed lifetimes, and benchmark
evidence.

This is not a claim that every custom allocator beats `malloc`. The more useful
question is when a specialized allocator wins, why it wins, and what contract you
accept in exchange.

## Origin

ZenithMemory started in the middle of a different obsession.

I was getting into game development, but a lot of the tools I touched felt far
away from the machine. Everything worked, but the work felt padded by layers I
did not understand yet. I wanted to know what was happening underneath the nice
APIs: where memory came from, why allocation patterns mattered, why engines talk
about frame arenas and pools, and why a single careless lifetime can turn a clean
system into a mess.

So the first plan was bigger: build a game engine from scratch. And if the engine
was going to be from scratch, I thought, why not build the allocator too?

The engine eventually changed shape. The original idea was scrapped, then
rebuilt into a dedicated rendering engine, and for that project I ended up using
VMA because Vulkan memory management is its own very real problem and VMA is
excellent at solving it. But the allocator work never fully left my head. It
became one of those side projects that quietly waits until you are ready to give
it proper attention.

ZenithMemory is what came out of returning to that idea with more patience. It is
not just "I wrote malloc again." It is a small allocator library and evidence
project for understanding how different allocation strategies behave under
different engine-like workloads.

## What Is In The Project

| Area | Purpose |
| --- | --- |
| `include/Zenith` | Public headers for allocators, stats, utility functions, and typed helpers. |
| `src` | Allocator implementations. |
| `demo` | A readable walkthrough executable showing each allocator in action. |
| `tests` | Focused correctness tests for alignment, reset, exhaustion, reuse, coalescing, and typed construction. |
| `benchmark` | Synthetic microbenchmarks for isolated allocation behavior. |
| `enginesim` | A deterministic frame simulation that mixes scratch memory, particles, and resource blobs. |
| `docs/results` | Captured benchmark and EngineSim console output. |
| `docs/data` | CSV outputs from EngineSim scenarios. |
| `docs/assets` | Diagrams for allocator layouts, workload flow, and decision making. |

## Allocator Map

| Allocator | Allocation Model | Free Model | Best For | Tradeoff |
| --- | --- | --- | --- | --- |
| `LinearAllocator` | Bump pointer | Bulk `Reset()` only | Per-frame scratch memory | Cannot free individual allocations. |
| `StackAllocator` | Bump pointer with headers | LIFO free or `Reset()` | Nested temporary scopes | Frees must happen in reverse order. |
| `PoolAllocator` | Fixed-size chunks | O(1) return to free list | Particles, ECS-style objects, repeated fixed-size data | One chunk size per pool. |
| `FreeListAllocator` | Search free blocks | Any-order free with coalescing | Variable-size resources with mixed lifetimes | Search, metadata, and fragmentation overhead. |

The important part is not that one allocator is "the fastest." The important
part is that each allocator makes a promise. If your workload fits the promise,
the allocator becomes simple and predictable. If it does not, the same allocator
can become awkward or outright wrong for the job.

## How The Pieces Connect

All allocators inherit from `Zenith::Allocator`, which owns or wraps a fixed
backing memory block. The base class exposes a shared interface:

```cpp
virtual void *Allocate(std::size_t size, std::size_t alignment) = 0;
virtual void Free(void *ptr) = 0;
virtual void Reset() = 0;
```

Every allocator also records `MemoryStats`, including current usage, peak usage,
allocation count, deallocation count, total allocated bytes, and outstanding
memory. That makes the demo, tests, benchmarks, and EngineSim speak the same
language when reporting allocator behavior.

The utility layer in `Common.hpp` handles pointer arithmetic and alignment:

- `IsPowerOfTwo()` validates alignment.
- `CalculatePadding()` computes the bytes needed to align a pointer.
- `CalculatePaddingWithHeader<T>()` reserves enough padding for allocator
  metadata before the user pointer.
- `PtrAdd()`, `PtrSub()`, and `PtrDiff()` keep byte-level pointer math explicit.

`AllocationUtils.hpp` adds typed helpers:

```cpp
auto *transform = Zenith::Construct<Transform>(allocator, args...);
Zenith::Destroy(allocator, transform);
```

Those helpers allocate aligned storage, construct objects with placement new,
call destructors, and then return memory through the allocator's `Free()` path.
For reset-based allocators, destruction and memory reclamation are separate:
`Destroy()` runs the destructor, while `Reset()` reclaims the region.

## How Each Allocator Works

### LinearAllocator

The linear allocator is the simplest shape: it keeps an offset into a backing
buffer. Allocation aligns the current address, returns the next pointer, and
moves the offset forward.

There is no meaningful individual free. `Free()` is intentionally a no-op, and
`Reset()` rewinds the offset to zero. That makes it a natural fit for frame
scratch memory:

1. Start frame.
2. Allocate temporary arrays, command buffers, visibility lists, and transient
   working data.
3. End frame.
4. Reset the whole arena in one operation.

The optimization is not magic. It is the removal of work. No per-allocation heap
search. No per-object free. No fragmentation management. The cost is that all
allocations share one lifetime.

### StackAllocator

The stack allocator is also offset-based, but each allocation stores a small
header before the returned pointer. The header records the previous offset, so
freeing the most recent allocation can restore the allocator to the exact prior
state.

In debug builds, it also tracks the previous allocation pointer and asserts if
you free out of LIFO order. That makes incorrect usage fail loudly while keeping
the release design straightforward.

This is useful for scoped temporary work:

```cpp
auto *a = allocator.Allocate(64);
auto *b = allocator.Allocate(128);
allocator.Free(b);
allocator.Free(a);
```

It is fast because it still avoids searching, but it gives you more control than
a pure linear arena when lifetimes are nested.

### PoolAllocator

The pool allocator divides its backing memory into equal-size chunks. Free chunks
are linked together as a singly linked list, where the free chunk itself stores
the `next` pointer.

Allocation pops the head of the free list. Free pushes the chunk back onto the
front. Both operations are O(1), and because every allocation is the same size,
external fragmentation is not part of the problem.

This is the allocator for repeated fixed-size churn: particles, components,
commands, handles, small jobs, or any object type where you can choose a stable
chunk size ahead of time.

The tradeoff is capacity and shape. If your object does not fit the chunk, the
allocation fails. If your objects vary wildly in size, one pool either wastes
memory or stops being the right abstraction.

### FreeListAllocator

The free-list allocator manages variable-size allocations inside one backing
region. It begins as a single large free block. When you allocate, it searches
for a block that fits, applies alignment padding and header space, splits the
remaining space if there is enough room, and returns the aligned user pointer.

Each allocation stores a header containing:

- the full block size reserved by the allocator,
- the padding needed to recover the original block start on free.

When memory is freed, the allocator inserts the block back into the free list in
address order. If the returned block touches a neighboring free block, the blocks
are coalesced into one larger block.

Two placement policies are available:

- `FirstFit`: use the first block large enough for the request.
- `BestFit`: scan for the tightest block that can satisfy the request.

The free-list allocator also exposes introspection methods:

- `GetFreeBlockCount()`
- `GetLargestFreeBlockSize()`
- `GetTotalFreeBytes()`
- `GetExternalFragmentationRatio()`

That makes it possible to talk about degradation and recovery instead of only
timing allocation calls.

## Optimization Story

ZenithMemory optimizes by specializing the allocator to the lifetime pattern:

- Frame scratch memory becomes cheap because the linear allocator resets the
  whole region instead of tracking each object.
- Nested temporary memory becomes cheap because the stack allocator restores an
  old offset from a header.
- Fixed-size churn becomes cheap because the pool allocator only moves a free
  list head.
- Mixed-size lifetimes become manageable because the free-list allocator splits,
  tracks, and coalesces blocks.

There is a deliberate lesson here: optimization is not just "make code faster."
It is choosing which constraints you can rely on, then removing all the machinery
that those constraints make unnecessary.

## EngineSim

EngineSim is the part of the project that tries to make allocator behavior feel
closer to an engine frame loop.

It models four broad allocation streams:

- Linear allocator: per-frame scratch data such as visible indices, particle
  scratch arrays, pair buffers, and command bytes.
- Stack allocator: nested temporary system scratch.
- Pool allocator: fixed-size particle objects with churn over time.
- Free-list allocator: variable-size resource blobs with mixed lifetimes.

The simulation is deterministic through a seed and supports four scenarios:

| Scenario | Intent |
| --- | --- |
| `steady` | Stable frame pressure with regular particle and blob churn. |
| `burst` | Occasional heavier frames with more particles and blob creation. |
| `fragmentation` | Mixed-size, mixed-lifetime blob pressure that keeps fragmentation alive. |
| `recovery` | The fragmentation workload stops creating blobs halfway through, then measures whether memory coalesces back. |

Example:

```bash
./build/ZenithEngineSim --scenario recovery --frames 600 --seed 1337 --report-every 120
```

With CSV output:

```bash
./build/ZenithEngineSim --scenario burst --frames 600 --seed 1337 \
  --report-every 120 --csv docs/data/enginesim_burst.csv
```

## Evidence Snapshot

Full results, environment details, and commands are in
[docs/BENCHMARK_EVIDENCE.md](docs/BENCHMARK_EVIDENCE.md).

### Microbenchmark

From [docs/results/benchmark_micro.txt](docs/results/benchmark_micro.txt):

| Test | Result |
| --- | --- |
| Single alloc/free, 64B | `PoolAllocator`: 2.79 ns/op, `LinearAllocator`: 2.81 ns/op, `malloc/free`: 6.32 ns/op |
| Frame scratch steady | `LinearAllocator`: 3.22 ns/op, `malloc/free`: 17.07 ns/op |
| Particle churn steady | `PoolAllocator`: 47.71 ns/op, `malloc/free`: 58.39 ns/op |
| Resource blobs steady | `FreeListAllocator`: 663.02 ns/op, `malloc/free`: 43.46 ns/op |

The resource blob result is intentionally included because it is important:
the free list is not winning the microbenchmark. Its value is that it supports
variable-size allocation, any-order free, coalescing, and fragmentation
inspection inside a fixed region. That capability has a cost.

### EngineSim

From the committed EngineSim results:

| Scenario | Peak Particles | Max Linear Peak | Max Free-list Peak | Max Fragmentation | Allocation Failures |
| --- | ---: | ---: | ---: | ---: | ---: |
| `steady` | 20000 | 815.6 KiB | 685.4 KiB | 1.3% | 0 |
| `burst` | 25000 | 986.5 KiB | 1438.1 KiB | 4.2% | 0 |
| `fragmentation` | 12000 | 542.2 KiB | 4177.6 KiB | 14.0% | 0 |
| `recovery` | 12000 | 542.2 KiB | 4177.6 KiB | 52.5% | 0 |

The recovery scenario is the most interesting one. Fragmentation spikes while
old mixed-lifetime allocations age out, but by frame 600 the free-list allocator
returns to one full free block. That is the kind of behavior a raw ns/op table
does not show.

## Visuals

- [Project overview flow](docs/assets/project-overview-flow.svg)
- [Allocator memory layouts](docs/assets/allocator-memory-layouts.svg)
- [Allocator decision map](docs/assets/allocator-decision-map.svg)
- [EngineSim frame loop](docs/assets/enginesim-frame-loop.svg)
- [EngineSim workload comparison](docs/assets/enginesim-workload-comparison.svg)

## Quick Start

Requirements:

- C++17 compiler
- CMake 3.16 or newer

Build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Run the demo:

```bash
./build/ZenithDemo
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

Run benchmarks:

```bash
./build/ZenithBenchmark
```

Run EngineSim:

```bash
./build/ZenithEngineSim --scenario steady --frames 600 --seed 1337 --report-every 120
```

## Basic Usage

```cpp
#include <Zenith/Zenith.hpp>

struct Particle {
  float position[3];
  float velocity[3];
  float lifetime;
};

int main() {
  Zenith::PoolAllocator particles(sizeof(Particle), 1024);

  auto *particle = Zenith::Construct<Particle>(particles);
  if (particle == nullptr) {
    return 1;
  }

  particle->lifetime = 3.0f;

  Zenith::Destroy(particles, particle);
  return 0;
}
```

For frame scratch:

```cpp
Zenith::LinearAllocator frameArena(1024 * 1024);

void RunFrame() {
  void *visibleList = frameArena.Allocate(4096, alignof(std::uint32_t));
  (void)visibleList;

  frameArena.Reset();
}
```

## CMake Integration

### FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(
  ZenithMemory
  GIT_REPOSITORY https://github.com/WinterInOctober/ZenithMemory.git
  GIT_TAG main
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

Install:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build --prefix ~/.local
```

Consume:

```cmake
find_package(ZenithMemory REQUIRED)
target_link_libraries(YourApp PRIVATE Zenith::Memory)
```

There is also a small consumer example under
[examples/find_package_consumer](examples/find_package_consumer).

## Project Status And Limitations

ZenithMemory is a learning, portfolio, and systems-design project. It is useful
for studying allocator internals and for small controlled integrations, but it
is not a drop-in replacement for a production general-purpose allocator.

Current boundaries:

- Allocators are fixed-size region allocators. They do not grow automatically.
- Thread safety is not built in.
- `PoolAllocator` only guarantees alignment up to `std::max_align_t`.
- `LinearAllocator` reclaims memory through `Reset()`, not individual `Free()`.
- `StackAllocator` requires LIFO frees.
- `FreeListAllocator` supports mixed lifetimes, but pays search and metadata
  overhead.
- `DebugGuard.hpp` exists as debug instrumentation support, while the current
  allocator paths primarily rely on assertions, headers, and ownership checks.

## Why Not VMA?

VMA, the Vulkan Memory Allocator, is excellent for Vulkan GPU memory management.
This project has a different scope.

ZenithMemory focuses on CPU-side allocator fundamentals: frame arenas, stack
temporary memory, object pools, free lists, alignment, metadata, fragmentation,
and workload evidence. If the goal is production Vulkan resource allocation, VMA
is usually the better tool. If the goal is to understand allocator design and
engine memory patterns, ZenithMemory is the experiment.

## Documentation

- [Technical documentation](docs/DOCUMENTATION.md)
- [EngineSim design](docs/ENGINESIM_DESIGN.md)
- [Benchmark evidence](docs/BENCHMARK_EVIDENCE.md)

## What I Learned

- Fast in isolation is not the same as good in a frame lifecycle.
- The best allocator is usually the one whose restrictions match the system.
- Fragmentation needs context: peak fragmentation, recovery behavior, and
  largest-free-block size all tell different parts of the story.
- A benchmark is more credible when the raw output, commands, and environment
  are preserved.
- Writing the allocator was useful, but writing the evidence around it made the
  project much stronger.

## License

MIT
