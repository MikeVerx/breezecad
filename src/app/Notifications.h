#pragma once

#include "ui/Ui.h"

#include <string>
#include <vector>

namespace bz::app {

/// Short messages in the corner of the viewport ("Сохранено", errors).
class Notifications {
public:
    enum class Kind : unsigned char { Info, Success, Error };

    void Info(std::string text) { Push(std::move(text), Kind::Info, 3.5); }
    void Success(std::string text) { Push(std::move(text), Kind::Success, 2.5); }
    void Error(std::string text) { Push(std::move(text), Kind::Error, 8.0); }

    /// Draws active messages stacked above `bottomRight`. Returns true while
    /// one of them fades in or out (only then the frame loop keeps drawing).
    bool Draw(ui::Ui& ui, ImVec2 bottomRight, double now);

    /// True while a message fades in or out, is new, or is due to go away:
    /// a frame is needed now. A message simply on screen needs none.
    bool Animating(double now) const;
    /// Seconds until a message starts to fade out (the loop sleeps no
    /// longer than that); a large number when there are none.
    double SecondsToNextChange(double now) const;

private:
    struct Note {
        std::string text;
        Kind        kind;
        double      duration;
        double      shownAt = -1.0;   ///< set on first draw
    };
    void Push(std::string text, Kind kind, double seconds);

    std::vector<Note> myNotes;
};

} // namespace bz::app
