#include "app/Workspace.h"

#include "app/Notifications.h"
#include "app/Settings.h"
#include "app/assembly/InPlace.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "core/Monitor.h"
#include "core/Version.h"
#include "core/PathText.h"
#include "assembly/AssemblyFile.h"
#include "document/BzcFile.h"
#include "document/DocumentTypes.h"
#include "document/ThreadFeature.h"
#include "platform/FileDialogs.h"
#include "platform/Paths.h"
#if defined(__ANDROID__)
#include "platform/Android.h"
#endif

#include <algorithm>
#include <chrono>
#include <format>
#include <optional>

namespace bz::app {
namespace {

const std::string kGenerator = std::string("BreezeCAD ") + Version();

/// Why there is nothing to export; empty when there is something.
std::string NothingToExport(const Session& session) {
    if (!session.model.Bodies().empty()) return {};
    return session.assembly ? "В сборке ещё нет компонентов для экспорта" : "В детали ещё нет тел для экспорта";
}

} // namespace

Session* Workspace::ActiveTab() {
    if (myActive < 0 || myActive >= static_cast<int>(sessions.size())) return nullptr;
    return sessions[static_cast<std::size_t>(myActive)].get();
}

Session* Workspace::Active() {
    Session* tab = ActiveTab();
    if (tab && tab->assembly)
        if (Session* part = tab->assembly->InPlaceSession()) return part;
    return tab;
}

bool Workspace::IsOpen(const Session* s) const {
    for (const auto& tab : sessions) {
        if (tab.get() == s) return true;
        if (tab->assembly)
            for (const AssemblyState::PartEdit& e : tab->assembly->parts)
                if (e.session.get() == s) return true;
    }
    return false;
}

void Workspace::Activate(int index) {
    if (index < 0 || index >= static_cast<int>(sessions.size())) return;
    myActive = index;
    myShowHome = false;
}

std::string Workspace::NextUntitledName(std::string_view base) const {
    for (int n = 1;; ++n) {
        const std::string name = std::format("{}{}", Tr(std::string(base)), n);
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

Session& Workspace::NewAssembly() {
    auto session = std::make_unique<Session>();
    session->MakeAssembly();
    session->untitledName = NextUntitledName("Сборка");
    sessions.push_back(std::move(session));
    Activate(static_cast<int>(sessions.size()) - 1);
    return *sessions.back();
}

Expected<std::unique_ptr<Session>> Workspace::LoadSession(const std::filesystem::path& file,
                                                          std::vector<std::string>* warnings) {
    Expected<std::string> type = doc::ReadDocumentType(file);
    if (!type) return type.GetError();
    const doc::DocumentTypeInfo* info = doc::DocumentTypeRegistry::Default().Find(*type);
    if (!info || !info->canOpen)
        return Error{std::format("документы вида «{}» эта версия не открывает", info ? info->title : *type)};
    auto session = std::make_unique<Session>();
    if (*type == doc::kAssemblyType) {
        Expected<assembly::Assembly> loaded = assembly::LoadAssembly(file, warnings);
        if (!loaded) return loaded.GetError();
        session->MakeAssembly().assembly = std::move(*loaded);
        session->assembly->MarkSaved();
    } else {
        doc::LoadReport report;
        Expected<doc::Document> loaded = doc::LoadBzc(file, &report);
        if (!loaded) return loaded.GetError();
        session->doc = std::move(*loaded);
        if (warnings) warnings->insert(warnings->end(), report.warnings.begin(), report.warnings.end());
    }
    return session;
}

bool Workspace::Open(const std::filesystem::path& file, Notifications& notes, Settings& settings) {
    for (std::size_t i = 0; i < sessions.size(); ++i) {
        if (!sessions[i]->path.empty() && SameFile(sessions[i]->path, file)) {
            Activate(static_cast<int>(i));
            return true;
        }
    }
    // A part some assembly holds edited in memory: two copies must not go on apart.
    if (!PartFreeToOpen(*this, notes, file)) {
        lastOpenError = Tr("деталь правится в открытой сборке: сохраните сборку или закончите правку");
        return false;
    }
    std::vector<std::string> warnings;
    mon::Action("open", ToUtf8(file.filename()));
    const auto loadStart = std::chrono::steady_clock::now();
    Expected<std::unique_ptr<Session>> loaded = LoadSession(file, &warnings);
    mon::Timed("open", "чтение файла (геометрия — потом, в перестроении)",
               std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - loadStart).count());
    lastOpenError.clear();
    if (!loaded) {
        lastOpenError = Tr(loaded.GetError().message);
        log::Warn("Could not open {}: {}", ToUtf8(file), loaded.GetError().message);
        notes.Error(Trf("Не удалось открыть «{}»: {}", ToUtf8(file.filename()), Tr(loaded.GetError().message)));
        settings.RemoveRecent(file);
        return false;
    }
    (*loaded)->path = file;
    // Its parts as they are now in their folder (Android: fetched again from
    // the projects folder — they may come from a computer).
    if ((*loaded)->assembly)
        for (const assembly::Component& c : (*loaded)->assembly->assembly.components)
            platform::RefreshDocument(assembly::ResolvePartPath(file, c.path));
    sessions.push_back(std::move(*loaded));
    Activate(static_cast<int>(sessions.size()) - 1);
    settings.AddRecent(file);
    for (const auto& w : warnings) notes.Info(Tr(w));
    log::Info("Opened {}", ToUtf8(file));
    return true;
}

bool Workspace::Save(Session& session, bool saveAs, Notifications& notes, Settings& settings) {
    // A part edited in an assembly is saved with it (into its copy beside it).
    if (session.host) return Save(*session.host, saveAs, notes, settings);
    std::filesystem::path target = session.path;
    if (target.empty() || saveAs) {
        if (const std::string problem = platform::FileDialogProblem(); !problem.empty()) {
            notes.Error(problem);
            return false;
        }
        const std::filesystem::path folder = session.path.empty() ? platform::DocumentsDir() : session.path.parent_path();
        const doc::DocumentTypeInfo* type = doc::DocumentTypeRegistry::Default().Find(session.DocumentType());
        const std::string ext = type ? type->extension : std::string(".bzc");
        const std::string name = session.assembly ? Tr("Сборка BreezeCAD") : Tr("Деталь BreezeCAD");
        const auto chosen = platform::ShowSaveDialog(Tr("Сохранить документ"), folder / PathFromUtf8(session.Title() + ext),
                                                     {{name, "*" + ext}}, ext);
        if (!chosen) return false;
        target = *chosen;
        // Another tab with that file would go on with its own copy: whichever
        // was saved last would silently replace the other's work.
        for (const auto& other : sessions)
            if (other.get() != &session && !other->path.empty() && SameFile(other->path, target)) {
                notes.Error(Trf("«{}» открыт в другой вкладке: закройте её или сохраните под другим именем",
                                ToUtf8(target.filename())));
                return false;
            }
    }
    return SaveTo(session, target, notes, settings);
}

bool Workspace::GatherParts(Session& session, const std::filesystem::path& from, const std::filesystem::path& to,
                            Notifications& notes) {
    if (!session.assembly) return true;
    assembly::PartFolder folder;
    folder.newFile = [&to](const std::string& name) { return platform::NewFileBeside(to, name); };
    folder.written = [](const std::filesystem::path& file) { return platform::FileWritten(file); };
    folder.subfolders = platform::PartsInSubfolders();
    const assembly::GatherResult gathered = assembly::GatherParts(session.assembly->assembly, from, to, folder);
    for (const std::string& p : gathered.problems) notes.Error(Tr(p));
    if (!gathered.copied.empty()) {
        std::string names;
        for (const std::string& n : gathered.copied) names += (names.empty() ? "" : ", ") + n;
        notes.Info(Trf("Детали скопированы в папку сборки: {}", names));
        log::Info("Assembly {}: parts copied beside it: {}", ToUtf8(to.filename()), names);
    }
    if (!gathered.ok)
        if (const std::string why = platform::BesideProblem(to); !why.empty()) notes.Error(Tr(why));
    return gathered.ok;
}

bool Workspace::SaveTo(Session& session, const std::filesystem::path& requested, Notifications& notes, Settings& settings,
                       std::string* error) {
    // A part edited in an assembly: the assembly is saved, the part with it.
    if (session.host) return SaveTo(*session.host, session.host->path.empty() ? requested : session.host->path, notes, settings, error);
    // Android: a projects-folder document whose copy lies apart from the
    // others (made by 0.2.3.x) is saved beside them from now on.
    const std::filesystem::path target = platform::SettledDocument(requested);
#if defined(__ANDROID__)
    // The local copy of a document lives in the app's cache, which the system
    // may clear while the app is away: its folder made again.
    if (platform::android::IsImported(target)) {
        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
    }
#endif
    // An assembly takes its parts along: each one lies in its folder (a part
    // from elsewhere is copied there), so the folder carried to another
    // computer or phone opens whole. The paths are then relative to `target`.
    const std::filesystem::path from = session.path;
    // Projections the parts took from each other name the assembly: a new
    // name (the first save, «Сохранить как») goes into them before they are written.
    const std::string oldName = session.ContextName();
    const std::string newName = ToUtf8(target.stem());
    const auto renameBack = [&] {
        if (session.assembly && oldName != newName) RenameContext(session, newName, oldName);
    };
    if (session.assembly && oldName != newName) RenameContext(session, oldName, newName);
    if (session.assembly) {
        // Parts edited or made in this assembly go with it: the new ones are
        // written first (their copies beside it are made from them), the
        // edited ones into those copies once every part lies beside it — never
        // into an original elsewhere (the rule for parts of assemblies).
        const std::vector<std::vector<ObjectId>> users = PartUsers(session, from);
        std::string partError;
        if (!WriteCreatedParts(session, users, partError)) {
            renameBack();
            const std::string message = Trf("Сборка НЕ сохранена: {}", Tr(partError));
            if (error) *error = message;
            notes.Error(message);
            return false;
        }
        if (!GatherParts(session, from, target, notes)) {
            renameBack();
            assembly::RebasePaths(session.assembly->assembly, target, from);
            const std::string message = Tr("Сборка НЕ сохранена: не все её детали удалось положить рядом с ней");
            if (error) *error = message;
            notes.Error(message);
            return false;
        }
        session.path = target;   // the paths are relative to it already: written as they are
        if (!WriteEditedParts(session, users, target, partError)) {
            assembly::RebasePaths(session.assembly->assembly, target, from);
            session.path = from;
            renameBack();
            const std::string message = Trf("Сборка НЕ сохранена: {}", Tr(partError));
            if (error) *error = message;
            notes.Error(message);
            return false;
        }
    }
    const Status status = session.WriteFile(target, kGenerator);
    if (!status) {
        if (session.assembly) {
            assembly::RebasePaths(session.assembly->assembly, target, from);
            session.path = from;
        }
        if (error) *error = status.GetError().message;
        notes.Error(Trf("Не удалось сохранить: {}", Tr(status.GetError().message)));
        return false;
    }
    // An assembly saved somewhere else refers to its parts from there now.
    if (session.assembly && session.path != target) assembly::RebasePaths(session.assembly->assembly, session.path, target);
    session.path = target;
    // On Android `target` is a local copy: the document itself is in the
    // projects folder or wherever the user picked. Until the copy reached
    // it nothing is saved (0.2.3.2: it never did, the folder kept an empty
    // file and the open copy was the only one). The work stays unsaved:
    // the autosave keeps taking it, the next save tries again.
    if (!platform::FileWritten(target)) {
        const std::string message = Trf("Не удалось записать «{}» в папку проектов: документ НЕ сохранён. "
                                        "Изменения не потеряны — документ открыт, сохраните его ещё раз",
                                        ToUtf8(target.filename()));
        if (error) *error = message;
        notes.Error(message);
        log::Error("Saved {} locally, but could not write it to its document", ToUtf8(target));
        return false;
    }
    session.MarkSaved();
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
    if (const std::string empty = NothingToExport(session); !empty.empty()) {
        notes.Info(Tr(empty));
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
    return ExportTo(session, format, *chosen, notes);
}

bool Workspace::ExportTo(Session& session, ExportFormat format, const std::filesystem::path& target, Notifications& notes,
                         std::string* error) {
    if (const std::string empty = NothingToExport(session); !empty.empty()) {
        if (error) *error = empty;
        notes.Info(Tr(empty));
        return false;
    }
    // A long export (a threaded part as STEP on a phone) is work, not a hang: no hang
    // report, and on Android the watchdog does not offer to close the app meanwhile.
    const mon::Waiting waiting;
#if defined(__ANDROID__)
    struct Busy {
        Busy() { platform::android::SetWatchdogState("wait"); }
        ~Busy() { platform::android::SetWatchdogState("fg"); }
    } busy;
#endif
    session.RebuildNow();   // a rebuild may still be under way in slices
    // Cosmetic threads are only drawn in the model; a file gets them cut.
    std::optional<kernel::Model> real;
    if (std::ranges::any_of(session.doc.Features(), [](const auto& f) {
            const auto* t = dynamic_cast<const doc::ThreadFeature*>(f.get());
            return t && t->cosmetic && !t->suppressed;
        })) {
        doc::Document copy = session.doc.Clone();
        for (const auto& f : copy.Features())
            if (auto* t = dynamic_cast<doc::ThreadFeature*>(f.get())) t->cosmetic = false;
        copy.MarkChanged(0);
        real.emplace();
        real->Rebuild(copy);
        for (const auto& f : copy.Features())
            if (f->status.state == doc::FeatureState::Error && dynamic_cast<const doc::ThreadFeature*>(f.get()))
                notes.Error(Trf("{}: резьба для файла не построилась — {}", f->name, Tr(f->status.message)));
    }
    const kernel::Model& model = real ? *real : session.model;
    const Status status = format == ExportFormat::Step ? model.ExportStep(target)
                        : format == ExportFormat::Stl  ? model.ExportStl(target)
                                                       : model.Export3mf(target);
    if (!status) {
        if (error) *error = status.GetError().message;
        notes.Error(Tr(status.GetError().message));
        return false;
    }
    if (!platform::FileWritten(target)) {
        const std::string message = Trf("Не удалось записать «{}» в выбранную папку", ToUtf8(target.filename()));
        if (error) *error = message;
        notes.Error(message);
        return false;
    }
    notes.Success(Trf("Экспортировано: {}", ToUtf8(target.filename())));
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
