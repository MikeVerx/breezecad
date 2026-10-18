// platform::Window on Android: the activity's window with OpenGL ES 3 (EGL).
// Coordinates are dp (density-independent pixels); the drawable is in
// pixels, as on a Retina Mac.
//
// The EGL context lives as long as the Window object: the 3D view's
// resources (OCCT) are in it. When the app goes to the background the
// system takes the window surface away; only that surface is remade when
// it comes back. OCCT is given a 1x1 pbuffer as its own surface (its view
// draws into a framebuffer of its own), so its handle never goes stale.
#include "platform/Window.h"

#include "core/Log.h"
#include "platform/Android.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/configuration.h>
#include <android/looper.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include <chrono>
#include <fstream>
#include <vector>

namespace bz::platform {

namespace {
float gDensity = 1.0f;   ///< for CurrentFramebufferScale: one window per app

EGLDisplay Display(void* p) { return static_cast<EGLDisplay>(p); }
EGLSurface Surface(void* p) { return static_cast<EGLSurface>(p); }
EGLContext Context(void* p) { return static_cast<EGLContext>(p); }

float DensityOf(android_app* app) {
    const int dpi = AConfiguration_getDensity(app->config);
    return dpi > 0 && dpi != ACONFIGURATION_DENSITY_ANY && dpi != ACONFIGURATION_DENSITY_NONE ? static_cast<float>(dpi) / 160.0f : 1.0f;
}
} // namespace

struct AndroidWindowAccess {
    static void Command(android_app* app, int32_t cmd) {
        if (auto* w = static_cast<Window*>(app->userData)) w->OnCommand(cmd);
    }
    static int32_t Input(android_app* app, AInputEvent* event) {
        auto* w = static_cast<Window*>(app->userData);
        return w && w->myInput ? w->myInput(event) : 0;
    }
};

bool InitGlfw() { return true; }

Window::~Window() {
    android_app* app = android::App();
    EGLDisplay display = Display(myDisplay);
    if (display != EGL_NO_DISPLAY && display) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (mySurface) eglDestroySurface(display, Surface(mySurface));
        if (myPbuffer) eglDestroySurface(display, Surface(myPbuffer));
        if (myContext) eglDestroyContext(display, Context(myContext));
        eglTerminate(display);
    }
    if (app && app->userData == this) {
        app->userData = nullptr;
        app->onAppCmd = nullptr;
        app->onInputEvent = nullptr;
    }
}

bool Window::Create(const WindowConfig&) {
    android_app* app = android::App();
    if (!app) return false;
    app->userData = this;
    app->onAppCmd = AndroidWindowAccess::Command;
    app->onInputEvent = AndroidWindowAccess::Input;
    myDensity = gDensity = DensityOf(app);

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr)) {
        log::Error("Android: no EGL display (0x{:x})", eglGetError());
        return false;
    }
    myDisplay = display;
    // The window itself needs neither depth nor multisampling: the 3D view
    // is drawn by OCCT into its own framebuffer, the interface flat on top.
    const EGLint attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
                              EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0, EGL_DEPTH_SIZE, 0,
                              EGL_SAMPLES, 0, EGL_NONE};
    EGLConfig config = nullptr;
    EGLint found = 0;
    if (!eglChooseConfig(display, attribs, &config, 1, &found) || found == 0) {
        log::Error("Android: no OpenGL ES 3 configuration (EGL 0x{:x})", eglGetError());
        return false;
    }
    myConfig = config;
    const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    myContext = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    const EGLint pbufferAttribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    myPbuffer = eglCreatePbufferSurface(display, config, pbufferAttribs);
    if (Context(myContext) == EGL_NO_CONTEXT || Surface(myPbuffer) == EGL_NO_SURFACE) {
        log::Error("Android: cannot start OpenGL ES (EGL 0x{:x})", eglGetError());
        return false;
    }

    // The window arrives with the first commands: wait for it (a few frames
    // of the system at most). Leaving before it means the app is closing.
    while (!mySurface && !app->destroyRequested) {
        android_poll_source* source = nullptr;
        if (ALooper_pollOnce(-1, nullptr, nullptr, reinterpret_cast<void**>(&source)) == ALOOPER_POLL_ERROR) break;
        if (source) source->process(app, source);
    }
    if (!mySurface) return false;
    if (const auto* v = reinterpret_cast<const char*>(glGetString(GL_VERSION))) myGLVersion = v;
    if (const auto* v = reinterpret_cast<const char*>(glGetString(GL_VENDOR))) myGLVendor = v;
    if (const auto* v = reinterpret_cast<const char*>(glGetString(GL_RENDERER))) myGLRenderer = v;
    log::Info("Window: {} ({} / {}), density {:.2f}", myGLVersion, myGLVendor, myGLRenderer, myDensity);
    return true;
}

bool Window::CreateSurface() {
    android_app* app = android::App();
    if (!app || !app->window || !myDisplay) return false;
    EGLDisplay display = Display(myDisplay);
    EGLSurface surface = eglCreateWindowSurface(display, static_cast<EGLConfig>(myConfig), app->window, nullptr);
    if (surface == EGL_NO_SURFACE) {
        log::Error("Android: no window surface (EGL 0x{:x})", eglGetError());
        return false;
    }
    mySurface = surface;
    if (!eglMakeCurrent(display, surface, surface, Context(myContext))) {
        log::Error("Android: the context does not take the window (EGL 0x{:x})", eglGetError());
        return false;
    }
    eglSwapInterval(display, 1);
    return true;
}

