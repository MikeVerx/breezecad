// platform/Mobile.h and platform/Ios.h on iOS and iPadOS (docs/dev/IOS-PORT.md).
//
// Files: the projects folder is the app's own Documents folder. With
// UIFileSharingEnabled and LSSupportsOpeningDocumentsInPlace (Info.plist)
// the Files app shows it as "На iPad → BreezeCAD": the user's documents lie
// there as on a computer, no copies back and forth (Android needs those for
// SAF). A file picked elsewhere comes in as a copy (the picker's asCopy) and
// is saved into the projects folder.
//
// Everything here runs on the main thread, which the program runs on (Ios.h).
#include "platform/Ios.h"
#include "platform/Mobile.h"

#include "core/Log.h"

#import <GameController/GameController.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <map>
#include <mutex>
#include <system_error>

namespace fs = std::filesystem;

// --- The picker's answers ------------------------------------------------------

@interface BZPickerDelegate : NSObject <UIDocumentPickerDelegate>
@end

namespace {

using namespace bz::platform;

struct Shared {
    std::mutex                        mutex;
    mobile::TypedText                 typed;
    std::optional<mobile::PickResult> picked;
    int                               screenRevision = 0;
    float                             keyboardHeight = 0;   ///< points over the view's bottom
    bool                              background = false;
    bool                              closing = false;
    bool                              wantsKeyboard = false;
};
Shared& S() {
    static Shared s;
    return s;
}

void*             gView = nullptr;
BZPickerDelegate* gPickerDelegate = nil;

fs::path ToPath(NSString* s) { return fs::path(s ? std::string(s.UTF8String) : std::string()); }
NSString* ToNs(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }

fs::path Folder(NSSearchPathDirectory which) {
    NSString* dir = NSSearchPathForDirectoriesInDomains(which, NSUserDomainMask, YES).firstObject;
    std::error_code ec;
    fs::create_directories(ToPath(dir), ec);
    return ToPath(dir);
}

fs::path Projects() { return Folder(NSDocumentDirectory); }

std::string FileUri(const fs::path& p) { return "file://" + p.string(); }

fs::path FromUri(const std::string& uri) {
    if (uri.rfind("file://", 0) != 0) return fs::path(uri);
    NSURL* url = [NSURL URLWithString:ToNs(uri)];
    return url.fileURL ? ToPath(url.path) : fs::path(uri.substr(7));
}

bool Under(const fs::path& file, const fs::path& folder) {
    std::error_code ec;
    const fs::path a = fs::weakly_canonical(file, ec), b = fs::weakly_canonical(folder, ec);
    const auto [end, rest] = std::mismatch(b.begin(), b.end(), a.begin(), a.end());
    return end == b.end();
}

std::string Lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

UIViewController* Presenter() {
    UIView* view = (__bridge UIView*)gView;
    UIViewController* c = view.window.rootViewController;
    while (c.presentedViewController) c = c.presentedViewController;
    return c;
}

void Answer(mobile::PickResult r) {
    {
        const std::lock_guard lock(S().mutex);
        S().picked = std::move(r);
    }
    Window::PostEmptyEvent();
}

} // namespace

@implementation BZPickerDelegate
- (void)documentPicker:(UIDocumentPickerViewController*)picker didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    NSURL* url = urls.firstObject;
    mobile::PickResult r;
    r.kind = mobile::PickResult::Kind::Open;
    if (url) {
        r.uri = FileUri(ToPath(url.path));
        r.name = std::string(url.lastPathComponent.UTF8String);
    }
    Answer(std::move(r));
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)picker {
    Answer({});   // empty: cancelled
}
@end

