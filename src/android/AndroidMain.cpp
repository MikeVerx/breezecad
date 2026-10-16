// BreezeCAD on Android — step 2 of the port (docs/ADR/0004-mobile-touch-ui.md):
// a window with OpenGL ES 3, the desktop's render::Viewport and a part built
// by the kernel, turned with the fingers. No interface yet (ImGui comes with
// the mobile layout, step 4); the part is the bundled example.
//
// A NativeActivity: the system loads libbreezecad.so and calls android_main
// on a thread of its own (android_native_app_glue). Everything — window,
// input, drawing — happens on that thread; the part is built on another one.
//
// Gestures (ADR-0004, cursor off): 1 finger — rotate, 2 fingers — move and
// pinch to zoom, double tap — show all.
#include "core/Log.h"
#include "core/Math.h"
#include "core/NativeSurface.h"
#include "document/BzcFile.h"
#include "document/Document.h"
#include "kernel/Model.h"
#include "render/Viewport.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/looper.h>
#include <android_native_app_glue.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace bz::android {
namespace {

constexpr double kOrbitPerDp = 0.0075;            // as kOrbitPerPixel on the desktop
constexpr Vec3   kHomeDirection{1.0, -1.0, 1.0};   // as ui::kHomeDirection
constexpr double kTapSeconds = 0.25, kDoubleTapSeconds = 0.35, kTapSlopDp = 12.0;
constexpr const char* kExampleAsset = "examples/bracket.bzc";

/// The clock of Viewport::StepAnimation on Android (render/Viewport.cpp).
double Now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/// A document and the solids built from it, made on the loading thread and
/// then handed to the drawing thread whole.
struct Part {
    explicit Part(doc::Document d) : document(std::move(d)) {}
    doc::Document document;
    kernel::Model model;
};

class App {
public:
    explicit App(android_app* state) : myState(state) {}

    ~App() {
        if (myLoader.joinable()) myLoader.join();
        DestroySurface();
    }

    void Start() {
        const fs::path data = myState->activity->internalDataPath ? myState->activity->internalDataPath : "/data/local/tmp";
        log::OpenFile(data / "logs" / "output.log");
        log::Info("BreezeCAD for Android: start, data in {}", data.string());
        myDensity = std::max(1.0, AConfiguration_getDensity(myState->config) / 160.0);
        StartLoading(data);
    }

    void OnCommand(int32_t cmd) {
        switch (cmd) {
            case APP_CMD_INIT_WINDOW:   CreateSurface(); break;
            case APP_CMD_TERM_WINDOW:   DestroySurface(); break;
            case APP_CMD_WINDOW_RESIZED:
            case APP_CMD_CONFIG_CHANGED:
                myDensity = std::max(1.0, AConfiguration_getDensity(myState->config) / 160.0);
                myWantFrame = true;
                break;
            case APP_CMD_WINDOW_REDRAW_NEEDED:
            case APP_CMD_GAINED_FOCUS:  myWantFrame = true; break;
            default: break;
        }
    }

    int32_t OnInput(AInputEvent* event) {
        if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION || !myViewport) return 0;
        const int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        const std::size_t count = AMotionEvent_getPointerCount(event);
        auto at = [&](std::size_t i) {
            return Vec2{AMotionEvent_getX(event, i) / myDensity, AMotionEvent_getY(event, i) / myDensity};
        };
        // Fingers 1 and 2 (when there): centre and spread.
        const Vec2   a = at(0);
        const Vec2   b = count > 1 ? at(1) : a;
        const Vec2   centre = (a + b) * 0.5;
        const double spread = std::hypot(b.x - a.x, b.y - a.y);

