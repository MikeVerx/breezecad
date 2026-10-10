#pragma once
// The splash window shown while the program starts: a picture of a part,
// the name, the version and what is loading now, with a progress bar.
// A small borderless window of its own (own OpenGL and ImGui contexts), so
// the main window can be set up hidden and appear only when it is ready.

#include "core/Color.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

struct GLFWwindow;
struct ImGuiContext;
struct ImFont;

namespace bz::app {

class Splash {
public:
    Splash() = default;
    ~Splash();
    Splash(const Splash&) = delete;
    Splash& operator=(const Splash&) = delete;

    /// Opens the window centred on the primary monitor and draws it. False
    /// (and no splash) when the window cannot be made; starting goes on.
    bool Open(const std::filesystem::path& assets, bool dark);
    /// What is being done now and how far starting is (0..1); drawn at once.
    void Step(const std::string& status, float progress);
    /// Closes the splash, after it has been up long enough not to flash.
    void Close();

private:
    void Draw();

    GLFWwindow*   myWindow = nullptr;
    ImGuiContext* myContext = nullptr;
    float         myScale = 1.0f;
    bool          myDark = false;
    bool          myRounded = false;   ///< a transparent framebuffer: rounded corners
    unsigned      myPart = 0, myMark = 0;   ///< GL textures
    int           myPartW = 0, myPartH = 0;
    ImFont*       myRegular = nullptr;
    ImFont*       myMedium = nullptr;
    ImFont*       myStrong = nullptr;
    ImFont*       myMono = nullptr;
    std::string   myStatus;
    float         myProgress = 0;
    std::chrono::steady_clock::time_point myOpenedAt;
};

} // namespace bz::app
