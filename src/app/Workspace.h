#pragma once

#include "app/Session.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace bz::app {

class Notifications;
struct Settings;

enum class ExportFormat : unsigned char { Step, Stl, ThreeMf };

/// The open documents and the file operations on them.
class Workspace {
public:
    std::vector<std::unique_ptr<Session>> sessions;

    Session* Active();
    int  ActiveIndex() const { return myActive; }
    void Activate(int index);

    /// Home screen instead of a document (also shown when nothing is open).
    bool ShowsHome() const { return myShowHome || sessions.empty(); }
    void ShowHome(bool show) { myShowHome = show; }

    /// New empty part, made active.
    Session& NewPart();

    /// Opens a .bzc file (or activates it when already open).
    bool Open(const std::filesystem::path& file, Notifications& notes, Settings& settings);

    /// Saves to the session's file; asks for a name when it has none or
    /// when `saveAs` is set. Returns false if cancelled or failed.
    bool Save(Session& session, bool saveAs, Notifications& notes, Settings& settings);

    /// Asks for a file name and exports the bodies.
    bool Export(Session& session, ExportFormat format, Notifications& notes);

    /// Closes without asking (the caller has dealt with unsaved changes).
    void Close(int index);

    /// Next "ДетальN" name not used by open documents.
    std::string NextUntitledName() const;

private:
    int  myActive   = -1;
    bool myShowHome = true;
};

} // namespace bz::app
