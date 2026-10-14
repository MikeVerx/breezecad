#pragma once
// SPDX-License-Identifier: MPL-2.0
// Other processes of this program: the crash and hang window runs as one
// (a crashed or frozen process cannot draw a window of its own).

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bz::platform {

using ProcessId = std::int64_t;

/// This process.
ProcessId CurrentProcessId();

/// Starts `program` with `args`, not waiting for it. Its id in `id` (if
/// given); false if it could not start.
bool StartProcess(const std::filesystem::path& program, const std::vector<std::string>& args, ProcessId* id = nullptr);

/// The process is still there. A finished child of ours is collected here.
bool ProcessRunning(ProcessId id);

/// Ends the process at once (it gets no chance to save anything).
void StopProcess(ProcessId id);

} // namespace bz::platform
