// Android: the bridge to BreezeActivity (resources/android/java). See Android.h.
#include "platform/Android.h"

#include "core/Log.h"

#include <android/asset_manager.h>
#include <android/looper.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

namespace fs = std::filesystem;

namespace bz::platform::android {
namespace {

android_app* gApp = nullptr;

struct Shared {
    std::mutex                mutex;
    Screen                    screen;
    TypedText                 typed;
    std::optional<PickResult> picked;
};
Shared& S() {
    static Shared s;
    return s;
}

void Wake() {
    if (gApp && gApp->looper) ALooper_wake(gApp->looper);
}

/// The app thread's JNI environment (attached once, detached at exit by the system).
JNIEnv* Env() {
    if (!gApp) return nullptr;
    thread_local JNIEnv* env = nullptr;
    if (!env && gApp->activity->vm->AttachCurrentThread(&env, nullptr) != JNI_OK) env = nullptr;
    return env;
}

jobject Activity() { return gApp ? gApp->activity->clazz : nullptr; }

/// Calls an instance method of the activity; clears a Java exception (logged).
template <typename Call>
auto WithMethod(const char* name, const char* signature, Call call) -> decltype(call(nullptr, nullptr)) {
    using Result = decltype(call(nullptr, nullptr));
    JNIEnv* env = Env();
    if (!env || !Activity()) return Result{};
    jclass type = env->GetObjectClass(Activity());
    jmethodID method = env->GetMethodID(type, name, signature);
    env->DeleteLocalRef(type);
    if (!method) {
        env->ExceptionClear();
        log::Error("Android: no Java method {}{}", name, signature);
        return Result{};
    }
    Result result = call(env, method);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        log::Error("Android: Java {} threw", name);
    }
    return result;
}

std::string Text(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* chars = env->GetStringUTFChars(s, nullptr);
    std::string out = chars ? chars : "";
    env->ReleaseStringUTFChars(s, chars);
    return out;
}

jstring Java(JNIEnv* env, const std::string& s) { return env->NewStringUTF(s.c_str()); }

// --- Documents outside the app: local copy <-> content URI ----------------------

/// local file -> its document's URI; kept in DataDir()/documents.map, so a
/// document recovered from autosave after a crash still saves to its place.
std::map<std::string, std::string>& Links() {
    static std::map<std::string, std::string> links;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        std::ifstream in(DataDir() / "documents.map");
        for (std::string line; std::getline(in, line);)
            if (const auto tab = line.find('\t'); tab != std::string::npos) links[line.substr(0, tab)] = line.substr(tab + 1);
    }
    return links;
}

void SaveLinks() {
    std::ofstream out(DataDir() / "documents.map", std::ios::trunc);
    for (const auto& [local, uri] : Links()) out << local << '\t' << uri << '\n';
}

/// A local file for a document named `name`: cache/documents/<n>/<name>, so
/// two documents of the same name from different folders do not meet.
fs::path LocalFor(const std::string& uri, const std::string& name) {
    for (const auto& [local, linked] : Links())
        if (linked == uri) return local;
    std::error_code ec;
    for (int n = 1;; ++n) {
        const fs::path dir = CacheDir() / "documents" / std::to_string(n);
        if (!fs::exists(dir / name, ec)) {
            fs::create_directories(dir, ec);
            return dir / name;
        }
    }
}

} // namespace

void SetApp(android_app* app) { gApp = app; }
android_app* App() { return gApp; }

fs::path DataDir() {
    return gApp && gApp->activity->internalDataPath ? fs::path(gApp->activity->internalDataPath) : fs::path("/data/local/tmp/breezecad");
}

