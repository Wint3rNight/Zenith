#pragma once

#include "Allocator.hpp"

namespace Zenith {

class StackAllocator : public Allocator {
public:
  struct AllocationHeader {
    std::size_t padding;
    std::size_t previousOffset;

#ifndef NDEBUG
    void *previousAllocation;
#endif
  };

  // Creates a LIFO allocator with a fixed backing buffer.
  explicit StackAllocator(std::size_t totalSize);
  // Creates a LIFO allocator using caller-owned backing memory.
  StackAllocator(std::size_t totalSize, void *preallocated);
  // Destroys the allocator and its owned backing memory.
  ~StackAllocator() override;

  // Pushes an aligned block onto the top of the stack.
  void *Allocate(std::size_t size,
                 std::size_t alignment = DEFAULT_ALIGNMENT) override;
  // Pops the most recent allocation off the stack.
  void Free(void *ptr) override;
  // Clears the entire stack and resets usage tracking.
  void Reset() override;

  // Returns how many bytes are currently reserved in the stack.
  std::size_t GetUsedMemory() const { return m_offset; }

private:
  std::size_t m_offset = 0;

#ifndef NDEBUG
  void *m_prevAllocation = nullptr;
#endif
};

} // namespace Zenith
