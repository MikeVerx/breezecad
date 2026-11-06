// BreezeCAD on iOS and iPadOS: the entry point (docs/dev/IOS-PORT.md). UIKit
// starts the app; once it has launched, the program (app/Application) runs
// on the main thread as on the desktop, and its loop pumps the main run loop
// (platform/Window_ios.mm), as SDL does on iOS. The view here is the window:
// a CAEAGLLayer for OpenGL ES, the fingers and the Apple Pencil, the system
// keyboard (UIKeyInput).
//
// Crashes (ADR-0004, layer 1, as on Android): the signal handler writes the
// report; the next start shows it (Application_mobile.cpp, CheckLastExit).
#include "app/Application.h"
#include "app/BetaTesting.h"
#include "app/CrashGuard.h"
#include "core/Log.h"
#include "core/Monitor.h"
#include "platform/Ios.h"
#include "platform/Mobile.h"
#include "platform/Window.h"

#include <BRepMesh_IncrementalMeshFactory.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>
#include <TCollection_AsciiString.hxx>

#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

// UIKit after the program's headers: its macros reach none of them.
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES3/gl.h>

namespace ios = bz::platform::ios;

namespace {

double Now() { return bz::platform::Window::Time(); }

/// OCCT's own messages (shader errors, OpenGL warnings) into the app's log.
class OcctToLog : public Message_Printer {
protected:
    void send(const TCollection_AsciiString& text, const Message_Gravity gravity) const override {
        if (gravity < Message_Warning) return;
        bz::log::Warn("OCCT: {}", text.ToCString());
    }
};

void Say(int fd, const char* text) { (void)!::write(fd, text, std::strlen(text)); }

void OnSignal(int signal) {
    const char* name = signal == SIGSEGV ? "SIGSEGV" : signal == SIGABRT ? "SIGABRT" : signal == SIGFPE ? "SIGFPE"
                     : signal == SIGBUS  ? "SIGBUS"  : signal == SIGILL  ? "SIGILL"  : "signal";
    bz::mon::CrashNote(name);
    if (const char* report = bz::app::crash::ReportPath(); report[0]) {
        const int fd = ::open(report, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            Say(fd, bz::app::crash::ReportHeader());
            Say(fd, "Crash: ");
            Say(fd, name);
            Say(fd, "\n");
            Say(fd, bz::app::crash::ReasonLine());
            Say(fd, "Last action: ");
            Say(fd, bz::mon::LastAction().data());
            Say(fd, "\nStack:\n");
            void* frames[48];
            const int count = backtrace(frames, 48);
            backtrace_symbols_fd(frames, count, fd);
            ::close(fd);
        }
    }
    std::signal(signal, SIG_DFL);   // the system's own report next
    std::raise(signal);
}

void CatchCrashes() {
    static char altStack[64 * 1024];   // a stack overflow is reported too
    stack_t ss{};
    ss.ss_sp = altStack;
    ss.ss_size = sizeof altStack;
    sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_handler = OnSignal;
    sa.sa_flags = static_cast<int>(SA_ONSTACK | SA_RESETHAND);
    for (const int s : {SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL}) sigaction(s, &sa, nullptr);
}

[[noreturn]] void OnTerminate() {
    std::string what = "unknown";
    if (const std::exception_ptr e = std::current_exception()) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception& x) {
            what = x.what();
        } catch (...) {
        }
    }
    bz::log::Error("Crash: uncaught exception: {} (last action: {})", what, bz::mon::LastAction());
    bz::mon::CrashNote("uncaught exception");
    bz::app::crash::NoteReason(("uncaught exception: " + what).c_str());
    std::abort();
}

} // namespace

// --- The view ---------------------------------------------------------------------

@interface BZView : UIView <UIKeyInput>
@property(nonatomic) UITextAutocorrectionType autocorrectionType;
@property(nonatomic) UITextAutocapitalizationType autocapitalizationType;
@property(nonatomic) UITextSpellCheckingType spellCheckingType;
@property(nonatomic) UITextSmartQuotesType smartQuotesType;
@property(nonatomic) UITextSmartDashesType smartDashesType;
@property(nonatomic) UIKeyboardType keyboardType;
@end

@implementation BZView {
    NSMutableDictionary<NSValue*, NSNumber*>* _ids;   ///< UITouch → the pointer's id while it is down
    int     _nextId;
    UIView* _noKeyboard;   ///< the input view while no system keyboard is wanted: nothing on screen
}

