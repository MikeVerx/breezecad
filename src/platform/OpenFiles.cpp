#include "platform/OpenFiles.h"

#if !defined(__APPLE__) || defined(BZ_IOS)   // iOS: files come through the projects folder
namespace bz::platform {
void ListenForOpenRequests() {}
std::vector<std::filesystem::path> TakeOpenRequests() { return {}; }
} // namespace bz::platform
#endif
