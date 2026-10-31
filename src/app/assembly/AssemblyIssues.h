#pragma once
// The two counters in the header of an assembly: a red one for part files
// that are missing or cannot be read, a yellow one for the assembly's own
// errors (constraints that do not hold or repeat others). A click opens the
// list; a missing part is given its file again from there (one file, or a
// folder searched for every missing one by name), as Inventor's "Resolve
// link" does, without a window in front of the model.

#include "app/AppContext.h"

#include <filesystem>

namespace bz::app {

/// Draws the counters ending at `right`, centred on `centreY`; returns the
/// width they took (0 when there is nothing wrong or no assembly in front).
float DrawIssueCounters(AppContext& app, float right, float centreY);

/// Gives component `component` the part file `file` (one undo step).
/// False with a message when the file is not a part.
bool ReplacePart(AppContext& app, Session& session, ObjectId component, const std::filesystem::path& file);

/// For every component whose file is missing, a part of that name in
/// `folder` (and below it); returns how many were found.
int FindMissingParts(AppContext& app, Session& session, const std::filesystem::path& folder);

} // namespace bz::app
