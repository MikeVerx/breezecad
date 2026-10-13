#include "core/Profile.h"

#include <algorithm>
#include <cstring>
#include <format>

namespace bz::prof {
namespace {

thread_local std::vector<Part> theFrame;

} // namespace

void Add(const char* name, double ms) {
    for (Part& p : theFrame)
        if (p.name == name || std::strcmp(p.name, name) == 0) {
            p.ms += ms;
            return;
        }
    theFrame.push_back({name, ms});
}

std::vector<Part> TakeFrame() {
    std::vector<Part> out;
    out.swap(theFrame);
    return out;
}

std::string Describe(std::vector<Part> parts, std::size_t count) {
    std::sort(parts.begin(), parts.end(), [](const Part& a, const Part& b) { return a.ms > b.ms; });
    std::string out;
    for (std::size_t i = 0; i < parts.size() && i < count; ++i) {
        if (!out.empty()) out += ", ";
        out += std::format("{} {:.0f} ms", parts[i].name, parts[i].ms);
    }
    return out;
}

} // namespace bz::prof
