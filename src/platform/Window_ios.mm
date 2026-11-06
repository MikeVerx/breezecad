// platform::Window on iOS and iPadOS: the app's view (a CAEAGLLayer) with
// OpenGL ES 3 (EAGL). Coordinates are points; the drawable is in pixels, as
// on a Retina Mac and like dp on Android.
//
// The program runs on the main thread (platform/Ios.h): WaitEvents runs the
// main run loop, which delivers the touches, the keyboard and the system's
// notices to the view, and the view hands them here.
//
// There is no framebuffer 0 on iOS: the window is a framebuffer of ours with
// the layer's colour buffer. OCCT is given the context with that framebuffer
// bound (it wraps it as its "window"); its view draws into a framebuffer of
// its own, and ReclaimDrawable binds ours again for the interface.
#include "platform/Window.h"

#include "core/Log.h"
#include "platform/Ios.h"

#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES3/gl.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <vector>

namespace bz::platform {

namespace {
float   gDensity = 1.0f;          ///< for CurrentFramebufferScale: one window per app
Window* gWindow = nullptr;         ///< the window touches go to

EAGLContext* Context(void* p) { return (__bridge EAGLContext*)p; }
UIView*      View(void* p) { return (__bridge UIView*)p; }
} // namespace

struct IosWindowAccess {
    /// The layer's colour buffer again, for the view's size now.
    static bool Storage(Window& w) {
        if (!w.myContext || !w.myView) return false;
        [EAGLContext setCurrentContext:Context(w.myContext)];
        glBindRenderbuffer(GL_RENDERBUFFER, w.myColorBuffer);
        if (![Context(w.myContext) renderbufferStorage:GL_RENDERBUFFER fromDrawable:(CAEAGLLayer*)View(w.myView).layer]) {
            log::Error("iOS: the layer gives no colour buffer");
            return false;
        }
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &w.myPixelsWide);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &w.myPixelsHigh);
        glBindFramebuffer(GL_FRAMEBUFFER, w.myFramebuffer);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, w.myColorBuffer);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            log::Error("iOS: the window framebuffer is incomplete ({}x{})", w.myPixelsWide, w.myPixelsHigh);
            return false;
        }
        w.myDensity = gDensity = static_cast<float>(View(w.myView).contentScaleFactor);
        return true;
    }
    static void Touch(Window& w, const Window::Touch& t) {
        if (w.myTouch) w.myTouch(t);
    }
    static void Key(Window& w, const Window::Key& k) {
        if (w.myKey) w.myKey(k);
    }
    static void Changed(Window& w) {
        if (!ios::InBackground()) Storage(w);
        if (w.myChanged) w.myChanged();
    }
};

void ios::DeliverTouch(const Window::Touch& touch) {
    if (gWindow) IosWindowAccess::Touch(*gWindow, touch);
}

void ios::DeliverKeyPress(const Window::Key& key) {
    if (gWindow) IosWindowAccess::Key(*gWindow, key);
}

void ios::ViewChanged() {
    if (gWindow) IosWindowAccess::Changed(*gWindow);
}

bool InitGlfw() { return true; }

Window::~Window() {
    if (gWindow == this) gWindow = nullptr;
    if (myContext) {
        if ([EAGLContext setCurrentContext:Context(myContext)]) {
            if (myColorBuffer) glDeleteRenderbuffers(1, &myColorBuffer);
            if (myFramebuffer) glDeleteFramebuffers(1, &myFramebuffer);
        }
        [EAGLContext setCurrentContext:nil];
        CFRelease(myContext);   // retained in Create
        myContext = nullptr;
    }
}

bool Window::Create(const WindowConfig&) {
    myView = ios::View();
    if (!myView) {
        log::Error("iOS: no view to draw into");
        return false;
    }
    EAGLContext* context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
    if (!context || ![EAGLContext setCurrentContext:context]) {
        log::Error("iOS: cannot start OpenGL ES 3");
        return false;
    }
    myContext = (void*)CFBridgingRetain(context);
    glGenFramebuffers(1, &myFramebuffer);
    glGenRenderbuffers(1, &myColorBuffer);
    if (!IosWindowAccess::Storage(*this)) return false;
    gWindow = this;
    glViewport(0, 0, myPixelsWide, myPixelsHigh);
    if (const auto* v = reinterpret_cast<const char*>(glGetString(GL_VERSION))) myGLVersion = v;
    if (const auto* v = reinterpret_cast<const char*>(glGetString(GL_VENDOR))) myGLVendor = v;
    if (const auto* v = reinterpret_cast<const char*>(glGetString(GL_RENDERER))) myGLRenderer = v;
    log::Info("Window: {} ({} / {}), {}x{} px, scale {:.2f}", myGLVersion, myGLVendor, myGLRenderer, myPixelsWide, myPixelsHigh, myDensity);
    return true;
}

