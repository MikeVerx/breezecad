// SPDX-License-Identifier: MPL-2.0
#include "core/CrashCode.h"

#include <doctest.h>

#include <string>

using bz::crash::ErrorCode;
using bz::crash::ReportField;

namespace {

std::string LinuxReport(const char* folder, const char* address, const char* topFrame) {
    return std::string("BreezeCAD crash report\nVersion: 0.2.1.1-Beta (next abc1234, 2026-10-14)\nCrash: SIGSEGV\n"
                       "Last action: key: file.new\nStack:\n") +
           folder + "/breezecad(+0x1a2b3)[" + address + "]\n" + folder + "/lib/" + topFrame + " [0x7f0000001234]\n" +
           "/usr/lib/libc.so.6(+0x2a1ca) [0x7f1122334455]\n";
}

} // namespace

TEST_CASE("crash code: the same place gives the same code wherever the program ran") {
    const std::string a = LinuxReport("/home/esf/Downloads/BreezeCAD", "0x55aa00001111", "libTKBRep.so.8(+0x4455)");
    const std::string b = LinuxReport("/opt/BreezeCAD-beta/program", "0x5600bbbb2222", "libTKBRep.so.8(+0x4455)");
    CHECK(ErrorCode(a) == ErrorCode(b));
    CHECK(ErrorCode(a).starts_with("BZ-SEGV-"));
    CHECK(ErrorCode(a).size() == std::string("BZ-SEGV-123456").size());
}

TEST_CASE("crash code: another place gives another code") {
    const std::string a = LinuxReport("/x", "0x1", "libTKBRep.so.8(+0x4455)");
    const std::string b = LinuxReport("/x", "0x1", "libTKBRep.so.8(+0x4466)");
    CHECK(ErrorCode(a) != ErrorCode(b));
}

TEST_CASE("crash code: Windows exceptions and hangs") {
    const std::string windows = "Version: 0.2.1.1-Beta\nCrash: exception 0xc0000005 at TKBRep.dll+0x1234\nStack:\n"
                                "  #0 breezecad.exe+0x10\n  #1 TKBRep.dll+0x1234\n";
    CHECK(ErrorCode(windows).starts_with("BZ-ACCESS-"));
    const std::string hang = "Version: 0.2.1.1-Beta\nHang: no response for 15 s\nLast action: key: file.new\n";
    const std::string otherHang = "Version: 0.2.1.1-Beta\nHang: no response for 40 s\nLast action: key: file.open\n";
    CHECK(ErrorCode(hang).starts_with("BZ-HANG-"));
    CHECK(ErrorCode(hang) != ErrorCode(otherHang));
    CHECK(ErrorCode("nothing here").starts_with("BZ-UNKNOWN-"));
}

TEST_CASE("report fields") {
    const std::string report = "Version: 1.0\nCrash: SIGABRT\nReason: uncaught exception: bad\n";
    CHECK(ReportField(report, "Crash") == "SIGABRT");
    CHECK(ReportField(report, "Reason") == "uncaught exception: bad");
    CHECK(ReportField(report, "Hang").empty());
    CHECK(ReportField(report, "Vers").empty());
}
