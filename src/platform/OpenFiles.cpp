#include "platform/OpenFiles.h"

#ifndef __APPLE__
namespace bz::platform {
void ListenForOpenRequests() {}
std::vector<std::filesystem::path> TakeOpenRequests() { return {}; }
} // namespace bz::platform
#endif
