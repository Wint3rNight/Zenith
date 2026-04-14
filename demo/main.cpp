#include "Zenith/Common.hpp"
#include "Zenith/FreeListAllocator.hpp"
#include "Zenith/LinearAllocator.hpp"
#include "Zenith/MemoryStats.hpp"
#include "Zenith/PoolAllocator.hpp"
#include "Zenith/StackAllocator.hpp"

#include <cstdio>
#include <cstring>
#include <iostream>

// Prints an allocation result and infers its visible alignment.
void PrintAllocation(const char *label, void *ptr, std::size_t size) {
  if (ptr) {
    std::size_t alignVal = Zenith::ToUptr(ptr) % 16 == 0  ? std::size_t{16}
                           : Zenith::ToUptr(ptr) % 8 == 0 ? std::size_t{8}
                           : Zenith::ToUptr(ptr) % 4 == 0 ? std::size_t{4}
                                                          : std::size_t{1};
    std::printf("  %-20s → address: %p  (size: %zu, aligned to %zu)\n", label,
                ptr, size, alignVal);
  } else {
    std::printf("  %-20s → ALLOCATION FAILED (nullptr)\n", label);
  }
}

struct Vec3 {
  float x, y, z;
};

struct Transform {
  Vec3 position;
  Vec3 rotation;
  Vec3 scale;
  float padding;
};

struct Particle {
  Vec3 position;
  Vec3 velocity;
  float lifetime;
  float size;
};

// Demonstrates sequential frame-style allocations with bulk reset.
void DemoLinearAllocator() {
  std::cout << "\n";
  std::cout
      << "╔══════════════════════════════════════════════════════════════╗\n";
  std::cout
      << "║          DEMO 1: LINEAR ALLOCATOR (Arena Allocator)         ║\n";
  std::cout
      << "╠══════════════════════════════════════════════════════════════╣\n";
  std::cout
      << "║  Use case: Per-frame scratch memory.                        ║\n";
  std::cout
      << "║  Allocations are sequential. Free all at once via Reset().  ║\n";
  std::cout
      << "╚══════════════════════════════════════════════════════════════╝\n";

  Zenith::LinearAllocator allocator(1024);

  std::cout << "\n  -- Simulating Frame 1 allocations --\n";

  void *transformData =
      allocator.Allocate(sizeof(Transform), alignof(Transform));
  PrintAllocation("Transform", transformData, sizeof(Transform));

  void *particleArray =
      allocator.Allocate(sizeof(Particle) * 4, alignof(Particle));
  PrintAllocation("4x Particles", particleArray, sizeof(Particle) * 4);

  void *tempString = allocator.Allocate(64, 1);
  PrintAllocation("Temp string (64B)", tempString, 64);

  void *vec3Array = allocator.Allocate(sizeof(Vec3) * 10, alignof(Vec3));
  PrintAllocation("10x Vec3", vec3Array, sizeof(Vec3) * 10);

  if (transformData) {
    Transform *t = static_cast<Transform *>(transformData);
    t->position = {1.0f, 2.0f, 3.0f};
    t->rotation = {0.0f, 90.0f, 0.0f};
    t->scale = {1.0f, 1.0f, 1.0f};
    std::printf("\n  Transform position: (%.1f, %.1f, %.1f)\n",
                static_cast<double>(t->position.x),
                static_cast<double>(t->position.y),
                static_cast<double>(t->position.z));
  }

  allocator.GetStats().Print("Linear (Frame 1)");
  std::cout << "\n  -- Reset (simulate new frame) --\n";
  allocator.Reset();
  std::cout << "  All frame 1 allocations freed instantly. Used: "
            << allocator.GetUsedMemory() << " bytes.\n";

  void *newData = allocator.Allocate(256, 16);
  PrintAllocation("Frame 2 data", newData, 256);

  allocator.GetStats().Print("Linear (Frame 2)");
  allocator.Reset();
}

// Demonstrates LIFO allocation and free behavior.
void DemoStackAllocator() {
  std::cout << "\n";
  std::cout
      << "╔══════════════════════════════════════════════════════════════╗\n";
  std::cout
      << "║          DEMO 2: STACK ALLOCATOR (LIFO)                     ║\n";
  std::cout
      << "╠══════════════════════════════════════════════════════════════╣\n";
  std::cout
      << "║  Use case: Scoped temporary allocations.                    ║\n";
  std::cout
      << "║  Must free in reverse order (like a stack).                 ║\n";
  std::cout
      << "╚══════════════════════════════════════════════════════════════╝\n";

  Zenith::StackAllocator allocator(1024);

  std::cout << "\n  -- Allocating in order: A, B, C --\n";

  void *a = allocator.Allocate(64, 8);
  PrintAllocation("Block A (64B)", a, 64);

  void *b = allocator.Allocate(128, 16);
  PrintAllocation("Block B (128B)", b, 128);

  void *c = allocator.Allocate(32, 8);
  PrintAllocation("Block C (32B)", c, 32);

  std::cout << "\n  Used memory: " << allocator.GetUsedMemory() << " bytes\n";
  allocator.GetStats().Print("Stack (A+B+C)");

  std::cout << "  -- Freeing in reverse: C, B, A --\n";

  allocator.Free(c);
  std::cout << "  Freed C. Used: " << allocator.GetUsedMemory() << " bytes\n";

  allocator.Free(b);
  std::cout << "  Freed B. Used: " << allocator.GetUsedMemory() << " bytes\n";

  allocator.Free(a);
  std::cout << "  Freed A. Used: " << allocator.GetUsedMemory() << " bytes\n";

  allocator.GetStats().Print("Stack (all freed)");
  allocator.Reset();
}