        switch (action) {
            case AMOTION_EVENT_ACTION_DOWN:
                myDownAt = Now();
                myDownPos = a;
                myMoved = false;
                myPivot = myViewport->ModelCenter();
                break;
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
            case AMOTION_EVENT_ACTION_POINTER_UP:
                myMoved = true;   // a second finger is never a tap
                break;
            case AMOTION_EVENT_ACTION_MOVE: {
                if (count != myFingers) break;   // the reference was set by the last event
                if (count == 1) {
                    const Vec2 d = a - myLast;
                    if (std::hypot(a.x - myDownPos.x, a.y - myDownPos.y) > kTapSlopDp) myMoved = true;
                    if (myMoved && (d.x != 0 || d.y != 0)) myViewport->Orbit(d.x * kOrbitPerDp, d.y * kOrbitPerDp, myPivot);
                } else {
                    myViewport->Pan(centre.x - myLastCentre.x, centre.y - myLastCentre.y);
                    if (myLastSpread > 1.0 && spread > 1.0) myViewport->ZoomAt(spread / myLastSpread, centre);
                }
                myWantFrame = true;
                break;
            }
            case AMOTION_EVENT_ACTION_UP:
                if (!myMoved && Now() - myDownAt < kTapSeconds) OnTap(a);
                break;
            default: break;
        }
        // The fingers still down after this event: the next move counts from
        // them. A lifted finger is still listed in its POINTER_UP event.
        std::vector<Vec2> down;
        if (action != AMOTION_EVENT_ACTION_UP && action != AMOTION_EVENT_ACTION_CANCEL) {
            const std::size_t lifted =
                action == AMOTION_EVENT_ACTION_POINTER_UP
                    ? static_cast<std::size_t>((AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                                               AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT)
                    : count;
            for (std::size_t i = 0; i < count; ++i)
                if (i != lifted) down.push_back(at(i));
        }
        if (down.size() == 1 && myFingers > 1) myPivot = myViewport->ModelCenter();   // back to rotating
        myFingers = down.size();
        if (!down.empty()) {
            myLast = down[0];
            const Vec2 second = down.size() > 1 ? down[1] : down[0];
            myLastCentre = (down[0] + second) * 0.5;
            myLastSpread = std::hypot(second.x - down[0].x, second.y - down[0].y);
        }
        return 1;
    }

    /// Something to draw: an animation, a gesture, a part that just arrived.
    bool WantsFrame() const { return mySurface != EGL_NO_SURFACE && (myWantFrame || myPartReady || (myViewport && myViewport->NeedsRender())); }

    void DrawFrame() {
        if (mySurface == EGL_NO_SURFACE || !myViewport) return;
        eglMakeCurrent(myDisplay, mySurface, mySurface, myContext);
        AdoptLoadedPart();

        EGLint w = 0, h = 0;
        eglQuerySurface(myDisplay, mySurface, EGL_WIDTH, &w);
        eglQuerySurface(myDisplay, mySurface, EGL_HEIGHT, &h);
        if (w <= 0 || h <= 0) return;
        myViewport->SetSize(w / myDensity, h / myDensity, myDensity);
        myViewport->StepAnimation(Now());
        myViewport->Render();
        Present(w, h);
        eglSwapBuffers(myDisplay, mySurface);
        myWantFrame = false;
    }

    void Wake() { ALooper_wake(myState->looper); }

private:
    // --- Window and OpenGL ES ----------------------------------------------

    void CreateSurface() {
        if (!myState->window) return;
        myDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        eglInitialize(myDisplay, nullptr, nullptr);
        // The window itself needs neither depth nor multisampling: the 3D
        // view is drawn by OCCT into its own framebuffer and copied here.
        const EGLint attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8,
                                  EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0, EGL_DEPTH_SIZE, 0, EGL_SAMPLES, 0, EGL_NONE};
        EGLConfig config = nullptr;
        EGLint    found = 0;
        if (!eglChooseConfig(myDisplay, attribs, &config, 1, &found) || found == 0) {
            log::Error("Android: no OpenGL ES 3 configuration (EGL 0x{:x})", eglGetError());
            return;
        }
        mySurface = eglCreateWindowSurface(myDisplay, config, myState->window, nullptr);
        const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        myContext = eglCreateContext(myDisplay, config, EGL_NO_CONTEXT, contextAttribs);
        if (mySurface == EGL_NO_SURFACE || myContext == EGL_NO_CONTEXT ||
            !eglMakeCurrent(myDisplay, mySurface, mySurface, myContext)) {
            log::Error("Android: cannot start OpenGL ES (EGL 0x{:x})", eglGetError());
            DestroySurface();
            return;
        }
        log::Info("Android: OpenGL ES {} on {}, density {:.2f}", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                  reinterpret_cast<const char*>(glGetString(GL_RENDERER)), myDensity);

