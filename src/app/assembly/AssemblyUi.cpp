#include "app/assembly/AssemblyUi.h"

#include "app/Format.h"
#include "core/Monitor.h"
#include "app/Operations.h"
#include "app/features/OperationEditors.h"
#include "app/shell/Shell.h"
#include "app/sketch/SketchEditor.h"
#include "assembly/AssemblyFile.h"
#include "assembly/Solver.h"
#include "core/Expression.h"
#include "core/I18n.h"
#include "core/PathText.h"
#include "document/BzcFile.h"
#include "document/DocumentTypes.h"
#include "kernel/AutoDrop.h"
#include "kernel/ComponentPattern.h"
#include "platform/FileDialogs.h"
#include "platform/Paths.h"
#include "ui/Menus.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <array>
#include <functional>
#include <cmath>
#include <format>

namespace bz::app {
namespace {

using assembly::Component;
using assembly::Placement;
using assembly::Rotation;

constexpr float kDragStartPixels = 4.0f;

Session* ActiveAssembly(AppContext& app) {
    Session* s = app.Doc();
    return s && s->assembly && !app.workspace.ShowsHome() ? s : nullptr;
}

/// Leaves whatever is being edited in the active document.
void StopEditing(AppContext& app) {
    Session* s = app.Doc();
    if (!s) return;
    if (OperationEditor* open = app.ops.Active()) open->Cancel(app, *s);
    if (s->mode == EditMode::Sketch) app.sketch.Finish(app, *s);
    if (s->mode == EditMode::PickPlane) s->mode = EditMode::Model;
}

/// A file name from a designation ("Болт М10×40 ГОСТ 7798-70").
std::string SafeFileName(std::string name) {
    for (char& ch : name)
        if (std::string_view("/\\:*?\"<>|").find(ch) != std::string_view::npos) ch = '_';
    return name;
}

/// "Кронштейн" for a part file: its name in the part, or the file name.
std::string BaseName(const kernel::AssemblyModel& geometry, ObjectId id, const std::filesystem::path& file) {
    if (const doc::Document* part = geometry.PartOf(id); part && !part->info.name.empty()) return part->info.name;
    const std::u8string stem = file.stem().u8string();
    return {stem.begin(), stem.end()};
}

double ParseLength(const std::string& text, bool& ok) {
    const Expected<double> v = EvaluateExpression(text, Quantity::Length, UnitSystem{}, {});
    ok = static_cast<bool>(v);
    return ok ? *v : 0.0;
}

/// Rotates a component by `angle` about the assembly axis `axis` through
/// the centre of its box (a quarter turn keeps it where it was).
void RotateComponent(AssemblyState& st, Component& c, Vec3 axis, double angle) {
    Vec3 pivot = c.placement.translation;
    if (const auto box = st.geometry.ComponentBounds(c.id)) pivot = (box->min + box->max) * 0.5;
    const Rotation r = Rotation::AxisAngle(axis, angle);
    c.placement.rotation = (r * c.placement.rotation).Orthonormalized();
    c.placement.translation = r * (c.placement.translation - pivot) + pivot;
    st.assembly.Touch();
}

void LabelText(ui::Ui& ui, const std::string& text, const Color& color) {
    ui::FontScope font(ui.fonts.bodySmall);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(color));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

/// A text field committing a component property as one undo step.
void ComponentText(AppContext& app, AssemblyState& st, Component& c, const char* label, const char* id,
                   std::string assembly::ComponentProperties::*field, const std::string& fromPart, float width) {
    ui::Ui& ui = app.ui;
    ui::FieldLabel(ui, label);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRect(p, {p.x + width, p.y + ui.S(28)}, ui::ToU32(ui.C().fieldBorder), ui.S(5));
    const std::string hint = fromPart.empty() ? std::string(Tr("Не задано")) : fromPart;
    if (ui::CommitInput(ui, id, c.properties.*field, app.shell.edit, width, false, hint.c_str())) {
        st.Checkpoint(N_("Свойства компонента"));
        c.properties.*field = app.shell.edit.text;
        st.assembly.Touch();
    }
    ImGui::Dummy({0, ui.S(8)});
}

void DrawComponentProperties(AppContext& app, Session& session, Component& c, float width) {
    ui::Ui& ui = app.ui;
    AssemblyState& st = *session.assembly;
    const doc::Document* part = st.geometry.PartOf(c.id);

    ui::FieldLabel(ui, Tr("Имя в дереве"));
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRect(p, {p.x + width, p.y + ui.S(28)}, ui::ToU32(ui.C().fieldBorder), ui.S(5));
        if (ui::CommitInput(ui, "##c-name", c.name, app.shell.edit, width, false) && !app.shell.edit.text.empty()) {
            st.Checkpoint(N_("Переименование"));
            c.name = app.shell.edit.text;
            st.assembly.Touch();
        }
        ImGui::Dummy({0, ui.S(8)});
    }
    LabelText(ui, Trf("Файл: {}", c.path), ui.C().textSecondary);
    if (const std::string problem = st.geometry.ProblemOf(c.id); !problem.empty()) LabelText(ui, Tr(problem), ui.C().danger);
    ImGui::Dummy({0, ui.S(8)});

    ComponentText(app, st, c, Tr("Обозначение"), "##c-designation", &assembly::ComponentProperties::designation,
                  part ? part->info.designation : std::string{}, width);
    ComponentText(app, st, c, Tr("Наименование"), "##c-pname", &assembly::ComponentProperties::name,
                  part ? part->info.name : std::string{}, width);
    ComponentText(app, st, c, Tr("Материал"), "##c-material", &assembly::ComponentProperties::material,
                  part ? part->info.material : std::string{}, width);
    ui::FieldLabel(ui, Tr("Количество"));
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRect(p, {p.x + width, p.y + ui.S(28)}, ui::ToU32(ui.C().fieldBorder), ui.S(5));
        if (ui::CommitInput(ui, "##c-qty", std::to_string(c.properties.quantity), app.shell.edit, width, true)) {
            const int q = std::atoi(app.shell.edit.text.c_str());
            if (q >= 1) {
                st.Checkpoint(N_("Свойства компонента"));
                c.properties.quantity = q;
                st.assembly.Touch();
            } else {
                app.notes.Error(Tr("Количество — целое число от 1"));
            }
        }
        ImGui::Dummy({0, ui.S(12)});
    }

