#pragma once

#include "Scenario.hpp"

#include <Zenith/Zenith.hpp>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iosfwd>
#include <random>
#include <string>
#include <vector>

namespace Zenith::EngineSim {

struct EngineSimOptions {
  ScenarioKind scenario = ScenarioKind::Steady;
  std::size_t frames = 0;
  std::uint32_t seed = 1337;
  std::size_t reportEvery = 60;
  std::string csvPath;
};

struct FrameSnapshot {
  std::size_t frameIndex = 0;
  const char *scenarioName = "";
  bool isBurstFrame = false;
  std::size_t particlesAlive = 0;
  std::size_t resourceBlobsAlive = 0;
  std::size_t linearUsedBytes = 0;
  std::size_t linearPeakBytes = 0;
  std::size_t stackUsedBytes = 0;
  std::size_t stackPeakBytes = 0;
  std::size_t poolCapacity = 0;
  std::size_t poolFreeSlots = 0;
  std::size_t freeListUsedBytes = 0;
  std::size_t freeListPeakBytes = 0;
  std::size_t freeListTotalFreeBytes = 0;
  std::size_t freeListLargestFreeBlock = 0;
  std::size_t freeListFreeBlockCount = 0;
  double freeListFragmentationRatio = 0.0;
  std::size_t allocationFailuresThisFrame = 0;
  std::size_t allocationFailuresTotal = 0;
};

struct SimulationSummary {
  std::size_t framesRun = 0;
  std::size_t totalAllocationFailures = 0;
  std::size_t peakParticlesAlive = 0;
  std::size_t peakResourceBlobsAlive = 0;
  std::size_t maxLinearPeakBytes = 0;
  std::size_t maxStackPeakBytes = 0;
  std::size_t maxFreeListPeakBytes = 0;
  double maxFragmentationRatio = 0.0;

  std::size_t steadyFrameCount = 0;
  std::size_t burstFrameCount = 0;
  double steadyLinearPeakBytesTotal = 0.0;
  double burstLinearPeakBytesTotal = 0.0;
  double steadyPoolUsedTotal = 0.0;
  double burstPoolUsedTotal = 0.0;
  double steadyFreeListUsedBytesTotal = 0.0;
  double burstFreeListUsedBytesTotal = 0.0;
  double steadyFragmentationRatioTotal = 0.0;
  double burstFragmentationRatioTotal = 0.0;
  std::size_t steadyAllocationFailures = 0;
  std::size_t burstAllocationFailures = 0;
};

class EngineSim {
public:
  explicit EngineSim(const EngineSimOptions &options);

  bool Run(std::ostream &out, std::ostream &err);

  const std::vector<FrameSnapshot> &GetSnapshots() const { return m_snapshots; }
  const SimulationSummary &GetSummary() const { return m_summary; }

private:
  struct Particle {
    std::uint32_t id = 0;
    float position[3] = {0.0f, 0.0f, 0.0f};
    float velocity[3] = {0.0f, 0.0f, 0.0f};
    float lifetime = 0.0f;
    bool active = false;
  };

  enum class ResourceKind { Event, Asset, Command };

  struct ResourceBlob {
    void *memory = nullptr;
    std::size_t size = 0;
    std::uint32_t remainingLifetimeFrames = 0;
    ResourceKind kind = ResourceKind::Event;
  };

  static constexpr std::size_t kLinearAllocatorSize = 1024 * 1024;
  static constexpr std::size_t kStackAllocatorSize = 256 * 1024;
  static constexpr std::size_t kFreeListAllocatorSize = 8 * 1024 * 1024;
  static constexpr std::size_t kParticleCapacity = 50000;

  EngineSimOptions m_options;
  std::mt19937 m_rng;

  std::vector<std::byte> m_linearMemory;
  std::vector<std::byte> m_stackMemory;
  std::vector<std::byte> m_freeListMemory;

  LinearAllocator m_linearAllocator;
  StackAllocator m_stackAllocator;
  PoolAllocator m_particlePool;
  FreeListAllocator m_freeListAllocator;

  std::vector<Particle *> m_liveParticles;
  std::vector<ResourceBlob> m_liveBlobs;
  std::vector<FrameSnapshot> m_snapshots;
  SimulationSummary m_summary;

  std::uint32_t m_nextParticleId = 1;
  std::size_t m_allocationFailuresThisFrame = 0;
  std::size_t m_allocationFailuresTotal = 0;
  std::ofstream m_csvStream;

  static std::size_t RoundUp(std::size_t value, std::size_t alignment);

  void CleanupLiveData();
  void ResetFrameState();
  void UpdateParticles(const ScenarioFramePlan &plan);
  void SpawnParticles(std::size_t targetParticles);
  void DespawnParticlesToTarget(std::size_t targetParticles);
  void BuildFrameScratch();
  void RunSystemScratch();
  void AgeAndCollectResourceBlobs();
  void CreateResourceBlobs(const ScenarioFramePlan &plan);
  std::size_t DetermineBlobCreateCount(const ScenarioFramePlan &plan) const;
  ResourceKind ChooseResourceKind();
  std::size_t RandomSize(std::size_t minValue, std::size_t maxValue);
  std::uint32_t RandomLifetime(std::uint32_t minValue, std::uint32_t maxValue);
  float RandomFloat(float minValue, float maxValue);
  void RecordFailure();
  FrameSnapshot CaptureSnapshot(std::size_t frameIndex,
                                bool isBurstFrame) const;
  void UpdateSummary(const FrameSnapshot &snapshot);
  void PrintHeader(std::ostream &out) const;
  void PrintFrameReport(const FrameSnapshot &snapshot, std::ostream &out) const;
  void PrintSummary(std::ostream &out) const;
  bool OpenCsv(std::ostream &err);
  void WriteCsvHeader();
  void WriteCsvRow(const FrameSnapshot &snapshot);
};

} // namespace Zenith::EngineSim
