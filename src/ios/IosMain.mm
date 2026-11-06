// BreezeCAD on iOS and iPadOS — step 2 of the port (docs/dev/IOS-PORT.md):
// a view with OpenGL ES 3 (EAGL), the desktop's render::Viewport and a part
// built by the kernel, turned with the fingers. No interface yet (ImGui and
// the mobile layout come with step 3, as on Android); the part is the
// bundled example. The same step as Android's feat/android-viewer.
//
// UIKit owns the main thread and its run loop: the view draws there, on a
// CADisplayLink that runs only while there is something to draw (an idle 3D
// view costs no battery). The part is built on a thread of its own.
//
// OpenGL ES on iOS is deprecated (still there in iOS 18 and later); the
// move to Metal is planned after the port works (ANGLE or OCCT's own).
//
// Gestures (ADR-0004, cursor off): 1 finger — rotate, 2 fingers — move and
// pinch to zoom, double tap — show all.
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES3/gl.h>

#include "core/Log.h"
#include "core/Math.h"
#include "core/NativeSurface.h"
#include "document/BzcFile.h"
#include "document/Document.h"
#include "kernel/Model.h"
#include "render/Viewport.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace bz;   // the Objective-C classes below are at file scope

namespace {

constexpr double kOrbitPerPoint = 0.0075;          // as on Android (kOrbitPerDp)
constexpr Vec3   kHomeDirection{1.0, -1.0, 1.0};   // as ui::kHomeDirection
constexpr double kTapSeconds = 0.25, kDoubleTapSeconds = 0.35, kTapSlopPoints = 12.0;

/// The clock of Viewport::StepAnimation on iOS (render/Viewport.cpp).
double Now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

fs::path ToPath(NSString* s) { return fs::path(s ? std::string([s UTF8String]) : std::string()); }

/// A document and the solids built from it, made on the loading thread and
/// then handed to the main thread whole.
struct Part {
    explicit Part(doc::Document d) : document(std::move(d)) {}
    doc::Document document;
    kernel::Model model;
};

} // namespace

// --- The view ---------------------------------------------------------------

@interface BZGLView : UIView
@end

@implementation BZGLView {
    EAGLContext*   _context;
    CADisplayLink* _link;
    GLuint         _framebuffer;    ///< the window: the layer's colour buffer
    GLuint         _colorBuffer;
    GLuint         _copyFbo;        ///< reads OCCT's picture for the copy
    GLint          _width, _height; ///< of the colour buffer, in pixels
    BOOL           _active;         ///< in front: no OpenGL in the background (iOS kills the app for it)
    BOOL           _wantFrame;
    BOOL           _reportedGlError;

    std::unique_ptr<render::Viewport> _viewport;
    render::CameraState               _camera;

    std::thread           _loader;
    std::mutex            _loadedMutex;
    std::unique_ptr<Part> _loaded;   ///< from the loader, guarded by _loadedMutex
    std::atomic<bool>     _partReady;
    std::unique_ptr<Part> _part;     ///< the one shown

    // Touch: the fingers down, in the order they came, and where they were
    // at the last event.
    NSMutableArray<UITouch*>* _touches;
    std::size_t _fingers;
    Vec2        _last, _lastCentre, _downPos, _lastTap;
    double      _lastSpread, _downAt, _lastTapAt;
    BOOL        _moved;
    Vec3        _pivot;
}

+ (Class)layerClass {
    return [CAEAGLLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self) return nil;
    _touches = [NSMutableArray array];
    _partReady = false;
    _active = YES;
    _wantFrame = YES;
    self.multipleTouchEnabled = YES;
    self.contentScaleFactor = UIScreen.mainScreen.nativeScale;

    CAEAGLLayer* layer = (CAEAGLLayer*)self.layer;
    layer.opaque = YES;
    layer.drawableProperties = @{kEAGLDrawablePropertyRetainedBacking: @NO,
                                 kEAGLDrawablePropertyColorFormat: kEAGLColorFormatRGBA8};

    _context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
    if (!_context || ![EAGLContext setCurrentContext:_context]) {
        bz::log::Error("iOS: cannot start OpenGL ES 3");
        _context = nil;
        return self;
    }
    glGenFramebuffers(1, &_framebuffer);
    glGenRenderbuffers(1, &_colorBuffer);

    _link = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    [_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];

    NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
    [center addObserver:self selector:@selector(willResign:) name:UIApplicationWillResignActiveNotification object:nil];
    [center addObserver:self selector:@selector(didBecomeActive:) name:UIApplicationDidBecomeActiveNotification object:nil];
    return self;
}

- (void)dealloc {
    if (_loader.joinable()) _loader.join();
    [_link invalidate];
    if (_context && [EAGLContext setCurrentContext:_context]) {
        _viewport.reset();   // frees OCCT's framebuffer while its context is current
        if (_copyFbo) glDeleteFramebuffers(1, &_copyFbo);
        glDeleteRenderbuffers(1, &_colorBuffer);
        glDeleteFramebuffers(1, &_framebuffer);
        [EAGLContext setCurrentContext:nil];
    }
}

// --- Life ---------------------------------------------------------------------

