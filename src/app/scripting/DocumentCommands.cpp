// Documents as a whole: which are open, new, open, save, export, undo.

#include "app/AppContext.h"
#include "app/Operations.h"
#include "app/Session.h"
#include "app/Workspace.h"
#include "app/scripting/Commands.h"
#include "core/PathText.h"

#include <cctype>
#include <filesystem>
#include <format>

namespace bz::app::scripting {

namespace {

/// An absolute path from the caller (relative paths would depend on where
/// the program happened to start).
std::filesystem::path AbsolutePath(const Json& args, const char* key) {
    const std::filesystem::path p = PathFromUtf8(Text(args, key));
    if (!p.is_absolute()) Fail(std::format("«{}»: нужен полный путь (например C:/Детали/вал.bzc)", key));
    return p;
}

Json DescribeSession(const Session& s, int index, bool active) {
    Json out = {{"index", index},
                {"title", s.Title()},
                {"type", std::string(s.DocumentType())},
                {"modified", s.IsModified()},
                {"active", active}};
    if (!s.path.empty()) out["path"] = ToUtf8(s.path);
    return out;
}

Json Documents(AppContext& app, const Json&) {
    Json list = Json::array();
    const int active = app.workspace.ShowsHome() ? -1 : app.workspace.ActiveIndex();
    for (int i = 0; i < static_cast<int>(app.workspace.sessions.size()); ++i)
        list.push_back(DescribeSession(*app.workspace.sessions[static_cast<std::size_t>(i)], i, i == active));
    return list;
}

Json NewPart(AppContext& app, const Json&) {
    NewDocument(app);
    return DescribeSession(*app.Doc(), app.workspace.ActiveIndex(), true);
}

Json Open(AppContext& app, const Json& args) {
    const std::filesystem::path file = AbsolutePath(args, "path");
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) Fail(std::format("Нет файла «{}»", ToUtf8(file)));
    if (!app.workspace.Open(file, app.notes, app.settings))
        Fail(app.workspace.lastOpenError.empty()
                 ? std::format("Не удалось открыть «{}» (подробности — в уведомлении BreezeCAD)", ToUtf8(file))
                 : std::format("Не удалось открыть «{}»: {}", ToUtf8(file), app.workspace.lastOpenError));
    app.workspace.ShowHome(false);
    return DescribeSession(*app.Doc(), app.workspace.ActiveIndex(), true);
}

Json Activate(AppContext& app, const Json& args) {
    const int index = Integer(args, "index");
    if (index < 0 || index >= static_cast<int>(app.workspace.sessions.size())) Fail(std::format("Нет документа с номером {}", index));
    app.workspace.Activate(index);
    app.workspace.ShowHome(false);
    return DescribeSession(*app.Doc(), index, true);
}

Json Save(AppContext& app, const Json& args) {
    Session& s = Active(app);
    std::filesystem::path target = s.path;
    if (Has(args, "path")) {
        target = AbsolutePath(args, "path");
        if (!target.has_extension()) target += ".bzc";
    }
    if (target.empty()) Fail("Документ ещё ни разу не сохранялся — укажите «path»");
    std::error_code ec;   // as doc.export: a new folder is made
    if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), ec);
    std::string error;
    if (!app.workspace.SaveTo(s, target, app.notes, app.settings, &error)) Fail(std::format("Не удалось сохранить: {}", error));
    return DescribeSession(s, app.workspace.ActiveIndex(), true);
}

Json Export(AppContext& app, const Json& args) {
    Session&                    s    = Active(app);
    const std::filesystem::path file = AbsolutePath(args, "path");
    std::string                 ext  = ToUtf8(file.extension());
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    static const std::vector<std::string> kFormats = {"step", "stl", "3mf"};
    const std::size_t byExtension = ext == ".stl" ? 1 : ext == ".3mf" ? 2 : 0;
    const auto        format      = static_cast<ExportFormat>(Choice(args, "format", kFormats, byExtension));
    std::error_code   ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    std::string error;
    if (!app.workspace.ExportTo(s, format, file, app.notes, &error)) Fail(error);
    return {{"path", ToUtf8(file)}, {"format", kFormats[static_cast<std::size_t>(format)]},
            {"bytes", std::filesystem::file_size(file, ec)}};
}

Json UndoRedo(AppContext& app, bool undo) {
    Session& s = Active(app);
    if (undo ? !s.CanUndo() : !s.CanRedo()) Fail(undo ? "Отменять нечего" : "Повторять нечего");
    // The step this call undid (or redid): the top of the stack before it.
    const std::string step = undo ? s.UndoLabel() : s.RedoLabel();
    if (undo) Undo(app);
    else Redo(app);
    s.RebuildNow();
    return {{undo ? "undone" : "redone", step},
            {"next_undo", s.CanUndo() ? s.UndoLabel() : ""},
            {"can_redo", s.CanRedo()}};
}

} // namespace

void AddDocumentCommands(Commands& out) {
    out.push_back({"app.documents",
                   "Открытые документы: номер, имя, тип (part/assembly), путь, есть ли несохранённые изменения, какой активный.",
                   Schema(), false, &Documents});
    out.push_back({"app.new_part", "Создаёт новую пустую деталь и делает её активной.", Schema(), true, &NewPart});
    out.push_back({"app.open", "Открывает файл .bzc (деталь или сборку) и делает его активным.",
                   Schema({{"path", StringArg("Полный путь к файлу.")}}, {"path"}), true, &Open});
    out.push_back({"app.activate", "Делает активным открытый документ по номеру из app.documents.",
                   Schema({{"index", IntegerArg("Номер документа.")}}, {"index"}), true, &Activate});
    out.push_back({"doc.save",
                   "Сохраняет активный документ: в его файл или, если задан path, в новый файл (дальше документ связан с ним).",
                   Schema({{"path", StringArg("Полный путь .bzc (папка создаётся при нужде); не задан — сохранить в текущий файл.")}}), true, &Save});
    out.push_back({"doc.export", "Экспортирует тела активного документа в STEP, STL или 3MF.",
                   Schema({{"path", StringArg("Полный путь файла; папка создаётся при нужде.")},
                           {"format", EnumArg("Формат; не задан — по расширению path.", {"step", "stl", "3mf"})}},
                          {"path"}),
                   true, &Export});   // "changes": it brings the geometry up to date, not while the user edits
    out.push_back({"doc.undo", "Отменяет последний шаг активного документа (как Ctrl+Z). Ответ: undone — что отменено, next_undo — что отменится следующим.", Schema(), true,
                   [](AppContext& app, const Json&) { return UndoRedo(app, true); }});
    out.push_back({"doc.redo", "Повторяет отменённый шаг (как Ctrl+Y). Ответ: redone — что повторено.", Schema(), true,
                   [](AppContext& app, const Json&) { return UndoRedo(app, false); }});
}

} // namespace bz::app::scripting
