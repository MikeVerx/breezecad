#pragma once
// Documents the system asks the running program to open: on macOS a double
// click in Finder (or a file dropped on the Dock icon) comes as an Apple
// event, not as program arguments. Elsewhere nothing comes this way (the
// program gets its files as arguments, and a second start passes them on).
#include <filesystem>
#include <vector>

namespace bz::platform {

/// Starts listening (after the first window exists: GLFW's application
/// delegate is in place then).
void ListenForOpenRequests();
/// The files asked for since the last call (in the order asked).
std::vector<std::filesystem::path> TakeOpenRequests();

} // namespace bz::platform
