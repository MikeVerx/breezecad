#include "app/Workspace.h"

#include "app/Notifications.h"
#include "app/Settings.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "core/PathText.h"
#include "document/BzcFile.h"
#include "platform/FileDialogs.h"
#include "platform/Paths.h"

#include <algorithm>

namespace bz::app {
namespace {

constexpr const char* kGenerator = "BreezeCAD 0.2.0";

} // namespace

Session* Workspace::Active() {
    if (myActive < 0 || myActive >= static_cast<int>(sessions.size())) return nullptr;
    return sessions[static_cast<std::size_t>(myActive)].get();
}

void Workspace::Activate(int index) {
    if (index < 0 || index >= static_cast<int>(sessions.size())) return;
    myActive = index;
    myShowHome = false;
}

std::string Workspace::NextUntitledName() const {
    for (int n = 1;; ++n) {
        const std::string name = Trf("Деталь{}", n);
        const bool used = std::any_of(sessions.begin(), sessions.end(), [&](const auto& s) { return s->Title() == name; });
        if (!used) return name;
    }
}

Session& Workspace::NewPart() {
    auto session = std::make_unique<Session>();
    session->untitledName = NextUntitledName();
    sessions.push_back(std::move(session));
    Activate(static_cast<int>(sessions.size()) - 1);
    return *sessions.back();
}

bool Workspace::Open(const std::filesystem::path& file, Notifications& notes, Settings& settings) {
    for (std::size_t i = 0; i < sessions.size(); ++i) {
        if (!sessions[i]->path.empty() && std::filesystem::equivalent(sessions[i]->path, file)) {
            Activate(static_cast<int>(i));
            return true;
        }
    }
    doc::LoadReport report;
    Expected<doc::Document> loaded = doc::LoadBzc(file, &report);
    if (!loaded) {
        notes.Error(Trf("Не удалось открыть «{}»: {}", ToUtf8(file.filename()), Tr(loaded.GetError().message)));
        settings.RemoveRecent(file);
        return false;
    }
    auto session = std::make_unique<Session>();
    session->doc = std::move(*loaded);
    session->path = file;
    sessions.push_back(std::move(session));
    Activate(static_cast<int>(sessions.size()) - 1);
    settings.AddRecent(file);
    for (const auto& w : report.warnings) notes.Info(Tr(w));
    log::Info("Opened {}", ToUtf8(file));
    return true;
}

bool Workspace::Save(Session& session, bool saveAs, Notifications& notes, Settings& settings) {
    std::filesystem::path target = session.path;
    if (target.empty() || saveAs) {
        if (const std::string problem = platform::FileDialogProblem(); !problem.empty()) {
            notes.Error(problem);
            return false;
        }
        const std::filesystem::path folder = session.path.empty() ? platform::DocumentsDir() : session.path.parent_path();
        const auto chosen = platform::ShowSaveDialog(Tr("Сохранить документ"), folder / (session.Title() + ".bzc"),
                                                     {{Tr("Документ BreezeCAD"), "*.bzc"}}, ".bzc");
        if (!chosen) return false;
        target = *chosen;
    }
    const Status status = doc::SaveBzc(session.doc, target, kGenerator);
    if (!status) {
        notes.Error(Trf("Не удалось сохранить: {}", Tr(status.GetError().message)));
        return false;
    }
    session.path = target;
    session.history.MarkSaved();
    session.unsavedSince = -1.0;
    settings.AddRecent(target);
    notes.Success(Trf("Сохранено: {}", ToUtf8(target.filename())));
    log::Info("Saved {}", ToUtf8(target));
    return true;
}

bool Workspace::Export(Session& session, ExportFormat format, Notifications& notes) {
    struct Info {
        const char* name;
        const char* pattern;
        const char* ext;
    };
    const Info info = format == ExportFormat::Step ? Info{"STEP AP214", "*.step", ".step"}
                    : format == ExportFormat::Stl  ? Info{"STL", "*.stl", ".stl"}
                                                   : Info{Tr("3MF для 3D-печати"), "*.3mf", ".3mf"};
    if (session.model.Bodies().empty()) {
        notes.Info(Tr("В детали ещё нет тел для экспорта"));
        return false;
    }
    if (const std::string problem = platform::FileDialogProblem(); !problem.empty()) {
        notes.Error(problem);
        return false;
    }
    const std::filesystem::path folder = session.path.empty() ? platform::DocumentsDir() : session.path.parent_path();
    const auto chosen = platform::ShowSaveDialog(Trf("Экспорт в {}", info.name),
                                                 folder / (session.Title() + info.ext), {{info.name, info.pattern}}, info.ext);
    if (!chosen) return false;
    const Status status = format == ExportFormat::Step ? session.model.ExportStep(*chosen)
                        : format == ExportFormat::Stl  ? session.model.ExportStl(*chosen)
                                                       : session.model.Export3mf(*chosen);
    if (!status) {
        notes.Error(Tr(status.GetError().message));
        return false;
    }
    notes.Success(Trf("Экспортировано: {}", ToUtf8(chosen->filename())));
    return true;
}

void Workspace::Close(int index) {
    if (index < 0 || index >= static_cast<int>(sessions.size())) return;
    sessions.erase(sessions.begin() + index);
    if (sessions.empty()) {
        myActive = -1;
        myShowHome = true;
    } else {
        myActive = std::clamp(myActive > index ? myActive - 1 : myActive, 0, static_cast<int>(sessions.size()) - 1);
    }
}

} // namespace bz::app
