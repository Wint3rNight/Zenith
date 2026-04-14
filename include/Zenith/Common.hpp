#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace Zenith {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using uptr = std::uintptr_t;

constexpr std::size_t DEFAULT_ALIGNMENT = alignof(std::max_align_t);

// Returns true when the value can be used as a valid power-of-two alignment.
constexpr bool IsPowerOfTwo(std::size_t value) {
  return value != 0 && (value & (value - 1)) == 0;
}

// Computes the bytes needed to align the current address.
inline std::size_t CalculatePadding(uptr currentAddress,
                                    std::size_t alignment) {
  assert(IsPowerOfTwo(alignment) && "Alignment must be a power of 2");

  std::size_t remainder = currentAddress & (alignment - 1);

  if (remainder == 0) {
    return 0;
  }

  return alignment - remainder;
}

template <typename HeaderType>
// Computes alignment padding while reserving space for an allocation header.
std::size_t CalculatePaddingWithHeader(uptr currentAddress,
                                       std::size_t alignment) {
  assert(IsPowerOfTwo(alignment) && "Alignment must be a power of 2");

  std::size_t padding = CalculatePadding(currentAddress, alignment);
  std::size_t headerSize = sizeof(HeaderType);

  if (padding < headerSize) {
    std::size_t neededSpace = headerSize - padding;

    if (neededSpace % alignment != 0) {
      padding += alignment * (1 + neededSpace / alignment);
    } else {
      padding += neededSpace;
    }
  }

  return padding;
}

// Converts a pointer to an integer address for arithmetic.
inline uptr ToUptr(const void *ptr) { return reinterpret_cast<uptr>(ptr); }

// Converts an integer address back into a raw pointer.
inline void *ToPtr(uptr address) { return reinterpret_cast<void *>(address); }

// Advances a pointer by the requested byte offset.
inline void *PtrAdd(void *ptr, std::size_t offset) {
  return reinterpret_cast<void *>(ToUptr(ptr) + offset);
}

// Moves a pointer backward by the requested byte offset.
inline void *PtrSub(void *ptr, std::size_t offset) {
  return reinterpret_cast<void *>(ToUptr(ptr) - offset);
}

// Returns the byte distance between two pointers.
inline std::size_t PtrDiff(const void *a, const void *b) {
  return static_cast<std::size_t>(ToUptr(a) - ToUptr(b));
}

} // namespace Zenith
