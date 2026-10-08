#include "app/Autosave.h"

#include "app/Settings.h"
#include "core/Log.h"
#include "core/PathText.h"
#include "document/BzcFile.h"

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
    if (session.autosavedRevision == session.doc.Revision()) return false;
    if (now - session.unsavedSince < settings.autosaveMinutes * 60.0) return false;
    if (idleSeconds < kIdleBeforeWrite) return false;

    const std::filesystem::path folder = myRoot / session.autosaveKey;
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);

    // Rotate slots 1..3 by reading the last slot from info.json.
    int slot = 1;
    if (std::ifstream in(folder / "info.json"); in) {
        const nlohmann::json info = nlohmann::json::parse(in, nullptr, false);
        if (!info.is_discarded()) slot = info.value("slot", 0) % kCopies + 1;
    }
    const std::filesystem::path copy = folder / std::format("copy{}.bzc", slot);
    const Status status = doc::SaveBzc(session.doc, copy, "BreezeCAD autosave");
    if (!status) {
        log::Warn("Autosave failed: {}", status.GetError().message);
        session.unsavedSince = now;   // try again after another interval
        return false;
    }
    const nlohmann::json info{{"title", session.Title()}, {"original", ToUtf8(session.path)},
                              {"slot", slot}, {"savedAt", NowText()}};
    std::ofstream(folder / "info.json", std::ios::trunc) << info.dump(1);

    session.autosavedRevision = session.doc.Revision();
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
    for (const auto& entry : std::filesystem::directory_iterator(myRoot, ec)) {
        if (!entry.is_directory()) continue;
        std::ifstream in(entry.path() / "info.json");
        if (!in) continue;
        const nlohmann::json info = nlohmann::json::parse(in, nullptr, false);
        if (info.is_discarded()) continue;
        RecoverableDocument doc;
        doc.folder = entry.path();
        doc.copy = entry.path() / std::format("copy{}.bzc", info.value("slot", 1));
        doc.original = PathFromUtf8(info.value("original", ""));
        doc.title = info.value("title", "Без имени");
        doc.savedAt = info.value("savedAt", "");
        if (std::filesystem::exists(doc.copy, ec)) found.push_back(std::move(doc));
    }
    return found;
}

void Autosave::Remove(const RecoverableDocument& doc) const {
    std::error_code ec;
    std::filesystem::remove_all(doc.folder, ec);
}

} // namespace bz::app
