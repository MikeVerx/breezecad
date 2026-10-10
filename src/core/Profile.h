#pragma once
// Where the time of a slow frame went. Code marks its costly parts with
// `prof::Scope`; the application logs the parts of any frame that took too
// long (the log stays on the computer and comes with bug reports).

#include <chrono>
#include <string>
#include <vector>

namespace bz::prof {

/// Milliseconds spent in each named part during the current frame.
struct Part {
    const char* name;
    double      ms;
};

/// Adds `ms` to the part `name` of the current frame (same thread only).
void Add(const char* name, double ms);

/// The parts recorded since the last call, and resets them.
std::vector<Part> TakeFrame();

/// "rebuild 812 ms, sync 120 ms, ..." — the largest parts first.
std::string Describe(std::vector<Part> parts, std::size_t count = 6);

/// Times its own lifetime into a part.
class Scope {
public:
    explicit Scope(const char* name) : myName(name), myStart(std::chrono::steady_clock::now()) {}
    ~Scope() { Add(myName, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - myStart).count()); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    const char*                           myName;
    std::chrono::steady_clock::time_point myStart;
};

} // namespace bz::prof
