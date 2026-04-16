#include "Zenith/StackAllocator.hpp"

#include <cassert>

namespace Zenith {

// Initializes the allocator with an empty stack offset.
StackAllocator::StackAllocator(std::size_t totalSize)
    : Allocator(totalSize), m_offset(0) {}

// Initializes the allocator on top of caller-provided backing memory.
StackAllocator::StackAllocator(std::size_t totalSize, void *preallocated)
    : Allocator(totalSize, preallocated), m_offset(0) {}

// Clears the tracked stack offset before destruction.
StackAllocator::~StackAllocator() { m_offset = 0; }

// Pushes a new aligned allocation and stores metadata just before it.
void *StackAllocator::Allocate(std::size_t size, std::size_t alignment) {
  assert(size > 0 && "Cannot allocate 0 bytes");
  assert(IsPowerOfTwo(alignment) && "Alignment must be a power of 2");

  if (size == 0 || !IsPowerOfTwo(alignment)) {
    return nullptr;
  }

  uptr currentAddress = ToUptr(m_start) + m_offset;
  std::size_t padding =
      CalculatePaddingWithHeader<AllocationHeader>(currentAddress, alignment);
  if (m_offset + padding + size > m_totalSize) {
    return nullptr;
  }

  uptr alignedAddress = currentAddress + padding;
  AllocationHeader *header = reinterpret_cast<AllocationHeader *>(
      alignedAddress - sizeof(AllocationHeader));
  header->padding = padding;
  header->previousOffset = m_offset;

#ifndef NDEBUG
  header->previousAllocation = m_prevAllocation;
#endif

  m_offset += padding + size;

  void *userPtr = reinterpret_cast<void *>(alignedAddress);

#ifndef NDEBUG
  m_prevAllocation = userPtr;
#endif

  m_stats.RecordAllocation(padding + size);

  return userPtr;
}

// Pops the most recent allocation and restores the previous offset.
void StackAllocator::Free(void *ptr) {
  if (ptr == nullptr)
    return;

#ifndef NDEBUG
  assert(ptr == m_prevAllocation &&
         "Stack allocator: Free() must be called in LIFO order! "
         "You must free the most recently allocated pointer first.");
#endif

  AllocationHeader *header = reinterpret_cast<AllocationHeader *>(
      ToUptr(ptr) - sizeof(AllocationHeader));

  std::size_t previousOffset = header->previousOffset;
  std::size_t releasedBytes = m_offset - previousOffset;

  m_offset = previousOffset;

  m_stats.RecordDeallocation(releasedBytes);

#ifndef NDEBUG
  m_prevAllocation = header->previousAllocation;
#endif
}

// Empties the stack allocator in one operation.
void StackAllocator::Reset() {
  m_offset = 0;
  m_stats.Reset();

#ifndef NDEBUG
  m_prevAllocation = nullptr;
#endif
}

} // namespace Zenith