    // Position: the part's origin in the assembly.
    ui::FieldLabel(ui, Tr("Положение начала детали, мм"));
    const float third = (width - ui.S(8)) / 3;
    const char* axes[] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; ++i) {
        double& value = i == 0 ? c.placement.translation.x : i == 1 ? c.placement.translation.y : c.placement.translation.z;
        if (i > 0) ImGui::SameLine(0, ui.S(4));
        ImGui::BeginGroup();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRect(p, {p.x + third, p.y + ui.S(28)}, ui::ToU32(ui.C().fieldBorder), ui.S(5));
        const std::string id = std::format("##c-pos{}", i);
        if (ui::CommitInput(ui, id.c_str(), FormatNumber(value, 3), app.shell.edit, third, true, axes[i])) {
            bool ok = false;
            const double v = ParseLength(app.shell.edit.text, ok);
            if (!ok) {
                app.notes.Error(Tr("Не число"));
            } else if (c.grounded) {
                app.notes.Info(Tr("Компонент закреплён — сначала освободите его"));
            } else {
                st.Checkpoint(N_("Перемещение компонента"));
                value = v;
                st.assembly.Touch();
            }
        }
        ImGui::EndGroup();
    }
    ImGui::Dummy({0, ui.S(10)});
    ui::FieldLabel(ui, Tr("Повернуть на 90°"));
    {
        const float bw = (width - ui.S(8)) / 3;
        const struct {
            const char* id;
            const char* label;
            Vec3        axis;
        } turns[] = {{"##rx", "X", {1, 0, 0}}, {"##ry", "Y", {0, 1, 0}}, {"##rz", "Z", {0, 0, 1}}};
        for (int i = 0; i < 3; ++i) {
            if (i > 0) ImGui::SameLine(0, ui.S(4));
            if (ui::SecondaryButton(ui, turns[i].id, turns[i].label, nullptr, {bw, ui.S(30)}, !c.grounded)) {
                st.Checkpoint(N_("Поворот компонента"));
                RotateComponent(st, c, turns[i].axis, kPi / 2);
            }
        }
    }
    ImGui::Dummy({0, ui.S(10)});
    static constexpr std::array<const char*, 2> fixed{N_("Свободен"), N_("Закреплён")};
    const int g = ui::Segmented(ui, "grounded", fixed, c.grounded ? 1 : 0, 2, width);
    if (g != (c.grounded ? 1 : 0)) {
        st.Checkpoint(g ? N_("Закрепить") : N_("Освободить"));
        c.grounded = g == 1;
        st.assembly.Touch();
    }
    ImGui::Dummy({0, ui.S(6)});
    if (!c.grounded) {
        const auto f = st.freedom.find(c.id);
        if (f != st.freedom.end())
            LabelText(ui, Trf("Свободы: перемещений {}, вращений {}", f->second.translations, f->second.rotations),
                      ui.C().textSecondary);
    }
}

kernel::AssemblyPick PickFor(assembly::ConstraintType type) {
    switch (type) {
        case assembly::ConstraintType::Insert:
        case assembly::ConstraintType::Rotational: return kernel::AssemblyPick::Circle;
        case assembly::ConstraintType::AxisMate: return kernel::AssemblyPick::Axis;
        default:                                 return kernel::AssemblyPick::Face;
    }
}

/// Why `g` cannot be used for this constraint (with `first` already
/// picked, if any), or nullptr when it can.
const char* Unfit(assembly::ConstraintType type, const assembly::GeometryRef& g, const assembly::GeometryRef* first) {
    using assembly::GeometryKind;
    switch (type) {
        case assembly::ConstraintType::Angle:
            if (g.direction.Length() < 0.5) return N_("Для угла нужна плоская грань или ось");
            break;
        case assembly::ConstraintType::Tangent:
            if (g.kind != GeometryKind::Plane && g.kind != GeometryKind::Cylinder && g.kind != GeometryKind::Sphere)
                return N_("Для касания нужна плоская, цилиндрическая или сферическая грань");
            if (first && first->kind == GeometryKind::Plane && g.kind == GeometryKind::Plane)
                return N_("Две плоскости не касаются — выберите цилиндр или сферу");
            break;
        case assembly::ConstraintType::Slider:
        case assembly::ConstraintType::Cylindrical:
        case assembly::ConstraintType::Planar:
        case assembly::ConstraintType::Rigid:
            if (g.direction.Length() < 0.5) return N_("Нужна плоская или круглая грань: от неё берётся ось соединения");
            break;
        case assembly::ConstraintType::Mate:
            if (first && first->kind == GeometryKind::Plane && g.HasAxis())
                return nullptr;   // an axis in a plane
            if (first && first->HasAxis() != g.HasAxis() && first->kind != GeometryKind::Plane && g.kind != GeometryKind::Plane)
                return N_("Совмещаются две плоскости или две оси");
            break;
        default: break;
    }
    return nullptr;
}

const char* PickPrompt(assembly::ConstraintType type, int picked) {
    switch (type) {
        case assembly::ConstraintType::Insert:
            return picked == 0 ? N_("Вставка: щёлкните у круглой кромки первого компонента (отверстие, торец)")
                               : N_("Вставка: щёлкните у круглой кромки второго компонента");
        case assembly::ConstraintType::AxisMate:
            return picked == 0 ? N_("Совпадение осей: выберите цилиндрическую грань первого компонента")
                               : N_("Совпадение осей: выберите цилиндрическую грань второго компонента");
        case assembly::ConstraintType::Angle:
            return picked == 0 ? N_("Угол: выберите грань первого компонента") : N_("Угол: выберите грань второго компонента");
        case assembly::ConstraintType::Tangent:
            return picked == 0 ? N_("Касание: выберите грань первого компонента")
                               : N_("Касание: выберите грань второго компонента");
        case assembly::ConstraintType::Mate:
            return picked == 0 ? N_("Совмещение: выберите грань первого компонента")
                               : N_("Совмещение: выберите грань второго компонента");
        case assembly::ConstraintType::Rotational:
            return picked == 0 ? N_("Вращение: щёлкните у круглой кромки неподвижного компонента")
                               : N_("Вращение: щёлкните у круглой кромки вращающегося компонента");
        case assembly::ConstraintType::Slider:
        case assembly::ConstraintType::Cylindrical:
        case assembly::ConstraintType::Planar:
        case assembly::ConstraintType::Ball:
        case assembly::ConstraintType::Rigid:
            return picked == 0 ? N_("Соединение: выберите грань первого компонента (вдоль её оси или нормали — движение)")
                               : N_("Соединение: выберите грань второго компонента");
    }
    return "";
}

void Prompt(ui::Ui& ui, const ViewportFrame& vf, const char* text) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = ui.fonts.medium;
    const ImVec2 ts = font->CalcTextSizeA(ui.S(13), FLT_MAX, 0, text);
    const ImVec2 p{(vf.min.x + vf.max.x - ts.x) / 2 - ui.S(14), vf.min.y + ui.S(14)};
    dl->AddRectFilled(p, {p.x + ts.x + ui.S(28), p.y + ui.S(34)}, ui::ToU32(ui.C().panel.WithAlpha(0.94f)), ui.S(17));
    dl->AddText(font, ui.S(13), {p.x + ui.S(14), p.y + ui.S(9)}, ui::ToU32(ui.C().textSecondary), text);
}