fs::path CacheDir() {
    const fs::path dir = DataDir().parent_path() / "cache";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

fs::path UnpackAssets() {
    const fs::path dest = DataDir() / "assets";
    if (!gApp) return dest;
    AAssetManager* assets = gApp->activity->assetManager;
    // asset-list.txt (made by scripts/android/package-apk.sh): every file,
    // the first line being the build's stamp. The NDK cannot list folders.
    AAsset* list = AAssetManager_open(assets, "asset-list.txt", AASSET_MODE_BUFFER);
    if (!list) {
        log::Error("Android: no asset-list.txt in the APK");
        return dest;
    }
    const std::string text(static_cast<const char*>(AAsset_getBuffer(list)), static_cast<std::size_t>(AAsset_getLength(list)));
    AAsset_close(list);
    std::istringstream lines(text);
    std::string stamp;
    std::getline(lines, stamp);
    std::string had;
    std::getline(std::ifstream(dest / ".stamp"), had);
    if (had == stamp) return dest;

    const auto start = std::chrono::steady_clock::now();
    std::error_code ec;
    fs::remove_all(dest, ec);
    int count = 0;
    for (std::string name; std::getline(lines, name);) {
        if (name.empty()) continue;
        AAsset* a = AAssetManager_open(assets, name.c_str(), AASSET_MODE_STREAMING);
        if (!a) continue;
        const fs::path file = dest / fs::u8path(name);
        fs::create_directories(file.parent_path(), ec);
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        char buffer[64 * 1024];
        for (int n; (n = AAsset_read(a, buffer, sizeof buffer)) > 0;) out.write(buffer, n);
        AAsset_close(a);
        ++count;
    }
    std::ofstream(dest / ".stamp") << stamp;
    log::Info("Android: {} assets unpacked in {} ms", count,
              std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    return dest;
}

Screen CurrentScreen() {
    const std::lock_guard lock(S().mutex);
    return S().screen;
}

void ShowKeyboard(bool show) {
    WithMethod("showKeyboard", "(Z)V", [&](JNIEnv* env, jmethodID m) {
        env->CallVoidMethod(Activity(), m, static_cast<jboolean>(show));
        return 0;
    });
}

TypedText TakeTypedText() {
    const std::lock_guard lock(S().mutex);
    TypedText out = std::move(S().typed);
    S().typed = {};
    return out;
}

void SetImmersive(bool on) {
    WithMethod("setImmersive", "(Z)V", [&](JNIEnv* env, jmethodID m) {
        env->CallVoidMethod(Activity(), m, static_cast<jboolean>(on));
        return 0;
    });
}

void SetGestureExclusion(const std::vector<Edges>& rects) {
    WithMethod("setGestureExclusion", "([F)V", [&](JNIEnv* env, jmethodID m) {
        std::vector<float> flat;
        for (const Edges& r : rects) flat.insert(flat.end(), {r.left, r.top, r.right, r.bottom});
        jfloatArray array = env->NewFloatArray(static_cast<jsize>(flat.size()));
        env->SetFloatArrayRegion(array, 0, static_cast<jsize>(flat.size()), flat.data());
        env->CallVoidMethod(Activity(), m, array);
        env->DeleteLocalRef(array);
        return 0;
    });
}

std::string ProjectsFolder() {
    return WithMethod("projectsFolder", "()Ljava/lang/String;", [&](JNIEnv* env, jmethodID m) {
        auto s = static_cast<jstring>(env->CallObjectMethod(Activity(), m));
        std::string out = Text(env, s);
        if (s) env->DeleteLocalRef(s);
        return out;
    });
}

void ChooseProjectsFolder() {
    WithMethod("chooseProjectsFolder", "()V", [&](JNIEnv* env, jmethodID m) {
        env->CallVoidMethod(Activity(), m);
        return 0;
    });
}

std::vector<FolderEntry> ListProjects() {
    std::vector<FolderEntry> out = WithMethod("listProjects", "()[Ljava/lang/String;", [&](JNIEnv* env, jmethodID m) {
        std::vector<FolderEntry> entries;
        auto array = static_cast<jobjectArray>(env->CallObjectMethod(Activity(), m));
        if (!array) return entries;
        const jsize n = env->GetArrayLength(array);
        for (jsize i = 0; i + 2 < n; i += 3) {
            FolderEntry e;
            auto at = [&](jsize k) {
                auto s = static_cast<jstring>(env->GetObjectArrayElement(array, k));
                std::string t = Text(env, s);
                if (s) env->DeleteLocalRef(s);
                return t;
            };
            e.name = at(i);
            e.uri = at(i + 1);
            e.modified = std::atoll(at(i + 2).c_str());
            entries.push_back(std::move(e));
        }
        env->DeleteLocalRef(array);
        return entries;
    });
    std::sort(out.begin(), out.end(), [](const FolderEntry& a, const FolderEntry& b) { return a.modified > b.modified; });
    return out;
}

fs::path Import(const std::string& uri, const std::string& name) {
    const fs::path local = LocalFor(uri, name.empty() ? std::string("document.bzc") : name);
    const bool copied = WithMethod("copyFromUri", "(Ljava/lang/String;Ljava/lang/String;)Z", [&](JNIEnv* env, jmethodID m) {
        jstring a = Java(env, uri), b = Java(env, local.string());
        const bool ok = env->CallBooleanMethod(Activity(), m, a, b);
        env->DeleteLocalRef(a);
        env->DeleteLocalRef(b);
        return ok;
    });
    if (!copied) {
        log::Error("Android: cannot read {}", uri);
        return {};
    }
    Links()[local.string()] = uri;
    SaveLinks();
    return local;
}

fs::path CreateInProjects(const std::string& name) {
    const std::string uri = WithMethod("createInProjects", "(Ljava/lang/String;)Ljava/lang/String;", [&](JNIEnv* env, jmethodID m) {
        jstring a = Java(env, name);
        auto s = static_cast<jstring>(env->CallObjectMethod(Activity(), m, a));
        env->DeleteLocalRef(a);
        std::string out = Text(env, s);
        if (s) env->DeleteLocalRef(s);
        return out;
    });
    if (uri.empty()) return {};
    const fs::path local = LocalFor(uri, name);
    Links()[local.string()] = uri;
    SaveLinks();
    return local;
}

bool WriteBack(const fs::path& local) {
    const auto it = Links().find(local.string());
    if (it == Links().end()) return false;
    const std::string uri = it->second;
    const bool ok = WithMethod("copyToUri", "(Ljava/lang/String;Ljava/lang/String;)Z", [&](JNIEnv* env, jmethodID m) {
        jstring a = Java(env, local.string()), b = Java(env, uri);
        const bool r = env->CallBooleanMethod(Activity(), m, a, b);
        env->DeleteLocalRef(a);
        env->DeleteLocalRef(b);
        return r;
    });
    if (!ok) log::Error("Android: cannot write {} back to {}", local.string(), uri);
    return ok;
}

void Link(const fs::path& local, const std::string& uri) {
    Links()[local.string()] = uri;
    SaveLinks();
}

bool IsImported(const fs::path& local) { return Links().contains(local.string()); }

void PickToOpen(const std::vector<std::string>& mimeTypes) {
    WithMethod("pickToOpen", "([Ljava/lang/String;)V", [&](JNIEnv* env, jmethodID m) {
        jclass stringType = env->FindClass("java/lang/String");
        jobjectArray array = env->NewObjectArray(static_cast<jsize>(mimeTypes.size()), stringType, nullptr);
        for (std::size_t i = 0; i < mimeTypes.size(); ++i) {
            jstring s = Java(env, mimeTypes[i]);
            env->SetObjectArrayElement(array, static_cast<jsize>(i), s);
            env->DeleteLocalRef(s);
        }
        env->CallVoidMethod(Activity(), m, array);
        env->DeleteLocalRef(array);
        env->DeleteLocalRef(stringType);
        return 0;
    });
}

void PickToSave(const std::string& suggestedName, const std::string& mime, const fs::path& local) {
    WithMethod("pickToSave", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V", [&](JNIEnv* env, jmethodID m) {
        jstring a = Java(env, suggestedName), b = Java(env, mime), c = Java(env, local.string());
        env->CallVoidMethod(Activity(), m, a, b, c);
        env->DeleteLocalRef(a);
        env->DeleteLocalRef(b);
        env->DeleteLocalRef(c);
        return 0;
    });
}

std::optional<PickResult> TakePickResult() {
    const std::lock_guard lock(S().mutex);
    std::optional<PickResult> out = std::move(S().picked);
    S().picked.reset();
    return out;
}

void Share(const fs::path& file, const std::string& mime, const std::string& subject) {
    WithMethod("share", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V", [&](JNIEnv* env, jmethodID m) {
        jstring a = Java(env, file.string()), b = Java(env, mime), c = Java(env, subject);
        env->CallVoidMethod(Activity(), m, a, b, c);
        env->DeleteLocalRef(a);
        env->DeleteLocalRef(b);
        env->DeleteLocalRef(c);
        return 0;
    });
}

