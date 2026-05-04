#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Zenith::EngineSim {

enum class ScenarioKind { Steady, Burst, Fragmentation, Recovery };

struct ScenarioSettings {
  std::size_t defaultFrames = 0;
  std::size_t targetParticles = 0;
  std::size_t fixedBlobCreates = 0;
  std::size_t targetBlobCount = 0;
  std::size_t maxBlobCreatesPerFrame = 0;
  std::size_t blobMinSize = 0;
  std::size_t blobMaxSize = 0;
  std::uint32_t blobMinLifetime = 0;
  std::uint32_t blobMaxLifetime = 0;
};

struct ScenarioFramePlan {
  bool isBurstFrame = false;
  std::size_t targetParticles = 0;
  std::size_t fixedBlobCreates = 0;
  std::size_t targetBlobCount = 0;
  std::size_t maxBlobCreatesPerFrame = 0;
  std::size_t blobMinSize = 0;
  std::size_t blobMaxSize = 0;
  std::uint32_t blobMinLifetime = 0;
  std::uint32_t blobMaxLifetime = 0;
};

const char *ToString(ScenarioKind kind);
bool ParseScenarioKind(std::string_view text, ScenarioKind &outKind);
ScenarioSettings GetScenarioSettings(ScenarioKind kind);
ScenarioFramePlan GetFramePlan(ScenarioKind kind, std::size_t frameIndex);

} // namespace Zenith::EngineSim