namespace bz::platform {

// --- ios -------------------------------------------------------------------------

void  ios::SetView(void* view) { gView = view; }
void* ios::View() { return gView; }

fs::path ios::BundleDir() { return ToPath(NSBundle.mainBundle.bundlePath); }

void ios::DeliverText(const std::string& text) {
    {
        const std::lock_guard lock(S().mutex);
        S().typed.push_back({text, 0});
    }
}

void ios::DeliverKey(int key) {
    {
        const std::lock_guard lock(S().mutex);
        S().typed.push_back({{}, key});
    }
}

void ios::SetInBackground(bool background) {
    const std::lock_guard lock(S().mutex);
    S().background = background;
}
bool ios::InBackground() {
    const std::lock_guard lock(S().mutex);
    return S().background;
}
void ios::SetClosing() {
    const std::lock_guard lock(S().mutex);
    S().closing = true;
}
bool ios::Closing() {
    const std::lock_guard lock(S().mutex);
    return S().closing;
}
bool ios::WantsKeyboard() {
    const std::lock_guard lock(S().mutex);
    return S().wantsKeyboard;
}

bool ios::PrefersDark() {
    UIView* view = (__bridge UIView*)gView;
    UITraitCollection* traits = view ? view.traitCollection : UITraitCollection.currentTraitCollection;
    return traits.userInterfaceStyle == UIUserInterfaceStyleDark;
}

// --- mobile: folders ---------------------------------------------------------------

fs::path mobile::DataDir() { return Folder(NSApplicationSupportDirectory); }
fs::path mobile::CacheDir() { return Folder(NSCachesDirectory); }
fs::path mobile::UnpackAssets() { return ios::BundleDir() / "assets"; }   // in the bundle already

// --- mobile: the screen and the keyboard ---------------------------------------------

mobile::Screen mobile::CurrentScreen() {
    Screen s;
    UIView* view = (__bridge UIView*)gView;
    if (view) {
        // The safe area: the status bar and the home indicator as bars; a
        // phone's notch, which is on the left or right in landscape, as the cut-out.
        const UIEdgeInsets safe = view.safeAreaInsets;
        s.bars = {0, static_cast<float>(safe.top), 0, static_cast<float>(safe.bottom)};
        s.cutout = {static_cast<float>(safe.left), 0, static_cast<float>(safe.right), 0};
        s.cutoutFrom = 0;
        s.cutoutTo = static_cast<float>(view.bounds.size.height);
    }
    if (@available(iOS 14.0, *)) s.hardKeyboard = GCKeyboard.coalescedKeyboard != nil;
    const std::lock_guard lock(S().mutex);
    s.ime.bottom = S().keyboardHeight;
    s.revision = S().screenRevision;
    return s;
}

void mobile::ShowKeyboard(bool show) {
    {
        const std::lock_guard lock(S().mutex);
        S().wantsKeyboard = show;
    }
    // The view stays the first responder (a hardware keyboard's keys come
    // to it); its input view is the system keyboard only while one is wanted
    // (app/ios/IosMain.mm, inputView).
    UIView* view = (__bridge UIView*)gView;
    [view reloadInputViews];
    [view becomeFirstResponder];
}

mobile::TypedText mobile::TakeTypedText() {
    const std::lock_guard lock(S().mutex);
    return std::exchange(S().typed, {});
}

void mobile::SetImmersive(bool) {}   // the app is full screen and hides the status bar already
void mobile::SetGestureExclusion(const std::vector<Edges>&) {}

// --- mobile: files ---------------------------------------------------------------------

std::string mobile::ProjectsFolder() { return FileUri(Projects()); }

void mobile::ChooseProjectsFolder() {
    // One folder on iOS: the app's own, seen in the Files app.
    Answer({PickResult::Kind::Folder, FileUri(Projects()), std::string([UIDevice.currentDevice.model UTF8String]) + " → BreezeCAD"});
}

std::vector<mobile::FolderEntry> mobile::ListProjects() {
    std::vector<FolderEntry> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(Projects(), ec)) {
        const std::string ext = Lower(e.path().extension().string());
        if (ext != ".bzc" && ext != ".bza") continue;
        const auto t = e.last_write_time(ec);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            (t - fs::file_time_type::clock::now() + std::chrono::system_clock::now()).time_since_epoch())
                            .count();
        out.push_back({e.path().filename().string(), FileUri(e.path()), static_cast<long long>(ms)});
    }
    std::ranges::sort(out, [](const FolderEntry& a, const FolderEntry& b) { return a.modified > b.modified; });
    return out;
}

fs::path mobile::LocalCopyFor(const std::string& uri, const std::string& name) {
    const fs::path file = FromUri(uri);
    if (Under(file, Projects())) return file;
    std::error_code ec;
    const fs::path dir = CacheDir() / "documents";
    fs::create_directories(dir, ec);
    return dir / fs::path(name);
}

fs::path mobile::Import(const std::string& uri, const std::string& name) {
    const fs::path file = FromUri(uri);
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        bz::log::Warn("iOS: no file {}", file.string());
        return {};
    }
    if (Under(file, Projects())) return file;   // a document of the projects folder: opened in place
    const fs::path local = LocalCopyFor(uri, name);
    fs::copy_file(file, local, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        bz::log::Warn("iOS: cannot copy {}: {}", file.string(), ec.message());
        return {};
    }
    return local;
}

fs::path mobile::CreateInProjects(const std::string& name) { return Projects() / fs::path(name); }

bool mobile::WriteBack(const fs::path& local) { return InProjects(local); }   // written in place already

