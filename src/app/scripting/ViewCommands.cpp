// What the user sees and the libraries: a picture of the model (so the
// caller can look at what it built), the camera and display style,
// appearances of bodies and faces, standard parts.

#include "app/AppContext.h"
#include "app/Operations.h"
#include "app/AppearanceLibrary.h"
#include "app/Session.h"
#include "app/Workspace.h"
#include "app/scripting/Commands.h"
#include "core/I18n.h"
#include "document/Commands.h"
#include "document/StandardPacks.h"
#include "document/StandardParts.h"
#include "platform/Paths.h"

#include <miniz.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace bz::app::scripting {

namespace {

std::string Base64(const void* data, std::size_t size) {
    static constexpr char kChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto* b = static_cast<const std::uint8_t*>(data);
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (std::size_t i = 0; i < size; i += 3) {
        const std::uint32_t n = (std::uint32_t{b[i]} << 16) | (i + 1 < size ? std::uint32_t{b[i + 1]} << 8 : 0u) |
                                (i + 2 < size ? std::uint32_t{b[i + 2]} : 0u);
        out += kChars[(n >> 18) & 63];
        out += kChars[(n >> 12) & 63];
        out += i + 1 < size ? kChars[(n >> 6) & 63] : '=';
        out += i + 2 < size ? kChars[n & 63] : '=';
    }
    return out;
}

/// Where the camera looks from (the eye's side of the model) and its up.
std::pair<Vec3, Vec3> ViewDirection(const Json& args) {
    if (Has(args, "direction")) {
        const Vec3 d = Point3(args["direction"], "direction");
        if (d.Length() < 1e-9) Fail("«direction» — нулевой вектор");
        Vec3 up = Has(args, "up") ? Point3(args["up"], "up") : Vec3{0, 0, 1};
        if (up.Normalized().Cross(d.Normalized()).Length() < 1e-3) up = {0, 1, 0};
        return {d.Normalized(), up};
    }
    static const std::vector<std::string> kViews = {"iso", "front", "back", "top", "bottom", "left", "right"};
    switch (Choice(args, "view", kViews, 0)) {
        case 1: return {{0, -1, 0}, {0, 0, 1}};
        case 2: return {{0, 1, 0}, {0, 0, 1}};
        case 3: return {{0, 0, 1}, {0, 1, 0}};
        case 4: return {{0, 0, -1}, {0, -1, 0}};
        case 5: return {{-1, 0, 0}, {0, 0, 1}};
        case 6: return {{1, 0, 0}, {0, 0, 1}};
        default: return {Vec3{1, -1, 1}.Normalized(), {0, 0, 1}};
    }
}

Json CameraJson(const render::CameraState& c) {
    return {{"eye", ToJson3(c.eye)}, {"center", ToJson3(c.center)}, {"up", ToJson3(c.up)}, {"visible_height_mm", c.scale}};
}

/// The viewport brought up to the active document now (the frame would do
/// it later): its bodies and their appearance.
Session& SyncedView(AppContext& app) {
    Session& s = Active(app);
    s.RebuildNow();
    app.viewport.Sync(s.model);
    app.viewport.SetLook(s.doc.look, &s, s.doc.LookRevision());
    app.viewport.SetDisplayStyle(app.shell.displayStyle);
    return s;
}

Json Snapshot(AppContext& app, const Json& args) {
    SyncedView(app);
    const int width  = std::clamp(Integer(args, "width", 1024), 64, 2048);
    const int height = std::clamp(Integer(args, "height", 768), 64, 2048);
    render::CameraState camera = app.viewport.Camera();
    if (Has(args, "view") || Has(args, "direction") || !Flag(args, "current_camera", false)) {
        const auto [from, up] = ViewDirection(args);
        // Framed for the picture asked for, not for the window: a wide window
        // and a squarer picture would crop the model at the sides.
        camera = app.viewport.FitCamera(-from, up, {}, 1.15, static_cast<double>(width) / height);
    }
    const auto image = app.viewport.Snapshot(camera, width, height);
    if (!image) Fail("Не удалось получить изображение: графика ещё не готова (окно программы свёрнуто?)");
    std::size_t size = 0;
    void* png = tdefl_write_image_to_png_file_in_memory(image->rgba.data(), image->width, image->height, 4, &size);
    if (!png) Fail("Не удалось сжать изображение в PNG");
    Json out = {{"image_png_base64", Base64(png, size)}, {"width", image->width}, {"height", image->height}, {"camera", CameraJson(camera)}};
    mz_free(png);
    return out;
}

Json SetView(AppContext& app, const Json& args) {
    SyncedView(app);
    if (Has(args, "style")) {
        static const std::vector<std::string> kStyles = {"shaded", "shaded_with_edges", "wireframe", "hidden_lines", "transparent"};
        app.shell.displayStyle = static_cast<render::DisplayStyle>(Choice(args, "style", kStyles, 1));
    }
    if (Has(args, "view") || Has(args, "direction") || Flag(args, "fit", false)) {
        render::CameraState target;
        if (Has(args, "view") || Has(args, "direction")) {
            const auto [from, up] = ViewDirection(args);
            target = app.viewport.FitCamera(-from, up);
        } else {
            const render::CameraState now = app.viewport.Camera();
            target = app.viewport.FitCamera(now.center - now.eye, now.up);
        }
        app.viewport.AnimateTo(target, 0.0);
    }
    return {{"camera", CameraJson(app.viewport.Camera())}};
}

// --- appearances -------------------------------------------------------------

const char* FinishName(doc::Finish f) {
    switch (f) {
        case doc::Finish::Metal: return "metal";
        case doc::Finish::Glass: return "glass";
        default: return "matte";
    }
}

Json AppearanceJson(const doc::Appearance& a) {
    Json j = {{"id", a.id}, {"name", Tr(a.name)}, {"color", doc::ColorToHex(a.face)}, {"finish", FinishName(a.finish)}};
    if (a.HasPicture()) j["picture_size_mm"] = a.pictureSize;
    return j;
}

AppearanceLibrary UserLibrary() {
    AppearanceLibrary library(platform::ConfigDir() / "appearances");
    library.Reload();
    return library;
}

Json Appearances(AppContext& app, const Json&) {
    Json standard = Json::array(), user = Json::array();
    for (const doc::Appearance& a : doc::StandardAppearances()) standard.push_back(AppearanceJson(a));
    const AppearanceLibrary library = UserLibrary();
    for (const doc::Appearance& a : library.Items()) user.push_back(AppearanceJson(a));
    Json out = {{"standard", std::move(standard)}, {"user", std::move(user)}};
    if (Session* s = app.Doc()) {
        Json bodies = Json::array();
        for (const auto& b : s->doc.look.bodies) bodies.push_back({{"body", b.body.value}, {"appearance", b.appearance}});
        out["document"] = {{"bodies", std::move(bodies)}, {"faces_with_own_look", s->doc.look.faces.size()}};
    }
    return out;
}

Json SetAppearance(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    s.RebuildNow();
    const AppearanceLibrary library = UserLibrary();
    std::optional<doc::Appearance> chosen;
    if (Has(args, "appearance")) {
        const std::string id = Text(args, "appearance");
        if (const doc::Appearance* a = doc::FindStandard(id)) chosen = *a;
        else if (const doc::Appearance* u = library.Find(id)) chosen = *u;
        else if (const doc::Appearance* d = s.doc.look.Find(id)) chosen = *d;
        else Fail(std::format("Нет внешнего вида «{}» (список — appearance.list)", id));
    } else if (Has(args, "color")) {
        const auto colour = doc::ColorFromHex(Text(args, "color"));
        if (!colour) Fail("«color» — цвет \"#RRGGBB\" или \"#RRGGBBAA\"");
        static const std::vector<std::string> kFinishes = {"matte", "metal", "glass"};
        doc::Appearance a;
        a.face   = *colour;
        a.finish = static_cast<doc::Finish>(Choice(args, "finish", kFinishes, 0));
        a.id     = std::format("doc.{}.{}", doc::ColorToHex(a.face).substr(1), FinishName(a.finish));
        a.name   = doc::ColorToHex(a.face);
        chosen   = a;
    } else if (!Flag(args, "reset", false)) {
        Fail("Задайте «appearance» (id из appearance.list), «color» или reset: true");
    }
    const doc::Appearance* a = chosen ? &*chosen : nullptr;

    doc::Look look = s.doc.look;
    int       count = 0;
    if (Has(args, "faces")) {
        for (const Json& v : Required(args, "faces")) {
            const kernel::FaceInfo& f = FaceArg(s, v, "faces");
            look.SetFace(f.body, f.ref, a);
            ++count;
        }
    } else {
        for (const auto& b : s.model.Bodies())
            if (!Has(args, "body") || b.id.value == static_cast<std::uint64_t>(Integer(args, "body"))) {
                look.SetBody(b.id, a);
                ++count;
            }
        if (count == 0) Fail(Has(args, "body") ? "Нет тела с таким id (part.tree → bodies)" : "В детали нет тел");
    }
    if (a && a->HasPicture()) look.pictures[a->picture] = library.PictureBytes(a->picture);
    look.Prune();
    if (look != s.doc.look) s.Execute(doc::SetLook(std::move(look), Tr("Внешний вид")));
    return {{"applied_to", count}, {"what", Has(args, "faces") ? "faces" : "bodies"}, {"appearance", a ? AppearanceJson(*a) : Json(nullptr)}};
}

// --- standard parts ------------------------------------------------------------

const char* FamilyName(doc::parts::Family f) {
    switch (f) {
        case doc::parts::Family::Iso: return "ISO";
        case doc::parts::Family::Din: return "DIN";
        default: return "ГОСТ";
    }
}

Json LibraryList(AppContext&, const Json& args) {
    const std::string family = Text(args, "family", "");
    Json groups = Json::array();
    for (const doc::parts::GroupInfo& g : doc::parts::Groups()) {
        Json kinds = Json::array();
        for (const doc::parts::PartKind k : g.kinds) {
            Json standards = Json::array();
            for (const doc::parts::StandardInfo* st : doc::parts::StandardsOf(k)) {
                if (!family.empty() && family != FamilyName(doc::parts::FamilyOf(*st)) && !doc::parts::InEveryFamily(*st)) continue;
                Json j = {{"standard", st->id}, {"title", st->title}};
                if (!st->note.empty()) j["note"] = Tr(std::string(st->note));
                standards.push_back(std::move(j));
            }
            if (!standards.empty()) kinds.push_back({{"kind", Tr(std::string(doc::parts::KindOf(k).title))}, {"standards", std::move(standards)}});
        }
        if (!kinds.empty()) groups.push_back({{"group", Tr(std::string(g.name))}, {"kinds", std::move(kinds)}});
    }
    return groups;
}

const doc::parts::StandardInfo& StandardArg(const Json& args) {
    const std::string id = Text(args, "standard");
    const doc::parts::StandardInfo* st = doc::parts::FindStandard(id);
    if (!st) Fail(std::format("Нет стандарта «{}» (список — library.list)", id));
    return *st;
}

/// The size as the tables write it (doc::parts::MatchSize: «М10» typed as
/// M10, a washer «6» typed as M6).
std::string SizeArg(const doc::parts::StandardInfo& st, const Json& args) { return doc::parts::MatchSize(st, Text(args, "size")); }

Json LibrarySizes(AppContext&, const Json& args) {
    const doc::parts::StandardInfo& st = StandardArg(args);
    if (Has(args, "size")) {
        const std::string size = SizeArg(st, args);
        Json out = {{"standard", st.id}, {"size", size}, {"lengths", doc::parts::Lengths(st, size)}};
        if (const auto range = doc::parts::FreeLength(st))
            out["length_range"] = {{"min", range->min}, {"max", range->max}, {"default", range->preset}};
        return out;
    }
    Json out = {{"standard", st.id}, {"title", st.title}, {"sizes", doc::parts::Sizes(st)}, {"has_thread", doc::parts::HasThread(st.kind)},
                {"note", "Длины размера — library.sizes с size."}};
    if (const std::string_view title = doc::parts::SizeTitle(st); !title.empty()) out["size_title"] = Tr(title);
    Json inputs = Json::array();
    for (const doc::parts::InputInfo& in : doc::parts::Inputs(st))
        inputs.push_back({{"name", in.name}, {"title", Tr(in.title)}, {"min", in.min}, {"max", in.max}, {"default", in.preset}});
    if (!inputs.empty()) out["inputs"] = std::move(inputs);
    if (const auto range = doc::parts::FreeLength(st)) {
        out["length_range"] = {{"min", range->min}, {"max", range->max}, {"default", range->preset}};
        out["note"] = "Длина — любая из диапазона length_range, мм.";
    }
    return out;
}

/// Saves the active part into the user's library: chosen variables become the table.
Json LibrarySavePart(AppContext& app, const Json& args) {
    const Session* s = app.Doc();
    if (!s || s->assembly) Fail("Активный документ — не деталь");
    doc::parts::UserItem item;
    item.title = Text(args, "title");
    item.group = Text(args, "group", "Мои изделия");
    if (const auto v = args.find("variables"); v != args.end() && v->is_array())
        for (const Json& name : *v)
            if (name.is_string()) item.variables.push_back(name.get<std::string>());
    if (const auto rows = args.find("rows"); rows != args.end() && rows->is_array())
        for (const Json& row : *rows) {
            if (!row.is_array()) Fail("rows — список строк [числа по переменным]");
            std::vector<double> values;
            for (const Json& x : row) {
                if (!x.is_number()) Fail("В rows — только числа, мм");
                values.push_back(x.get<double>());
            }
            item.rows.push_back(std::move(values));
        }
    const Expected<std::string> id = doc::parts::AddUserItem(StandardPackDir() / std::string(doc::parts::kUserPackFile), s->doc, item);
    if (!id) Fail(id.GetError().message);
    LoadStandardPacks(app);
    const doc::parts::StandardInfo* st = doc::parts::FindStandard(*id);
    return {{"standard", *id}, {"sizes", st ? doc::parts::Sizes(*st) : std::vector<std::string>{}},
            {"note", "Изделие в моей библиотеке: library.make_part с этим standard."}};
}

/// Adds a pack of standard parts (.bzlib) to the user's library and reloads the packs.
Json LibraryAddPack(AppContext& app, const Json& args) {
    const std::string path = Text(args, "path");
    const std::filesystem::path file{std::u8string(path.begin(), path.end())};
    const Expected<std::filesystem::path> copy = doc::parts::InstallPack(file, StandardPackDir());
    if (!copy) Fail(copy.GetError().message);
    LoadStandardPacks(app);
    for (const auto& pack : doc::parts::LoadedPacks())
        if (pack->file == *copy) {
            Json standards = Json::array();
            for (const doc::parts::PackStandard& st : pack->standards) standards.push_back(st.id);
            return {{"pack", pack->id}, {"title", pack->title}, {"standards", std::move(standards)},
                    {"note", "Пакет в библиотеке: library.list, library.sizes, library.make_part."}};
        }
    Fail("Пакет скопирован, но не загружен: его виды или стандарты уже есть в другом пакете");
}

Json LibraryMake(AppContext& app, const Json& args) {
    doc::parts::Request request;
    const doc::parts::StandardInfo& st = StandardArg(args);
    request.standard   = st.id;
    request.size       = SizeArg(st, args);
    request.length     = Number(args, "length", 0.0);
    request.realThread = Flag(args, "real_thread", false);
    request.appearance = Text(args, "appearance", "");
    if (const auto in = args.find("inputs"); in != args.end() && in->is_object())
        for (const auto& [name, value] : in->items()) {
            if (!value.is_number()) Fail(std::format("inputs.{} — число, мм", name));
            request.inputs.emplace_back(name, value.get<double>());
        }
    Expected<doc::parts::Made> made = doc::parts::MakePart(request);
    if (!made) Fail(Tr(made.GetError().message));
    Session& s = app.workspace.NewPart();
    s.untitledName = made->doc.info.designation;
    s.doc = std::move(made->doc);
    s.doc.units.length = static_cast<LengthUnit>(app.settings.newPartLength);
    s.history.MarkUnsaved();
    s.RebuildNow();
    Json out = {{"designation", s.untitledName},
                {"envelope_mm", {made->envelope.x, made->envelope.y, made->envelope.z}},
                {"open_documents", app.workspace.sessions.size()},
                {"note", "Деталь открыта новым документом (активна): её можно менять, сохранить doc.save, вставить в сборку."}};
    if (const auto props = s.model.Properties()) out["volume_mm3"] = props->volume;
    return out;
}

Json ViewArgs(Json p) {
    p["view"]      = EnumArg("Откуда смотреть: iso (по умолчанию), front, back, top, bottom, left, right.",
                             {"iso", "front", "back", "top", "bottom", "left", "right"});
    p["direction"] = Point3Arg("Или направление от модели к глазу [x, y, z] (iso — [1, -1, 1]).");
    p["up"]        = Point3Arg("Верх картинки для direction (по умолчанию [0, 0, 1]).");
    return p;
}

} // namespace

