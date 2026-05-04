#include "Zenith/FreeListAllocator.hpp"
#include "Zenith/LinearAllocator.hpp"
#include "Zenith/PoolAllocator.hpp"
#include "Zenith/StackAllocator.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib> // malloc, free
#include <iostream>
#include <random>
#include <vector>

// Prevents the compiler from optimizing benchmarked pointers away.
static void DoNotOptimize(void *ptr) {
  asm volatile("" : : "r"(ptr) : "memory");
}

constexpr std::size_t ITERATIONS = 100000;
constexpr std::size_t ALLOC_SIZE = 64;
constexpr std::size_t POOL_CHUNK_SIZE = 64;
constexpr std::size_t ALIGNMENT = 16;

constexpr std::size_t BACKING_SIZE = ITERATIONS * (ALLOC_SIZE + ALIGNMENT) * 2;

using Clock = std::chrono::high_resolution_clock;
using Duration = std::chrono::duration<double, std::micro>;

struct BenchmarkResult {
  const char *name;
  double totalMicroseconds;
  double avgNanoseconds;

  // Prints one formatted benchmark row.
  void Print() const {
    std::printf("  %-25s │ %10.2f μs total │ %8.2f ns/op\n", name,
                totalMicroseconds, avgNanoseconds);
  }
};

struct PatternBenchmarkResult {
  const char *scenario;
  const char *allocator;
  double totalMicroseconds;
  double avgNanoseconds;
  std::size_t operationCount;
  std::size_t failureCount;

  // Prints one formatted benchmark row for patterned scenarios.
  void Print() const {
    std::printf("  %-30s │ %-18s │ %10.2f μs │ %8.2f ns/op │ %8zu │ %5zu\n",
                scenario, allocator, totalMicroseconds, avgNanoseconds,
                operationCount, failureCount);
  }
};

constexpr std::size_t WORKLOAD_FRAMES = 600;
constexpr std::size_t BURST_INTERVAL = 120;
constexpr std::size_t BASE_PARTICLES = 20000;
constexpr std::size_t BURST_PARTICLES = 5000;
constexpr std::size_t BURST_BLOB_BONUS = 128;
constexpr std::size_t FRAME_SCRATCH_BACKING_SIZE = 2 * 1024 * 1024;
constexpr std::size_t FREE_LIST_WORKLOAD_BYTES = 16 * 1024 * 1024;

struct ParticleState {
  void *memory = nullptr;
  std::uint32_t lifetime = 0;
};

struct ResourceBlobState {
  void *memory = nullptr;
  std::size_t size = 0;
  std::uint32_t lifetime = 0;
};

struct ParticleScratch {
  float position[3];
  float speed;
};

bool IsBurstFrame(std::size_t frameIndex) {
  return frameIndex % BURST_INTERVAL == 0;
}

std::size_t ParticleTargetForFrame(bool burst, std::size_t frameIndex) {
  std::size_t target = BASE_PARTICLES;
  if (burst && IsBurstFrame(frameIndex)) {
    target += BURST_PARTICLES;
  }
  return target;
}

template <typename AllocateFn, typename FreeFn, typename ResetFn>
PatternBenchmarkResult
RunFrameScratchPattern(const char *scenario, const char *allocator, bool burst,
                       AllocateFn allocateFn, FreeFn freeFn, ResetFn resetFn) {
  std::size_t operations = 0;
  std::size_t failures = 0;

  auto start = Clock::now();

  for (std::size_t frame = 0; frame < WORKLOAD_FRAMES; ++frame) {
    resetFn();

    std::size_t particleCount = ParticleTargetForFrame(burst, frame);
    std::size_t visibleCount = (particleCount * 3) / 4;
    std::size_t pairCount = std::min<std::size_t>(particleCount * 2, 120000);
    std::size_t commandBytes =
        4096 + std::min<std::size_t>(particleCount * 12, 128 * 1024);

    std::array<void *, 4> allocations = {
        allocateFn(visibleCount * sizeof(std::uint32_t),
                   alignof(std::uint32_t)),
        allocateFn(particleCount * sizeof(ParticleScratch),
                   alignof(ParticleScratch)),
        allocateFn(pairCount * sizeof(std::array<std::uint32_t, 2>),
                   alignof(std::array<std::uint32_t, 2>)),
        allocateFn(commandBytes, ALIGNMENT),
    };

    for (void *ptr : allocations) {
      if (ptr == nullptr) {
        ++failures;
      } else {
        ++operations;
        DoNotOptimize(ptr);
      }
    }

    freeFn(allocations);
  }

  auto end = Clock::now();
  double totalUs = std::chrono::duration_cast<Duration>(end - start).count();
  double avgNs = operations > 0
                     ? (totalUs * 1000.0) / static_cast<double>(operations)
                     : 0.0;

  return {scenario, allocator, totalUs, avgNs, operations, failures};
}