        NativeGlSurface surface;
        surface.window = reinterpret_cast<std::uintptr_t>(myState->window);
        surface.glContext = myContext;
        surface.display = myDisplay;
        surface.eglConfig = config;
        myViewport = std::make_unique<render::Viewport>();
        bool started = false;
        try {   // OCCT reports a window it cannot use by throwing
            started = myViewport->Init(surface);
        } catch (...) {
            log::Error("Android: OCCT refused the window");
        }
        if (!started) {
            log::Error("Android: the 3D view did not start");
            myViewport.reset();
            return;
        }
        // The window comes back after the app was in the background: the
        // part and the camera are as they were.
        if (myPart) {
            myViewport->Sync(myPart->model);
            myViewport->SetCamera(myCamera);
        }
        myWantFrame = true;
    }

    void DestroySurface() {
        if (myDisplay == EGL_NO_DISPLAY) return;
        if (myViewport) {
            if (mySurface != EGL_NO_SURFACE) eglMakeCurrent(myDisplay, mySurface, mySurface, myContext);
            myCamera = myViewport->Camera();
            myViewport.reset();   // frees OCCT's framebuffer while its context is current
        }
        if (myCopyFbo != 0) glDeleteFramebuffers(1, &myCopyFbo);
        myCopyFbo = 0;
        eglMakeCurrent(myDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (myContext != EGL_NO_CONTEXT) eglDestroyContext(myDisplay, myContext);
        if (mySurface != EGL_NO_SURFACE) eglDestroySurface(myDisplay, mySurface);
        eglTerminate(myDisplay);
        myDisplay = EGL_NO_DISPLAY;
        mySurface = EGL_NO_SURFACE;
        myContext = EGL_NO_CONTEXT;
    }

    /// Copies the view's picture (OCCT's colour texture) to the window. A
    /// framebuffer blit touches no shader or vertex state that OCCT keeps
    /// cached; the framebuffer bindings and scissor are put back.
    void Present(EGLint w, EGLint h) {
        const GLuint texture = myViewport->TextureId();
        if (texture == 0) return;
        GLint read = 0, draw = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
        const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);

        if (myCopyFbo == 0) glGenFramebuffers(1, &myCopyFbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, myCopyFbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);   // re-made on resize
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        const GLenum back = GL_BACK;
        glDrawBuffers(1, &back);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        if (scissor) glEnable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw));
        if (const GLenum error = glGetError(); error != GL_NO_ERROR && !myReportedGlError) {
            myReportedGlError = true;
            log::Warn("Android: OpenGL error 0x{:x} after copying the view to the window", error);
        }
    }

    // --- The part ------------------------------------------------------------

    /// Copies the example from the APK to the app's folder (the document code
    /// reads files, not assets) and builds it on a thread of its own.
    void StartLoading(const fs::path& data) {
        const fs::path file = data / "examples" / "bracket.bzc";
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        if (AAsset* asset = AAssetManager_open(myState->activity->assetManager, kExampleAsset, AASSET_MODE_BUFFER)) {
            const auto*  bytes = static_cast<const char*>(AAsset_getBuffer(asset));
            const auto   size = static_cast<std::streamsize>(AAsset_getLength(asset));
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            if (bytes) out.write(bytes, size);
            AAsset_close(asset);
        } else {
            log::Error("Android: no {} in the APK", kExampleAsset);
            return;
        }
        myLoader = std::thread([this, file] {
            const double start = Now();
            auto loaded = doc::LoadBzc(file);
            if (!loaded) {
                log::Error("Android: cannot open {}", file.string());
                return;
            }
            auto part = std::make_unique<Part>(std::move(*loaded));
            part->model.Rebuild(part->document);
            render::Viewport::PrepareMeshes(part->model, 1);
            log::Info("Android: {} built in {:.2f} s", file.filename().string(), Now() - start);
            {
                const std::lock_guard lock(myLoadedMutex);
                myLoaded = std::move(part);
            }
            myPartReady = true;
            Wake();
        });
    }

    void AdoptLoadedPart() {
        if (!myPartReady.exchange(false)) return;
        std::unique_ptr<Part> part;
        {
            const std::lock_guard lock(myLoadedMutex);
            part = std::move(myLoaded);
        }
        if (!part) return;
        myPart = std::move(part);
        myViewport->Sync(myPart->model);
        myViewport->SetCamera(myViewport->FitCamera(-kHomeDirection, {0, 0, 1}));
        myCamera = myViewport->Camera();
    }

    void OnTap(Vec2 at) {
        const double now = Now();
        const bool twice = now - myLastTapAt < kDoubleTapSeconds && std::hypot(at.x - myLastTap.x, at.y - myLastTap.y) < 3 * kTapSlopDp;
        myLastTapAt = twice ? 0.0 : now;
        myLastTap = at;
        if (!twice) return;
        const render::CameraState camera = myViewport->Camera();
        myViewport->AnimateTo(myViewport->FitCamera(camera.center - camera.eye, camera.up), 0.35);
        myWantFrame = true;
    }

    android_app* myState;
    double       myDensity = 1.0;

    EGLDisplay myDisplay = EGL_NO_DISPLAY;
    EGLSurface mySurface = EGL_NO_SURFACE;
    EGLContext myContext = EGL_NO_CONTEXT;
    GLuint     myCopyFbo = 0;
    bool       myReportedGlError = false;
    bool       myWantFrame = true;

    std::unique_ptr<render::Viewport> myViewport;
    render::CameraState               myCamera;

    std::thread           myLoader;
    std::mutex            myLoadedMutex;
    std::unique_ptr<Part> myLoaded;   ///< from the loader, guarded by myLoadedMutex
    std::atomic<bool>     myPartReady{false};
    std::unique_ptr<Part> myPart;     ///< the one shown

    // Touch: where the fingers were at the last event.
    std::size_t myFingers = 0;
    Vec2        myLast, myLastCentre, myDownPos, myLastTap;
    double      myLastSpread = 0.0, myDownAt = 0.0, myLastTapAt = 0.0;
    bool        myMoved = false;
    Vec3        myPivot;
};

void OnCommand(android_app* state, int32_t cmd) { static_cast<App*>(state->userData)->OnCommand(cmd); }
int32_t OnInput(android_app* state, AInputEvent* event) { return static_cast<App*>(state->userData)->OnInput(event); }

} // namespace
} // namespace bz::android

void android_main(android_app* state) {
    bz::android::App app(state);
    state->userData = &app;
    state->onAppCmd = bz::android::OnCommand;
    state->onInputEvent = bz::android::OnInput;
    app.Start();

    // Sleeps until something happens; draws only when there is something to
    // draw (an idle 3D view costs no battery).
    while (!state->destroyRequested) {
        android_poll_source* source = nullptr;
        const int result = ALooper_pollOnce(app.WantsFrame() ? 0 : -1, nullptr, nullptr, reinterpret_cast<void**>(&source));
        if (result == ALOOPER_POLL_ERROR) break;
        if (source != nullptr) source->process(state, source);
        if (app.WantsFrame()) app.DrawFrame();
    }
    bz::log::Info("BreezeCAD for Android: exit");
}
