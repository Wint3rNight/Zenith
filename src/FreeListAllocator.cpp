#include "Zenith/FreeListAllocator.hpp"

#include <cassert>
#include <limits>

namespace Zenith {

// Creates a free-list allocator and seeds it with one large free block.
FreeListAllocator::FreeListAllocator(std::size_t totalSize,
                                     PlacementPolicy policy)
    : Allocator(totalSize), m_policy(policy), m_freeList(nullptr) {
  Reset();
}

// Creates a free-list allocator on top of caller-provided backing memory.
FreeListAllocator::FreeListAllocator(std::size_t totalSize, void *preallocated,
                                     PlacementPolicy policy)
    : Allocator(totalSize, preallocated), m_policy(policy), m_freeList(nullptr) {
  Reset();
}

// Clears the free-list head before destruction.
FreeListAllocator::~FreeListAllocator() { m_freeList = nullptr; }

// Reserves memory from a suitable free block and splits leftovers if needed.
void *FreeListAllocator::Allocate(std::size_t size, std::size_t alignment) {
  assert(size > 0 && "Cannot allocate 0 bytes");
  assert(IsPowerOfTwo(alignment) && "Alignment must be a power of 2");

  if (size == 0 || !IsPowerOfTwo(alignment)) {
    return nullptr;
  }

  std::size_t allocSize = size;
  if (allocSize < sizeof(FreeBlock)) {
    allocSize = sizeof(FreeBlock);
  }

  std::size_t padding = 0;
  FreeBlock *prevBlock = nullptr;
  FreeBlock *foundBlock = nullptr;

  Find(allocSize, alignment, padding, prevBlock, foundBlock);

  if (foundBlock == nullptr) {
    return nullptr;
  }

  std::size_t requiredSpace = padding + allocSize;
  std::size_t remainingSpace = foundBlock->blockSize - requiredSpace;

  if (prevBlock != nullptr) {
    prevBlock->next = foundBlock->next;
  } else {
    m_freeList = foundBlock->next;
  }

  if (remainingSpace >= sizeof(FreeBlock)) {
    FreeBlock *newBlock =
        reinterpret_cast<FreeBlock *>(ToUptr(foundBlock) + requiredSpace);
    newBlock->blockSize = remainingSpace;
    newBlock->next = nullptr;

    InsertAndCoalesce(newBlock);
  } else {
    requiredSpace = foundBlock->blockSize;
    padding = requiredSpace - allocSize;
  }

  uptr blockStart = ToUptr(foundBlock);
  uptr userAddress = blockStart + padding;

  AllocationHeader *header = reinterpret_cast<AllocationHeader *>(
      userAddress - sizeof(AllocationHeader));
  header->blockSize = requiredSpace;
  header->padding = padding;

  m_stats.RecordAllocation(requiredSpace);

  return reinterpret_cast<void *>(userAddress);
}

// Returns a block to the free list and attempts to merge neighbors.
void FreeListAllocator::Free(void *ptr) {
  if (ptr == nullptr)
    return;

#ifndef NDEBUG
  {
    uptr userAddress = ToUptr(ptr);
    uptr start = ToUptr(m_start);
    uptr end = start + m_totalSize;

    assert(userAddress >= start && userAddress < end &&
           "Pointer does not belong to this free-list allocator");
  }
#endif

  uptr userAddress = ToUptr(ptr);
  AllocationHeader *header = reinterpret_cast<AllocationHeader *>(
      userAddress - sizeof(AllocationHeader));

  std::size_t blockSize = header->blockSize;
  std::size_t padding = header->padding;

#ifndef NDEBUG
  assert(blockSize > 0 && blockSize <= m_totalSize &&
         "Allocation header is corrupted or pointer is invalid");
#endif

  uptr blockStart = userAddress - padding;
  FreeBlock *freeBlock = reinterpret_cast<FreeBlock *>(blockStart);
  freeBlock->blockSize = blockSize;
  freeBlock->next = nullptr;

  m_stats.RecordDeallocation(blockSize);
  InsertAndCoalesce(freeBlock);
}

// Resets the allocator to a single free block spanning the whole buffer.
void FreeListAllocator::Reset() {
  m_freeList = reinterpret_cast<FreeBlock *>(m_start);
  m_freeList->blockSize = m_totalSize;
  m_freeList->next = nullptr;
  m_stats.Reset();
}

// Chooses the active placement-policy search strategy.
void FreeListAllocator::Find(std::size_t size, std::size_t alignment,
                             std::size_t &outPadding, FreeBlock *&outPrevBlock,
                             FreeBlock *&outFoundBlock) {
  switch (m_policy) {
  case PlacementPolicy::FirstFit:
    FindFirstFit(size, alignment, outPadding, outPrevBlock, outFoundBlock);
    break;
  case PlacementPolicy::BestFit:
    FindBestFit(size, alignment, outPadding, outPrevBlock, outFoundBlock);
    break;
  }
}

// Scans for the first block large enough to satisfy the request.
void FreeListAllocator::FindFirstFit(std::size_t size, std::size_t alignment,
                                     std::size_t &outPadding,
                                     FreeBlock *&outPrevBlock,
                                     FreeBlock *&outFoundBlock) {
  FreeBlock *prev = nullptr;
  FreeBlock *current = m_freeList;

  while (current != nullptr) {
    std::size_t padding = CalculatePaddingWithHeader<AllocationHeader>(
        ToUptr(current), alignment);
    std::size_t requiredSpace = padding + size;
    if (current->blockSize >= requiredSpace) {
      outPadding = padding;
      outPrevBlock = prev;
      outFoundBlock = current;
      return;
    }

    prev = current;
    current = current->next;
  }

  outPrevBlock = nullptr;
  outFoundBlock = nullptr;
}

// Scans for the tightest-fitting block to reduce wasted space.
void FreeListAllocator::FindBestFit(std::size_t size, std::size_t alignment,
                                    std::size_t &outPadding,
                                    FreeBlock *&outPrevBlock,
                                    FreeBlock *&outFoundBlock) {
  FreeBlock *bestPrev = nullptr;
  FreeBlock *bestBlock = nullptr;
  std::size_t bestPadding = 0;
  std::size_t bestDiff = std::numeric_limits<std::size_t>::max();

  FreeBlock *prev = nullptr;
  FreeBlock *current = m_freeList;

  while (current != nullptr) {
    std::size_t padding = CalculatePaddingWithHeader<AllocationHeader>(
        ToUptr(current), alignment);
    std::size_t requiredSpace = padding + size;

    if (current->blockSize >= requiredSpace) {
      std::size_t diff = current->blockSize - requiredSpace;

      if (diff < bestDiff) {
        bestDiff = diff;
        bestPrev = prev;
        bestBlock = current;
        bestPadding = padding;

        if (diff == 0)
          break;
      }
    }

    prev = current;
    current = current->next;
  }

  outPadding = bestPadding;
  outPrevBlock = bestPrev;
  outFoundBlock = bestBlock;
}

// Inserts a free block in order and merges touching neighbors.
void FreeListAllocator::InsertAndCoalesce(FreeBlock *block) {
  FreeBlock *prev = nullptr;
  FreeBlock *current = m_freeList;

  while (current != nullptr && ToUptr(current) < ToUptr(block)) {
    prev = current;
    current = current->next;
  }

  if (prev == nullptr) {
    block->next = m_freeList;
    m_freeList = block;
  } else {
    block->next = current;
    prev->next = block;
  }

  if (block->next != nullptr) {
    uptr blockEnd = ToUptr(block) + block->blockSize;
    uptr nextStart = ToUptr(block->next);

    if (blockEnd == nextStart) {
      block->blockSize += block->next->blockSize;
      block->next = block->next->next;
    }
  }

  if (prev != nullptr) {
    uptr prevEnd = ToUptr(prev) + prev->blockSize;
    uptr blockStart = ToUptr(block);

    if (prevEnd == blockStart) {
      prev->blockSize += block->blockSize;
      prev->next = block->next;
    }
  }
}

} // namespace Zenith