template <typename AllocateFn, typename FreeFn>
PatternBenchmarkResult
RunParticleChurnPattern(const char *scenario, const char *allocator, bool burst,
                        AllocateFn allocateFn, FreeFn freeFn) {
  std::size_t operations = 0;
  std::size_t failures = 0;
  std::vector<ParticleState> particles;
  particles.reserve(60000);

  std::mt19937 rng(burst ? 20260417u : 1337u);
  std::uniform_int_distribution<std::uint32_t> lifetimeDist(90, 180);

  auto start = Clock::now();

  for (std::size_t frame = 0; frame < WORKLOAD_FRAMES; ++frame) {
    std::size_t index = 0;
    while (index < particles.size()) {
      ParticleState &state = particles[index];
      if (state.lifetime > 0) {
        --state.lifetime;
      }

      if (state.lifetime == 0) {
        freeFn(state.memory, 64);
        ++operations;
        particles[index] = particles.back();
        particles.pop_back();
        continue;
      }

      ++index;
    }

    std::size_t target = ParticleTargetForFrame(burst, frame);
    while (particles.size() > target) {
      ParticleState state = particles.back();
      particles.pop_back();
      freeFn(state.memory, 64);
      ++operations;
    }

    while (particles.size() < target) {
      void *ptr = allocateFn(64, ALIGNMENT);
      if (ptr == nullptr) {
        ++failures;
        break;
      }
      ++operations;
      DoNotOptimize(ptr);
      particles.push_back({ptr, lifetimeDist(rng)});
    }
  }

  for (const ParticleState &state : particles) {
    freeFn(state.memory, 64);
    ++operations;
  }

  auto end = Clock::now();
  double totalUs = std::chrono::duration_cast<Duration>(end - start).count();
  double avgNs = operations > 0
                     ? (totalUs * 1000.0) / static_cast<double>(operations)
                     : 0.0;

  return {scenario, allocator, totalUs, avgNs, operations, failures};
}

template <typename AllocateFn, typename FreeFn>
PatternBenchmarkResult
RunResourceBlobPattern(const char *scenario, const char *allocator, bool burst,
                       AllocateFn allocateFn, FreeFn freeFn) {
  std::size_t operations = 0;
  std::size_t failures = 0;
  std::vector<ResourceBlobState> blobs;
  blobs.reserve(2500);

  std::mt19937 rng(burst ? 424242u : 7331u);
  std::uniform_int_distribution<std::uint32_t> lifetimeDist(30, 120);

  auto start = Clock::now();

  for (std::size_t frame = 0; frame < WORKLOAD_FRAMES; ++frame) {
    std::size_t index = 0;
    while (index < blobs.size()) {
      ResourceBlobState &state = blobs[index];
      if (state.lifetime > 0) {
        --state.lifetime;
      }

      if (state.lifetime == 0) {
        freeFn(state.memory, state.size);
        ++operations;
        blobs[index] = blobs.back();
        blobs.pop_back();
        continue;
      }

      ++index;
    }

    std::size_t createsThisFrame = 16;
    std::size_t minSize = 64;
    std::size_t maxSize = 1024;
    if (burst && IsBurstFrame(frame)) {
      createsThisFrame += BURST_BLOB_BONUS;
      minSize = 256;
      maxSize = 2048;
    }

    std::uniform_int_distribution<std::size_t> sizeDist(minSize, maxSize);
    for (std::size_t i = 0; i < createsThisFrame; ++i) {
      std::size_t size = sizeDist(rng);
      void *ptr = allocateFn(size, ALIGNMENT);
      if (ptr == nullptr) {
        ++failures;
        continue;
      }

      ++operations;
      DoNotOptimize(ptr);
      blobs.push_back({ptr, size, lifetimeDist(rng)});
    }
  }

  for (const ResourceBlobState &state : blobs) {
    freeFn(state.memory, state.size);
    ++operations;
  }

  auto end = Clock::now();
  double totalUs = std::chrono::duration_cast<Duration>(end - start).count();
  double avgNs = operations > 0
                     ? (totalUs * 1000.0) / static_cast<double>(operations)
                     : 0.0;

  return {scenario, allocator, totalUs, avgNs, operations, failures};
}

