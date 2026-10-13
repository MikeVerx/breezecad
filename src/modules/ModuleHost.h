#pragma once
// The host side of the C ABI (bz_module.h): the table of functions every
// module gets in init(). The read API answers from the assembly the program
// last set active; the program sets it when the active tab changes and
// clears it before that assembly goes away.

#include "modules/bz_module.h"

#include <functional>
#include <string>
#include <string_view>

namespace bz::kernel {
class AssemblyReader;
}

namespace bz::modules {

/// The table passed to modules; lives as long as the program.
const BzHostApi* HostApi();

/// Assembly the read API answers about; nullptr: none open (BZ_ENODATA).
/// The reader must stay valid until the next call. Main thread only.
void SetActiveAssembly(const kernel::AssemblyReader* reader);

/// What execute() runs: a JSON request in, a JSON reply out (the program
/// sets it, see app/ModuleCommands). Unset: every request gets an error.
/// Main thread only.
using CommandHandler = std::function<std::string(std::string_view request)>;
void SetCommandHandler(CommandHandler handler);

/// Called from any thread by request_main(): wake the main loop up
/// (glfwPostEmptyEvent). Set once at start, before modules load.
void SetWakeHandler(std::function<void()> wake);

/// What config_dir() answers (UTF-8). Set once at start, before modules load.
void SetConfigDir(std::string utf8);

/// True once after a module asked for main_tick (request_main). Any thread.
bool TakeMainRequest();

} // namespace bz::modules