+ (Class)layerClass {
    return [CAEAGLLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self) return nil;
    _ids = [NSMutableDictionary dictionary];
    _noKeyboard = [[UIView alloc] initWithFrame:CGRectZero];
    // No shortcut bar over a hardware keyboard either.
    self.inputAssistantItem.leadingBarButtonGroups = @[];
    self.inputAssistantItem.trailingBarButtonGroups = @[];
    self.multipleTouchEnabled = YES;
    self.contentScaleFactor = UIScreen.mainScreen.nativeScale;
    self.autocorrectionType = UITextAutocorrectionTypeNo;
    self.autocapitalizationType = UITextAutocapitalizationTypeNone;
    self.spellCheckingType = UITextSpellCheckingTypeNo;
    self.smartQuotesType = UITextSmartQuotesTypeNo;
    self.smartDashesType = UITextSmartDashesTypeNo;
    self.keyboardType = UIKeyboardTypeDefault;
    CAEAGLLayer* layer = (CAEAGLLayer*)self.layer;
    layer.opaque = YES;
    layer.drawableProperties = @{kEAGLDrawablePropertyRetainedBacking: @NO,
                                 kEAGLDrawablePropertyColorFormat: kEAGLColorFormatRGBA8};

    NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
    [center addObserver:self selector:@selector(keyboardMoved:) name:UIKeyboardWillChangeFrameNotification object:nil];
    [center addObserver:self selector:@selector(keyboardMoved:) name:UIKeyboardWillHideNotification object:nil];
    return self;
}

- (void)layoutSubviews {
    [super layoutSubviews];
    ios::ViewChanged();
    ios::ScreenMoved();
}

- (void)safeAreaInsetsDidChange {
    [super safeAreaInsetsDidChange];
    ios::ScreenMoved();
}

- (void)traitCollectionDidChange:(UITraitCollection*)previous {
    [super traitCollectionDidChange:previous];
    ios::ViewChanged();   // light or dark: the app follows the system's theme
}

- (void)keyboardMoved:(NSNotification*)note {
    const CGRect end = [note.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
    const CGRect inView = [self convertRect:end fromView:nil];
    const BOOL hiding = [note.name isEqualToString:UIKeyboardWillHideNotification];
    const CGFloat height = hiding ? 0 : MAX(0, CGRectGetMaxY(self.bounds) - CGRectGetMinY(inView));
    ios::KeyboardMoved(static_cast<float>(height));
}

// --- Touch -------------------------------------------------------------------------

- (void)deliver:(NSSet<UITouch*>*)touches phase:(int)phase {
    for (UITouch* t in touches) {
        NSValue* key = [NSValue valueWithNonretainedObject:t];
        NSNumber* known = _ids[key];
        int pid = known ? known.intValue : 0;
        if (!known) {
            if (phase != 0) continue;   // never seen going down
            pid = ++_nextId;
            _ids[key] = @(pid);
        }
        const CGPoint p = [t locationInView:self];
        bz::platform::Window::Touch touch;
        touch.id = pid;
        touch.phase = phase;
        touch.tool = t.type == UITouchTypePencil ? 1 : t.type == UITouchTypeIndirectPointer ? 2 : 0;
        touch.x = static_cast<float>(p.x);
        touch.y = static_cast<float>(p.y);
        touch.time = Now();
        ios::DeliverTouch(touch);
        if (phase >= 2) [_ids removeObjectForKey:key];
    }
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self deliver:touches phase:0]; }
- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self deliver:touches phase:1]; }
- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self deliver:touches phase:2]; }
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self deliver:touches phase:3]; }

// --- The keyboard -----------------------------------------------------------------------
//
// The view is always the first responder: a hardware keyboard's keys come to
// it (pressesBegan). The system keyboard shows only while a field wants it —
// otherwise the input view is an empty one. Characters, from either keyboard,
// come through UIKeyInput (insertText); keys with a meaning of their own
// (arrows, Esc, Enter, Backspace, Cmd/Ctrl shortcuts) through the presses.

- (BOOL)canBecomeFirstResponder {
    return YES;
}

- (void)didMoveToWindow {
    [super didMoveToWindow];
    if (self.window) [self becomeFirstResponder];
}

- (UIView*)inputView {
    return ios::WantsKeyboard() ? nil : _noKeyboard;
}

/// Whether a press is the program's alone: no character for UIKeyInput to
/// type (Enter and Backspace too, or they would come twice).
- (BOOL)takesPress:(UIPress*)press {
    UIKey* key = press.key;
    if (!key) return NO;
    const UIKeyModifierFlags mods = key.modifierFlags;
    if (mods & (UIKeyModifierCommand | UIKeyModifierControl)) return YES;
    return key.characters.length == 0 || key.keyCode == UIKeyboardHIDUsageKeyboardReturnOrEnter ||
           key.keyCode == UIKeyboardHIDUsageKeyboardDeleteOrBackspace || key.keyCode == UIKeyboardHIDUsageKeyboardTab ||
           key.keyCode == UIKeyboardHIDUsageKeyboardEscape || key.keyCode == UIKeyboardHIDUsageKeyboardDeleteForward;
}