- (void)willResign:(NSNotification*)note {
    // OpenGL in the background ends the app: finish what was sent and stop.
    if (_context && [EAGLContext setCurrentContext:_context]) glFinish();
    _active = NO;
    _link.paused = YES;
    if (_viewport) _camera = _viewport->Camera();
}

- (void)didBecomeActive:(NSNotification*)note {
    _active = YES;
    [self setNeedsLayout];   // the size may have changed meanwhile (a turn, Split View)
    [self wake];
}

- (void)wake {
    _wantFrame = YES;
    if (_active) _link.paused = NO;
}

// --- The window's buffer and the 3D view ----------------------------------------

- (void)layoutSubviews {
    [super layoutSubviews];
    if (!_context || !_active) return;
    [EAGLContext setCurrentContext:_context];
    glBindRenderbuffer(GL_RENDERBUFFER, _colorBuffer);
    if (![_context renderbufferStorage:GL_RENDERBUFFER fromDrawable:(CAEAGLLayer*)self.layer]) {
        bz::log::Error("iOS: the layer gives no colour buffer");
        return;
    }
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &_width);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &_height);
    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _colorBuffer);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        bz::log::Error("iOS: the window framebuffer is incomplete ({}x{})", _width, _height);
        return;
    }
    glViewport(0, 0, _width, _height);
    if (!_viewport) [self startViewport];
    [self wake];
}

- (void)startViewport {
    // OCCT takes the context current at Init as the app's own and wraps the
    // framebuffer bound now as its "window" (OpenGl_Window_1.mm, iOS):
    // ours, bound in layoutSubviews. Its view draws into a framebuffer of its
    // own, copied here every frame (as on Android).
    bz::log::Info("iOS: OpenGL ES {} on {}, {}x{} px, scale {:.2f}", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
              reinterpret_cast<const char*>(glGetString(GL_RENDERER)), _width, _height, static_cast<double>(self.contentScaleFactor));
    NativeGlSurface surface;
    surface.window = reinterpret_cast<std::uintptr_t>((__bridge void*)self);
    surface.glContext = (__bridge void*)_context;
    _viewport = std::make_unique<render::Viewport>();
    bool started = false;
    try {   // OCCT reports a context it cannot use by throwing
        started = _viewport->Init(surface);
    } catch (...) {
        bz::log::Error("iOS: OCCT refused the OpenGL ES context");
    }
    if (!started) {
        bz::log::Error("iOS: the 3D view did not start");
        _viewport.reset();
        return;
    }
    if (_part) {
        _viewport->Sync(_part->model);
        _viewport->SetCamera(_camera);
    }
}

- (BOOL)wantsFrame {
    return _active && _viewport && (_wantFrame || _partReady || _viewport->NeedsRender());
}

- (void)tick:(CADisplayLink*)link {
    if (![self wantsFrame]) {
        _link.paused = YES;   // sleeps until a touch, a part or the app's return
        return;
    }
    [self drawFrame];
}

- (void)drawFrame {
    [EAGLContext setCurrentContext:_context];
    [self adoptLoadedPart];
    if (_width <= 0 || _height <= 0) return;
    const double scale = static_cast<double>(self.contentScaleFactor);
    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
    _viewport->SetSize(_width / scale, _height / scale, scale);
    _viewport->StepAnimation(Now());
    _viewport->Render();
    [self present];
    glBindRenderbuffer(GL_RENDERBUFFER, _colorBuffer);
    [_context presentRenderbuffer:GL_RENDERBUFFER];
    _wantFrame = NO;
}

/// Copies the view's picture (OCCT's colour texture) to the window. A
/// framebuffer blit touches no shader or vertex state that OCCT keeps
/// cached; the framebuffer bindings and scissor are put back.
- (void)present {
    const GLuint texture = _viewport->TextureId();
    if (texture == 0) return;
    GLint read = 0, draw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);

    if (_copyFbo == 0) glGenFramebuffers(1, &_copyFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, _copyFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);   // re-made on resize
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _framebuffer);
    const GLenum attachment = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &attachment);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBlitFramebuffer(0, 0, _width, _height, 0, 0, _width, _height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    if (scissor) glEnable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw));
    if (const GLenum error = glGetError(); error != GL_NO_ERROR && !_reportedGlError) {
        _reportedGlError = YES;
        bz::log::Warn("iOS: OpenGL error 0x{:x} after copying the view to the window", error);
    }
}

// --- The part -------------------------------------------------------------------

- (void)startLoading:(const fs::path&)file {
    _loader = std::thread([self, file] {
        const double start = Now();
        auto loaded = doc::LoadBzc(file);
        if (!loaded) {
            bz::log::Error("iOS: cannot open {}: {}", file.string(), loaded.GetError().message);
            return;
        }
        auto part = std::make_unique<Part>(std::move(*loaded));
        part->model.Rebuild(part->document);
        render::Viewport::PrepareMeshes(part->model, 1);
        bz::log::Info("iOS: {} built in {:.2f} s", file.filename().string(), Now() - start);
        {
            const std::lock_guard lock(self->_loadedMutex);
            self->_loaded = std::move(part);
        }
        self->_partReady = true;
        dispatch_async(dispatch_get_main_queue(), ^{
            [self wake];
        });
    });
}

