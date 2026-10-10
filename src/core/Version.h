#pragma once
// Which build this is: the version, the git commit and the build date.

#include <string>

namespace bz {

/// "0.2.0".
const char* Version();
/// Short git commit ("3b9256a"); empty when built outside git.
const char* Commit();
/// Branch the build came from ("next"); may be empty.
const char* Branch();
/// Build date, "2026-10-10" (UTC).
const char* BuildDate();
/// "0.2.0 (next 3b9256a, 2026-10-10)", or "0.2.0" when nothing more is known.
std::string VersionLine();

} // namespace bz
