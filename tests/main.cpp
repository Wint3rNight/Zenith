#include <Zenith/Zenith.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

struct Tracked {
  explicit Tracked(int initialValue) : value(initialValue) { ++aliveCount; }
  ~Tracked() { --aliveCount; }

  int value;
  static int aliveCount;
};

int Tracked::aliveCount = 0;

[[noreturn]] void Fail(const char *expr, const char *file, int line) {
  throw std::runtime_error(std::string(file) + ":" + std::to_string(line) +
                           " check failed: " + expr);
}

#define CHECK(expr)                                                           \
  do {                                                                        \
    if (!(expr)) {                                                            \
      Fail(#expr, __FILE__, __LINE__);                                        \
    }                                                                         \
  } while (false)

bool IsAligned(const void *ptr, std::size_t alignment) {
  return ptr != nullptr && (Zenith::ToUptr(ptr) % alignment) == 0;
}

void TestLinearAllocatorResetAndAlignment() {
  Zenith::LinearAllocator allocator(256);

  void *first = allocator.Allocate(32, 16);
  void *second = allocator.Allocate(48, 32);

  CHECK(first != nullptr);
  CHECK(second != nullptr);
  CHECK(IsAligned(first, 16));
  CHECK(IsAligned(second, 32));
  CHECK(allocator.GetUsedMemory() > 0);
  CHECK(allocator.GetStats().HasOutstandingMemory());

  allocator.Reset();

  CHECK(allocator.GetUsedMemory() == 0);
  CHECK(!allocator.GetStats().HasOutstandingMemory());
  CHECK(allocator.GetStats().allocationCount == 0);
}

void TestLinearAllocatorOutOfMemoryHandling() {
  Zenith::LinearAllocator allocator(64);

  void *valid = allocator.Allocate(48, 16);
  CHECK(valid != nullptr);
  CHECK(allocator.Allocate(32, 16) == nullptr);
}

void TestStackAllocatorLifoAndExternalBuffer() {
  alignas(std::max_align_t) std::array<std::byte, 256> backing{};
  Zenith::StackAllocator allocator(backing.size(), backing.data());

  void *first = allocator.Allocate(32, 16);
  void *second = allocator.Allocate(48, 16);

  CHECK(first != nullptr);
  CHECK(second != nullptr);
  CHECK(allocator.GetUsedMemory() > 0);

  std::size_t usedBeforeFree = allocator.GetUsedMemory();
  allocator.Free(second);
  CHECK(allocator.GetUsedMemory() < usedBeforeFree);

  allocator.Free(first);
  CHECK(allocator.GetUsedMemory() == 0);
  CHECK(!allocator.GetStats().HasOutstandingMemory());
}

void TestPoolAllocatorReuseAndExhaustion() {
  alignas(std::max_align_t) std::array<std::byte, 256> backing{};
  Zenith::PoolAllocator allocator(32, 8, backing.data());

  void *first = allocator.Allocate(32, 16);
  void *second = allocator.Allocate(32, 16);

  CHECK(first != nullptr);
  CHECK(second != nullptr);
  CHECK(first != second);
  CHECK(allocator.GetFreeCount() == 6);

  allocator.Free(first);
  CHECK(allocator.GetFreeCount() == 7);

  void *reused = allocator.Allocate(32, 16);
  CHECK(reused == first);
  CHECK(allocator.GetFreeCount() == 6);

  void *slots[6];
  for (void *&slot : slots) {
    slot = allocator.Allocate(32, 16);
    CHECK(slot != nullptr);
  }

  CHECK(allocator.GetFreeCount() == 0);
  CHECK(allocator.Allocate(32, 16) == nullptr);
}

void TestFreeListAllocatorCoalescingAndReuse() {
  alignas(std::max_align_t) std::array<std::byte, 512> backing{};
  Zenith::FreeListAllocator allocator(backing.size(), backing.data());

  void *first = allocator.Allocate(64, 16);
  void *second = allocator.Allocate(64, 16);
  void *third = allocator.Allocate(64, 16);

  CHECK(first != nullptr);
  CHECK(second != nullptr);
  CHECK(third != nullptr);

  allocator.Free(second);
  allocator.Free(first);

  void *merged = allocator.Allocate(96, 16);
  CHECK(merged == first);

  allocator.Free(merged);
  allocator.Free(third);
  CHECK(!allocator.GetStats().HasOutstandingMemory());
}

void TestTypedHelpersConstructAndDestroy() {
  Zenith::PoolAllocator allocator(32, 2);
  CHECK(Tracked::aliveCount == 0);

  Tracked *tracked = Zenith::Construct<Tracked>(allocator, 42);
  CHECK(tracked != nullptr);
  CHECK(tracked->value == 42);
  CHECK(Tracked::aliveCount == 1);

  Zenith::Destroy(allocator, tracked);
  CHECK(Tracked::aliveCount == 0);
  CHECK(!allocator.GetStats().HasOutstandingMemory());
}

void TestAllocateTypeGuardRails() {
  Zenith::LinearAllocator allocator(64);

  CHECK(Zenith::AllocateType<std::uint32_t>(allocator, 0) == nullptr);
  CHECK(Zenith::AllocateType<std::uint32_t>(
            allocator, std::numeric_limits<std::size_t>::max()) == nullptr);
}

struct TestCase {
  const char *name;
  void (*fn)();
};

} // namespace

int main() {
  const TestCase tests[] = {
      {"Linear allocator reset and alignment", TestLinearAllocatorResetAndAlignment},
      {"Linear allocator out-of-memory handling",
       TestLinearAllocatorOutOfMemoryHandling},
      {"Stack allocator LIFO and external buffer",
       TestStackAllocatorLifoAndExternalBuffer},
      {"Pool allocator reuse and exhaustion", TestPoolAllocatorReuseAndExhaustion},
      {"Free-list allocator coalescing and reuse",
       TestFreeListAllocatorCoalescingAndReuse},
      {"Typed helpers construct and destroy", TestTypedHelpersConstructAndDestroy},
      {"Typed helper guard rails", TestAllocateTypeGuardRails},
  };

  std::size_t passed = 0;

  for (const TestCase &test : tests) {
    try {
      test.fn();
      ++passed;
      std::cout << "[PASS] " << test.name << "\n";
    } catch (const std::exception &ex) {
      std::cerr << "[FAIL] " << test.name << "\n";
      std::cerr << "       " << ex.what() << "\n";
      return 1;
    }
  }

  std::cout << "All " << passed << " tests passed.\n";
  return 0;
}
