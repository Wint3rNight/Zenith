#pragma once

#include "Allocator.hpp"

namespace Zenith {

class PoolAllocator : public Allocator {
public:
  struct FreeNode {
    FreeNode *next;
  };

  // Creates a fixed-size chunk pool with the requested capacity.
  PoolAllocator(std::size_t chunkSize, std::size_t chunkCount);
  // Creates a fixed-size chunk pool using caller-owned backing memory.
  PoolAllocator(std::size_t chunkSize, std::size_t chunkCount,
                void *preallocated);
  // Destroys the allocator and its owned backing memory.
  ~PoolAllocator() override;

  // Removes one free chunk from the pool.
  void *Allocate(std::size_t size,
                 std::size_t alignment = DEFAULT_ALIGNMENT) override;
  // Returns a chunk back to the front of the free list.
  void Free(void *ptr) override;
  // Rebuilds the free list so every chunk becomes available again.
  void Reset() override;

  // Returns the size of each chunk in the pool.
  std::size_t GetChunkSize() const { return m_chunkSize; }
  // Returns the total number of chunks managed by the pool.
  std::size_t GetChunkCount() const { return m_chunkCount; }
  // Counts how many chunks are currently free.
  std::size_t GetFreeCount() const;

private:
  // Links every chunk into the internal free list.
  void BuildFreeList();

  std::size_t m_chunkSize;
  std::size_t m_chunkCount;
  FreeNode *m_freeList;
};

} // namespace Zenith
