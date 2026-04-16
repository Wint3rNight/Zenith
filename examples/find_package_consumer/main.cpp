#include <Zenith/Zenith.hpp>

#include <array>
#include <cstddef>
#include <iostream>

struct Particle {
  float x;
  float y;
  float z;
  float lifetime;
};

int main() {
  std::array<std::byte, 4096> frameMemory{};
  Zenith::LinearAllocator frameAllocator(frameMemory.size(), frameMemory.data());

  float *scratch = Zenith::AllocateType<float>(frameAllocator, 32);
  if (scratch == nullptr) {
    std::cerr << "Failed to allocate frame scratch memory\n";
    return 1;
  }

  Zenith::PoolAllocator particlePool(sizeof(Particle), 16);
  Particle *particle =
      Zenith::Construct<Particle>(particlePool, Particle{1.0f, 2.0f, 3.0f, 5.0f});
  if (particle == nullptr) {
    std::cerr << "Failed to construct particle\n";
    return 1;
  }

  particle->lifetime -= 1.0f;
  Zenith::Destroy(particlePool, particle);
  frameAllocator.Reset();

  std::cout << "Zenith find_package consumer completed successfully\n";
  return 0;
}
