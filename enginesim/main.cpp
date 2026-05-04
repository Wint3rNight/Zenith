#include "EngineSim.hpp"

#include <iostream>
#include <limits>
#include <string>

namespace {

void PrintUsage(std::ostream &out) {
  out << "Usage: ZenithEngineSim [options]\n"
         "  --scenario <steady|burst|fragmentation|recovery>\n"
         "  --frames <count>\n"
         "  --seed <uint32>\n"
         "  --report-every <count>\n"
         "  --csv <path>\n"
         "  --help\n";
}

bool ParsePositiveSize(const char *text, std::size_t &outValue) {
  try {
    std::string input(text);
    std::size_t consumed = 0;
    unsigned long long value = std::stoull(input, &consumed, 10);
    if (consumed != input.size() || value == 0 ||
        value > std::numeric_limits<std::size_t>::max()) {
      return false;
    }
    outValue = static_cast<std::size_t>(value);
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseSeed(const char *text, std::uint32_t &outValue) {
  try {
    std::string input(text);
    std::size_t consumed = 0;
    unsigned long value = std::stoul(input, &consumed, 10);
    if (consumed != input.size() ||
        value > std::numeric_limits<std::uint32_t>::max()) {
      return false;
    }
    outValue = static_cast<std::uint32_t>(value);
    return true;
  } catch (...) {
    return false;
  }
}

} // namespace

int main(int argc, char **argv) {
  Zenith::EngineSim::EngineSimOptions options;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--help") {
      PrintUsage(std::cout);
      return 0;
    }

    if (i + 1 >= argc) {
      PrintUsage(std::cerr);
      return 1;
    }

    const char *value = argv[++i];

    if (arg == "--scenario") {
      if (!Zenith::EngineSim::ParseScenarioKind(value, options.scenario)) {
        std::cerr << "Invalid scenario: " << value << "\n";
        PrintUsage(std::cerr);
        return 1;
      }
      continue;
    }

    if (arg == "--frames") {
      if (!ParsePositiveSize(value, options.frames)) {
        std::cerr << "Invalid frame count: " << value << "\n";
        PrintUsage(std::cerr);
        return 1;
      }
      continue;
    }

    if (arg == "--seed") {
      if (!ParseSeed(value, options.seed)) {
        std::cerr << "Invalid seed: " << value << "\n";
        PrintUsage(std::cerr);
        return 1;
      }
      continue;
    }

    if (arg == "--report-every") {
      if (!ParsePositiveSize(value, options.reportEvery)) {
        std::cerr << "Invalid report interval: " << value << "\n";
        PrintUsage(std::cerr);
        return 1;
      }
      continue;
    }

    if (arg == "--csv") {
      options.csvPath = value;
      continue;
    }

    std::cerr << "Unknown option: " << arg << "\n";
    PrintUsage(std::cerr);
    return 1;
  }

  Zenith::EngineSim::EngineSim sim(options);
  return sim.Run(std::cout, std::cerr) ? 0 : 1;
}