// Demonstrates constant-time allocation from fixed-size chunks.
void DemoPoolAllocator() {
  std::cout << "\n";
  std::cout
      << "╔══════════════════════════════════════════════════════════════╗\n";
  std::cout
      << "║          DEMO 3: POOL ALLOCATOR (Fixed-size chunks)         ║\n";
  std::cout
      << "╠══════════════════════════════════════════════════════════════╣\n";
  std::cout
      << "║  Use case: ECS components, particles, identical objects.    ║\n";
  std::cout
      << "║  O(1) alloc and free. Zero fragmentation.                   ║\n";
  std::cout
      << "╚══════════════════════════════════════════════════════════════╝\n";

  constexpr std::size_t CHUNK_SIZE = 32;
  constexpr std::size_t CHUNK_COUNT = 10;

  Zenith::PoolAllocator allocator(CHUNK_SIZE, CHUNK_COUNT);

  std::cout << "\n  Pool: " << CHUNK_COUNT << " chunks of " << CHUNK_SIZE
            << " bytes each. Free: " << allocator.GetFreeCount() << "\n";

  std::cout << "\n  -- Allocating 5 particles --\n";
  void *particles[5];
  for (int i = 0; i < 5; ++i) {
    particles[i] = allocator.Allocate(sizeof(Particle));
    if (particles[i]) {
      Particle *p = static_cast<Particle *>(particles[i]);
      p->position = {static_cast<float>(i), 0.0f, 0.0f};
      p->lifetime = 2.0f;
      std::printf("  Particle %d at %p (pos: %.0f, 0, 0)\n", i, particles[i],
                  static_cast<double>(p->position.x));
    }
  }

  std::cout << "\n  Free chunks remaining: " << allocator.GetFreeCount()
            << "\n";

  std::cout << "\n  -- Freeing particles 1 and 3 --\n";
  allocator.Free(particles[1]);
  allocator.Free(particles[3]);
  std::cout << "  Free chunks after freeing 2: " << allocator.GetFreeCount()
            << "\n";

  std::cout << "\n  -- Allocating 2 new particles (reusing freed chunks) --\n";
  void *newP1 = allocator.Allocate(sizeof(Particle));
  void *newP2 = allocator.Allocate(sizeof(Particle));
  std::printf("  New particle A at %p\n", newP1);
  std::printf("  New particle B at %p\n", newP2);

  allocator.GetStats().Print("Pool (Particles)");
  allocator.Reset();
}

// Demonstrates variable-size allocation with block reuse and coalescing.
void DemoFreeListAllocator() {
  std::cout << "\n";
  std::cout
      << "╔══════════════════════════════════════════════════════════════╗\n";
  std::cout
      << "║          DEMO 4: FREE LIST ALLOCATOR (General purpose)      ║\n";
  std::cout
      << "╠══════════════════════════════════════════════════════════════╣\n";
  std::cout
      << "║  Use case: General-purpose allocator replacing malloc.      ║\n";
  std::cout
      << "║  Supports variable sizes, any-order free, coalescing.       ║\n";
  std::cout
      << "╚══════════════════════════════════════════════════════════════╝\n";

  Zenith::FreeListAllocator allocator(
      2048, Zenith::FreeListAllocator::PlacementPolicy::FirstFit);

  std::cout << "\n  -- Allocating various sizes --\n";

  void *mesh = allocator.Allocate(512, 16);
  PrintAllocation("Mesh data (512B)", mesh, 512);

  void *texture = allocator.Allocate(256, 16);
  PrintAllocation("Texture (256B)", texture, 256);

  void *audio = allocator.Allocate(128, 8);
  PrintAllocation("Audio clip (128B)", audio, 128);

  void *config = allocator.Allocate(64, 8);
  PrintAllocation("Config data (64B)", config, 64);

  allocator.GetStats().Print("FreeList (4 allocs)");

  std::cout << "  -- Freeing texture and audio (middle blocks) --\n";
  allocator.Free(texture);
  allocator.Free(audio);

  allocator.GetStats().Print("FreeList (after 2 frees)");

  std::cout << "  -- Allocating 300B (should fit in coalesced space) --\n";
  void *newBlock = allocator.Allocate(300, 16);
  PrintAllocation("New 300B block", newBlock, 300);

  allocator.Free(mesh);
  allocator.Free(config);
  allocator.Free(newBlock);

  allocator.GetStats().Print("FreeList (all freed)");
  allocator.Reset();
}

// Runs all allocator demos and prints the banner.
int main() {
  std::cout
      << "╔══════════════════════════════════════════════════════════════╗\n";
  std::cout
      << "║                                                              ║\n";
  std::cout
      << "║               ZENITH MEMORY — Allocator Demo                ║\n";
  std::cout
      << "║                                                              ║\n";
  std::cout
      << "║    A modern C++ memory allocator library for game engines    ║\n";
  std::cout
      << "║                                                              ║\n";
  std::cout
      << "╚══════════════════════════════════════════════════════════════╝\n";

  DemoLinearAllocator();
  DemoStackAllocator();
  DemoPoolAllocator();
  DemoFreeListAllocator();

  std::cout
      << "\n══════════════════════════════════════════════════════════════\n";
  std::cout << "  All demos completed successfully!\n";
  std::cout
      << "══════════════════════════════════════════════════════════════\n\n";

  return 0;
}
