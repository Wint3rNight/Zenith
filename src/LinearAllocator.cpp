#include "Zenith/LinearAllocator.hpp"

#include <cassert>

namespace Zenith {

// Initializes the allocator with an empty linear offset.
LinearAllocator::LinearAllocator(std::size_t totalSize)
    : Allocator(totalSize), m_offset(0) {}

// Clears the tracked offset before the base allocator frees the buffer.
LinearAllocator::~LinearAllocator() { m_offset = 0; }

// Bumps the current offset forward to hand out the next aligned block.
void *LinearAllocator::Allocate(std::size_t size, std::size_t alignment) {
  assert(size > 0 && "Cannot allocate 0 bytes");
  assert(IsPowerOfTwo(alignment) && "Alignment must be a power of 2");

  if (size == 0 || !IsPowerOfTwo(alignment)) {
    return nullptr;
  }

  uptr currentAddress = ToUptr(m_start) + m_offset;
  std::size_t padding = CalculatePadding(currentAddress, alignment);
  if (m_offset + padding + size > m_totalSize) {
    return nullptr;
  }

  m_offset += padding;
  void *alignedPtr = PtrAdd(m_start, m_offset);
  m_offset += size;
  m_stats.RecordAllocation(padding + size);

  return alignedPtr;
}

// Linear allocators do not support freeing individual allocations.
void LinearAllocator::Free([[maybe_unused]] void *ptr) {}

// Rewinds the allocator so the whole buffer can be reused.
void LinearAllocator::Reset() {
  m_offset = 0;
  m_stats.Reset();
}

} // namespace Zenith