/// Value and options of a constraint (being made or chosen in the tree).
/// Returns true when something changed (`before` is called first, once).
bool ConstraintOptions(AppContext& app, assembly::Constraint& c, float width, const std::function<void()>& before) {
    ui::Ui& ui = app.ui;
    bool changed = false;
    const bool angle = c.type == assembly::ConstraintType::Angle;
    const bool noValue = c.type == assembly::ConstraintType::AxisMate || c.type == assembly::ConstraintType::Slider ||
                         c.type == assembly::ConstraintType::Cylindrical || c.type == assembly::ConstraintType::Ball;
    if (!noValue) {
        ui::FieldLabel(ui, angle ? Tr("Угол, °") : assembly::IsJoint(c.type) ? Tr("Смещение вдоль оси, мм") : Tr("Зазор, мм"));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRect(p, {p.x + width, p.y + ui.S(28)}, ui::ToU32(ui.C().fieldBorder), ui.S(5));
        const std::string shown = FormatNumber(angle ? c.value * 180.0 / kPi : c.value, 3);
        if (ui::CommitInput(ui, "##con-value", shown, app.shell.edit, width, true)) {
            UnitSystem units;
            units.angle = AngleUnit::Degree;
            const Expected<double> v = EvaluateExpression(app.shell.edit.text, angle ? Quantity::Angle : Quantity::Length, units, {});
            if (!v) {
                app.notes.Error(Tr("Не число"));
            } else {
                before();
                c.value = *v;
                changed = true;
            }
        }
        ImGui::Dummy({0, ui.S(10)});
    }
    const char* option = nullptr;
    switch (c.type) {
        case assembly::ConstraintType::Mate:    option = N_("Заподлицо (грани смотрят в одну сторону)"); break;
        case assembly::ConstraintType::Insert:  option = N_("Соосно в одну сторону (не навстречу)"); break;
        case assembly::ConstraintType::Tangent: option = N_("Внутреннее касание"); break;
        case assembly::ConstraintType::Ball:    break;
        default:
            if (assembly::IsJoint(c.type)) option = N_("Оси в одну сторону (не навстречу)");
            break;
    }
    if (option) {
        static constexpr std::array<const char*, 2> labels{N_("Нет"), N_("Да")};
        ui::FieldLabel(ui, Tr(option));
        const int v = ui::Segmented(ui, "con-flip", labels, c.flipped ? 1 : 0, 2, width);
        if (v != (c.flipped ? 1 : 0)) {
            before();
            c.flipped = v == 1;
            changed = true;
        }
        ImGui::Dummy({0, ui.S(10)});
    }
    return changed;
}

/// One number field; true with the new value in `out` when committed.
bool NumberField(AppContext& app, const char* id, const char* label, double shown, bool angle, float width, double& out) {
    ui::Ui& ui = app.ui;
    ui::FieldLabel(ui, label);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRect(p, {p.x + width, p.y + ui.S(28)}, ui::ToU32(ui.C().fieldBorder), ui.S(5));
    bool done = false;
    if (ui::CommitInput(ui, id, FormatNumber(angle ? shown * 180.0 / kPi : shown, 3), app.shell.edit, width, true)) {
        UnitSystem units;
        units.angle = AngleUnit::Degree;
        const Expected<double> v = EvaluateExpression(app.shell.edit.text, angle ? Quantity::Angle : Quantity::Length, units, {});
        if (!v) app.notes.Error(Tr("Не число"));
        else {
            out  = *v;
            done = true;
        }
    }
    ImGui::Dummy({0, ui.S(10)});
    return done;
}

/// Position, lock and limits of a joint with one coordinate (chosen in the tree).
void JointMotionOptions(AppContext& app, AssemblyState& st, assembly::Constraint& c, float width) {
    const assembly::JointMotion motion = assembly::MotionOf(c.type);
    if (motion == assembly::JointMotion::None) return;
    ui::Ui& ui = app.ui;
    const bool angle = motion == assembly::JointMotion::Rotation;
    const double now = assembly::JointPosition(st.assembly, c).value_or(0.0);
    double v = 0.0;
    if (NumberField(app, "##joint-pos", angle ? Tr("Положение, °") : Tr("Положение, мм"), now, angle, width, v)) {
        st.Checkpoint(N_("Положение соединения"));
        assembly::DriveJoint(st.assembly, c.id, v);
    }
    static constexpr std::array<const char*, 2> labels{N_("Нет"), N_("Да")};
    ui::FieldLabel(ui, Tr("Заблокировать в этом положении"));
    if (const int k = ui::Segmented(ui, "joint-lock", labels, c.locked ? 1 : 0, 2, width); k != (c.locked ? 1 : 0)) {
        st.Checkpoint(N_("Изменение зависимости"));
        c.locked   = k == 1;
        c.position = now;
        st.assembly.Touch();
    }
    ImGui::Dummy({0, ui.S(10)});
    ui::FieldLabel(ui, Tr("Пределы"));
    if (const int k = ui::Segmented(ui, "joint-limits", labels, c.limits.enabled ? 1 : 0, 2, width);
        k != (c.limits.enabled ? 1 : 0)) {
        st.Checkpoint(N_("Изменение зависимости"));
        c.limits.enabled = k == 1;
        if (c.limits.enabled) {
            const double span = angle ? kPi / 4 : 10.0;
            c.limits.lower = now - span;
            c.limits.upper = now + span;
        }
        st.assembly.Touch();
    }
    ImGui::Dummy({0, ui.S(10)});
    if (!c.limits.enabled) return;
    if (NumberField(app, "##joint-lo", angle ? Tr("От, °") : Tr("От, мм"), c.limits.lower, angle, width, v)) {
        st.Checkpoint(N_("Изменение зависимости"));
        c.limits.lower = v;
        st.assembly.Touch();
    }
    if (NumberField(app, "##joint-hi", angle ? Tr("До, °") : Tr("До, мм"), c.limits.upper, angle, width, v)) {
        st.Checkpoint(N_("Изменение зависимости"));
        c.limits.upper = v;
        st.assembly.Touch();
    }
    if (c.limits.upper < c.limits.lower) LabelText(ui, Tr("«До» меньше «От» — пределы не действуют как задумано"), ui.C().danger);
}

} // namespace

kernel::AssemblyPick ConstraintPick(assembly::ConstraintType type) { return PickFor(type); }

const char* ConstraintUnfit(assembly::ConstraintType type, const assembly::GeometryRef& g, const assembly::GeometryRef* first) {
    return Unfit(type, g, first);
}

bool InAssembly(AppContext& app) { return ActiveAssembly(app) != nullptr; }

void NewAssembly(AppContext& app) {
    StopEditing(app);
    app.workspace.NewAssembly();
    app.shell.ribbonTab = RibbonTab::Model;
    app.shell.leftTab = LeftTab::Tree;
}

