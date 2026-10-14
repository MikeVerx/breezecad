#pragma once
// SPDX-License-Identifier: MPL-2.0
// The crash and hang window: this program started again with
//   --crash-window <report>                    after a crash (app/CrashGuard)
//   --hang-window <pid> <report> <seconds>     while process <pid> does not respond
// It says what happened in plain words, shows the error code to copy, and
// offers the report: copy it, open its folder, restart the program; for a
// hang also "wait" or "end the program". A small window of its own: the
// program that failed cannot draw.

#include <filesystem>
#include <vector>

namespace bz::app {

/// The arguments (without the program's own name) ask for this window.
bool IsCrashWindowRequest(const std::vector<std::filesystem::path>& args);
/// Shows the window until it is closed; the process's exit code.
int RunCrashWindow(const std::vector<std::filesystem::path>& args);

} // namespace bz::app
