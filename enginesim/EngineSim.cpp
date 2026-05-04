#include "EngineSim.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>

namespace Zenith::EngineSim {

namespace {

struct ParticleScratch {
  float position[3];
  float speed;
};

double ToKiB(std::size_t bytes) { return static_cast<double>(bytes) / 1024.0; }

double ToKiB(double bytes) { return bytes / 1024.0; }

} // namespace

std::size_t EngineSim::RoundUp(std::size_t value, std::size_t alignment) {
  std::size_t remainder = value % alignment;
  if (remainder == 0) {
    return value;
  }
  return value + (alignment - remainder);
}

EngineSim::EngineSim(const EngineSimOptions &options)
    : m_options(options), m_rng(options.seed),
      m_linearMemory(kLinearAllocatorSize), m_stackMemory(kStackAllocatorSize),
      m_freeListMemory(kFreeListAllocatorSize),
      m_linearAllocator(m_linearMemory.size(), m_linearMemory.data()),
      m_stackAllocator(m_stackMemory.size(), m_stackMemory.data()),
      m_particlePool(RoundUp(sizeof(Particle), alignof(std::max_align_t)),
                     kParticleCapacity),
      m_freeListAllocator(m_freeListMemory.size(), m_freeListMemory.data()) {
  if (m_options.frames == 0) {
    m_options.frames = GetScenarioSettings(m_options.scenario).defaultFrames;
  }

  m_liveParticles.reserve(kParticleCapacity);
  m_liveBlobs.reserve(2500);
  m_snapshots.reserve(m_options.frames);
}

bool EngineSim::Run(std::ostream &out, std::ostream &err) {
  CleanupLiveData();
  m_snapshots.clear();
  m_summary = {};
  m_nextParticleId = 1;
  m_allocationFailuresThisFrame = 0;
  m_allocationFailuresTotal = 0;

  if (!OpenCsv(err)) {
    return false;
  }

  PrintHeader(out);

  for (std::size_t frameIndex = 0; frameIndex < m_options.frames;
       ++frameIndex) {
    ResetFrameState();

    ScenarioFramePlan plan = GetFramePlan(m_options.scenario, frameIndex);

    UpdateParticles(plan);
    BuildFrameScratch();
    RunSystemScratch();
    AgeAndCollectResourceBlobs();
    CreateResourceBlobs(plan);

    FrameSnapshot snapshot = CaptureSnapshot(frameIndex, plan.isBurstFrame);
    m_snapshots.push_back(snapshot);
    UpdateSummary(snapshot);
    WriteCsvRow(snapshot);

    if (frameIndex == 0 || ((frameIndex + 1) % m_options.reportEvery) == 0 ||
        (frameIndex + 1) == m_options.frames) {
      PrintFrameReport(snapshot, out);
    }
  }

  PrintSummary(out);
  CleanupLiveData();

  if (m_csvStream.is_open()) {
    m_csvStream.close();
  }

  return true;
}

void EngineSim::CleanupLiveData() {
  for (Particle *particle : m_liveParticles) {
    Destroy(m_particlePool, particle);
  }
  m_liveParticles.clear();

  for (const ResourceBlob &blob : m_liveBlobs) {
    m_freeListAllocator.Free(blob.memory);
  }
  m_liveBlobs.clear();

  m_linearAllocator.Reset();
  m_stackAllocator.Reset();
  m_particlePool.Reset();
  m_freeListAllocator.Reset();
}

void EngineSim::ResetFrameState() {
  m_allocationFailuresThisFrame = 0;
  m_linearAllocator.Reset();
  m_stackAllocator.Reset();
}

void EngineSim::UpdateParticles(const ScenarioFramePlan &plan) {
  std::size_t index = 0;
  while (index < m_liveParticles.size()) {
    Particle *particle = m_liveParticles[index];
    particle->position[0] += particle->velocity[0];
    particle->position[1] += particle->velocity[1];
    particle->position[2] += particle->velocity[2];
    particle->lifetime -= 1.0f;

    if (particle->lifetime <= 0.0f) {
      particle->active = false;
      Destroy(m_particlePool, particle);
      m_liveParticles[index] = m_liveParticles.back();
      m_liveParticles.pop_back();
      continue;
    }

    ++index;
  }

  DespawnParticlesToTarget(plan.targetParticles);
  SpawnParticles(plan.targetParticles);
}

void EngineSim::SpawnParticles(std::size_t targetParticles) {
  while (m_liveParticles.size() < targetParticles) {
    Particle candidate = {};
    candidate.id = m_nextParticleId++;
    candidate.position[0] = RandomFloat(-100.0f, 100.0f);
    candidate.position[1] = RandomFloat(-50.0f, 50.0f);
    candidate.position[2] = RandomFloat(-100.0f, 100.0f);
    candidate.velocity[0] = RandomFloat(-0.25f, 0.25f);
    candidate.velocity[1] = RandomFloat(0.05f, 0.2f);
    candidate.velocity[2] = RandomFloat(-0.25f, 0.25f);
    candidate.lifetime = RandomFloat(90.0f, 180.0f);
    candidate.active = true;

    Particle *particle = Construct<Particle>(m_particlePool, candidate);
    if (particle == nullptr) {
      RecordFailure();
      break;
    }

    m_liveParticles.push_back(particle);
  }
}

void EngineSim::DespawnParticlesToTarget(std::size_t targetParticles) {
  while (m_liveParticles.size() > targetParticles) {
    Particle *particle = m_liveParticles.back();
    m_liveParticles.pop_back();
    particle->active = false;
    Destroy(m_particlePool, particle);
  }
}

void EngineSim::BuildFrameScratch() {
  std::size_t particleCount = m_liveParticles.size();
  std::size_t visibleCount = (particleCount * 3) / 4;
  std::size_t pairCount = std::min<std::size_t>(particleCount * 2, 120000);
  std::size_t commandBytes =
      4096 + std::min<std::size_t>(particleCount * 12, 128 * 1024);

  if (visibleCount > 0) {
    std::uint32_t *visibleIndices =
        AllocateType<std::uint32_t>(m_linearAllocator, visibleCount);
    if (visibleIndices == nullptr) {
      RecordFailure();
    } else {
      for (std::size_t i = 0; i < visibleCount; ++i) {
        visibleIndices[i] = m_liveParticles[i]->id;
      }
    }
  }

  if (particleCount > 0) {
    ParticleScratch *transformed =
        AllocateType<ParticleScratch>(m_linearAllocator, particleCount);
    if (transformed == nullptr) {
      RecordFailure();
    } else {
      for (std::size_t i = 0; i < particleCount; ++i) {
        Particle *particle = m_liveParticles[i];
        transformed[i].position[0] = particle->position[0] + 1.0f;
        transformed[i].position[1] = particle->position[1] + 1.0f;
        transformed[i].position[2] = particle->position[2] + 1.0f;
        transformed[i].speed = particle->velocity[0] * particle->velocity[0] +
                               particle->velocity[1] * particle->velocity[1] +
                               particle->velocity[2] * particle->velocity[2];
      }
    }
  }

  if (pairCount > 0) {
    std::array<std::uint32_t, 2> *pairs =
        AllocateType<std::array<std::uint32_t, 2>>(m_linearAllocator,
                                                   pairCount);
    if (pairs == nullptr) {
      RecordFailure();
    } else {
      for (std::size_t i = 0; i < pairCount; ++i) {
        pairs[i][0] = static_cast<std::uint32_t>(i % (particleCount + 1));
        pairs[i][1] = static_cast<std::uint32_t>((i + 1) % (particleCount + 1));
      }
    }
  }

  std::byte *commandPayload =
      AllocateType<std::byte>(m_linearAllocator, commandBytes);
  if (commandPayload == nullptr) {
    RecordFailure();
  } else {
    std::memset(commandPayload, 0xAB, commandBytes);
  }
}

void EngineSim::RunSystemScratch() {
  std::size_t batchCount = std::max<std::size_t>(
      128, std::min<std::size_t>(m_liveParticles.size(), 4096));

  void *allocationOrder[3] = {};
  std::size_t allocationCount = 0;

  float *sortKeys = AllocateType<float>(m_stackAllocator, batchCount);
  if (sortKeys == nullptr) {
    RecordFailure();
  } else {
    allocationOrder[allocationCount++] = sortKeys;
    for (std::size_t i = 0; i < batchCount; ++i) {
      sortKeys[i] = RandomFloat(0.0f, 1.0f);
    }
  }

  std::size_t filteredCount = (batchCount / 2) + 1;
  std::uint32_t *filteredIndices =
      AllocateType<std::uint32_t>(m_stackAllocator, filteredCount);
  if (filteredIndices == nullptr) {
    RecordFailure();
  } else {
    allocationOrder[allocationCount++] = filteredIndices;
    for (std::size_t i = 0; i < filteredCount; ++i) {
      filteredIndices[i] = static_cast<std::uint32_t>(i * 2);
    }
  }

  float *reductions = AllocateType<float>(m_stackAllocator, 256);
  if (reductions == nullptr) {
    RecordFailure();
  } else {
    allocationOrder[allocationCount++] = reductions;
    for (std::size_t i = 0; i < 256; ++i) {
      reductions[i] = static_cast<float>(i) * 0.5f;
    }
  }

  while (allocationCount > 0) {
    m_stackAllocator.Free(allocationOrder[--allocationCount]);
  }
}

void EngineSim::AgeAndCollectResourceBlobs() {
  std::size_t index = 0;
  while (index < m_liveBlobs.size()) {
    ResourceBlob &blob = m_liveBlobs[index];
    if (blob.remainingLifetimeFrames > 0) {
      --blob.remainingLifetimeFrames;
    }

    if (blob.remainingLifetimeFrames == 0) {
      m_freeListAllocator.Free(blob.memory);
      m_liveBlobs[index] = m_liveBlobs.back();
      m_liveBlobs.pop_back();
      continue;
    }

    ++index;
  }
}

void EngineSim::CreateResourceBlobs(const ScenarioFramePlan &plan) {
  std::size_t createCount = DetermineBlobCreateCount(plan);

  for (std::size_t i = 0; i < createCount; ++i) {
    std::size_t size = RandomSize(plan.blobMinSize, plan.blobMaxSize);
    void *memory = m_freeListAllocator.Allocate(size, 16);
    if (memory == nullptr) {
      RecordFailure();
      continue;
    }

    ResourceKind kind = ChooseResourceKind();
    std::uint8_t pattern = 0x11;
    switch (kind) {
    case ResourceKind::Event:
      pattern = 0x11;
      break;
    case ResourceKind::Asset:
      pattern = 0x22;
      break;
    case ResourceKind::Command:
      pattern = 0x33;
      break;
    }

    std::memset(memory, pattern, size);
    m_liveBlobs.push_back(
        {memory, size,
         RandomLifetime(plan.blobMinLifetime, plan.blobMaxLifetime), kind});
  }
}

std::size_t
EngineSim::DetermineBlobCreateCount(const ScenarioFramePlan &plan) const {
  if (plan.maxBlobCreatesPerFrame == 0) {
    return 0;
  }

  if (plan.targetBlobCount > 0) {
    if (m_liveBlobs.size() >= plan.targetBlobCount) {
      return 0;
    }

    std::size_t gap = plan.targetBlobCount - m_liveBlobs.size();
    return std::min(plan.maxBlobCreatesPerFrame, gap);
  }

  return std::min(plan.maxBlobCreatesPerFrame, plan.fixedBlobCreates);
}

EngineSim::ResourceKind EngineSim::ChooseResourceKind() {
  std::uniform_int_distribution<int> distribution(0, 2);
  switch (distribution(m_rng)) {
  case 0:
    return ResourceKind::Event;
  case 1:
    return ResourceKind::Asset;
  default:
    return ResourceKind::Command;
  }
}

std::size_t EngineSim::RandomSize(std::size_t minValue, std::size_t maxValue) {
  std::uniform_int_distribution<std::size_t> distribution(minValue, maxValue);
  return distribution(m_rng);
}

std::uint32_t EngineSim::RandomLifetime(std::uint32_t minValue,
                                        std::uint32_t maxValue) {
  std::uniform_int_distribution<std::uint32_t> distribution(minValue, maxValue);
  return distribution(m_rng);
}

float EngineSim::RandomFloat(float minValue, float maxValue) {
  std::uniform_real_distribution<float> distribution(minValue, maxValue);
  return distribution(m_rng);
}

void EngineSim::RecordFailure() {
  ++m_allocationFailuresThisFrame;
  ++m_allocationFailuresTotal;
}

FrameSnapshot EngineSim::CaptureSnapshot(std::size_t frameIndex,
                                         bool isBurstFrame) const {
  FrameSnapshot snapshot = {};
  snapshot.frameIndex = frameIndex;
  snapshot.scenarioName = ToString(m_options.scenario);
  snapshot.isBurstFrame = isBurstFrame;
  snapshot.particlesAlive = m_liveParticles.size();
  snapshot.resourceBlobsAlive = m_liveBlobs.size();
  snapshot.linearUsedBytes = m_linearAllocator.GetStats().usedBytes;
  snapshot.linearPeakBytes = m_linearAllocator.GetStats().peakUsedBytes;
  snapshot.stackUsedBytes = m_stackAllocator.GetStats().usedBytes;
  snapshot.stackPeakBytes = m_stackAllocator.GetStats().peakUsedBytes;
  snapshot.poolCapacity = m_particlePool.GetChunkCount();
  snapshot.poolFreeSlots = m_particlePool.GetFreeCount();
  snapshot.freeListUsedBytes = m_freeListAllocator.GetStats().usedBytes;
  snapshot.freeListPeakBytes = m_freeListAllocator.GetStats().peakUsedBytes;
  snapshot.freeListTotalFreeBytes = m_freeListAllocator.GetTotalFreeBytes();
  snapshot.freeListLargestFreeBlock =
      m_freeListAllocator.GetLargestFreeBlockSize();
  snapshot.freeListFreeBlockCount = m_freeListAllocator.GetFreeBlockCount();
  snapshot.freeListFragmentationRatio =
      m_freeListAllocator.GetExternalFragmentationRatio();
  snapshot.allocationFailuresThisFrame = m_allocationFailuresThisFrame;
  snapshot.allocationFailuresTotal = m_allocationFailuresTotal;
  return snapshot;
}

void EngineSim::UpdateSummary(const FrameSnapshot &snapshot) {
  m_summary.framesRun = snapshot.frameIndex + 1;
  m_summary.totalAllocationFailures = snapshot.allocationFailuresTotal;
  m_summary.peakParticlesAlive =
      std::max(m_summary.peakParticlesAlive, snapshot.particlesAlive);
  m_summary.peakResourceBlobsAlive =
      std::max(m_summary.peakResourceBlobsAlive, snapshot.resourceBlobsAlive);
  m_summary.maxLinearPeakBytes =
      std::max(m_summary.maxLinearPeakBytes, snapshot.linearPeakBytes);
  m_summary.maxStackPeakBytes =
      std::max(m_summary.maxStackPeakBytes, snapshot.stackPeakBytes);
  m_summary.maxFreeListPeakBytes =
      std::max(m_summary.maxFreeListPeakBytes, snapshot.freeListPeakBytes);
  m_summary.maxFragmentationRatio = std::max(
      m_summary.maxFragmentationRatio, snapshot.freeListFragmentationRatio);

  std::size_t poolUsed = snapshot.poolCapacity - snapshot.poolFreeSlots;
  if (snapshot.isBurstFrame) {
    ++m_summary.burstFrameCount;
    m_summary.burstLinearPeakBytesTotal +=
        static_cast<double>(snapshot.linearPeakBytes);
    m_summary.burstPoolUsedTotal += static_cast<double>(poolUsed);
    m_summary.burstFreeListUsedBytesTotal +=
        static_cast<double>(snapshot.freeListUsedBytes);
    m_summary.burstFragmentationRatioTotal +=
        snapshot.freeListFragmentationRatio;
    m_summary.burstAllocationFailures += snapshot.allocationFailuresThisFrame;
    return;
  }

  ++m_summary.steadyFrameCount;
  m_summary.steadyLinearPeakBytesTotal +=
      static_cast<double>(snapshot.linearPeakBytes);
  m_summary.steadyPoolUsedTotal += static_cast<double>(poolUsed);
  m_summary.steadyFreeListUsedBytesTotal +=
      static_cast<double>(snapshot.freeListUsedBytes);
  m_summary.steadyFragmentationRatioTotal +=
      snapshot.freeListFragmentationRatio;
  m_summary.steadyAllocationFailures += snapshot.allocationFailuresThisFrame;
}

void EngineSim::PrintHeader(std::ostream &out) const {
  out << "\nZenith EngineSim\n";
  out << "Scenario: " << ToString(m_options.scenario)
      << " | Frames: " << m_options.frames << " | Seed: " << m_options.seed
      << " | Report every: " << m_options.reportEvery << "\n";
  out << "---------------------------------------------------------------------"
         "-----------\n";
  out << "Frame  Mode  Particles  Blobs  LinearKiB  StackPeakKiB  PoolUsed  "
         "FreeListKiB  LargestFreeKiB  Frag%  Fail\n";
  out << "---------------------------------------------------------------------"
         "-----------\n";
}

void EngineSim::PrintFrameReport(const FrameSnapshot &snapshot,
                                 std::ostream &out) const {
  std::size_t poolUsed = snapshot.poolCapacity - snapshot.poolFreeSlots;
  char mode = snapshot.isBurstFrame ? 'B' : 'S';

  out << std::setw(5) << (snapshot.frameIndex + 1) << " " << std::setw(4)
      << mode << " " << std::setw(10) << snapshot.particlesAlive << " "
      << std::setw(6) << snapshot.resourceBlobsAlive << " " << std::setw(10)
      << std::fixed << std::setprecision(1) << ToKiB(snapshot.linearPeakBytes)
      << " " << std::setw(13) << ToKiB(snapshot.stackPeakBytes) << " "
      << std::setw(8) << poolUsed << " " << std::setw(11)
      << ToKiB(snapshot.freeListUsedBytes) << " " << std::setw(15)
      << ToKiB(snapshot.freeListLargestFreeBlock) << " " << std::setw(6)
      << (snapshot.freeListFragmentationRatio * 100.0) << " " << std::setw(4)
      << snapshot.allocationFailuresTotal << "\n";
}

void EngineSim::PrintSummary(std::ostream &out) const {
  auto average = [](double total, std::size_t count) {
    return count > 0 ? (total / static_cast<double>(count)) : 0.0;
  };

  out << "---------------------------------------------------------------------"
         "-----------\n";
  out << "Summary\n";
  out << "  Frames run: " << m_summary.framesRun << "\n";
  out << "  Peak particles alive: " << m_summary.peakParticlesAlive << "\n";
  out << "  Peak resource blobs alive: " << m_summary.peakResourceBlobsAlive
      << "\n";
  out << "  Max linear peak: " << std::fixed << std::setprecision(1)
      << ToKiB(m_summary.maxLinearPeakBytes) << " KiB\n";
  out << "  Max stack peak: " << ToKiB(m_summary.maxStackPeakBytes) << " KiB\n";
  out << "  Max free-list peak: " << ToKiB(m_summary.maxFreeListPeakBytes)
      << " KiB\n";
  out << "  Max free-list fragmentation: "
      << (m_summary.maxFragmentationRatio * 100.0) << "%\n";
  out << "  Total allocation failures: " << m_summary.totalAllocationFailures
      << "\n";

  out << "  Burst vs steady comparison\n";
  out << "    Steady frames: " << m_summary.steadyFrameCount << "\n";
  out << "    Burst frames: " << m_summary.burstFrameCount << "\n";

  if (m_summary.steadyFrameCount > 0) {
    out << "    Avg steady linear peak: "
        << ToKiB(average(m_summary.steadyLinearPeakBytesTotal,
                         m_summary.steadyFrameCount))
        << " KiB\n";
    out << "    Avg steady pool used: "
        << average(m_summary.steadyPoolUsedTotal, m_summary.steadyFrameCount)
        << " chunks\n";
    out << "    Avg steady free-list used: "
        << ToKiB(average(m_summary.steadyFreeListUsedBytesTotal,
                         m_summary.steadyFrameCount))
        << " KiB\n";
    out << "    Avg steady fragmentation: "
        << (average(m_summary.steadyFragmentationRatioTotal,
                    m_summary.steadyFrameCount) *
            100.0)
        << "%\n";
    out << "    Steady allocation failures: "
        << m_summary.steadyAllocationFailures << "\n";
  }

  if (m_summary.burstFrameCount > 0) {
    out << "    Avg burst linear peak: "
        << ToKiB(average(m_summary.burstLinearPeakBytesTotal,
                         m_summary.burstFrameCount))
        << " KiB\n";
    out << "    Avg burst pool used: "
        << average(m_summary.burstPoolUsedTotal, m_summary.burstFrameCount)
        << " chunks\n";
    out << "    Avg burst free-list used: "
        << ToKiB(average(m_summary.burstFreeListUsedBytesTotal,
                         m_summary.burstFrameCount))
        << " KiB\n";
    out << "    Avg burst fragmentation: "
        << (average(m_summary.burstFragmentationRatioTotal,
                    m_summary.burstFrameCount) *
            100.0)
        << "%\n";
    out << "    Burst allocation failures: "
        << m_summary.burstAllocationFailures << "\n";
  }
}

bool EngineSim::OpenCsv(std::ostream &err) {
  if (m_csvStream.is_open()) {
    m_csvStream.close();
  }

  if (m_options.csvPath.empty()) {
    return true;
  }

  m_csvStream.open(m_options.csvPath, std::ios::out | std::ios::trunc);
  if (!m_csvStream) {
    err << "Failed to open CSV output: " << m_options.csvPath << "\n";
    return false;
  }

  WriteCsvHeader();
  return true;
}

void EngineSim::WriteCsvHeader() {
  if (!m_csvStream.is_open()) {
    return;
  }

  m_csvStream
      << "frame,scenario,is_burst_frame,particles_alive,resource_blobs_alive,"
         "linear_used_bytes,"
         "linear_peak_bytes,stack_used_bytes,stack_peak_bytes,pool_capacity,"
         "pool_free_slots,free_list_used_bytes,free_list_peak_bytes,"
         "free_list_total_free_bytes,free_list_largest_free_block,"
         "free_list_free_block_count,free_list_fragmentation_ratio,"
         "allocation_failures_this_frame,allocation_failures_total\n";
}

void EngineSim::WriteCsvRow(const FrameSnapshot &snapshot) {
  if (!m_csvStream.is_open()) {
    return;
  }

  m_csvStream << snapshot.frameIndex << "," << snapshot.scenarioName << ","
              << (snapshot.isBurstFrame ? 1 : 0) << ","
              << snapshot.particlesAlive << "," << snapshot.resourceBlobsAlive
              << "," << snapshot.linearUsedBytes << ","
              << snapshot.linearPeakBytes << "," << snapshot.stackUsedBytes
              << "," << snapshot.stackPeakBytes << "," << snapshot.poolCapacity
              << "," << snapshot.poolFreeSlots << ","
              << snapshot.freeListUsedBytes << "," << snapshot.freeListPeakBytes
              << "," << snapshot.freeListTotalFreeBytes << ","
              << snapshot.freeListLargestFreeBlock << ","
              << snapshot.freeListFreeBlockCount << ","
              << snapshot.freeListFragmentationRatio << ","
              << snapshot.allocationFailuresThisFrame << ","
              << snapshot.allocationFailuresTotal << "\n";
}

} // namespace Zenith::EngineSim