ObjectId PlaceFile(AppContext& app, Session& session, const std::filesystem::path& file, bool checkpoint) {
    AssemblyState& st = *session.assembly;
    if (!session.path.empty() && SameFile(session.path, file)) {
        app.notes.Error(Tr("Сборку нельзя вставить в саму себя"));
        return {};
    }
    if (Expected<std::string> type = doc::ReadDocumentType(file); !type || *type != doc::kPartType) {
        app.notes.Error(Trf("«{}» — не деталь: {}", ToUtf8(file.filename()),
                            type ? Tr("подсборки появятся позже") : Tr(type.GetError().message)));
        return {};
    }
    if (checkpoint) st.Checkpoint(N_("Вставка компонента"));
    // Where the rest ends, before the new one is there.
    st.geometry.Update(st.assembly, session.path);
    const std::optional<kernel::Bounds> before = session.model.BoundingBox();

    Component& c = st.assembly.AddComponent(assembly::RelativePartPath(session.path, file), "Деталь");
    const ObjectId id = c.id;
    st.geometry.Update(st.assembly, session.path, &st.problems);
    Component& placed = *st.assembly.FindComponent(id);
    {
        const std::string base = BaseName(st.geometry, id, file);
        placed.name.clear();
        for (int n = 1;; ++n) {
            std::string name = std::format("{}:{}", base, n);
            if (std::none_of(st.assembly.components.begin(), st.assembly.components.end(),
                             [&](const Component& o) { return o.name == name; })) {
                placed.name = std::move(name);
                break;
            }
        }
    }
    // Beside everything else, so it can be seen and dragged into place.
    if (before && !placed.grounded)
        if (const auto box = st.geometry.ComponentBounds(id)) placed.placement.translation.x += before->max.x - box->min.x + 20.0;
    st.assembly.Touch();
    st.selected = id;
    app.shell.keepModelInView = true;
    return id;
}

void PlaceComponent(AppContext& app) {
    Session* s = ActiveAssembly(app);
    if (!s) return;
    if (const std::string problem = platform::FileDialogProblem(); !problem.empty()) {
        app.notes.Error(problem);
        return;
    }
    const std::filesystem::path folder = !s->path.empty() ? s->path.parent_path()
                                       : !app.settings.recentFiles.empty() ? app.settings.recentFiles.front().parent_path()
                                                                           : platform::DocumentsDir();
    const auto file = platform::ShowOpenDialog(Tr("Вставить компонент"), {{Tr("Деталь BreezeCAD"), "*.bzc"}}, folder);
    if (!file) return;
    StopEditing(app);
    if (PlaceFile(app, *s, *file)) app.settings.AddRecent(*file);
}

namespace {

/// The file of a standard part next to the assembly, made when missing.
std::optional<std::filesystem::path> StandardPartFile(AppContext& app, const Session& session,
                                                      const doc::parts::Request& request) {
    const std::string designation = doc::parts::Designation(request);
    // Standard parts live in a folder next to the assembly (or among the
    // user's documents while it has no file): equal ones share one file.
    const std::filesystem::path base = session.path.empty() ? platform::DocumentsDir() / "BreezeCAD" : session.path.parent_path();
    const std::filesystem::path folder = base / PathFromUtf8("Стандартные изделия");
    const std::filesystem::path file = folder / PathFromUtf8(SafeFileName(designation) + ".bzc");
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        Expected<doc::parts::Made> made = doc::parts::MakePart(request);
        if (!made) {
            app.notes.Error(Tr(made.GetError().message));
            return std::nullopt;
        }
        std::filesystem::create_directories(folder, ec);
        if (const Status saved = doc::SaveBzc(made->doc, file, "BreezeCAD"); !saved) {
            app.notes.Error(Trf("Не удалось сохранить деталь: {}", Tr(saved.GetError().message)));
            return std::nullopt;
        }
    }
    return file;
}

/// Standard of `kind` from the same family as `like` (ГОСТ with ГОСТ...).
std::string SameFamily(const doc::parts::StandardInfo& like, doc::parts::PartKind kind) {
    const auto all = doc::parts::StandardsOf(kind);
    for (const doc::parts::StandardInfo* s : all)
        if (doc::parts::FamilyOf(*s) == doc::parts::FamilyOf(like)) return std::string(s->id);
    // DIN screws take ISO washers and nuts.
    for (const doc::parts::StandardInfo* s : all)
        if (doc::parts::FamilyOf(*s) == doc::parts::Family::Iso) return std::string(s->id);
    return all.empty() ? std::string{} : std::string(all.front()->id);
}

/// AutoDrop onto `rim`: plans, makes the part files, adds the components
/// and their Вставка constraints as one undo step.
void DropOnRim(AppContext& app, Session& s, const assembly::GeometryRef& rim) {
    AssemblyState& st = *s.assembly;
    const auto& d = st.dropping;
    Expected<kernel::AutoDropPlan> plan = kernel::PlanAutoDrop(st.geometry, st.assembly, rim, {d.screw, d.washer, d.nut});
    if (!plan) {
        app.notes.Error(Tr(plan.GetError().message));
        return;
    }
    std::vector<std::filesystem::path> files;
    for (const kernel::DropItem& item : plan->items) {
        auto file = StandardPartFile(app, s, item.request);
        if (!file) return;
        files.push_back(std::move(*file));
    }
    st.Checkpoint(N_("Вставка стандартного изделия"));
    std::vector<ObjectId> ids;
    for (std::size_t i = 0; i < plan->items.size(); ++i) {
        const kernel::DropItem& item = plan->items[i];
        const ObjectId id = PlaceFile(app, s, files[i], false);
        if (!id) return;
        Component& c = *st.assembly.FindComponent(id);
        c.placement = item.placement;
        c.grounded = false;
        if (c.properties.designation.empty()) c.properties.designation = doc::parts::Designation(item.request);
        assembly::Constraint k = item.insert;
        k.first.component = item.seatOn >= 0 ? ids[std::size_t(item.seatOn)] : item.insert.first.component;
        k.second.component = id;
        st.assembly.AddConstraint(k);
        ids.push_back(id);
    }
    st.assembly.Touch();
    const std::size_t screw = d.washer.empty() ? 0 : 1;   // the plan: [washer,] screw[, nut]
    st.selected = ids[screw];
    // More holes like this one: offer to fill them all at once.
    const int holes = int(st.geometry.SameHoles(st.assembly, rim).size());
    st.offer = holes > 1 ? AssemblyState::Offer{ids[screw], holes} : AssemblyState::Offer{};
    app.notes.Success(Trf("Вставлено: {} (пакет {} мм)", doc::parts::Designation(plan->items[screw].request),
                          FormatNumber(plan->stack, 1)));
}

/// Parts made by the library ("Стандартные изделия" next to the assembly)
/// come along with the component a pattern repeats; other parts do not.
bool IsStandardPart(const Component& c) {
    return c.path.starts_with("Стандартные изделия/") || c.path.find("/Стандартные изделия/") != std::string::npos;
}