void PrintPatternTable(const char *title,
                       const std::vector<PatternBenchmarkResult> &results) {
  std::cout << "\n  " << title << "\n";
  std::cout << "  "
               "┌────────────────────────────────┬────────────────────┬────────"
               "──────┬────────────┬──────────┬───────┐\n";
  std::cout << "  "
               "│ Scenario                       │ Allocator          │ Total "
               "Time   │ Per Op     │ Ops      │ Fails │\n";
  std::cout << "  "
               "├────────────────────────────────┼────────────────────┼────────"
               "──────┼────────────┼──────────┼───────┤\n";

  for (const PatternBenchmarkResult &result : results) {
    result.Print();
  }

  std::cout << "  "
               "└────────────────────────────────┴────────────────────┴────────"
               "──────┴────────────┴──────────┴───────┘\n";

  if (results.empty()) {
    return;
  }

  double baselineNs = results.front().avgNanoseconds;
  if (baselineNs <= 0.0) {
    return;
  }

  std::cout << "  Speed vs " << results.front().allocator << ":\n";
  for (const PatternBenchmarkResult &result : results) {
    if (result.avgNanoseconds <= 0.0) {
      continue;
    }
    double ratio = baselineNs / result.avgNanoseconds;
    std::printf("    %-18s : %6.2fx %s\n", result.allocator, ratio,
                ratio >= 1.0 ? "faster" : "slower");
  }
}

PatternBenchmarkResult BenchFrameScratchMalloc(bool burst) {
  return RunFrameScratchPattern(
      burst ? "Frame scratch (burst)" : "Frame scratch (steady)", "malloc",
      burst, [](std::size_t size, std::size_t) { return std::malloc(size); },
      [](const std::array<void *, 4> &allocations) {
        for (void *ptr : allocations) {
          std::free(ptr);
        }
      },
      []() {});
}

PatternBenchmarkResult BenchFrameScratchLinear(bool burst) {
  Zenith::LinearAllocator allocator(FRAME_SCRATCH_BACKING_SIZE);

  return RunFrameScratchPattern(
      burst ? "Frame scratch (burst)" : "Frame scratch (steady)", "Linear",
      burst,
      [&allocator](std::size_t size, std::size_t alignment) {
        return allocator.Allocate(size, alignment);
      },
      [](const std::array<void *, 4> &) {},
      [&allocator]() { allocator.Reset(); });
}

PatternBenchmarkResult BenchFrameScratchStack(bool burst) {
  Zenith::StackAllocator allocator(FRAME_SCRATCH_BACKING_SIZE);

  return RunFrameScratchPattern(
      burst ? "Frame scratch (burst)" : "Frame scratch (steady)", "Stack",
      burst,
      [&allocator](std::size_t size, std::size_t alignment) {
        return allocator.Allocate(size, alignment);
      },
      [&allocator](const std::array<void *, 4> &allocations) {
        for (auto it = allocations.rbegin(); it != allocations.rend(); ++it) {
          if (*it != nullptr) {
            allocator.Free(*it);
          }
        }
      },
      []() {});
}

PatternBenchmarkResult BenchFrameScratchFreeList(bool burst) {
  Zenith::FreeListAllocator allocator(
      FRAME_SCRATCH_BACKING_SIZE,
      Zenith::FreeListAllocator::PlacementPolicy::FirstFit);

  return RunFrameScratchPattern(
      burst ? "Frame scratch (burst)" : "Frame scratch (steady)", "FreeList",
      burst,
      [&allocator](std::size_t size, std::size_t alignment) {
        return allocator.Allocate(size, alignment);
      },
      [&allocator](const std::array<void *, 4> &allocations) {
        for (void *ptr : allocations) {
          if (ptr != nullptr) {
            allocator.Free(ptr);
          }
        }
      },
      []() {});
}

PatternBenchmarkResult BenchParticleChurnMalloc(bool burst) {
  return RunParticleChurnPattern(
      burst ? "Particle churn (burst)" : "Particle churn (steady)", "malloc",
      burst, [](std::size_t size, std::size_t) { return std::malloc(size); },
      [](void *ptr, std::size_t) { std::free(ptr); });
}

