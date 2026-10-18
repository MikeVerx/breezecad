// libbzcrash.so: the error code of a crash or hang report for the watchdog's
// error screen (resources/android/java, CrashActivity) — the desktop's code
// (core/CrashCode), without loading the whole program.
#include "core/CrashCode.h"

#include <jni.h>

#include <string>

extern "C" JNIEXPORT jstring JNICALL Java_io_github_mikeverx_breezecad_CrashActivity_nativeErrorCode(JNIEnv* env, jclass, jstring report) {
    std::string text;
    if (report) {
        const char* chars = env->GetStringUTFChars(report, nullptr);
        if (chars) text = chars;
        env->ReleaseStringUTFChars(report, chars);
    }
    return env->NewStringUTF(bz::crash::ErrorCode(text).c_str());
}