/// A pattern of `component` with its group: into the holes like the one it
/// sits in when there are such, otherwise a row. Selected when made.
bool MakePattern(AppContext& app, Session& s, ObjectId component) {
    AssemblyState& st = *s.assembly;
    assembly::ComponentPattern p;
    p.seed = kernel::PatternGroup(st.assembly, component, IsStandardPart);
    if (p.seed.empty()) return false;
    if (auto rim = kernel::SeatHoleOf(st.assembly, p.seed); rim && st.geometry.SameHoles(st.assembly, *rim).size() > 1) {
        p.kind = assembly::PatternKind::Holes;
        p.rim = *rim;
    } else {
        p.kind = assembly::PatternKind::Rectangular;
        p.count1 = 3;
        if (const auto box = st.geometry.ComponentBounds(component))
            p.spacing1 = std::max(1.0, std::ceil((box->max.x - box->min.x) * 1.5));
    }
    const auto spots = kernel::PatternSpots(st.geometry, st.assembly, p);
    if (!spots) {
        app.notes.Error(Tr(spots.GetError().message));
        return false;
    }
    st.Checkpoint(N_("Массив компонентов"));
    const ObjectId id = st.assembly.AddPattern(p).id;
    const Expected<int> made = kernel::ApplyPattern(st.geometry, st.assembly, id);
    if (!made) {
        st.assembly.RemovePattern(id);
        app.notes.Error(Tr(made.GetError().message));
        return false;
    }
    st.selected = id;
    st.offer = {};
    app.notes.Success(Trf("{}: экземпляров {}", st.assembly.FindPattern(id)->name, *made + 1));
    return true;
}

/// A number field committing through `apply` (an expression in mm or °).
void NumberField(AppContext& app, const char* label, const char* id, double value, bool angle, float width,
                 const std::function<void(double)>& apply) {
    ui::Ui& ui = app.ui;
    ui::FieldLabel(ui, label);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRect(p, {p.x + width, p.y + ui.S(28)}, ui::ToU32(ui.C().fieldBorder), ui.S(5));
    if (ui::CommitInput(ui, id, FormatNumber(angle ? value * 180.0 / kPi : value, 3), app.shell.edit, width, true)) {
        UnitSystem units;
        units.angle = AngleUnit::Degree;
        const Expected<double> v = EvaluateExpression(app.shell.edit.text, angle ? Quantity::Angle : Quantity::Length, units, {});
        if (v) apply(*v);
        else app.notes.Error(Tr("Не число"));
    }
    ImGui::Dummy({0, ui.S(8)});
}

/// Changes pattern `id` by `edit` and rebuilds it (one undo step); when the
/// new pattern has nowhere to go it stays as it was.
void EditPattern(AppContext& app, AssemblyState& st, ObjectId id, const std::function<void(assembly::ComponentPattern&)>& edit) {
    const assembly::ComponentPattern* p = st.assembly.FindPattern(id);
    if (!p) return;
    assembly::ComponentPattern next = *p;
    edit(next);
    if (const auto spots = kernel::PatternSpots(st.geometry, st.assembly, next); !spots) {
        app.notes.Error(Tr(spots.GetError().message));
        return;
    }
    st.Checkpoint(N_("Изменение массива"));
    *st.assembly.FindPattern(id) = std::move(next);
    if (const Expected<int> made = kernel::ApplyPattern(st.geometry, st.assembly, id); !made)
        app.notes.Error(Tr(made.GetError().message));
}

constexpr std::array<Vec3, 6> kDirections{Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1},
                                          Vec3{-1, 0, 0}, Vec3{0, -1, 0}, Vec3{0, 0, -1}};
constexpr std::array<const char*, 6> kDirectionNames{"X", "Y", "Z", "−X", "−Y", "−Z"};

int DirectionIndex(Vec3 d) {
    for (std::size_t i = 0; i < kDirections.size(); ++i)
        if ((d.Normalized() - kDirections[i]).Length() < 1e-6) return int(i);
    return -1;
}

void DrawPatternProperties(AppContext& app, Session& session, const assembly::ComponentPattern& pattern, float width) {
    ui::Ui& ui = app.ui;
    AssemblyState& st = *session.assembly;
    const ObjectId id = pattern.id;
    std::string seed;
    for (ObjectId c : pattern.seed)
        if (const Component* comp = st.assembly.FindComponent(c)) seed += (seed.empty() ? "" : ", ") + comp->name;
    LabelText(ui, Trf("Образец: {}", seed), ui.C().textSecondary);
    LabelText(ui, Trf("Экземпляров: {} (с образцом)", pattern.instances.size() + 1), ui.C().textSecondary);
    ImGui::Dummy({0, ui.S(8)});

    static constexpr std::array<const char*, 3> kinds{N_("Отверстия"), N_("Прямоуг."), N_("Круговой")};
    ui::FieldLabel(ui, Tr("Вид массива"));
    const auto rim = kernel::SeatHoleOf(st.assembly, pattern.seed);
    const int kind = ui::Segmented(ui, "pat-kind", kinds, int(pattern.kind), 3, width, rim ? 0 : 1);
    if (kind != int(pattern.kind))
        app.Later([&app, &st, id, kind, rim] {
            EditPattern(app, st, id, [&](assembly::ComponentPattern& p) {
                p.kind = assembly::PatternKind(kind);
                if (p.kind == assembly::PatternKind::Holes && rim) p.rim = *rim;
            });
        });
    ImGui::Dummy({0, ui.S(10)});

    const auto later = [&app, &st, id](std::function<void(assembly::ComponentPattern&)> edit) {
        app.Later([&app, &st, id, edit = std::move(edit)] { EditPattern(app, st, id, edit); });
    };
    const auto count = [&](const char* label, const char* fid, int value, int assembly::ComponentPattern::*member) {
        NumberField(app, label, fid, value, false, width, [&](double v) {
            const int n = int(std::lround(v));
            if (n < 1 || n > 500) {
                app.notes.Error(Tr("Количество — от 1 до 500"));
                return;
            }
            later([member, n](assembly::ComponentPattern& p) { p.*member = n; });
        });
    };
    switch (pattern.kind) {
        case assembly::PatternKind::Holes: {
            const Component* holder = st.assembly.FindComponent(pattern.rim.component);
            LabelText(ui, Trf("Во все отверстия ⌀{} мм на «{}», как у образца", FormatNumber(2 * pattern.rim.radius, 2),
                              holder ? holder->name : "?"),
                      ui.C().textMuted);
            break;
        }
        case assembly::PatternKind::Rectangular:
            for (int row = 0; row < 2; ++row) {
                const Vec3 dir = row ? pattern.direction2 : pattern.direction1;
                ui::FieldLabel(ui, row ? Tr("Направление 2") : Tr("Направление 1"));
                ImGui::PushID(row);
                const int was = DirectionIndex(dir);
                const int now = ui::Segmented(ui, "pat-dir", kDirectionNames, was, 6, width);
                if (now != was && now >= 0)
                    later([row, now](assembly::ComponentPattern& p) { (row ? p.direction2 : p.direction1) = kDirections[std::size_t(now)]; });
                ImGui::Dummy({0, ui.S(6)});
                count(Tr("Количество"), "##pat-count", row ? pattern.count2 : pattern.count1,
                      row ? &assembly::ComponentPattern::count2 : &assembly::ComponentPattern::count1);
                NumberField(app, Tr("Шаг, мм"), "##pat-step", row ? pattern.spacing2 : pattern.spacing1, false, width,
                            [&](double v) { later([row, v](assembly::ComponentPattern& p) { (row ? p.spacing2 : p.spacing1) = v; }); });
                ImGui::PopID();
            }
            break;
        case assembly::PatternKind::Circular: {
            const Component* owner = st.assembly.FindComponent(pattern.axis.component);
            LabelText(ui, owner ? Trf("Ось: {}", owner->name) : std::string(Tr("Ось: Z сборки через начало координат")),
                      ui.C().textSecondary);
            if (ui::SecondaryButton(ui, "##pat-axis", st.pickingAxis == id ? Tr("Щёлкните по круглой грани…") : Tr("Указать ось"),
                                    nullptr, {width, ui.S(28)}))
                st.pickingAxis = st.pickingAxis == id ? ObjectId{} : id;
            ImGui::Dummy({0, ui.S(8)});
            count(Tr("Количество"), "##pat-count", pattern.count, &assembly::ComponentPattern::count);
            NumberField(app, Tr("Угол, ° (360 — равномерно по кругу)"), "##pat-angle", pattern.angle, true, width,
                        [&](double v) { later([v](assembly::ComponentPattern& p) { p.angle = v; }); });
            break;
        }
    }
    ImGui::Dummy({0, ui.S(6)});
    if (ui::SecondaryButton(ui, "##pat-delete", Tr("Удалить массив"), nullptr, {width, ui.S(28)}))
        app.Later([&app, &session] { AssemblyDelete(app, session); });
}

