#pragma once
// The host side of the C ABI (bz_module.h): the table of functions every
// module gets in init(). The read API answers from the assembly the program
// last set active; the program sets it when the active tab changes and
// clears it before that assembly goes away.

#include "modules/bz_module.h"

namespace bz::kernel {
class AssemblyReader;
}

namespace bz::modules {

/// The table passed to modules; lives as long as the program.
const BzHostApi* HostApi();

/// Assembly the read API answers about; nullptr: none open (BZ_ENODATA).
/// The reader must stay valid until the next call. Main thread only.
void SetActiveAssembly(const kernel::AssemblyReader* reader);

} // namespace bz::modules
