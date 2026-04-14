#include "Zenith/FreeListAllocator.hpp"
#include "Zenith/LinearAllocator.hpp"
#include "Zenith/PoolAllocator.hpp"
#include "Zenith/StackAllocator.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib> // malloc, free
#include <iostream>
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
  std::cout << "  Run multiple times for consistent numbers.\n\n";

  return 0;
}