/// The offer after AutoDrop, over the top of the view.
void DrawOffer(AppContext& app, Session& session, const ViewportFrame& vf) {
    AssemblyState& st = *session.assembly;
    if (!st.offer.component) return;
    const Component* c = st.assembly.FindComponent(st.offer.component);
    if (!c || st.assembly.PatternOfInstance(c->id) ||
        std::any_of(st.assembly.patterns.begin(), st.assembly.patterns.end(), [&](const auto& p) { return p.Contains(c->id); })) {
        st.offer = {};
        return;
    }
    ui::Ui& ui = app.ui;
    const std::string text = Trf("Ещё {} таких отверстий", st.offer.holes - 1);
    const std::string yes = Tr("Во все такие отверстия");
    const std::string no = Tr("Не нужно");
    ImFont* font = ui.fonts.medium;
    const float tw = font->CalcTextSizeA(ui.S(13), FLT_MAX, 0, text.c_str()).x;
    const float bw = font->CalcTextSizeA(ui.S(13), FLT_MAX, 0, yes.c_str()).x + ui.S(24);
    const float nw = font->CalcTextSizeA(ui.S(13), FLT_MAX, 0, no.c_str()).x + ui.S(24);
    const float w = ui.S(14) + tw + ui.S(14) + bw + ui.S(4) + nw + ui.S(8);
    const float h = ui.S(44);
    ImGui::SetNextWindowPos({(vf.min.x + vf.max.x - w) / 2, vf.min.y + ui.S(56)});
    ImGui::SetNextWindowSize({w, h});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui.S(10));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::ToVec4(ui.C().panel.WithAlpha(0.97f)));
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##assembly-offer", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 o = ImGui::GetWindowPos();
        dl->AddText(font, ui.S(13), {o.x + ui.S(14), o.y + (h - ui.S(13)) / 2}, ui::ToU32(ui.C().textSecondary), text.c_str());
        ImGui::SetCursorPos({ui.S(14) + tw + ui.S(14), ui.S(8)});
        if (ui::PrimaryButton(ui, "##offer-yes", yes.c_str(), nullptr, {bw, ui.S(28)})) {
            const ObjectId screw = st.offer.component;
            st.dropping = {};
            app.Later([&app, &session, screw] { MakePattern(app, session, screw); });
        }
        ImGui::SameLine(0, ui.S(4));
        if (ui::SecondaryButton(ui, "##offer-no", no.c_str(), nullptr, {nw, ui.S(28)})) st.offer = {};
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

} // namespace

std::optional<std::filesystem::path> StandardPartFileOf(AppContext& app, const Session& session, const doc::parts::Request& request) {
    return StandardPartFile(app, session, request);
}

bool CanPattern(AppContext& app) {
    Session* s = ActiveAssembly(app);
    if (!s) return false;
    const assembly::Assembly& a = s->assembly->assembly;
    return a.FindComponent(s->assembly->selected) || a.FindPattern(s->assembly->selected);
}

void StartPattern(AppContext& app) {
    Session* s = ActiveAssembly(app);
    if (!s) return;
    StopEditing(app);
    AssemblyState& st = *s->assembly;
    if (st.assembly.FindPattern(st.selected)) return;   // already one: its panel edits it
    if (const assembly::ComponentPattern* p = st.assembly.PatternOfInstance(st.selected)) {
        st.selected = p->id;
        return;
    }
    if (!st.assembly.FindComponent(st.selected)) {
        app.notes.Info(Tr("Выберите компонент, который нужно повторить"));
        return;
    }
    MakePattern(app, *s, st.selected);
}

bool CanAutoDrop(const doc::parts::StandardInfo& standard) {
    using doc::parts::PartKind;
    return standard.kind == PartKind::HexBolt || standard.kind == PartKind::SocketScrew || standard.kind == PartKind::PanScrew;
}

bool StartAutoDrop(AppContext& app, const doc::parts::Request& request, bool washer, bool nut) {
    Session* s = ActiveAssembly(app);
    const doc::parts::StandardInfo* screw = doc::parts::FindStandard(request.standard);
    if (!s || !screw || !CanAutoDrop(*screw)) return false;
    StopEditing(app);
    AssemblyState& st = *s->assembly;
    st.creating = {};
    st.drag = {};
    st.dropping = {true, request.standard, washer ? SameFamily(*screw, doc::parts::PartKind::FlatWasher) : std::string{},
                   nut ? SameFamily(*screw, doc::parts::PartKind::HexNut) : std::string{}};
    return true;
}

bool PlaceStandardPart(AppContext& app, const doc::parts::Request& request) {
    Session* s = ActiveAssembly(app);
    if (!s) return false;
    const std::string designation = doc::parts::Designation(request);
    const auto file = StandardPartFile(app, *s, request);
    if (!file) return false;
    StopEditing(app);
    const ObjectId id = PlaceFile(app, *s, *file);
    if (!id) return false;
    // A standard part is listed by its designation.
    if (Component* c = s->assembly->assembly.FindComponent(id)) {
        if (c->properties.designation.empty()) c->properties.designation = designation;
    }
    app.notes.Success(Trf("Вставлено: {}", designation));
    return true;
}

void StartConstraint(AppContext& app, assembly::ConstraintType type) {
    Session* s = ActiveAssembly(app);
    if (!s) return;
    StopEditing(app);
    AssemblyState& st = *s->assembly;
    st.creating = {};
    st.creating.active = true;
    st.creating.constraint.type = type;
    if (type == assembly::ConstraintType::Angle) st.creating.constraint.value = kPi / 2;
    st.drag = {};
    app.shell.leftTab = LeftTab::Tree;
}