- (void)adoptLoadedPart {
    if (!_partReady.exchange(false)) return;
    std::unique_ptr<Part> part;
    {
        const std::lock_guard lock(_loadedMutex);
        part = std::move(_loaded);
    }
    if (!part) return;
    _part = std::move(part);
    _viewport->Sync(_part->model);
    _viewport->SetCamera(_viewport->FitCamera(-kHomeDirection, {0, 0, 1}));
    _camera = _viewport->Camera();
}

// --- Touch ----------------------------------------------------------------------

- (Vec2)pointOf:(UITouch*)touch {
    const CGPoint p = [touch locationInView:self];
    return Vec2{static_cast<double>(p.x), static_cast<double>(p.y)};
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    if (_touches.count == 0) {
        _downAt = Now();
        _downPos = [self pointOf:touches.anyObject];
        _moved = NO;
        if (_viewport) _pivot = _viewport->ModelCenter();
    } else {
        _moved = YES;   // a second finger is never a tap
    }
    for (UITouch* t in touches)
        if (![_touches containsObject:t]) [_touches addObject:t];
    [self takeFingers];
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    if (!_viewport || _touches.count == 0 || _touches.count != _fingers) return;
    const Vec2   a = [self pointOf:_touches[0]];
    const Vec2   b = _touches.count > 1 ? [self pointOf:_touches[1]] : a;
    const Vec2   centre = (a + b) * 0.5;
    const double spread = std::hypot(b.x - a.x, b.y - a.y);
    if (_touches.count == 1) {
        const Vec2 d = a - _last;
        if (std::hypot(a.x - _downPos.x, a.y - _downPos.y) > kTapSlopPoints) _moved = YES;
        if (_moved && (d.x != 0 || d.y != 0)) _viewport->Orbit(d.x * kOrbitPerPoint, d.y * kOrbitPerPoint, _pivot);
    } else {
        _viewport->Pan(centre.x - _lastCentre.x, centre.y - _lastCentre.y);
        if (_lastSpread > 1.0 && spread > 1.0) _viewport->ZoomAt(spread / _lastSpread, centre);
    }
    [self takeFingers];
    [self wake];
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    const BOOL last = _touches.count == touches.count;
    if (last && !_moved && Now() - _downAt < kTapSeconds) [self onTap:[self pointOf:touches.anyObject]];
    [self liftTouches:touches];
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self liftTouches:touches];
}

- (void)liftTouches:(NSSet<UITouch*>*)touches {
    for (UITouch* t in touches) [_touches removeObject:t];
    if (_touches.count == 1 && _fingers > 1 && _viewport) _pivot = _viewport->ModelCenter();   // back to rotating
    [self takeFingers];
}

/// The fingers still down: the next move counts from them.
- (void)takeFingers {
    _fingers = _touches.count;
    if (_fingers == 0) return;
    const Vec2 first = [self pointOf:_touches[0]];
    const Vec2 second = _fingers > 1 ? [self pointOf:_touches[1]] : first;
    _last = first;
    _lastCentre = (first + second) * 0.5;
    _lastSpread = std::hypot(second.x - first.x, second.y - first.y);
}

- (void)onTap:(Vec2)at {
    const double now = Now();
    const bool twice = now - _lastTapAt < kDoubleTapSeconds && std::hypot(at.x - _lastTap.x, at.y - _lastTap.y) < 3 * kTapSlopPoints;
    _lastTapAt = twice ? 0.0 : now;
    _lastTap = at;
    if (!twice || !_viewport) return;
    const render::CameraState camera = _viewport->Camera();
    _viewport->AnimateTo(_viewport->FitCamera(camera.center - camera.eye, camera.up), 0.35);
    [self wake];
}

@end

// --- The app ----------------------------------------------------------------------

@interface BZViewController : UIViewController
@end

@implementation BZViewController
- (void)loadView {
    self.view = [[BZGLView alloc] initWithFrame:UIScreen.mainScreen.bounds];
}
- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
@end

@interface BZAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation BZAppDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)options {
    // Application Support: settings, logs and the autosave later (as Android's internal data).
    NSString* support = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject;
    const fs::path data = ToPath(support);
    bz::log::OpenFile(data / "logs" / "output.log");
    bz::log::Info("BreezeCAD for iOS: start, {} {}, data in {}", std::string(UIDevice.currentDevice.model.UTF8String),
              std::string(UIDevice.currentDevice.systemVersion.UTF8String), data.string());

    auto* controller = [[BZViewController alloc] init];
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = controller;
    [self.window makeKeyAndVisible];

    // The example lies in the app's bundle (package-app.sh): a real file.
    NSString* example = [NSBundle.mainBundle pathForResource:@"bracket" ofType:@"bzc" inDirectory:@"examples"];
    if (example) {
        [(BZGLView*)controller.view startLoading:ToPath(example)];
    } else {
        bz::log::Error("iOS: no examples/bracket.bzc in the app");
    }
    return YES;
}
@end

int main(int argc, char* argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([BZAppDelegate class]));
    }
}