- (void)deliverPresses:(NSSet<UIPress*>*)presses down:(BOOL)down {
    for (UIPress* press in presses) {
        UIKey* key = press.key;
        if (!key) continue;
        bz::platform::Window::Key k;
        k.usage = static_cast<int>(key.keyCode);
        k.down = down;
        k.ctrl = (key.modifierFlags & UIKeyModifierControl) != 0;
        k.shift = (key.modifierFlags & UIKeyModifierShift) != 0;
        k.alt = (key.modifierFlags & UIKeyModifierAlternate) != 0;
        k.command = (key.modifierFlags & UIKeyModifierCommand) != 0;
        ios::DeliverKeyPress(k);
    }
}

- (void)pressesBegan:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
    [self deliverPresses:presses down:YES];
    NSMutableSet<UIPress*>* rest = [NSMutableSet set];
    for (UIPress* p in presses)
        if (![self takesPress:p]) [rest addObject:p];
    if (rest.count) [super pressesBegan:rest withEvent:event];   // characters: to insertText
}

- (void)pressesEnded:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
    [self deliverPresses:presses down:NO];
    NSMutableSet<UIPress*>* rest = [NSMutableSet set];
    for (UIPress* p in presses)
        if (![self takesPress:p]) [rest addObject:p];
    if (rest.count) [super pressesEnded:rest withEvent:event];
}

- (void)pressesCancelled:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
    [self deliverPresses:presses down:NO];
    [super pressesCancelled:presses withEvent:event];
}
- (BOOL)hasText {
    return YES;   // backspace always reaches the field
}
- (void)insertText:(NSString*)text {
    if ([text isEqualToString:@"\n"]) ios::DeliverKey(66);   // Enter, as an Android key code (Mobile.h)
    else ios::DeliverText(std::string(text.UTF8String));
}
- (void)deleteBackward {
    ios::DeliverKey(67);   // Backspace
}

@end

// --- The app --------------------------------------------------------------------------

@interface BZViewController : UIViewController
@end

@implementation BZViewController
- (void)loadView {
    self.view = [[BZView alloc] initWithFrame:UIScreen.mainScreen.bounds];
}
- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures { return UIRectEdgeAll; }   // the panels' edges are ours
@end

@interface BZAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation BZAppDelegate

- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)options {
    auto* controller = [[BZViewController alloc] init];
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = controller;
    [self.window makeKeyAndVisible];
    ios::SetView((__bridge void*)controller.view);
    // The program starts once UIKit is done launching: it does not return
    // until the app ends, and pumps the run loop meanwhile.
    [self performSelector:@selector(runProgram) withObject:nil afterDelay:0.0];
    return YES;
}

- (void)applicationWillResignActive:(UIApplication*)application {
    // No OpenGL in the background (iOS ends the app for it): finish what was sent.
    if ([EAGLContext currentContext]) glFinish();
    ios::SetInBackground(true);
    ios::ViewChanged();   // the program writes the autosave now (Application_mobile.cpp)
}

- (void)applicationDidBecomeActive:(UIApplication*)application {
    ios::SetInBackground(false);
    ios::ViewChanged();
}

- (void)applicationWillTerminate:(UIApplication*)application {
    ios::SetClosing();
}

- (void)runProgram {
    namespace mobile = bz::platform::mobile;
    const std::filesystem::path data = mobile::DataDir();
    bz::log::OpenFile(data / "logs" / "breezecad.log");   // the app's log until Application opens its own
    bz::log::Info("BreezeCAD for iOS: start, {} {}, data in {}", std::string(UIDevice.currentDevice.model.UTF8String),
                  std::string(UIDevice.currentDevice.systemVersion.UTF8String), data.string());

    Message::DefaultMessenger()->AddPrinter(new OcctToLog());
    // OCCT is static here: its mesher ("FastDiscret") registers itself from
    // a static object the linker leaves out when nothing refers to its file —
    // without it the faces are not meshed (only edges seen). Registered by hand.
    static const Handle(BRepMesh_IncrementalMeshFactory) mesher = new BRepMesh_IncrementalMeshFactory();

    bz::app::beta::BeginSession();
    bz::mon::StartFromEnvironment();
    bz::app::crash::Prepare();
    std::set_terminate(OnTerminate);
    CatchCrashes();

    int code = 1;
    {
        bz::app::Application app;
        if (app.Init({})) code = app.Run();
    }
    bz::mon::Stop(code);
    bz::app::beta::EndSession(code);
    bz::log::Info("BreezeCAD for iOS: exit ({})", code);
    std::exit(code);   // the program ended by itself (its "Выход"): the app goes too
}

@end

int main(int argc, char* argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([BZAppDelegate class]));
    }
}