void DrawConstraintPanel(AppContext& app, Session& session, float width) {
    ui::Ui& ui = app.ui;
    AssemblyState& st = *session.assembly;
    auto& creating = st.creating;
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(40);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const std::string title = Tr(std::string(assembly::ConstraintTitle(creating.constraint.type)));
        dl->AddText(ui.fonts.strong, ui.S(13), {p.x + ui.S(14), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(ui.C().text),
                    title.c_str());
        dl->AddLine({p.x, p.y + h - 1}, {p.x + width, p.y + h - 1}, ui::ToU32(ui.C().dividerSoft));
        ImGui::Dummy({width, h});
    }
    const float inner = width - ui.S(28);
    ImGui::SetCursorPos({ui.S(14), ImGui::GetCursorPosY() + ui.S(14)});
    ImGui::BeginGroup();
    LabelText(ui, Tr(PickPrompt(creating.constraint.type, creating.picked)), ui.C().textSecondary);
    if (creating.picked == 1)
        if (const Component* c = st.assembly.FindComponent(creating.constraint.first.component))
            LabelText(ui, Trf("Первый: {}", c->name), ui.C().text);
    ImGui::Dummy({0, ui.S(10)});
    ConstraintOptions(app, creating.constraint, inner, [] {});
    if (ui::SecondaryButton(ui, "##con-cancel", Tr("Отмена"), "Esc", {inner, ui.S(32)})) creating = {};
    ImGui::EndGroup();
}

void ToggleGrounded(AppContext& app) {
    Session* s = ActiveAssembly(app);
    if (!s) return;
    AssemblyState& st = *s->assembly;
    Component* c = st.assembly.FindComponent(st.selected);
    if (!c) return;
    st.Checkpoint(c->grounded ? N_("Освободить") : N_("Закрепить"));
    c->grounded = !c->grounded;
    st.assembly.Touch();
}

bool AssemblyEscape(AppContext& app, Session& session) {
    (void)app;
    AssemblyState& st = *session.assembly;
    if (st.pickingAxis) {
        st.pickingAxis = {};
        return true;
    }
    if (st.offer.component) {
        st.offer = {};
        return true;
    }
    if (st.dropping.active) {
        st.dropping = {};
        return true;
    }
    if (st.creating.active) {
        // Back one step: the first pick, then the command.
        if (st.creating.picked > 0) st.creating.picked = 0;
        else st.creating = {};
        return true;
    }
    if (st.drag.moving || st.drag.armed) {
        if (st.drag.moving)
            if (Component* c = st.assembly.FindComponent(st.drag.component)) {
                c->placement = st.drag.start;
                st.assembly.Touch();
            }
        st.drag = {};
        return true;
    }
    if (st.selected) {
        st.selected = {};
        return true;
    }
    return false;
}

bool AssemblyDelete(AppContext& app, Session& session) {
    (void)app;
    AssemblyState& st = *session.assembly;
    if (st.assembly.FindPattern(st.selected)) {
        st.Checkpoint(N_("Удаление массива"));
        st.assembly.RemovePattern(st.selected);
    } else if (st.assembly.FindComponent(st.selected)) {
        st.Checkpoint(N_("Удаление компонента"));
        st.assembly.RemoveComponent(st.selected);
    } else if (st.assembly.FindConstraint(st.selected)) {
        st.Checkpoint(N_("Удаление зависимости"));
        st.assembly.RemoveConstraint(st.selected);
    } else {
        return false;
    }
    st.selected = {};
    return true;
}

// --- tree ---------------------------------------------------------------------

void DrawAssemblyProperties(AppContext& app, Session& session, float width) {
    ui::Ui& ui = app.ui;
    AssemblyState& st = *session.assembly;
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(40);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        Component* c = st.assembly.FindComponent(st.selected);
        const assembly::Constraint* con = st.assembly.FindConstraint(st.selected);
        const assembly::ComponentPattern* pat = st.assembly.FindPattern(st.selected);
        const std::string title = c     ? Trf("Компонент {}", c->name)
                                  : con ? (assembly::IsJoint(con->type) ? Trf("Соединение {}", con->name)
                                                                        : Trf("Зависимость {}", con->name))
                                  : pat ? pat->name
                                        : std::string(Tr("Свойства сборки"));
        dl->AddText(ui.fonts.strong, ui.S(13), {p.x + ui.S(14), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(ui.C().text),
                    title.c_str());
        dl->AddLine({p.x, p.y + h - 1}, {p.x + width, p.y + h - 1}, ui::ToU32(ui.C().dividerSoft));
        ImGui::Dummy({width, h});
    }
    const float inner = width - ui.S(28);
    ImGui::SetCursorPos({ui.S(14), ImGui::GetCursorPosY() + ui.S(14)});
    ImGui::BeginGroup();
    if (Component* c = st.assembly.FindComponent(st.selected)) {
        DrawComponentProperties(app, session, *c, inner);
    } else if (const assembly::ComponentPattern* pat = st.assembly.FindPattern(st.selected)) {
        DrawPatternProperties(app, session, *pat, inner);
    } else if (assembly::Constraint* con = st.assembly.FindConstraint(st.selected)) {
        const Component* a = st.assembly.FindComponent(con->first.component);
        const Component* b = st.assembly.FindComponent(con->second.component);
        LabelText(ui, Trf("{} — {}", a ? a->name : "?", b ? b->name : "?"), ui.C().textSecondary);
        ImGui::Dummy({0, ui.S(8)});
        if (ConstraintOptions(app, *con, inner, [&st] { st.Checkpoint(N_("Изменение зависимости")); })) st.assembly.Touch();
        JointMotionOptions(app, st, *con, inner);
        switch (con->state) {
            case assembly::ConstraintState::Failed:
                LabelText(ui, Trf("Не выполняется (ошибка {} мм)", FormatNumber(con->error, 4)), ui.C().danger);
                break;
            case assembly::ConstraintState::Redundant:
                LabelText(ui, Tr("Лишняя: то же уже задано другими зависимостями"), ui.C().warning);
                break;
            default: break;
        }
    } else {
        const auto field = [&](const char* label, const char* id, std::string assembly::AssemblyInfo::*member, const char* hint) {
            ui::FieldLabel(ui, label);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRect(p, {p.x + inner, p.y + ui.S(28)}, ui::ToU32(ui.C().fieldBorder), ui.S(5));
            if (ui::CommitInput(ui, id, st.assembly.info.*member, app.shell.edit, inner, false, hint)) {
                st.Checkpoint(N_("Свойства сборки"));
                st.assembly.info.*member = app.shell.edit.text;
                st.assembly.Touch();
            }
            ImGui::Dummy({0, ui.S(10)});
        };
        field(Tr("Наименование"), "##a-name", &assembly::AssemblyInfo::name, session.Title().c_str());
        field(Tr("Обозначение"), "##a-designation", &assembly::AssemblyInfo::designation, Tr("[ОБОЗНАЧЕНИЕ]"));
        LabelText(ui, Trf("Компонентов: {}", st.assembly.components.size()), ui.C().textSecondary);
        LabelText(ui, Trf("Зависимостей: {}", st.assembly.constraints.size()), ui.C().textSecondary);
        LabelText(ui, Tr("Выберите компонент в дереве или в окне, чтобы увидеть его свойства."), ui.C().textMuted);
    }
    ImGui::EndGroup();
}