void Window::DestroySurface() {
    if (!mySurface) return;
    EGLDisplay display = Display(myDisplay);
    // The context stays, on the pbuffer: OCCT's textures and buffers live on.
    eglMakeCurrent(display, Surface(myPbuffer), Surface(myPbuffer), Context(myContext));
    eglDestroySurface(display, Surface(mySurface));
    mySurface = nullptr;
}

void Window::OnCommand(int cmd) {
    android_app* app = android::App();
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            CreateSurface();
            if (myChanged) myChanged();
            break;
        case APP_CMD_TERM_WINDOW:
            DestroySurface();
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
        case APP_CMD_CONTENT_RECT_CHANGED:
        case APP_CMD_WINDOW_REDRAW_NEEDED:
        case APP_CMD_GAINED_FOCUS:
            if (app) myDensity = gDensity = DensityOf(app);
            if (myChanged) myChanged();
            break;
        case APP_CMD_RESUME: android::SetWatchdogState("fg"); break;
        case APP_CMD_PAUSE:
            android::SetWatchdogState("bg");
            if (myChanged) myChanged();   // the app saves its autosave now: it may be killed in the background
            break;
        case APP_CMD_DESTROY: myCloseRequested = true; break;
        default: break;
    }
}

NativeGlSurface Window::NativeSurface() const {
    NativeGlSurface surface;
    // OCCT takes the surface current at Viewport::Init as its own: the
    // pbuffer, which outlives the window surface (see the top of this file).
    eglMakeCurrent(Display(myDisplay), Surface(myPbuffer), Surface(myPbuffer), Context(myContext));
    android_app* app = android::App();
    surface.window = reinterpret_cast<std::uintptr_t>(app ? app->window : nullptr);
    surface.glContext = myContext;
    surface.display = myDisplay;
    surface.eglConfig = myConfig;
    return surface;
}

bool Window::ShouldClose() const {
    android_app* app = android::App();
    return myCloseRequested || (app && app->destroyRequested);
}
void Window::SetShouldClose(bool close) { myCloseRequested = close; }
void Window::SetTitle(const std::string&) {}
void Window::SetIcon(const unsigned char*, int) {}

void Window::SwapBuffers() {
    if (mySurface) eglSwapBuffers(Display(myDisplay), Surface(mySurface));
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
bool Window::IsMinimized() const { return !mySurface; }

bool Window::ReclaimDrawable() {
    // OCCT made its pbuffer current for the 3D render: the window back.
    if (!mySurface || eglGetCurrentSurface(EGL_DRAW) == Surface(mySurface)) return false;
    eglMakeCurrent(Display(myDisplay), Surface(mySurface), Surface(mySurface), Context(myContext));
    return true;
}

void Window::FramebufferSize(int& width, int& height) const {
    EGLint w = 0, h = 0;
    if (mySurface) {
        eglQuerySurface(Display(myDisplay), Surface(mySurface), EGL_WIDTH, &w);
        eglQuerySurface(Display(myDisplay), Surface(mySurface), EGL_HEIGHT, &h);
    } else if (android_app* app = android::App(); app && app->window) {
        w = ANativeWindow_getWidth(app->window);
        h = ANativeWindow_getHeight(app->window);
    }
    width = w;
    height = h;
}

void Window::WindowSize(int& width, int& height) const {
    int w = 0, h = 0;
    FramebufferSize(w, h);
    width = static_cast<int>(static_cast<float>(w) / myDensity + 0.5f);
    height = static_cast<int>(static_cast<float>(h) / myDensity + 0.5f);
}

bool Window::CanDraw() const { return mySurface != nullptr; }

void Window::SetInputHandler(std::function<int(const void*)> handler) { myInput = std::move(handler); }
void Window::SetChangeHandler(std::function<void()> handler) { myChanged = std::move(handler); }

double Window::Time() {
    // The same clock as render::Viewport's animations (steady_clock from its
    // epoch): with "seconds since start" here the camera animation began
    // "in the future", never ended and put the view back every frame — the
    // view froze after "Домой", the cube or a sketch start.
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

float Window::CurrentFramebufferScale() { return gDensity; }

namespace {
void Pump(int timeoutMs) {
    android_app* app = android::App();
    if (!app) return;
    android_poll_source* source = nullptr;
    int result = ALooper_pollOnce(timeoutMs, nullptr, nullptr, reinterpret_cast<void**>(&source));
    // Everything that is waiting, then back to the program.
    while (result != ALOOPER_POLL_TIMEOUT && result != ALOOPER_POLL_ERROR) {
        if (source) source->process(app, source);
        if (app->destroyRequested) return;
        source = nullptr;
        result = ALooper_pollOnce(0, nullptr, nullptr, reinterpret_cast<void**>(&source));
    }
}
} // namespace

void Window::WaitEvents(double timeoutSeconds) { Pump(static_cast<int>(timeoutSeconds * 1000.0)); }
void Window::PollEvents() { Pump(0); }
void Window::PostEmptyEvent() {
    if (android_app* app = android::App()) ALooper_wake(app->looper);
}

void SaveFramebufferPpm(const Window& window, const std::filesystem::path& file) {
    int w = 0, h = 0;
    window.FramebufferSize(w, h);
    // GLES reads RGBA only.
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
