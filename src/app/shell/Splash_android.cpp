// The splash on Android: none of its own. The system shows the app's
// launch screen while it starts; a window of our own over it is not
// possible (one window per activity). See shell/Splash.h.
#include "app/shell/Splash.h"

namespace bz::app {

Splash::~Splash() = default;
bool Splash::Open(const std::filesystem::path&, bool) { return false; }
void Splash::Step(const std::string&, float) {}
void Splash::Close() {}
void Splash::Draw() {}

} // namespace bz::app
