#pragma once

#include "Allocator.hpp"

#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace Zenith {

// Returns typed storage for one or more objects from the provided allocator.
template <typename T>
T *AllocateType(Allocator &allocator, std::size_t count = 1) {
  static_assert(!std::is_void_v<T>, "AllocateType requires a concrete type");

  if (count == 0 ||
      count > (std::numeric_limits<std::size_t>::max() / sizeof(T))) {
    return nullptr;
  }

  void *memory = allocator.Allocate(sizeof(T) * count, alignof(T));
  return static_cast<T *>(memory);
}

// Constructs one object in allocator-backed storage.
template <typename T, typename... Args>
T *Construct(Allocator &allocator, Args &&...args) {
  static_assert(!std::is_array_v<T>, "Construct does not support array types");

  void *memory = allocator.Allocate(sizeof(T), alignof(T));
  if (memory == nullptr) {
    return nullptr;
  }

  return ::new (memory) T(std::forward<Args>(args)...);
}

// Destroys one object and forwards the storage back to the allocator.
// For reset-based allocators, this only runs the destructor; memory is
// reclaimed when Reset() is called.
template <typename T>
void Destroy(Allocator &allocator, T *ptr) {
  if (ptr == nullptr) {
    return;
  }

  ptr->~T();
  allocator.Free(ptr);
}

} // namespace Zenith
