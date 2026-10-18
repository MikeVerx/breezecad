#include "app/Autosave.h"

#include "app/Settings.h"
#include "core/I18n.h"
#include "core/Folders.h"
#include "core/Log.h"
#include "core/PathText.h"
#include "document/BzcFile.h"
#include "document/JsonIO.h"

#include <nlohmann/json.hpp>

#include <ctime>
#include <format>
#include <fstream>

namespace bz::app {
namespace {

constexpr double kIdleBeforeWrite = 2.5;   // seconds without input
constexpr int    kCopies          = 3;

std::string NowText() {
    const std::time_t t = std::time(nullptr);
    const std::tm* local = std::localtime(&t);   // only called from the UI thread
    return local ? std::format("{:02}.{:02} {:02}:{:02}", local->tm_mday, local->tm_mon + 1, local->tm_hour, local->tm_min)
                 : std::string{};
}

} // namespace

bool Autosave::Update(Session& session, const Settings& settings, double now, double idleSeconds) {
    if (!session.IsModified()) {
        session.unsavedSince = -1.0;
        return false;
    }
    if (session.unsavedSince < 0) session.unsavedSince = now;
    if (settings.autosaveMinutes <= 0) return false;
    if (session.autosavedRevision == session.ContentRevision()) return false;
    if (now - session.unsavedSince < settings.autosaveMinutes * 60.0) return false;
    if (idleSeconds < kIdleBeforeWrite) return false;
    return Write(session, now);
}

bool Autosave::WriteNow(Session& session, double now) {
    if (!session.IsModified() || session.autosavedRevision == session.ContentRevision()) return false;
    return Write(session, now);
}

bool Autosave::Write(Session& session, double now) {
    const std::filesystem::path folder = myRoot / session.autosaveKey;
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);

    // Rotate slots 1..3 by reading the last slot from info.json.
    int slot = 1;
    if (std::ifstream in(folder / "info.json"); in) {
        const nlohmann::json info = nlohmann::json::parse(in, nullptr, false);
        int last = 0;   // a hand-edited "slot": "2" must not throw (B-11)
        if (info.is_object() && doc::json_io::ReadMember(info, "slot", last))
            slot = (last % kCopies + kCopies) % kCopies + 1;
    }
    const std::filesystem::path copy = folder / std::format("copy{}.bzc", slot);
    const Status status = session.WriteFile(copy, "BreezeCAD autosave");
    if (!status) {
        log::Warn("Autosave failed: {}", status.GetError().message);
        session.unsavedSince = now;   // try again after another interval
        return false;
    }
    const nlohmann::json info{{"title", session.Title()}, {"original", ToUtf8(session.path)},
                              {"slot", slot}, {"savedAt", NowText()}};
    std::ofstream(folder / "info.json", std::ios::trunc) << info.dump(1);

    session.autosavedRevision = session.ContentRevision();
    session.unsavedSince = now;   // next copy after another interval of changes
    log::Info("Autosaved {} to slot {}", session.Title(), slot);
    return true;
}

void Autosave::Discard(const Session& session) const {
    std::error_code ec;
    std::filesystem::remove_all(myRoot / session.autosaveKey, ec);
}

std::vector<RecoverableDocument> Autosave::FindRecoverable() const {
    std::vector<RecoverableDocument> found;
    std::error_code ec;
    for (const auto& entry : FolderEntries(myRoot)) {   // never throws (B-20)
        if (!IsFolder(entry)) continue;
        std::ifstream in(entry.path() / "info.json");
        if (!in) continue;
        const nlohmann::json info = nlohmann::json::parse(in, nullptr, false);
        if (!info.is_object()) continue;
        // Fields of another type are skipped, not thrown (B-11): this runs before the window.
        int slot = 1;
        std::string original, title = Tr("Без имени"), savedAt;
        using doc::json_io::ReadMember;
        if (!ReadMember(info, "slot", slot) || !ReadMember(info, "original", original) ||
            !ReadMember(info, "title", title) || !ReadMember(info, "savedAt", savedAt))
            continue;
        RecoverableDocument doc;
        doc.folder = entry.path();
        doc.copy = entry.path() / std::format("copy{}.bzc", slot);
        doc.original = PathFromUtf8(original);
        doc.title = title;
        doc.savedAt = savedAt;
        if (std::filesystem::exists(doc.copy, ec)) found.push_back(std::move(doc));
    }
    return found;
}

void Autosave::Remove(const RecoverableDocument& doc) const {
    std::error_code ec;
    std::filesystem::remove_all(doc.folder, ec);
}

} // namespace bz::app
