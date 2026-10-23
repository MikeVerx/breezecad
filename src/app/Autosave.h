#pragma once
// Smart autosave (agreed with the author): the timer runs only while a
// document has unsaved changes; the copy is written during a pause in the
// user's work (no input for 2.5 s) so it never interrupts; three copies are
// kept per document. Copies are removed when the document is saved or
// closed on purpose, so leftovers at start-up mean lost work to recover.

#include "app/Session.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bz::app {

struct Settings;

struct RecoverableDocument {
    std::filesystem::path folder;     ///< autosave folder of the document
    std::filesystem::path copy;       ///< newest copy to open
    std::filesystem::path original;   ///< the file it belongs to (empty if never saved)
    std::string           title;
    std::string           savedAt;    ///< "08.10 21:40"
    /// An assembly: its parts edited in it (in place, made in it, followed
    /// adaptively) as they stood — kept in memory there, not in their files.
    struct Part {
        std::filesystem::path file;      ///< where it is written on save
        std::filesystem::path copy;      ///< its copy in the autosave folder
        bool                  created = false;
    };
    std::vector<Part> parts;
};

class Autosave {
public:
    explicit Autosave(std::filesystem::path root) : myRoot(std::move(root)) {}

    /// Call every frame. `idleSeconds` is the time since the last user input.
    /// Returns true when a copy was written.
    bool Update(Session& session, const Settings& settings, double now, double idleSeconds);

    /// Writes a copy at once if the document has changes not in one yet
    /// (Android: the app went to the background, where it may be killed).
    bool WriteNow(Session& session, double now);

    /// Removes the session's copies (after saving or a deliberate close).
    void Discard(const Session& session) const;

    std::vector<RecoverableDocument> FindRecoverable() const;
    void Remove(const RecoverableDocument& doc) const;

private:
    bool Write(Session& session, double now);
    std::filesystem::path myRoot;
};

} // namespace bz::app