void Heartbeat() {
    static auto last = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now - last < std::chrono::milliseconds(900)) return;
    last = now;
    static const fs::path beat = [] {
        std::error_code ec;
        fs::create_directories(DataDir() / "watchdog", ec);
        return DataDir() / "watchdog" / "beat";
    }();
    // Rewritten, not just touched: the watchdog reads the modification time.
    if (FILE* f = std::fopen(beat.c_str(), "w")) {
        std::fputs("beat", f);
        std::fclose(f);
    }
}

void SetWatchdogState(const char* state) {
    std::error_code ec;
    fs::create_directories(DataDir() / "watchdog", ec);
    std::ofstream(DataDir() / "watchdog" / "state", std::ios::trunc)
        << state << (std::string_view(state) == "exit" ? "" : " " + std::to_string(::getpid()));
}

std::string LastExitReason() {
    return WithMethod("lastExitReason", "()Ljava/lang/String;", [&](JNIEnv* env, jmethodID m) {
        auto s = static_cast<jstring>(env->CallObjectMethod(Activity(), m));
        std::string out = Text(env, s);
        if (s) env->DeleteLocalRef(s);
        return out;
    });
}

void AskNotifications() {
    WithMethod("askNotifications", "()V", [&](JNIEnv* env, jmethodID m) {
        env->CallVoidMethod(Activity(), m);
        return 0;
    });
}