void AddViewCommands(Commands& out) {
    out.push_back({"view.snapshot",
                   "Картинка модели активного документа (PNG) — посмотреть, что получилось. Модель вписывается в кадр с "
                   "выбранной стороны; окно программы при этом не меняется. Внешний вид и стиль отображения — как на экране.",
                   Schema(ViewArgs({{"width", IntegerArg("Ширина, пикселей (64–2048, по умолчанию 1024).")},
                                    {"height", IntegerArg("Высота, пикселей (по умолчанию 768).")},
                                    {"current_camera", BoolArg("Снять как сейчас на экране (без view/direction).")}})),
                   false, &Snapshot});
    out.push_back({"view.set",
                   "Камера и отображение в окне программы: view/direction — повернуть и вписать модель; fit — вписать, не "
                   "поворачивая; style — shaded, shaded_with_edges, wireframe, hidden_lines, transparent.",
                   Schema(ViewArgs({{"fit", BoolArg("Вписать модель в окно.")},
                                    {"style", EnumArg("Стиль отображения.", {"shaded", "shaded_with_edges", "wireframe", "hidden_lines", "transparent"})}})),
                   false, &SetView});
    out.push_back({"appearance.list",
                   "Внешние виды (материалы): стандартные и библиотеки пользователя — id, имя, цвет, отделка; и что "
                   "назначено в активном документе.",
                   Schema(), false, &Appearances});
    out.push_back({"part.set_appearance",
                   "Назначает внешний вид телам (все или body) или отдельным граням (faces, номера из part.faces): по id из "
                   "appearance.list или цветом color \"#RRGGBB\" с отделкой finish. reset: true — вернуть по умолчанию. Один шаг отмены.",
                   Schema({{"appearance", StringArg("id внешнего вида (std.steel…).")},
                           {"color", StringArg("Или цвет \"#RRGGBB\" / \"#RRGGBBAA\".")},
                           {"finish", EnumArg("Для color: matte (по умолчанию), metal, glass.", {"matte", "metal", "glass"})},
                           {"body", IdArg("Только это тело (part.tree → bodies).")},
                           {"faces", IndexListArg("Или только эти грани.")},
                           {"reset", BoolArg("Снять назначенный вид.")}}),
                   true, &SetAppearance});
    out.push_back({"library.list",
                   "Библиотека стандартных изделий: группы (болты, винты, гайки, шайбы, подшипники, кольца, штифты…), виды и "
                   "стандарты (id для library.sizes / library.make_part).",
                   Schema({{"family", EnumArg("Только ГОСТ, ISO или DIN.", {"ГОСТ", "ISO", "DIN"})}}), false, &LibraryList});
    out.push_back({"library.sizes", "Размеры стандарта (M10, 6204…); с size — доступные длины этого размера, мм.",
                   Schema({{"standard", StringArg("id стандарта (gost7798…).")}, {"size", StringArg("Размер.")}}, {"standard"}), false,
                   &LibrarySizes});
    out.push_back({"library.make_part",
                   "Создаёт стандартную деталь новым документом (параметрическое дерево, свойства, обозначение «Болт М10×40 "
                   "ГОСТ 7798-70»). Резьба по умолчанию условная.",
                   Schema({{"standard", StringArg("id стандарта.")},
                           {"size", StringArg("Размер из library.sizes.")},
                           {"length", NumberArg("Длина из library.sizes (если у изделия есть длина; для length_range — любая "
                                                "из диапазона, по умолчанию default).")},
                           {"real_thread", BoolArg("Настоящая (вырезанная) резьба.")},
                           {"appearance", StringArg("Внешний вид (id); по умолчанию — свой для вида изделия.")},
                           {"inputs", {{"type", "object"},
                                       {"description", "Размеры, которые вводятся (inputs из library.sizes): {\"B\": 1250}, мм."}}}},
                          {"standard", "size"}),
                   true, &LibraryMake});
    out.push_back({"library.save_part",
                   "Сохраняет активную деталь в мою библиотеку: выбранные переменные детали становятся столбцами таблицы "
                   "размеров (переменная L — длина, любая), rows — размеры. Без rows — одна строка: деталь как есть.",
                   Schema({{"title", StringArg("Название изделия.")},
                           {"group", StringArg("Папка в окне «Библиотека» (по умолчанию «Мои изделия»).")},
                           {"variables", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Имена переменных детали."}}},
                           {"rows", {{"type", "array"}, {"items", {{"type", "array"}, {"items", {{"type", "number"}}}}},
                                     {"description", "Строки: значения переменных в порядке variables, мм."}}}},
                          {"title", "variables"}),
                   true, &LibrarySavePart});
    out.push_back({"library.add_pack",
                   "Добавляет в библиотеку пакет стандартных изделий (файл .bzlib: таблицы размеров и рецепт построения — "
                   "docs/ADR/0006-standard-part-packs.md). Файл проверяется и копируется в папку библиотеки пользователя.",
                   Schema({{"path", StringArg("Путь к файлу .bzlib.")}}, {"path"}), true, &LibraryAddPack});
}

} // namespace bz::app::scripting