bool mobile::InProjects(const fs::path& local) { return Under(local, Projects()); }
fs::path mobile::Settle(const fs::path& local) { return local; }
void mobile::FetchAgain(const fs::path&) {}
void mobile::Link(const fs::path&, const std::string&) {}
bool mobile::IsImported(const fs::path&) { return false; }

void mobile::PickToOpen(const std::vector<std::string>&) {
    // Any document; the app checks the type it opens (as on Android).
    if (!gPickerDelegate) gPickerDelegate = [[BZPickerDelegate alloc] init];
    UIDocumentPickerViewController* picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeItem]
                                                                                                         asCopy:YES];
    picker.delegate = gPickerDelegate;
    picker.allowsMultipleSelection = NO;
    UIViewController* presenter = Presenter();
    if (!presenter) {
        Answer({});
        return;
    }
    [presenter presentViewController:picker animated:YES completion:nil];
}

void mobile::PickToSave(const std::string& suggestedName, const std::string&, const fs::path&) {
    // "Save as" goes into the projects folder under a free name: the user
    // finds it in the Files app (and moves it from there if wanted).
    const fs::path folder = Projects();
    const fs::path wanted = fs::path(suggestedName.empty() ? std::string("Документ.bzc") : suggestedName);
    fs::path target = folder / wanted;
    std::error_code ec;
    for (int n = 2; fs::exists(target, ec); ++n)
        target = folder / fs::path(wanted.stem().string() + " (" + std::to_string(n) + ")" + wanted.extension().string());
    Answer({PickResult::Kind::Save, FileUri(target), target.filename().string()});
}

std::optional<mobile::PickResult> mobile::TakePickResult() {
    const std::lock_guard lock(S().mutex);
    return std::exchange(S().picked, std::nullopt);
}

void mobile::Share(const fs::path& file, const std::string&, const std::string& subject) {
    UIViewController* presenter = Presenter();
    if (!presenter) return;
    NSURL* url = [NSURL fileURLWithPath:ToNs(file.string())];
    UIActivityViewController* sheet = [[UIActivityViewController alloc] initWithActivityItems:@[url] applicationActivities:nil];
    [sheet setValue:ToNs(subject) forKey:@"subject"];
    // On an iPad the sheet is a popover: it needs a place to point at.
    UIView* view = (__bridge UIView*)gView;
    sheet.popoverPresentationController.sourceView = view;
    sheet.popoverPresentationController.sourceRect = CGRectMake(CGRectGetMidX(view.bounds), CGRectGetMidY(view.bounds), 1, 1);
    [presenter presentViewController:sheet animated:YES completion:nil];
}

// --- mobile: crashes, clipboard, the rest ----------------------------------------------

// No watchdog process on iOS: the hang guard writes its report, the next
// start shows it (Application's CheckLastExit reads the reports folder).
void mobile::Heartbeat() {}
void mobile::SetWatchdogState(const char*) {}
std::string mobile::LastExitReason() { return {}; }
void mobile::AskNotifications() {}

void mobile::SetClipboard(const std::string& text) { UIPasteboard.generalPasteboard.string = ToNs(text); }
std::string mobile::Clipboard() {
    NSString* s = UIPasteboard.generalPasteboard.string;
    return s ? std::string(s.UTF8String) : std::string();
}

bool mobile::OpenUrl(const std::string& url) {
    NSURL* u = [NSURL URLWithString:ToNs(url)];
    if (!u) return false;
    [UIApplication.sharedApplication openURL:u options:@{} completionHandler:nil];
    return true;
}

std::string mobile::TestCommands() {
    const char* commands = std::getenv("BZ_TEST_COMMANDS");   // scripts/ios/simulator.sh smoke
    return commands ? commands : "";
}

std::string mobile::PackageVersion() {
    NSDictionary* info = NSBundle.mainBundle.infoDictionary;
    NSString* version = info[@"CFBundleShortVersionString"];
    NSString* build = info[@"CFBundleVersion"];
    if (!version) return {};
    return "BreezeCAD " + std::string(version.UTF8String) + " (build " + std::string(build ? build.UTF8String : "?") + ")";
}

} // namespace bz::platform

// --- The keyboard's height, for the layout (MobileBetweenFrames) ---------------------------

namespace bz::platform::ios {
/// Called by IosMain on the keyboard's notices: its frame in the view.
void KeyboardMoved(float height) {
    {
        const std::lock_guard lock(S().mutex);
        S().keyboardHeight = std::max(0.0f, height);
        ++S().screenRevision;
    }
    Window::PostEmptyEvent();
}
/// The safe area or size changed: the layout reads the screen again.
void ScreenMoved() {
    {
        const std::lock_guard lock(S().mutex);
        ++S().screenRevision;
    }
    Window::PostEmptyEvent();
}
} // namespace bz::platform::ios