void SetClipboard(const std::string& text) {
    WithMethod("setClipboard", "(Ljava/lang/String;)V", [&](JNIEnv* env, jmethodID m) {
        jstring a = Java(env, text);
        env->CallVoidMethod(Activity(), m, a);
        env->DeleteLocalRef(a);
        return 0;
    });
}

std::string Clipboard() {
    return WithMethod("clipboard", "()Ljava/lang/String;", [&](JNIEnv* env, jmethodID m) {
        auto s = static_cast<jstring>(env->CallObjectMethod(Activity(), m));
        std::string out = Text(env, s);
        if (s) env->DeleteLocalRef(s);
        return out;
    });
}

bool OpenUrl(const std::string& url) {
    return WithMethod("openUrl", "(Ljava/lang/String;)Z", [&](JNIEnv* env, jmethodID m) {
        jstring a = Java(env, url);
        const bool ok = env->CallBooleanMethod(Activity(), m, a);
        env->DeleteLocalRef(a);
        return ok;
    });
}

std::string TestCommands() {
    return WithMethod("testCommands", "()Ljava/lang/String;", [&](JNIEnv* env, jmethodID m) {
        auto s = static_cast<jstring>(env->CallObjectMethod(Activity(), m));
        std::string out = Text(env, s);
        if (s) env->DeleteLocalRef(s);
        return out;
    });
}

std::string PackageVersion() {
    static const std::string version = WithMethod("packageVersion", "()Ljava/lang/String;", [&](JNIEnv* env, jmethodID m) {
        auto s = static_cast<jstring>(env->CallObjectMethod(Activity(), m));
        std::string out = Text(env, s);
        if (s) env->DeleteLocalRef(s);
        return out;
    });
    return version;
}

int UnicodeChar(int deviceId, int keyCode, int metaState) {
    JNIEnv* env = Env();
    if (!env || !Activity()) return 0;
    jclass type = env->GetObjectClass(Activity());
    jmethodID method = env->GetStaticMethodID(type, "unicodeChar", "(III)I");
    int result = 0;
    if (method) result = env->CallStaticIntMethod(type, method, deviceId, keyCode, metaState);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(type);
    return result;
}

namespace {
bool gVolumeTaken = false;
}
void SetVolumeKeysTaken(bool taken) { gVolumeTaken = taken; }
bool VolumeKeysTaken() { return gVolumeTaken; }

} // namespace bz::platform::android

// --- Called by Java (BreezeActivity's native methods), on its UI thread -----------

using namespace bz::platform::android;

extern "C" JNIEXPORT void JNICALL Java_io_github_mikeverx_breezecad_BreezeActivity_nativeScreen(
    JNIEnv* env, jclass, jfloatArray bars, jfloatArray gestures, jfloatArray cutout, jfloat cutoutFrom, jfloat cutoutTo,
    jfloatArray ime, jfloat corner, jboolean hardKeyboard) {
    auto edges = [env](jfloatArray a) {
        float v[4] = {};
        if (a && env->GetArrayLength(a) >= 4) env->GetFloatArrayRegion(a, 0, 4, v);
        return Edges{v[0], v[1], v[2], v[3]};
    };
    {
        const std::lock_guard lock(S().mutex);
        Screen& s = S().screen;
        s.bars = edges(bars);
        s.gestures = edges(gestures);
        s.cutout = edges(cutout);
        s.cutoutFrom = cutoutFrom;
        s.cutoutTo = cutoutTo;
        s.ime = edges(ime);
        s.cornerRadius = corner;
        s.hardKeyboard = hardKeyboard;
        ++s.revision;
    }
    Wake();
}

extern "C" JNIEXPORT void JNICALL Java_io_github_mikeverx_breezecad_BreezeActivity_nativeText(JNIEnv* env, jclass, jstring text,
                                                                                               jint keyCode) {
    {
        const std::lock_guard lock(S().mutex);
        S().typed.text += Text(env, text);
        if (keyCode != 0) S().typed.keys.push_back(keyCode);
    }
    Wake();
}

extern "C" JNIEXPORT void JNICALL Java_io_github_mikeverx_breezecad_BreezeActivity_nativePicked(JNIEnv* env, jclass, jint kind,
                                                                                                 jstring uri, jstring name) {
    {
        const std::lock_guard lock(S().mutex);
        PickResult r;
        r.kind = kind == 0 ? PickResult::Kind::Open : kind == 1 ? PickResult::Kind::Save : PickResult::Kind::Folder;
        r.uri = Text(env, uri);
        r.name = Text(env, name);
        S().picked = std::move(r);
    }
    Wake();
}
