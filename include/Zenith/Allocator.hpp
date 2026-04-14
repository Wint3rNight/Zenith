#pragma once

#include "Common.hpp"
#include "MemoryStats.hpp"
#include <cstdlib>

namespace Zenith {

class Allocator {
public:
  // Allocates and owns a backing memory block of the requested size.
  explicit Allocator(std::size_t totalSize) : m_totalSize(totalSize) {
    m_start = std::malloc(totalSize);
    assert(m_start != nullptr && "Failed to allocate backing memory");

    m_stats.totalSize = totalSize;
  }

  // Uses caller-provided backing memory instead of allocating internally.
  Allocator(std::size_t totalSize, void *preallocated)
      : m_totalSize(totalSize), m_start(preallocated), m_ownsMemory(false) {
    assert(m_start != nullptr && "Pre-allocated pointer must not be null");
    m_stats.totalSize = totalSize;
  }

  // Releases owned backing memory when the allocator is destroyed.
  virtual ~Allocator() {
    if (m_ownsMemory && m_start) {
      std::free(m_start);
      m_start = nullptr;
    }
  }

  Allocator(const Allocator &) = delete;
  Allocator &operator=(const Allocator &) = delete;

  // Transfers ownership of the backing memory and allocator state.
  Allocator(Allocator &&other) noexcept
      : m_totalSize(other.m_totalSize), m_start(other.m_start),
        m_ownsMemory(other.m_ownsMemory), m_stats(other.m_stats) {
    other.m_start = nullptr;
    other.m_totalSize = 0;
    other.m_ownsMemory = false;
  }

  // Replaces this allocator state with another allocator's owned memory.
  Allocator &operator=(Allocator &&other) noexcept {
    if (this != &other) {
      if (m_ownsMemory && m_start) {
        std::free(m_start);
      }
      m_totalSize = other.m_totalSize;
      m_start = other.m_start;
      m_ownsMemory = other.m_ownsMemory;
      m_stats = other.m_stats;
      other.m_start = nullptr;
      other.m_totalSize = 0;
      other.m_ownsMemory = false;
    }
    return *this;
  }

  // Reserves an aligned block from the allocator.
  virtual void *Allocate(std::size_t size,
                         std::size_t alignment = DEFAULT_ALIGNMENT) = 0;

  // Returns a previously allocated block to the allocator.
  virtual void Free(void *ptr) = 0;

  // Restores the allocator to its initial empty state.
  virtual void Reset() = 0;

  // Returns the current usage statistics for this allocator.
  const MemoryStats &GetStats() const { return m_stats; }
  // Returns the total size of the allocator's backing memory.
  std::size_t GetTotalSize() const { return m_totalSize; }
  // Returns the start address of the backing memory block.
  const void *GetStart() const { return m_start; }

protected:
  std::size_t m_totalSize = 0;
  void *m_start = nullptr;
  bool m_ownsMemory = true;
  MemoryStats m_stats;
};

} // namespace Zenith
