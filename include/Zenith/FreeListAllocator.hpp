#pragma once

#include "Allocator.hpp"

namespace Zenith {

class FreeListAllocator : public Allocator {
public:
  struct FreeBlock {
    std::size_t blockSize;
    FreeBlock *next;
  };

  struct AllocationHeader {
    std::size_t blockSize;
    std::size_t padding;
  };

  enum class PlacementPolicy {
    FirstFit,
    BestFit
  };

  explicit FreeListAllocator(
      std::size_t totalSize,
      PlacementPolicy policy = PlacementPolicy::FirstFit);
  // Destroys the allocator and its owned backing memory.
  ~FreeListAllocator() override;

  // Finds a suitable free block and splits it if needed.
  void *Allocate(std::size_t size,
                 std::size_t alignment = DEFAULT_ALIGNMENT) override;
  // Returns a block to the free list and merges neighbors when possible.
  void Free(void *ptr) override;
  // Restores the allocator to one large free block.
  void Reset() override;

private:
  // Dispatches to the active placement-policy search routine.
  void Find(std::size_t size, std::size_t alignment, std::size_t &outPadding,
            FreeBlock *&outPrevBlock, FreeBlock *&outFoundBlock);

  // Finds the first free block large enough for the request.
  void FindFirstFit(std::size_t size, std::size_t alignment,
                    std::size_t &outPadding, FreeBlock *&outPrevBlock,
                    FreeBlock *&outFoundBlock);

  // Finds the smallest free block that still satisfies the request.
  void FindBestFit(std::size_t size, std::size_t alignment,
                   std::size_t &outPadding, FreeBlock *&outPrevBlock,
                   FreeBlock *&outFoundBlock);

  // Inserts a freed block in address order and coalesces adjacent blocks.
  void InsertAndCoalesce(FreeBlock *block);

  PlacementPolicy m_policy;
  FreeBlock *m_freeList;
};

} // namespace Zenith
