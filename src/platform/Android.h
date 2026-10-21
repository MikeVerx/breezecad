#pragma once
// Android: the app's activity and what only Java can do (docs/ADR/0004-mobile-touch-ui.md,
// "Что менять в коде"). The activity is io.github.mikeverx.breezecad.BreezeActivity
// (resources/android/java), a NativeActivity with a few helpers: the system's
// file pickers (SAF), "share", the keyboard and its height, the screen's
// cut-out, corners and gesture areas, immersive mode, the crash watchdog
// and why the last run ended.
//
// Calls into Java are made from the app's thread (android_main) and return
// at once; what Java answers later (a picked file, new insets) is kept here
// and taken by the app between frames. Java's callbacks wake the loop up.

#if defined(__ANDROID__)

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct android_app;

namespace bz::platform::android {

/// The glue's state, set once at the start of android_main.
void          SetApp(android_app* app);
android_app*  App();

/// Folders of the app: internal data (settings, logs, autosave) and the
/// cache (copies of documents opened through the system's picker).
std::filesystem::path DataDir();
std::filesystem::path CacheDir();
/// Unpacks the APK's assets (fonts, icons, examples) into DataDir()/assets
/// once per version; returns that folder.
std::filesystem::path UnpackAssets();

/// The screen around the app's window, in dp (Java pushes it on every
/// change): the system bars, the areas where an edge swipe belongs to the
/// system, the camera cut-out and the keyboard.
struct Edges {
    float left = 0, top = 0, right = 0, bottom = 0;
};
struct Screen {
    Edges bars;          ///< status and navigation bars (zero in immersive mode)
    Edges gestures;      ///< system gesture areas (back, home)
    Edges cutout;        ///< the cut-out's safe inset per side
    float cutoutFrom = 0, cutoutTo = 0;   ///< along its side: where the camera is (dp, 0 = top/left)
    Edges ime;           ///< the keyboard (bottom: its height)
    float cornerRadius = 0;   ///< rounded screen corners (Android 12+), 0 if unknown
    bool  hardKeyboard = false;   ///< a physical keyboard is attached
    int   revision = 0;   ///< grows with every change
};
Screen CurrentScreen();

/// Shows or hides the system keyboard (for the field that has focus).
void ShowKeyboard(bool show);
/// What the system keyboard typed since the last call, in order: pieces of
/// text (UTF-8) and special keys as Android key codes (backspace, enter, …).
struct TypedItem {
    std::string text;
    int         key = 0;   ///< 0: text
};
using TypedText = std::vector<TypedItem>;
TypedText TakeTypedText();

/// Immersive mode: the system bars hidden, back to them with a swipe.
void SetImmersive(bool on);
/// Edges where the app wants the touches itself, not the system's back
/// gesture (in dp, left/top/right/bottom rectangles of the window).
void SetGestureExclusion(const std::vector<Edges>& rects);

// --- Files (SAF) ---------------------------------------------------------

/// The projects folder chosen once by the user ("Папка проектов"): its
/// content URI, or empty.
std::string ProjectsFolder();
/// Asks the user for the projects folder (system picker); the answer comes
/// as a PickResult with `folder` set.
void ChooseProjectsFolder();
/// The documents (.bzc, .bza) in the projects folder, newest first.
struct FolderEntry {
    std::string name;
    std::string uri;
    long long   modified = 0;   ///< ms since 1970
};
std::vector<FolderEntry> ListProjects();
/// Opens a document of the projects folder or any picked URI: copies it to
/// the cache and returns the local file to open (empty on failure). Saving
/// that file later writes it back (see WriteBack).
std::filesystem::path Import(const std::string& uri, const std::string& name);
/// A new file in the projects folder for `name`: returns the local file to
/// save to; WriteBack copies it there.
std::filesystem::path CreateInProjects(const std::string& name);
/// After the app wrote `local` (a file from Import or CreateInProjects):
/// copies it to its document. False when it is not such a file or failed.
bool WriteBack(const std::filesystem::path& local);
/// The local copy for the document `uri` named `name`: the one it already
/// has, or a new place no other document uses (cache/documents/<n>/).
std::filesystem::path LocalCopyFor(const std::string& uri, const std::string& name);
/// Whether `local` is the copy of a document of the projects folder. All of
/// them lie in one folder (cache/projects/<folder>/), as the documents lie in
/// theirs: an assembly refers to its parts by name, here as on a computer.
bool InProjects(const std::filesystem::path& local);
/// `local`, or — when it stands for a projects-folder document but lies
/// elsewhere (a copy made by 0.2.3.x) — the copy among the others, made from it.
std::filesystem::path Settle(const std::filesystem::path& local);
/// Copies a projects-folder document to `local` again (Import: an empty one
/// never replaces the copy). Nothing for other files.
void FetchAgain(const std::filesystem::path& local);
/// Remembers that `local` stands for the document `uri` (a "save as" answer).
void Link(const std::filesystem::path& local, const std::string& uri);
/// Whether `local` stands for a document outside the app (SAF).
bool IsImported(const std::filesystem::path& local);
/// The system's "open" picker (any file) and "save as" picker.
void PickToOpen(const std::vector<std::string>& mimeTypes);
void PickToSave(const std::string& suggestedName, const std::string& mime, const std::filesystem::path& local);
struct PickResult {
    enum class Kind { Open, Save, Folder } kind = Kind::Open;
    std::string uri;    ///< empty: cancelled
    std::string name;   ///< display name
};
std::optional<PickResult> TakePickResult();

/// The system's "share" sheet for a file (a report, an export).
void Share(const std::filesystem::path& file, const std::string& mime, const std::string& subject);

// --- Crashes and hangs (ADR-0004, "Сбои и зависания на Android") ----------

/// The watchdog process (":crash") is told the app lives: a beat at least
/// every few seconds. Silence → it offers "wait / close and send a report".
void Heartbeat();
/// "fg", "bg" or "exit": in front, in the background, ended normally.
void SetWatchdogState(const char* state);
/// Why the previous run ended, as the system says (Android 11+):
/// "crash", "native crash", "anr", "low memory", "user", "other" or "".
std::string LastExitReason();
/// Asks once for the notifications permission (Android 13+) that the
/// watchdog's notice needs.
void AskNotifications();

/// The system clipboard (text).
void        SetClipboard(const std::string& text);
std::string Clipboard();

/// Opens a web page in the browser.
bool OpenUrl(const std::string& url);

/// Commands of a test run (smoke-apk.sh: "example,sketch"), empty normally.
std::string TestCommands();

/// "BreezeCAD 0.2.2 (build 57)" from the APK (versionName, versionCode).
std::string PackageVersion();

/// The volume keys zoom the view (a setting): the app takes them instead of
/// the system's volume.
void SetVolumeKeysTaken(bool taken);
bool VolumeKeysTaken();

/// The character a hardware key makes (KeyCharacterMap), 0 if none.
int UnicodeChar(int deviceId, int keyCode, int metaState);

} // namespace bz::platform::android

#endif
