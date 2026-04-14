#pragma once

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <iostream>

namespace Zenith {

struct MemoryStats {
  std::size_t totalSize = 0;
  std::size_t usedBytes = 0;
  std::size_t peakUsedBytes = 0;
  std::size_t allocationCount = 0;
  std::size_t deallocationCount = 0;
  std::size_t totalAllocated = 0;

  // Updates usage counters after a successful allocation.
  void RecordAllocation(std::size_t bytes) {
    usedBytes += bytes;
    totalAllocated += bytes;
    allocationCount++;
    peakUsedBytes = std::max(peakUsedBytes, usedBytes);
  }

  // Updates usage counters after memory is returned.
  void RecordDeallocation(std::size_t bytes) {
    usedBytes -= bytes;
    deallocationCount++;
  }

  // Clears all tracked statistics back to their initial state.
  void Reset() {
    usedBytes = 0;
    peakUsedBytes = 0;
    allocationCount = 0;
    deallocationCount = 0;
    totalAllocated = 0;
  }

  // Reports whether alloc/free counts are mismatched.
  bool HasLeaks() const { return allocationCount != deallocationCount; }

  // Prints a formatted summary of the tracked memory statistics.
  void Print(const char *allocatorName) const {
    std::cout << "\n╔══════════════════════════════════════════╗\n";
    std::cout << "║ Memory Stats: " << std::setw(25) << std::left
              << allocatorName << " ║\n";
    std::cout << "╠══════════════════════════════════════════╣\n";
    std::cout << "║ Total Capacity:   " << std::setw(12) << std::right
              << totalSize << " bytes ║\n";
    std::cout << "║ Used:             " << std::setw(12) << usedBytes
              << " bytes ║\n";
    std::cout << "║ Peak Used:        " << std::setw(12) << peakUsedBytes
              << " bytes ║\n";
    std::cout << "║ Allocations:      " << std::setw(12) << allocationCount
              << "        ║\n";
    std::cout << "║ Deallocations:    " << std::setw(12) << deallocationCount
              << "        ║\n";
    std::cout << "║ Total Allocated:  " << std::setw(12) << totalAllocated
              << " bytes ║\n";
    std::cout << "║ Leaked:           " << std::setw(12)
              << (HasLeaks() ? "YES" : "NO") << "        ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n";
  }
};

} // namespace Zenith
