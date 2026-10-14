#pragma once
// SPDX-License-Identifier: MPL-2.0
// A short code for a crash or hang report ("BZ-SEGV-3F9A1C") that a user can
// read out or paste into a chat. The same failure in the same place of the
// same build gives the same code, so reports from different people can be
// matched without reading every stack.

#include <string>
#include <string_view>

namespace bz::crash {

/// The code for `report`, the text written by the crash handler or the
/// hang watchdog (src/app/CrashGuard.h describes it). "BZ-UNKNOWN-…" for
/// text that is neither.
std::string ErrorCode(std::string_view report);

/// The value of the first line "`key`: value" of `report` ("" if none).
std::string ReportField(std::string_view report, std::string_view key);

} // namespace bz::crash
