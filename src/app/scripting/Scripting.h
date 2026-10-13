#pragma once
// Commands on the open documents for modules (bz_module.h: execute()).
//
// A request is JSON: {"command": "part.tree", "args": {...}}; the reply is
// {"ok": true, "result": ...} or {"ok": false, "error": "..."}.
// "commands.list" describes every command with a JSON schema of its
// arguments, so a module (the MCP server) offers them without knowing them.
//
// Changes are the user's own undo steps (Ctrl+Z takes them back). While the
// user edits a sketch or an operation, commands that change something are
// refused: the program never pulls an edit from under the user's hands.

#include <string>
#include <string_view>

namespace bz::app {

struct AppContext;

namespace scripting {

/// Runs one request; never throws. Main thread only.
std::string Run(AppContext& app, std::string_view request);

} // namespace scripting
} // namespace bz::app
