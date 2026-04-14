#pragma once

#include "Allocator.hpp"

namespace Zenith {

class LinearAllocator : public Allocator {
public:
  // Creates a linear allocator with a fixed backing buffer.
  explicit LinearAllocator(std::size_t totalSize);
  // Destroys the allocator and its owned backing memory.
  ~LinearAllocator() override;
  // Reserves the next aligned block from the linear buffer.
  void *Allocate(std::size_t size,
                 std::size_t alignment = DEFAULT_ALIGNMENT) override;
  // Individual frees are ignored because linear allocators reset in bulk.
  void Free(void *ptr) override;
  // Releases all allocations at once by rewinding the offset.
  void Reset() override;
  // Returns how many bytes have been consumed from the buffer.
  std::size_t GetUsedMemory() const { return m_offset; }

private:
  std::size_t m_offset = 0;
};

} // namespace Zenith
