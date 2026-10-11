#pragma once
// What the computer is (for beta testers' reports): system, processor,
// memory, graphics cards with their driver versions, screens, free disk.
// Read locally, written only into the report the tester sends by hand.

#include <filesystem>
#include <string>
#include <vector>

namespace bz::platform {

struct SystemFact {
    std::string key;     ///< "cpu", "gpu.1", "display.1", …
    std::string value;
};

/// Everything that is cheap to ask the system. `disk`: a folder whose drive's
/// free space is reported (the reports folder).
std::vector<SystemFact> DescribeSystem(const std::filesystem::path& disk);

} // namespace bz::platform
