# ZenithMemory

A modern C++ memory allocator library designed for game engines and high-performance systems.

## Allocators

| Allocator | Alloc | Free | Use Case |
|-----------|-------|------|----------|
| `LinearAllocator` | O(1) | N/A (Reset only) | Per-frame scratch memory |
| `StackAllocator` | O(1) | O(1) LIFO | Scoped temporary allocations |
| `PoolAllocator` | O(1) | O(1) | Fixed-size objects (ECS, particles) |
| `FreeListAllocator` | O(n) | O(1) | General-purpose malloc replacement |

## Requirements

- C++17 compiler (GCC 7+, Clang 5+, MSVC 19.14+)
- CMake 3.16+

## Using ZenithMemory in Your Project

There are **three ways** to add ZenithMemory to your project, from easiest to most manual:

---

### Method 1: FetchContent (Recommended — zero setup)

Add this to your project's `CMakeLists.txt`:

```cmake
include(FetchContent)

FetchContent_Declare(
    ZenithMemory
    GIT_REPOSITORY https://github.com/YOUR_USERNAME/ZenithMemory.git
    GIT_TAG        v1.0.0   # or main, or a commit hash
)

FetchContent_MakeAvailable(ZenithMemory)

# Link against it
target_link_libraries(YourApp PRIVATE Zenith::Memory)
```

That's it. CMake downloads, builds, and links the library automatically. No manual installation needed.

---

### Method 2: add_subdirectory (local copy)

Clone or copy ZenithMemory into your project tree:

```
YourProject/
├── CMakeLists.txt
├── src/
│   └── main.cpp
└── external/
    └── ZenithMemory/    ← clone here
```

Then in your `CMakeLists.txt`:

```cmake
add_subdirectory(external/ZenithMemory)
target_link_libraries(YourApp PRIVATE Zenith::Memory)
```

---

### Method 3: System install + find_package

Build and install ZenithMemory system-wide (or to a custom prefix):

```bash
cd ZenithMemory
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
make -j$(nproc)
sudo cmake --install .
```

Then in the consuming project:

```cmake
find_package(ZenithMemory 1.0 REQUIRED)
target_link_libraries(YourApp PRIVATE Zenith::Memory)
```

If you installed to a custom prefix:

```bash
cmake .. -DCMAKE_PREFIX_PATH=/path/to/prefix
```

---

## Quick Start

```cpp
#include <Zenith/Zenith.hpp>    // includes everything
// Or include only what you need:
// #include <Zenith/LinearAllocator.hpp>

int main() {
    // 1. Linear Allocator — per-frame scratch memory
    Zenith::LinearAllocator frameAlloc(1024 * 1024); // 1 MB

    void* temp = frameAlloc.Allocate(256, 16);
    // ... use temp ...
    frameAlloc.Reset(); // free everything at once


    // 2. Pool Allocator — fixed-size objects
    struct Particle { float x, y, z, life; }; // 16 bytes
    Zenith::PoolAllocator pool(16, 1000);      // 1000 particles

    void* p1 = pool.Allocate(sizeof(Particle));
    void* p2 = pool.Allocate(sizeof(Particle));
    pool.Free(p1); // O(1) — any order
    pool.Free(p2);


    // 3. Stack Allocator — scoped LIFO allocations
    Zenith::StackAllocator stack(4096);

    void* a = stack.Allocate(64);
    void* b = stack.Allocate(128);
    stack.Free(b); // must free in reverse order
    stack.Free(a);


    // 4. Free List Allocator — general purpose
    Zenith::FreeListAllocator general(1024 * 1024);

    void* mesh = general.Allocate(4096, 16);
    void* tex  = general.Allocate(2048, 16);
    general.Free(mesh); // any order, with coalescing
    general.Free(tex);


    // 5. Check statistics
    general.GetStats().Print("General");

    return 0;
}
```

## Building the Library Standalone

```bash
cd ZenithMemory
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

./ZenithDemo        # run the demo
./ZenithBenchmark   # run performance benchmarks
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `ZENITH_BUILD_EXAMPLES` | `ON` (standalone) / `OFF` (subdirectory) | Build demo and benchmark |
| `ZENITH_INSTALL` | `ON` (standalone) / `OFF` (subdirectory) | Generate install targets |

## Project Structure

```
ZenithMemory/
├── CMakeLists.txt
├── cmake/
│   └── ZenithMemoryConfig.cmake
├── include/Zenith/
│   ├── Zenith.hpp              ← convenience header (includes everything)
│   ├── Common.hpp              ← alignment math, pointer arithmetic
│   ├── Allocator.hpp           ← abstract base class
│   ├── MemoryStats.hpp         ← allocation statistics
│   ├── DebugGuard.hpp          ← memory corruption detection
│   ├── LinearAllocator.hpp
│   ├── StackAllocator.hpp
│   ├── PoolAllocator.hpp
│   └── FreeListAllocator.hpp
├── src/
│   ├── LinearAllocator.cpp
│   ├── StackAllocator.cpp
│   ├── PoolAllocator.cpp
│   └── FreeListAllocator.cpp
├── demo/
│   └── main.cpp
└── benchmark/
    └── main.cpp
```

## License

MIT — use it however you want.
