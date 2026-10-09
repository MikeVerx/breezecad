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
};

class Autosave {
public:
    explicit Autosave(std::filesystem::path root) : myRoot(std::move(root)) {}

    /// Call every frame. `idleSeconds` is the time since the last user input.
    /// Returns true when a copy was written.
    bool Update(Session& session, const Settings& settings, double now, double idleSeconds);

    /// Removes the session's copies (after saving or a deliberate close).
    void Discard(const Session& session) const;

    std::vector<RecoverableDocument> FindRecoverable() const;
    void Remove(const RecoverableDocument& doc) const;

private:
    std::filesystem::path myRoot;
};

} // namespace bz::app
