#include "Scenario.hpp"

namespace Zenith::EngineSim {

namespace {

constexpr std::size_t kBurstInterval = 120;
constexpr std::size_t kRecoveryPivotFrame = 300;

ScenarioSettings MakeSteadySettings() {
  return {600, 20000, 16, 0, 16, 64, 1024, 30, 120};
}

ScenarioSettings MakeBurstSettings() {
  return {600, 20000, 16, 0, 144, 64, 2048, 30, 120};
}

ScenarioSettings MakeFragmentationSettings() {
  return {600, 12000, 0, 2000, 96, 64, 4096, 5, 200};
}

ScenarioSettings MakeRecoverySettings() {
  return {600, 12000, 0, 2000, 96, 64, 4096, 5, 200};
}

} // namespace

const char *ToString(ScenarioKind kind) {
  switch (kind) {
  case ScenarioKind::Steady:
    return "steady";
  case ScenarioKind::Burst:
    return "burst";
  case ScenarioKind::Fragmentation:
    return "fragmentation";
  case ScenarioKind::Recovery:
    return "recovery";
  }

  return "steady";
}

bool ParseScenarioKind(std::string_view text, ScenarioKind &outKind) {
  if (text == "steady") {
    outKind = ScenarioKind::Steady;
    return true;
  }
  if (text == "burst") {
    outKind = ScenarioKind::Burst;
    return true;
  }
  if (text == "fragmentation") {
    outKind = ScenarioKind::Fragmentation;
    return true;
  }
  if (text == "recovery") {
    outKind = ScenarioKind::Recovery;
    return true;
  }

  return false;
}

ScenarioSettings GetScenarioSettings(ScenarioKind kind) {
  switch (kind) {
  case ScenarioKind::Steady:
    return MakeSteadySettings();
  case ScenarioKind::Burst:
    return MakeBurstSettings();
  case ScenarioKind::Fragmentation:
    return MakeFragmentationSettings();
  case ScenarioKind::Recovery:
    return MakeRecoverySettings();
  }

  return MakeSteadySettings();
}

ScenarioFramePlan GetFramePlan(ScenarioKind kind, std::size_t frameIndex) {
  ScenarioSettings settings = GetScenarioSettings(kind);
  ScenarioFramePlan plan = {
      false,
      settings.targetParticles,
      settings.fixedBlobCreates,
      settings.targetBlobCount,
      settings.maxBlobCreatesPerFrame,
      settings.blobMinSize,
      settings.blobMaxSize,
      settings.blobMinLifetime,
      settings.blobMaxLifetime,
  };

  switch (kind) {
  case ScenarioKind::Steady:
    return plan;

  case ScenarioKind::Burst:
    if (frameIndex % kBurstInterval == 0) {
      plan.isBurstFrame = true;
      plan.targetParticles += 5000;
      plan.fixedBlobCreates += 128;
    }
    return plan;

  case ScenarioKind::Fragmentation:
    return plan;

  case ScenarioKind::Recovery:
    if (frameIndex >= kRecoveryPivotFrame) {
      plan.fixedBlobCreates = 0;
      plan.targetBlobCount = 0;
      plan.maxBlobCreatesPerFrame = 0;
    }
    return plan;
  }

  return plan;
}

} // namespace Zenith::EngineSim