PatternBenchmarkResult BenchParticleChurnPool(bool burst) {
  Zenith::PoolAllocator allocator(64, 60000);

  return RunParticleChurnPattern(
      burst ? "Particle churn (burst)" : "Particle churn (steady)", "Pool",
      burst,
      [&allocator](std::size_t size, std::size_t alignment) {
        return allocator.Allocate(size, alignment);
      },
      [&allocator](void *ptr, std::size_t) {
        if (ptr != nullptr) {
          allocator.Free(ptr);
        }
      });
}

PatternBenchmarkResult BenchParticleChurnFreeList(bool burst) {
  Zenith::FreeListAllocator allocator(
      FREE_LIST_WORKLOAD_BYTES,
      Zenith::FreeListAllocator::PlacementPolicy::FirstFit);

  return RunParticleChurnPattern(
      burst ? "Particle churn (burst)" : "Particle churn (steady)", "FreeList",
      burst,
      [&allocator](std::size_t size, std::size_t alignment) {
        return allocator.Allocate(size, alignment);
      },
      [&allocator](void *ptr, std::size_t) {
        if (ptr != nullptr) {
          allocator.Free(ptr);
        }
      });
}

PatternBenchmarkResult BenchResourceChurnMalloc(bool burst) {
  return RunResourceBlobPattern(
      burst ? "Resource blobs (burst)" : "Resource blobs (steady)", "malloc",
      burst, [](std::size_t size, std::size_t) { return std::malloc(size); },
      [](void *ptr, std::size_t) { std::free(ptr); });
}

PatternBenchmarkResult BenchResourceChurnFreeList(bool burst) {
  Zenith::FreeListAllocator allocator(
      FREE_LIST_WORKLOAD_BYTES,
      Zenith::FreeListAllocator::PlacementPolicy::FirstFit);

  return RunResourceBlobPattern(
      burst ? "Resource blobs (burst)" : "Resource blobs (steady)", "FreeList",
      burst,
      [&allocator](std::size_t size, std::size_t alignment) {
        return allocator.Allocate(size, alignment);
      },
      [&allocator](void *ptr, std::size_t) {
        if (ptr != nullptr) {
          allocator.Free(ptr);
        }
      });
}

// Benchmarks a malloc/free pair for each iteration.
BenchmarkResult BenchMalloc() {
  auto start = Clock::now();

  for (std::size_t i = 0; i < ITERATIONS; ++i) {
    void *ptr = std::malloc(ALLOC_SIZE);
    DoNotOptimize(ptr);
    std::free(ptr);
  }

  auto end = Clock::now();
  double totalUs = std::chrono::duration_cast<Duration>(end - start).count();

  return {"malloc / free", totalUs,
          (totalUs * 1000.0) / static_cast<double>(ITERATIONS)};
}

// Benchmarks a new[]/delete[] pair for each iteration.
BenchmarkResult BenchNew() {
  auto start = Clock::now();

  for (std::size_t i = 0; i < ITERATIONS; ++i) {
    char *ptr = new char[ALLOC_SIZE];
    DoNotOptimize(ptr);
    delete[] ptr;
  }

  auto end = Clock::now();
  double totalUs = std::chrono::duration_cast<Duration>(end - start).count();

  return {"new / delete", totalUs,
          (totalUs * 1000.0) / static_cast<double>(ITERATIONS)};
}

// Benchmarks bump allocation followed by a single reset.
BenchmarkResult BenchLinear() {
  Zenith::LinearAllocator allocator(BACKING_SIZE);

  auto start = Clock::now();

  for (std::size_t i = 0; i < ITERATIONS; ++i) {
    void *ptr = allocator.Allocate(ALLOC_SIZE, ALIGNMENT);
    DoNotOptimize(ptr);
  }
  allocator.Reset();

  auto end = Clock::now();
  double totalUs = std::chrono::duration_cast<Duration>(end - start).count();

  return {"Linear Allocator", totalUs,
          (totalUs * 1000.0) / static_cast<double>(ITERATIONS)};
}

// Benchmarks stack-style allocate/free pairs.
BenchmarkResult BenchStack() {
  Zenith::StackAllocator allocator(BACKING_SIZE);

  auto start = Clock::now();

  for (std::size_t i = 0; i < ITERATIONS; ++i) {
    void *ptr = allocator.Allocate(ALLOC_SIZE, ALIGNMENT);
    DoNotOptimize(ptr);
    allocator.Free(ptr);
  }

  auto end = Clock::now();
  double totalUs = std::chrono::duration_cast<Duration>(end - start).count();

  return {"Stack Allocator", totalUs,
          (totalUs * 1000.0) / static_cast<double>(ITERATIONS)};
}

