// Rules the compilers do not check on every platform: caught here, on Linux,
// before a Windows build fails on them.

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <string>

namespace {

/// Lines of our sources (comments and strings stripped roughly) matching `pattern`.
std::string Offenders(const std::regex& pattern) {
    std::string out;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(std::filesystem::path(BZ_SOURCE_DIR) / "src")) {
        const auto ext = entry.path().extension();
        if (ext != ".h" && ext != ".cpp") continue;
        std::ifstream in(entry.path());
        std::string line;
        for (int n = 1; std::getline(in, line); ++n) {
            if (const auto c = line.find("//"); c != std::string::npos) line.erase(c);
            line = std::regex_replace(line, std::regex(R"("([^"\\]|\\.)*")"), "\"\"");
            if (std::regex_search(line, pattern)) out += entry.path().filename().string() + ":" + std::to_string(n) + " " + line + "\n";
        }
    }
    return out;
}

} // namespace

TEST_CASE("no identifiers that windows.h turns into macros") {
    // near, far: empty macros; min, max: function-like macros (std::min( is fine with NOMINMAX, plain min( is not).
    CHECK(Offenders(std::regex(R"(\b(near|far)\b)")) == "");
    // Win32 API names that are macros (…A / …W): a function of ours by that name breaks.
    CHECK(Offenders(std::regex(R"(\b(LoadImage|DrawText|CreateWindow|GetObject|SendMessage|CreateFont)\s*\()")) == "");
}