NativeGlSurface Window::NativeSurface() const {
    // OCCT takes the context current at Viewport::Init as the app's own and
    // wraps the framebuffer bound then (OpenGl_Window_1.mm, iOS): ours.
    [EAGLContext setCurrentContext:Context(myContext)];
    glBindFramebuffer(GL_FRAMEBUFFER, myFramebuffer);
    glViewport(0, 0, myPixelsWide, myPixelsHigh);
    NativeGlSurface surface;
    surface.window = reinterpret_cast<std::uintptr_t>(myView);
    surface.glContext = myContext;
    return surface;
}

bool Window::ShouldClose() const { return myCloseRequested || ios::Closing(); }
void Window::SetShouldClose(bool close) { myCloseRequested = close; }
void Window::SetTitle(const std::string&) {}
void Window::SetIcon(const unsigned char*, int) {}

void Window::SwapBuffers() {
    if (ios::InBackground() || !myContext) return;
    glBindRenderbuffer(GL_RENDERBUFFER, myColorBuffer);
    [Context(myContext) presentRenderbuffer:GL_RENDERBUFFER];
}

float Window::ContentScale() const { return myDensity; }

void Window::MonitorWorkArea(int& x, int& y, int& width, int& height) const {
    x = y = 0;
    WindowSize(width, height);
}

void Window::Geometry(int& x, int& y, int& width, int& height, bool& maximized) const {
    x = y = -1;
    width = height = 0;
    maximized = true;
}

void Window::Show() {}
bool Window::IsMinimized() const { return ios::InBackground(); }

bool Window::ReclaimDrawable() {
    // HandBackToWindow binds framebuffer 0, which is nothing on iOS: ours.
    if (!myContext) return false;
    if ([EAGLContext currentContext] != Context(myContext)) [EAGLContext setCurrentContext:Context(myContext)];
    glBindFramebuffer(GL_FRAMEBUFFER, myFramebuffer);
    const GLenum attachment = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &attachment);
    return false;   // the usual way here, nothing to log
}

void Window::FramebufferSize(int& width, int& height) const {
    width = myPixelsWide;
    height = myPixelsHigh;
}

void Window::WindowSize(int& width, int& height) const {
    const CGSize size = myView ? View(myView).bounds.size : CGSizeZero;
    width = static_cast<int>(size.width + 0.5);
    height = static_cast<int>(size.height + 0.5);
}

bool Window::CanDraw() const { return myContext && !ios::InBackground() && myPixelsWide > 0 && myPixelsHigh > 0; }

void Window::SetTouchHandler(std::function<void(const Touch&)> handler) { myTouch = std::move(handler); }
void Window::SetKeyHandler(std::function<void(const Key&)> handler) { myKey = std::move(handler); }
void Window::SetChangeHandler(std::function<void()> handler) { myChanged = std::move(handler); }

double Window::Time() {
    // The same clock as render::Viewport's animations (see Window_android.cpp).
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

float Window::CurrentFramebufferScale() { return gDensity; }

void Window::WaitEvents(double timeoutSeconds) {
    // One event (a touch, a notice, a wake-up) or the timeout, then back to the program.
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, std::max(0.0, timeoutSeconds), true);
    PollEvents();
}

void Window::PollEvents() {
    // Everything waiting now; a bound, so a flood of events never stalls a frame.
    for (int i = 0; i < 64 && CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true) == kCFRunLoopRunHandledSource; ++i) {
    }
}

void Window::PostEmptyEvent() {
    // Any thread: a no-op block on the main run loop ends its wait.
    CFRunLoopRef main = CFRunLoopGetMain();
    CFRunLoopPerformBlock(main, kCFRunLoopDefaultMode, ^{
    });
    CFRunLoopWakeUp(main);
}

void SaveFramebufferPpm(const Window& window, const std::filesystem::path& file) {
    int w = 0, h = 0;
    window.FramebufferSize(w, h);
    std::vector<unsigned char> pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream out(file, std::ios::binary);
    if (!out) return;
    out << "P6\n" << w << ' ' << h << "\n255\n";
    for (int row = h - 1; row >= 0; --row)
        for (int x = 0; x < w; ++x) out.write(reinterpret_cast<const char*>(pixels.data()) + (static_cast<std::ptrdiff_t>(row) * w + x) * 4, 3);
}

} // namespace bz::platform
