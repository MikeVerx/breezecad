#pragma once

#include "app/Session.h"

#include "core/Expected.h"

#include <filesystem>
#include <string>
#include <string_view>
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

    /// The document in front: a part edited in place in the assembly in
    /// front is that part (every part tool then works on it as is).
    Session* Active();
    const Session* Active() const { return const_cast<Workspace*>(this)->Active(); }
    /// The tab in front (the assembly, also while one of its parts is edited in place).
    Session* ActiveTab();
    /// `s` is open: a tab, or a part edited in one of the assemblies.
    bool IsOpen(const Session* s) const;
    int  ActiveIndex() const { return myActive; }
    void Activate(int index);

    /// Home screen instead of a document (also shown when nothing is open).
    bool ShowsHome() const { return myShowHome || sessions.empty(); }
    void ShowHome(bool show) { myShowHome = show; }

    /// New empty part, made active.
    Session& NewPart();
    /// New empty assembly, made active.
    Session& NewAssembly();

    /// A session for a .bzc file of any kind this version opens (not added).
    static Expected<std::unique_ptr<Session>> LoadSession(const std::filesystem::path& file,
                                                          std::vector<std::string>* warnings);

    /// Opens a .bzc file (or activates it when already open).
    bool Open(const std::filesystem::path& file, Notifications& notes, Settings& settings);

    /// Saves to the session's file; asks for a name when it has none or
    /// when `saveAs` is set. Returns false if cancelled or failed.
    bool Save(Session& session, bool saveAs, Notifications& notes, Settings& settings);
    /// Saves to `target` without asking (the session's file from now on).
    bool SaveTo(Session& session, const std::filesystem::path& target, Notifications& notes, Settings& settings,
                std::string* error = nullptr);

    /// Asks for a file name and exports the bodies.
    bool Export(Session& session, ExportFormat format, Notifications& notes);
    /// Exports the bodies to `target` without asking. Errors go to `notes`
    /// and, when `error` is given, there as well.
    bool ExportTo(Session& session, ExportFormat format, const std::filesystem::path& target, Notifications& notes,
                  std::string* error = nullptr);

    /// Copies the parts of `session` (an assembly) that do not lie with it
    /// into its folder (assembly::GatherParts), the copies and problems
    /// reported in `notes`. False when a copy could not be made.
    static bool GatherParts(Session& session, const std::filesystem::path& from, const std::filesystem::path& to,
                            Notifications& notes);

    /// Closes without asking (the caller has dealt with unsaved changes).
    void Close(int index);

    /// Next "ДетальN" (or "<base>N") name not used by open documents.
    std::string NextUntitledName(std::string_view base = "Деталь") const;

private:
    int  myActive   = -1;
    bool myShowHome = true;
};

} // namespace bz::app