// --- 3D view --------------------------------------------------------------------

void DrawAssemblyViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    AssemblyState& st = *session.assembly;
    ui::Ui& ui = app.ui;
    AssemblyState::Drag& drag = st.drag;
    const ImVec2 mouse = ImGui::GetMousePos();
    const Vec2 pixel = vf.ToLocal(mouse);

    // What is under the cursor: a component (its body).
    std::optional<render::PickResult> hit;
    if (vf.hovered && !drag.moving) hit = app.viewport.Pick(pixel);
    const ObjectId hovered = hit ? hit->body : ObjectId{};
    app.viewport.SetHoveredBody(drag.moving ? drag.component : hovered ? hovered : st.selected);
    DrawOffer(app, session, vf);

    if (st.pickingAxis) {
        if (!st.assembly.FindPattern(st.pickingAxis)) {
            st.pickingAxis = {};
        } else {
            Prompt(ui, vf, Tr("Щёлкните по круглой грани — её ось станет осью массива (Esc — отмена)"));
            if (vf.hovered && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                const ObjectId id = st.pickingAxis;
                if (auto axis = st.geometry.GeometryAt(st.assembly, hovered, hit->point, kernel::AssemblyPick::Axis)) {
                    st.pickingAxis = {};
                    EditPattern(app, st, id, [&](assembly::ComponentPattern& p) { p.axis = *axis; });
                } else {
                    app.notes.Info(Tr("У этой грани нет оси — выберите цилиндр или конус"));
                }
            }
            return;
        }
    }

    if (st.dropping.active) {
        Prompt(ui, vf, Tr("Щёлкните по кромке отверстия — размер и длина подберутся сами (Esc — отмена)"));
        if (vf.hovered && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (auto rim = st.geometry.GeometryAt(st.assembly, hovered, hit->point, kernel::AssemblyPick::Circle))
                DropOnRim(app, session, *rim);
            else
                app.notes.Info(Tr("Здесь нет круглой кромки"));
        }
        for (const std::string& p : st.problems) app.notes.Error(Tr(p));
        st.problems.clear();
        return;
    }
    if (st.creating.active) {
        auto& creating = st.creating;
        Prompt(ui, vf, Tr(PickPrompt(creating.constraint.type, creating.picked)));
        if (vf.hovered && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const assembly::GeometryRef* first = creating.picked == 1 ? &creating.constraint.first : nullptr;
            std::optional<assembly::GeometryRef> g =
                st.geometry.GeometryAt(st.assembly, hovered, hit->point, PickFor(creating.constraint.type));
            if (!g) {
                app.notes.Info(Tr(creating.constraint.type == assembly::ConstraintType::Insert
                                      ? "Здесь нет круглой кромки"
                                      : creating.constraint.type == assembly::ConstraintType::AxisMate
                                            ? "У этой грани нет оси — выберите цилиндр или конус"
                                            : "Эту грань нельзя использовать"));
            } else if (const char* why = Unfit(creating.constraint.type, *g, first)) {
                app.notes.Info(Tr(why));
            } else if (first && first->component == g->component) {
                app.notes.Info(Tr("Вторую грань выберите на другом компоненте"));
            } else if (!first) {
                creating.constraint.first = *g;
                creating.picked = 1;
            } else {
                creating.constraint.second = *g;
                const Component* a = st.assembly.FindComponent(creating.constraint.first.component);
                const Component* b = st.assembly.FindComponent(g->component);
                if (a && b && a->grounded && b->grounded) app.notes.Info(Tr("Оба компонента закреплены — зависимость их не сдвинет"));
                st.Checkpoint(assembly::IsJoint(creating.constraint.type) ? N_("Соединение") : N_("Зависимость"));
                if (assembly::IsJoint(creating.constraint.type)) assembly::FitJoint(st.assembly, creating.constraint);
                const ObjectId id = st.assembly.AddConstraint(creating.constraint).id;
                st.selected = id;
                // Ready for the next one of the same kind, as in Inventor.
                const assembly::Constraint keep = creating.constraint;
                creating = {};
                creating.active = true;
                creating.constraint.type = keep.type;
                creating.constraint.value = keep.value;
                creating.constraint.flipped = keep.flipped;
            }
        }
        for (const std::string& p : st.problems) app.notes.Error(Tr(p));
        st.problems.clear();
        return;
    }
    if (vf.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        drag = {};
        if (hovered) {
            st.selected = hovered;
            if (const Component* c = st.assembly.FindComponent(hovered); c && !c->grounded) {
                drag.armed = true;
                drag.component = hovered;
                drag.pressAt = pixel;
                drag.grab = hit->point;
                drag.start = c->placement;
                drag.pivot = c->placement.translation;
                if (const auto box = st.geometry.ComponentBounds(hovered)) drag.pivot = (box->min + box->max) * 0.5;
            }
        } else {
            st.selected = {};
        }
    }
    if (drag.armed && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const Vec2 d = pixel - drag.pressAt;
        if (!drag.moving && d.Length() > app.ui.S(kDragStartPixels)) {
            drag.moving = true;
            st.Checkpoint(st.tool == AssemblyState::Tool::Rotate ? N_("Поворот компонента") : N_("Перемещение компонента"));
        }
        if (drag.moving)
            if (Component* c = st.assembly.FindComponent(drag.component)) {
                const ViewProjection& vp = vf.projection;
                if (st.tool == AssemblyState::Tool::Rotate) {
                    // Turntable about the box centre: sideways about the screen's
                    // up, up-down about its right.
                    const Vec3 right = vp.direction.Cross(vp.up).Normalized();
                    const Rotation r = Rotation::AxisAngle(vp.up, d.x * 0.01) * Rotation::AxisAngle(right, d.y * 0.01);
                    c->placement.rotation = (r * drag.start.rotation).Orthonormalized();
                    c->placement.translation = r * (drag.start.translation - drag.pivot) + drag.pivot;
                } else {
                    // The grabbed point follows the cursor in the plane facing the screen.
                    Vec3 origin, dir;
                    vp.Unproject(pixel, origin, dir);
                    const double denom = dir.Dot(vp.direction);
                    if (std::abs(denom) > 1e-12) {
                        const double t = (drag.grab - origin).Dot(vp.direction) / denom;
                        const Vec3 at = origin + dir * t;
                        c->placement.translation = drag.start.translation + (at - drag.grab);
                    }
                }
                st.assembly.Touch();
            }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (drag.moving) mon::Action("assembly", "конец перетаскивания");
        drag = {};
    }
    if (drag.moving) app.shell.handleHot = true;   // no menus while dragging

    // Prompt for an empty assembly.
    if (st.assembly.components.empty())
        Prompt(ui, vf, Tr("Вставьте первый компонент: «Вставить» на ленте — деталь из файла или стандартное изделие"));
    // Problems with part files: once each.
    for (const std::string& p : st.problems) app.notes.Error(Tr(p));
    st.problems.clear();
}

} // namespace bz::app
