#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Zenith {

namespace DebugGuard {

constexpr std::size_t GUARD_SIZE = 4;
constexpr std::uint8_t GUARD_PATTERN = 0xFD;
constexpr std::uint8_t FREED_PATTERN = 0xCD;
constexpr std::uint8_t UNINIT_PATTERN = 0xCC;

#ifndef NDEBUG

// Writes guard bytes around the user allocation and fills it as uninitialized.
inline void WriteGuards(void *userPtr, std::size_t userSize) {
  auto *bytePtr = static_cast<std::uint8_t *>(userPtr);

  std::memset(bytePtr - GUARD_SIZE, GUARD_PATTERN, GUARD_SIZE);
  std::memset(bytePtr, UNINIT_PATTERN, userSize);
  std::memset(bytePtr + userSize, GUARD_PATTERN, GUARD_SIZE);
}

// Verifies that the front and back guard bytes are still intact.
inline void ValidateGuards(const void *userPtr, std::size_t userSize) {
  const auto *bytePtr = static_cast<const std::uint8_t *>(userPtr);

  for (std::size_t i = 0; i < GUARD_SIZE; ++i) {
    assert(*(bytePtr - GUARD_SIZE + i) == GUARD_PATTERN &&
           "MEMORY CORRUPTION: Front guard overwritten! Buffer underflow "
           "detected.");
  }

  for (std::size_t i = 0; i < GUARD_SIZE; ++i) {
    assert(
        *(bytePtr + userSize + i) == GUARD_PATTERN &&
        "MEMORY CORRUPTION: Back guard overwritten! Buffer overflow detected.");
  }
}

// Overwrites released memory with a debug pattern.
inline void PoisonFreedMemory(void *userPtr, std::size_t userSize) {
  std::memset(userPtr, FREED_PATTERN, userSize);
}

// Returns the extra bytes needed to store both debug guards.
constexpr std::size_t ExtraGuardBytes() {
  return GUARD_SIZE * 2;
}

#else

// No-op in release builds.
inline void WriteGuards(void *, std::size_t) {}
// No-op in release builds.
inline void ValidateGuards(const void *, std::size_t) {}
// No-op in release builds.
inline void PoisonFreedMemory(void *, std::size_t) {}
// Returns zero when debug guards are disabled.
constexpr std::size_t ExtraGuardBytes() { return 0; }

#endif

} // namespace DebugGuard
} // namespace Zenith