// Benchmarks fixed-size pool allocate/free pairs.
BenchmarkResult BenchPool() {
  Zenith::PoolAllocator allocator(POOL_CHUNK_SIZE, ITERATIONS);

  auto start = Clock::now();

  for (std::size_t i = 0; i < ITERATIONS; ++i) {
    void *ptr = allocator.Allocate(ALLOC_SIZE, ALIGNMENT);
    DoNotOptimize(ptr);
    allocator.Free(ptr);
  }

  auto end = Clock::now();
  double totalUs = std::chrono::duration_cast<Duration>(end - start).count();

  return {"Pool Allocator", totalUs,
          (totalUs * 1000.0) / static_cast<double>(ITERATIONS)};
}

// Benchmarks free-list allocate/free pairs.
BenchmarkResult BenchFreeList() {
  Zenith::FreeListAllocator allocator(
      BACKING_SIZE, Zenith::FreeListAllocator::PlacementPolicy::FirstFit);

  auto start = Clock::now();

  for (std::size_t i = 0; i < ITERATIONS; ++i) {
    void *ptr = allocator.Allocate(ALLOC_SIZE, ALIGNMENT);
    DoNotOptimize(ptr);
    allocator.Free(ptr);
  }

  auto end = Clock::now();
  double totalUs = std::chrono::duration_cast<Duration>(end - start).count();

  return {"FreeList Allocator", totalUs,
          (totalUs * 1000.0) / static_cast<double>(ITERATIONS)};
}

constexpr std::size_t BULK_COUNT = 1000;

// Benchmarks many malloc allocations before freeing them in bulk.
BenchmarkResult BenchMallocBulk() {
  void *ptrs[BULK_COUNT];

  auto start = Clock::now();

  for (std::size_t round = 0; round < ITERATIONS / BULK_COUNT; ++round) {
    for (std::size_t i = 0; i < BULK_COUNT; ++i) {
      ptrs[i] = std::malloc(ALLOC_SIZE);
      DoNotOptimize(ptrs[i]);
    }
    for (std::size_t i = 0; i < BULK_COUNT; ++i) {
      std::free(ptrs[i]);
    }
  }

  auto end = Clock::now();
  double totalUs = std::chrono::duration_cast<Duration>(end - start).count();
  std::size_t totalOps = (ITERATIONS / BULK_COUNT) * BULK_COUNT;

  return {"malloc (bulk)", totalUs,
          (totalUs * 1000.0) / static_cast<double>(totalOps)};
}

// Benchmarks many pool allocations before freeing them in bulk.
BenchmarkResult BenchPoolBulk() {
  Zenith::PoolAllocator allocator(POOL_CHUNK_SIZE, BULK_COUNT);
  void *ptrs[BULK_COUNT];

  auto start = Clock::now();

  for (std::size_t round = 0; round < ITERATIONS / BULK_COUNT; ++round) {
    for (std::size_t i = 0; i < BULK_COUNT; ++i) {
      ptrs[i] = allocator.Allocate(ALLOC_SIZE);
      DoNotOptimize(ptrs[i]);
    }
    for (std::size_t i = 0; i < BULK_COUNT; ++i) {
      allocator.Free(ptrs[i]);
    }
  }

  auto end = Clock::now();
  double totalUs = std::chrono::duration_cast<Duration>(end - start).count();
  std::size_t totalOps = (ITERATIONS / BULK_COUNT) * BULK_COUNT;

  return {"Pool (bulk)", totalUs,
          (totalUs * 1000.0) / static_cast<double>(totalOps)};
}

