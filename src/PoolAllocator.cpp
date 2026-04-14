#include "Zenith/PoolAllocator.hpp"

#include <cassert>

namespace Zenith {

// Creates a pool and links every chunk into the free list.
PoolAllocator::PoolAllocator(std::size_t chunkSize, std::size_t chunkCount)
    : Allocator(chunkSize * chunkCount), m_chunkSize(chunkSize),
      m_chunkCount(chunkCount), m_freeList(nullptr) {
  assert(chunkSize >= sizeof(FreeNode) &&
         "Chunk size must be >= sizeof(void*) to hold the free list pointer");
  assert(chunkSize % alignof(std::max_align_t) == 0 &&
         "Chunk size should be a multiple of the default alignment");
  BuildFreeList();
}

// Drops the free-list head before destruction.
PoolAllocator::~PoolAllocator() { m_freeList = nullptr; }

// Rebuilds the singly linked list that tracks free chunks.
void PoolAllocator::BuildFreeList() {
  uptr startAddr = ToUptr(m_start);

  for (std::size_t i = 0; i < m_chunkCount; ++i) {
    uptr chunkAddr = startAddr + i * m_chunkSize;
    FreeNode *node = reinterpret_cast<FreeNode *>(chunkAddr);

    if (i + 1 < m_chunkCount) {
      node->next = reinterpret_cast<FreeNode *>(chunkAddr + m_chunkSize);
    } else {
      node->next = nullptr;
    }
  }

  m_freeList = reinterpret_cast<FreeNode *>(startAddr);
}

// Hands out the next available fixed-size chunk.
void *PoolAllocator::Allocate([[maybe_unused]] std::size_t size,
                              [[maybe_unused]] std::size_t alignment) {
  assert(size <= m_chunkSize && "Requested allocation exceeds pool chunk size");

  if (m_freeList == nullptr) {
    return nullptr;
  }

  FreeNode *head = m_freeList;
  m_freeList = head->next;

  m_stats.RecordAllocation(m_chunkSize);
  return reinterpret_cast<void *>(head);
}

// Returns a chunk back to the free list for reuse.
void PoolAllocator::Free(void *ptr) {
  if (ptr == nullptr)
    return;

#ifndef NDEBUG
  {
    uptr addr = ToUptr(ptr);
    uptr start = ToUptr(m_start);
    uptr end = start + m_totalSize;

    assert(addr >= start && addr < end &&
           "Pointer does not belong to this pool allocator");

    assert((addr - start) % m_chunkSize == 0 &&
           "Pointer is not aligned to a chunk boundary — "
           "it doesn't point to the start of a chunk");
  }
#endif

  FreeNode *node = reinterpret_cast<FreeNode *>(ptr);
  node->next = m_freeList;
  m_freeList = node;

  m_stats.RecordDeallocation(m_chunkSize);
}

// Restores the pool to its fully free state.
void PoolAllocator::Reset() {
  BuildFreeList();
  m_stats.Reset();
}

// Counts the currently available chunks in the free list.
std::size_t PoolAllocator::GetFreeCount() const {
  std::size_t count = 0;
  FreeNode *current = m_freeList;
  while (current != nullptr) {
    count++;
    current = current->next;
  }
  return count;
}

} // namespace Zenith
