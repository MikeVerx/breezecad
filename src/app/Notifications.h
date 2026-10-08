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
    /// any message is visible (the frame loop keeps redrawing to fade them).
    bool Draw(ui::Ui& ui, ImVec2 bottomRight, double now);

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
