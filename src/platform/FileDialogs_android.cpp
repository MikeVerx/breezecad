// Open and save on Android (docs/dev/MOBILE-PORT.md, "Папка проектов"): the
// projects folder chosen once, the system's pickers (SAF) otherwise. The
// program works with local copies of the documents; FileWritten copies a
// saved one back to its place.
//
// The pickers are another activity: the call waits for the answer, the
// window's events still flowing (the app pauses, comes back), as a modal
// dialog does on the desktop — so every caller works as it is.
#include "platform/FileDialogs.h"

#include "core/Log.h"
#include "core/Monitor.h"
#include "platform/Android.h"
#include "platform/Window.h"

#include <android_native_app_glue.h>

#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace bz::platform {
namespace {

std::optional<android::PickResult> Wait(android::PickResult::Kind kind) {
    // The loop waits for the system's picker on purpose: no hang report however
    // long the user looks for the file (it said "не отвечает" after 8 s, 18.10.2026).
    const mon::Waiting waiting;
    while (android_app* app = android::App()) {
        Window::WaitEvents(0.25);
        if (auto r = android::TakePickResult()) {
            if (r->kind == kind) return r;
        }
        if (app->destroyRequested) break;
    }
    return std::nullopt;
}

std::string Lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// "Деталь1.bzc" → "Деталь1 (2).bzc" while that name is taken in the projects folder.
std::string FreeName(const std::string& name) {
    const std::vector<android::FolderEntry> entries = android::ListProjects();
    const auto taken = [&](const std::string& n) {
        return std::ranges::any_of(entries, [&](const android::FolderEntry& e) { return Lower(e.name) == Lower(n); });
    };
    if (!taken(name)) return name;
    const fs::path p = fs::u8path(name);
    const std::string stem = p.stem().string(), ext = p.extension().string();
    for (int n = 2;; ++n)
        if (const std::string candidate = stem + " (" + std::to_string(n) + ")" + ext; !taken(candidate)) return candidate;
}

std::string MimeOf(const std::string& extension) {
    const std::string e = Lower(extension);
    if (e == ".stl") return "model/stl";
    if (e == ".3mf") return "model/3mf";
    if (e == ".step" || e == ".stp") return "model/step";
    if (e == ".json") return "application/json";
    if (e == ".zip") return "application/zip";
    return "application/octet-stream";
}

} // namespace

std::string FileDialogProblem() { return {}; }

std::optional<fs::path> ShowOpenDialog(const std::string&, const std::vector<FileFilter>& filters, const fs::path&) {
    // Images get the gallery's types; anything else any file (a .bzc has no MIME type of its own).
    std::vector<std::string> mimes;
    for (const FileFilter& f : filters)
        if (f.pattern.find("*.png") != std::string::npos) mimes = {"image/*"};
    android::PickToOpen(mimes);
    const auto picked = Wait(android::PickResult::Kind::Open);
    if (!picked || picked->uri.empty()) return std::nullopt;
    fs::path local = android::Import(picked->uri, picked->name);
    if (local.empty()) return std::nullopt;
    return local;
}

std::optional<fs::path> ShowSaveDialog(const std::string&, const fs::path& suggested, const std::vector<FileFilter>&,
                                       const std::string& extension) {
    std::string name = suggested.filename().string();
    if (!extension.empty() && !Lower(name).ends_with(Lower(extension))) name += extension;
    // Documents and exports go to the projects folder without asking, once
    // it is chosen (the author's choice); anything else, or no folder yet,
    // through the system's "save as".
    if (!android::ProjectsFolder().empty()) {
        if (fs::path local = android::CreateInProjects(FreeName(name)); !local.empty()) return local;
        log::Warn("Android: the projects folder refused a new file; asking where to save");
    }
    android::PickToSave(name, MimeOf(fs::path(name).extension().string()), android::CacheDir() / "out" / name);
    const auto picked = Wait(android::PickResult::Kind::Save);
    if (!picked || picked->uri.empty()) return std::nullopt;
    // A copy of its own for each document: one shared cache/out/<name> for two
    // documents of the same name linked both to the last one picked, and the
    // first one's next save went into the second's file.
    const fs::path local = android::LocalCopyFor(picked->uri, picked->name.empty() ? name : picked->name);
    android::Link(local, picked->uri);
    return local;
}

bool FileWritten(const fs::path& file) {
    // A file of the app's own (the cache, a share) has no document elsewhere.
    if (!android::IsImported(file)) return true;
    return android::WriteBack(file);
}

} // namespace bz::platform