// Runs the benchmark suite and prints the comparison tables.
int main() {
  std::cout << "\n";
  std::cout
      << "╔══════════════════════════════════════════════════════════════╗\n";
  std::cout
      << "║           ZENITH MEMORY — Performance Benchmark            ║\n";
  std::cout
      << "╠══════════════════════════════════════════════════════════════╣\n";
  std::printf("║  Iterations:   %zu\n", ITERATIONS);
  std::printf("║  Alloc size:   %zu bytes\n", ALLOC_SIZE);
  std::printf("║  Alignment:    %zu bytes\n", ALIGNMENT);
  std::cout
      << "╚══════════════════════════════════════════════════════════════╝\n";

  std::cout << "\n  Warming up...\n";
  for (int i = 0; i < 10000; ++i) {
    void *p = std::malloc(64);
    DoNotOptimize(p);
    std::free(p);
  }

  std::cout << "\n  ┌────────── Single Alloc/Free "
               "──────────────────────────────────────┐\n";
  std::cout
      << "  │ Allocator                 │   Total Time   │   Per Op        │\n";
  std::cout
      << "  ├───────────────────────────┼────────────────┼─────────────────┤\n";

  std::vector<BenchmarkResult> results;
  results.push_back(BenchMalloc());
  results.push_back(BenchNew());
  results.push_back(BenchLinear());
  results.push_back(BenchStack());
  results.push_back(BenchPool());
  results.push_back(BenchFreeList());

  for (auto &r : results) {
    r.Print();
  }

  std::cout << "  "
               "└──────────────────────────────────────────────────────────────"
               "───┘\n";

  std::cout << "\n  ┌────────── Bulk Alloc/Free (" << BULK_COUNT
            << " objects) ──────────────────────────┐\n";
  std::cout
      << "  │ Allocator                 │   Total Time   │   Per Op        │\n";
  std::cout
      << "  ├───────────────────────────┼────────────────┼─────────────────┤\n";

  auto mallocBulk = BenchMallocBulk();
  auto poolBulk = BenchPoolBulk();
  mallocBulk.Print();
  poolBulk.Print();

  std::cout << "  "
               "└──────────────────────────────────────────────────────────────"
               "───┘\n";

  std::cout << "\n  ┌────────── EngineSim-Patterned Microbenchmark "
               "────────────────────────┐\n";
  std::cout << "  │ These scenarios mimic allocation patterns from "
               "ZenithEngineSim,      │\n";
  std::cout
      << "  │ but still run as isolated microbenchmarks.                  "
         " │\n";
  std::cout
      << "  │ For full interleaved workload behavior, use ZenithEngineSim."
         " │\n";
  std::cout << "  "
               "└──────────────────────────────────────────────────────────────"
               "───┘\n";

  PrintPatternTable(
      "Scenario Group: Frame Scratch (transient per-frame allocations)",
      {BenchFrameScratchMalloc(false), BenchFrameScratchLinear(false),
       BenchFrameScratchStack(false), BenchFrameScratchFreeList(false)});
  PrintPatternTable(
      "Scenario Group: Frame Scratch Burst (periodic spikes every 120 frames)",
      {BenchFrameScratchMalloc(true), BenchFrameScratchLinear(true),
       BenchFrameScratchStack(true), BenchFrameScratchFreeList(true)});

  PrintPatternTable(
      "Scenario Group: Particle Churn (fixed-size long-lived objects)",
      {BenchParticleChurnMalloc(false), BenchParticleChurnPool(false),
       BenchParticleChurnFreeList(false)});
  PrintPatternTable(
      "Scenario Group: Particle Churn Burst (spawn spikes every 120 frames)",
      {BenchParticleChurnMalloc(true), BenchParticleChurnPool(true),
       BenchParticleChurnFreeList(true)});

  PrintPatternTable(
      "Scenario Group: Resource Blob Churn (variable-size payloads)",
      {BenchResourceChurnMalloc(false), BenchResourceChurnFreeList(false)});
  PrintPatternTable(
      "Scenario Group: Resource Blob Burst (mixed-size burst payloads)",
      {BenchResourceChurnMalloc(true), BenchResourceChurnFreeList(true)});

  double mallocNs = results[0].avgNanoseconds;
  std::cout << "\n  ┌────────── Speed vs malloc "
               "────────────────────────────────────────┐\n";
  for (auto &r : results) {
    double ratio = mallocNs / r.avgNanoseconds;
    std::printf("  │ %-25s │ %6.2fx %s\n", r.name, ratio,
                ratio >= 1.0 ? "faster" : "slower");
  }
  std::cout << "  "
               "└──────────────────────────────────────────────────────────────"
               "───┘\n";

  std::cout
      << "\n  Note: Results vary by system, CPU frequency, and OS scheduler.\n";
  std::cout
      << "  Microbenchmark numbers should not be interpreted as full engine "
         "performance.\n";
  std::cout
      << "  Run ZenithEngineSim steady/burst/fragmentation/recovery for real "
         "workload evidence.\n\n";

  return 0;
}
