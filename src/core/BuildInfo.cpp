#include "core/Version.h"

#include "core/BuildInfo.h"   // generated, see cmake/BuildInfo.cmake

#include <format>

namespace bz {

const char* Version() { return BZ_VERSION; }
const char* Commit() { return BZ_COMMIT; }
const char* Branch() { return BZ_BRANCH; }
const char* BuildDate() { return BZ_BUILD_DATE; }

std::string VersionLine() {
    const std::string commit = Commit(), branch = Branch();
    if (commit.empty()) return std::format("{} ({})", Version(), BuildDate());
    return branch.empty() ? std::format("{} ({}, {})", Version(), commit, BuildDate())
                          : std::format("{} ({} {}, {})", Version(), branch, commit, BuildDate());
}

} // namespace bz
