#include "platform/Launch.h"
#include "app/assembly/AssemblyUi.h"
#include "app/assembly/InPlace.h"
#include "app/viewport/Planes.h"
#include "app/viewport/ViewPrompt.h"

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

    // Open the part on its own, or find its file.
    {
        const std::filesystem::path file = assembly::ResolvePartPath(session.path, c.path);
        const float half = (width - ui.S(6)) / 2;
        if (ui::SecondaryButton(ui, "##c-open-tab", Tr("Открыть во вкладке"), nullptr, {half, ui.S(30)}))
            app.Later([&app, file] { OpenDocument(app, file); });
        ImGui::SameLine(0, ui.S(6));
        if (ui::SecondaryButton(ui, "##c-show-file", Tr("Показать в папке"), nullptr, {half, ui.S(30)}) && !platform::ShowInFolder(file))
            app.notes.Error(Tr("Не удалось открыть папку"));
        ImGui::Dummy({0, ui.S(8)});
    }

    ComponentText(app, st, c, Tr("Позиция в спецификации"), "##c-position", &assembly::ComponentProperties::position,
                  std::string{}, width);
    ComponentText(app, st, c, Tr("Обозначение"), "##c-designation", &assembly::ComponentProperties::designation,
                  part ? part->info.designation : std::string{}, width);
    ComponentText(app, st, c, Tr("Наименование"), "##c-pname", &assembly::ComponentProperties::name,
                  part ? part->info.name : std::string{}, width);
    ComponentText(app, st, c, Tr("Материал"), "##c-material", &assembly::ComponentProperties::material,
                  part ? part->info.material : std::string{}, width);
    ComponentText(app, st, c, Tr("Примечание"), "##c-note", &assembly::ComponentProperties::note, std::string{}, width);
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
            } else if (c.follower) {
                app.notes.Info(Tr("Деталь нарисована по другим деталям сборки и стоит на месте — двигайте те, по которым она построена"));
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
            if (ui::SecondaryButton(ui, turns[i].id, turns[i].label, nullptr, {bw, ui.S(30)}, !st.assembly.IsFixed(c.id))) {
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
    if (c.follower)
        LabelText(ui, Tr("Нарисована по другим деталям этой сборки: стоит на месте, зависимости двигают те, что с ней связаны"),
                  ui.C().textSecondary);
    else if (!c.grounded) {
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
        case assembly::ConstraintType::AxisMate:   return kernel::AssemblyPick::Line;
        // Inventor's mate takes what is under the cursor: a vertex, an edge or the face.
        case assembly::ConstraintType::Mate:
        case assembly::ConstraintType::Angle:
        case assembly::ConstraintType::Symmetry:   return kernel::AssemblyPick::Any;
        default:                                   return kernel::AssemblyPick::Face;
    }
}

/// How near (mm) a vertex or an edge must be to the cursor to be taken
/// rather than the face: a few pixels at the current zoom.
double PickTolerance(const ViewportFrame& vf) { return 7.0 * vf.projection.mmPerPixel; }

/// "plane", "axis"... of a picked geometry, for the panel.
const char* KindName(const assembly::GeometryRef& g) {
    using assembly::GeometryKind;
    using assembly::PickSource;
    switch (g.kind) {
        case GeometryKind::Plane:    return g.source == PickSource::Element ? N_("плоскость") : N_("грань");
        case GeometryKind::Axis:     return g.source == PickSource::Edge ? N_("ребро") : N_("ось");
        case GeometryKind::Point:    return g.source == PickSource::Vertex ? N_("вершина") : N_("точка");
        case GeometryKind::Cylinder: return N_("цилиндр");
        case GeometryKind::Sphere:   return N_("сфера");
        case GeometryKind::Circle:   return N_("круглая кромка");
    }
    return "";
}

/// "Кронштейн:1, грань" — what a pick took.
std::string PickTitle(const AssemblyState& st, const assembly::GeometryRef& g) {
    std::string what = Tr(KindName(g));
    if (g.source == assembly::PickSource::Element) what = Tr(std::string(assembly::OriginElementTitle(g.element)));
    if (what.empty()) what = Tr(KindName(g));
    return Trf("{}, {}", Tr(st.assembly.NameOf(g.component)), what);
}

/// Why `g` cannot be used for this constraint (with `first` already
/// picked, if any; `picked`: how many are picked), or nullptr when it can.
const char* Unfit(assembly::ConstraintType type, const assembly::GeometryRef& g, const assembly::GeometryRef* first, int picked = -1) {
    using assembly::GeometryKind;
    const auto family = [](const assembly::GeometryRef& r) {
        return r.kind == GeometryKind::Plane ? 0 : r.kind == GeometryKind::Point || r.kind == GeometryKind::Sphere ? 2 : 1;
    };
    switch (type) {
        case assembly::ConstraintType::Symmetry:
            if (picked == 2) {
                if (g.kind != GeometryKind::Plane) return N_("Плоскость симметрии — плоская грань или плоскость начала координат");
                break;
            }
            if (first && family(*first) != family(g))
                return N_("Симметричными бывают две грани, две оси или две точки — выберите такую же");
            break;
        case assembly::ConstraintType::Angle:
            if (g.direction.Length() < 0.5) return N_("Для угла нужна плоская грань, ось или прямое ребро");
            break;
        case assembly::ConstraintType::AxisMate:
            if (!g.HasAxis()) return N_("Нужна ось: цилиндрическая грань или прямое ребро");
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
            // Plane, line and point in any pair: face to face, a line in a
            // plane, a point on a line, two points together (Inventor's mate).
            if (g.kind == GeometryKind::Sphere) return N_("Для сферы — «Касание»");
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
            return picked == 0 ? N_("Совпадение осей: выберите цилиндрическую грань или прямое ребро первого компонента")
                               : N_("Совпадение осей: выберите цилиндрическую грань или прямое ребро второго компонента");
        case assembly::ConstraintType::Symmetry:
            return picked == 0   ? N_("Симметрия: выберите грань, ось или вершину первого компонента")
                   : picked == 1 ? N_("Симметрия: выберите такую же у второго компонента")
                                 : N_("Симметрия: выберите плоскость симметрии — грань или плоскость в дереве");
        case assembly::ConstraintType::Gear:
            return picked == 0 ? N_("Передача: выберите ведущее соединение «Вращение» — в дереве или щелчком по его детали")
                               : N_("Передача: выберите ведомое соединение «Вращение»");
        case assembly::ConstraintType::Screw:
            return picked == 0 ? N_("Винт-рейка: выберите соединение «Вращение» (винт, шестерня) — в дереве или по детали")
                               : N_("Винт-рейка: выберите соединение «Ползун» (гайка, рейка)");
        case assembly::ConstraintType::Angle:
            return picked == 0 ? N_("Угол: выберите грань или ребро первого компонента")
                               : N_("Угол: выберите грань или ребро второго компонента");
        case assembly::ConstraintType::Tangent:
            return picked == 0 ? N_("Касание: выберите грань первого компонента")
                               : N_("Касание: выберите грань второго компонента");
        case assembly::ConstraintType::Mate:
            return picked == 0 ? N_("Совмещение: выберите грань, ребро или вершину первого компонента (или плоскость в дереве)")
                               : N_("Совмещение: выберите грань, ребро или вершину второго компонента");
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

/// Shows a constraint's geometry in the view: a plane as a square (or the
/// plane overlay for named planes), a line as a long dash, a point as a dot.
void DrawGeometryMark(AppContext& app, const ViewportFrame& vf, const AssemblyState& st, const assembly::GeometryRef& g,
                      bool strong) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ui::ThemeColors& c = app.ui.C();
    const Placement at = st.assembly.PlacementOf(g.component);
    const Vec3 p = at.Point(g.origin);
    const Vec3 d = g.direction.Length() > 0.5 ? at.Direction(g.direction).Normalized() : Vec3{};
    // Sized to what is around: the component's box, or a fixed size.
    double size = 40.0;
    if (!g.OnOrigin())
        if (const auto box = st.geometry.ComponentBounds(g.component)) size = std::max(10.0, (box->max - box->min).Length() * 0.35);
    const ImU32 ink = ui::ToU32(strong ? c.accent : c.accent.WithAlpha(0.65f));
    const float width = app.ui.S(strong ? 2.4f : 1.6f);
    switch (g.kind) {
        case assembly::GeometryKind::Plane: {
            if (g.source == assembly::PickSource::Element) {
                ShownPlane plane;
                plane.id = g.element;
                Vec3 x = std::abs(d.z) < 0.9 ? Vec3{0, 0, 1}.Cross(d) : Vec3{1, 0, 0}.Cross(d);
                plane.frame = Frame{p, x.Normalized(), d};
                plane.half = size;
                plane.label = Tr(std::string(assembly::OriginElementTitle(g.element)));
                DrawPlane(app, dl, vf, plane, strong);
                break;
            }
            // A face: a small square on it and its normal.
            const Vec3 u = (std::abs(d.z) < 0.9 ? Vec3{0, 0, 1}.Cross(d) : Vec3{1, 0, 0}.Cross(d)).Normalized() * (size * 0.25);
            const Vec3 v = d.Cross(u);
            const ImVec2 q[4] = {vf.Project(p + u + v), vf.Project(p - u + v), vf.Project(p - u - v), vf.Project(p + u - v)};
            dl->AddQuadFilled(q[0], q[1], q[2], q[3], ui::ToU32(c.accent.WithAlpha(0.18f)));
            dl->AddQuad(q[0], q[1], q[2], q[3], ink, width);
            dl->AddLine(vf.Project(p), vf.Project(p + d * (size * 0.3)), ink, width);
            break;
        }
        case assembly::GeometryKind::Axis:
        case assembly::GeometryKind::Cylinder:
        case assembly::GeometryKind::Circle: {
            const ImVec2 a = vf.Project(p - d * size), b = vf.Project(p + d * size);
            const ImVec2 step{(b.x - a.x) / 24.0f, (b.y - a.y) / 24.0f};
            for (int i = 0; i < 24; i += 2)
                dl->AddLine({a.x + step.x * static_cast<float>(i), a.y + step.y * static_cast<float>(i)},
                            {a.x + step.x * static_cast<float>(i + 1), a.y + step.y * static_cast<float>(i + 1)}, ink, width);
            if (g.kind == assembly::GeometryKind::Circle) dl->AddCircle(vf.Project(p), app.ui.S(5), ink, 0, width);
            break;
        }
        case assembly::GeometryKind::Point:
        case assembly::GeometryKind::Sphere:
            dl->AddCircleFilled(vf.Project(p), app.ui.S(strong ? 5.5f : 4.0f), ink);
            dl->AddCircle(vf.Project(p), app.ui.S(9), ink, 0, app.ui.S(1.2f));
            break;
    }
}

void Prompt(ui::Ui& ui, const ViewportFrame& vf, const char* text) {
    DrawViewPrompt(ui, ImGui::GetWindowDrawList(), vf, text, ui.C().textSecondary);
}

/// Value and options of a constraint (being made or chosen in the tree).
/// Returns true when something changed (`before` is called first, once).
bool ConstraintOptions(AppContext& app, assembly::Constraint& c, float width, const std::function<void()>& before, bool direction = true) {
    ui::Ui& ui = app.ui;
    bool changed = false;
    bool asked = false;
    const auto change = [&] {
        if (!asked) before();
        asked = true;
        changed = true;
    };
    static constexpr std::array<const char*, 2> labels{N_("Нет"), N_("Да")};
    const bool angle = c.type == assembly::ConstraintType::Angle;
    const bool transmission = assembly::IsTransmission(c.type);
    // Mate and Angle: one value, or anywhere within a range (Inventor's limits).
    if (assembly::HasRange(c.type)) {
        ui::FieldLabel(ui, Tr("Пределы вместо одного значения"));
        if (const int k = ui::Segmented(ui, "con-range", labels, c.limits.enabled ? 1 : 0, 2, width); k != (c.limits.enabled ? 1 : 0)) {
            change();
            c.limits.enabled = k == 1;
            if (c.limits.enabled) {
                const double span = angle ? kPi / 12 : 5.0;
                c.limits.lower = std::max(angle ? 0.0 : -1e9, c.value - span);
                c.limits.upper = c.value + span;
            }
        }
        ImGui::Dummy({0, ui.S(10)});
    }
    const bool noValue = c.type == assembly::ConstraintType::AxisMate || c.type == assembly::ConstraintType::Slider ||
                         c.type == assembly::ConstraintType::Cylindrical || c.type == assembly::ConstraintType::Ball ||
                         c.type == assembly::ConstraintType::Symmetry || transmission ||
                         (assembly::HasRange(c.type) && c.limits.enabled);
    const auto number = [&](const char* id, const char* label, double shown, bool isAngle, double& into) {
        ui::FieldLabel(ui, label);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRect(p, {p.x + width, p.y + ui.S(28)}, ui::ToU32(ui.C().fieldBorder), ui.S(5));
        if (ui::CommitInput(ui, id, FormatNumber(isAngle ? shown * 180.0 / kPi : shown, 4), app.shell.edit, width, true)) {
            UnitSystem units;
            units.angle = AngleUnit::Degree;
            const Expected<double> v =
                EvaluateExpression(app.shell.edit.text, isAngle ? Quantity::Angle : Quantity::Length, units, {});
            if (!v) {
                app.notes.Error(Tr("Не число"));
            } else {
                change();
                into = *v;
            }
        }
        ImGui::Dummy({0, ui.S(10)});
    };
    if (!noValue)
        number("##con-value", angle ? Tr("Угол, °") : assembly::IsJoint(c.type) ? Tr("Смещение вдоль оси, мм") : Tr("Зазор, мм"),
               c.value, angle, c.value);
    if (assembly::HasRange(c.type) && c.limits.enabled) {
        number("##con-lo", angle ? Tr("От, °") : Tr("От, мм"), c.limits.lower, angle, c.limits.lower);
        number("##con-hi", angle ? Tr("До, °") : Tr("До, мм"), c.limits.upper, angle, c.limits.upper);
        if (c.limits.upper < c.limits.lower) LabelText(ui, Tr("«До» меньше «От» — пределы не действуют как задумано"), ui.C().danger);
    }
    if (transmission) {
        // Gear: how many turns the driven makes per turn of the drive (z1/z2);
        // screw: the lead, mm per turn.
        double ratio = c.ratio;
        number("##con-ratio",
               c.type == assembly::ConstraintType::Gear ? Tr("Передаточное число: оборотов ведомого на оборот ведущего")
                                                        : Tr("Ход: мм перемещения на оборот"),
               c.ratio, false, ratio);
        if (ratio != c.ratio) {
            if (std::abs(ratio) < 1e-9) app.notes.Error(Tr("Ноль не годится: соединения перестанут двигаться вместе"));
            else c.ratio = ratio;
        }
    }
    const char* option = nullptr;
    switch (c.type) {
        case assembly::ConstraintType::Mate:     option = N_("Заподлицо (грани смотрят в одну сторону)"); break;
        case assembly::ConstraintType::Insert:   option = N_("Соосно в одну сторону (не навстречу)"); break;
        case assembly::ConstraintType::Tangent:  option = N_("Внутреннее касание"); break;
        case assembly::ConstraintType::Symmetry: option = N_("Грани смотрят друг на друга"); break;
        case assembly::ConstraintType::Gear:     option = N_("Вращаются в разные стороны (внешнее зацепление)"); break;
        case assembly::ConstraintType::Screw:    option = N_("Обратное направление (левая резьба)"); break;
        case assembly::ConstraintType::Ball:     break;
        default:
            if (assembly::IsJoint(c.type)) option = N_("Оси в одну сторону (не навстречу)");
            break;
    }
    if (option && direction) {
        ui::FieldLabel(ui, Tr(option));
        const int v = ui::Segmented(ui, "con-flip", labels, c.flipped ? 1 : 0, 2, width);
        if (v != (c.flipped ? 1 : 0)) {
            change();
            c.flipped = v == 1;
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
    // A part from elsewhere is copied beside a saved assembly at once, so that
    // "Открыть деталь" opens the copy it will use (a new assembly: at its first save).
    if (!session.path.empty()) Workspace::GatherParts(session, session.path, session.path, app.notes);
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
            LabelText(ui, Trf("Во все отверстия Ø{} мм на «{}», как у образца", FormatNumber(2 * pattern.rim.radius, 2),
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
    if (st.creating.preview) st.Discard();   // another constraint asked for: the trial one goes
    st.creating = {};
    st.newPart = {};
    st.creating.active = true;
    st.creating.constraint.type = type;
    if (type == assembly::ConstraintType::Angle) st.creating.constraint.value = kPi / 2;
    if (type == assembly::ConstraintType::Gear) st.creating.constraint.flipped = true;   // external gears: opposite ways
    if (type == assembly::ConstraintType::Screw) st.creating.constraint.ratio = 2.0;     // a common lead, mm
    st.drag = {};
    app.shell.leftTab = LeftTab::Mate;
}

namespace {

/// How many picks the constraint takes.
int PicksNeeded(assembly::ConstraintType type) { return type == assembly::ConstraintType::Symmetry ? 3 : 2; }

/// The trial constraint taken back (the parts return where they were).
void DropPreview(AssemblyState& st) {
    if (!st.creating.preview) return;
    st.Discard();
    st.creating.preview = {};
}

/// All picked: the constraint goes into the assembly at once, so the parts
/// move and the direction and gap can be judged and tuned before it is kept.
/// Called again after each change of the options: the same trial, updated.
void ShowPreview(AppContext& app, AssemblyState& st) {
    auto& creating = st.creating;
    assembly::Constraint made = creating.constraint;
    if (assembly::IsJoint(made.type)) assembly::FitJoint(st.assembly, made);
    if (assembly::IsTransmission(made.type)) assembly::FitTransmission(st.assembly, made);
    if (creating.preview) {
        if (assembly::Constraint* shown = st.assembly.FindConstraint(creating.preview)) {
            made.id = shown->id;
            made.name = shown->name;
            made.suppressed = shown->suppressed;
            *shown = made;
            st.assembly.Touch();
        }
        return;
    }
    if (creating.redefine) {
        if (assembly::Constraint* old = st.assembly.FindConstraint(creating.redefine)) {
            st.Checkpoint(N_("Переопределение зависимости"));
            const ObjectId id = old->id;
            const std::string name = old->type == made.type ? old->name : std::string{};
            made.id = id;
            made.name = !name.empty() ? name : old->name;
            made.suppressed = old->suppressed;
            *old = made;
            if (name.empty()) {
                // A new type: a name like the others of it ("Симметрия:2").
                const std::string base = std::string(assembly::ConstraintTitle(made.type));
                for (int n = 1;; ++n) {
                    const std::string candidate = std::format("{}:{}", base, n);
                    if (std::none_of(st.assembly.constraints.begin(), st.assembly.constraints.end(),
                                     [&](const assembly::Constraint& o) { return o.name == candidate; })) {
                        old->name = candidate;
                        break;
                    }
                }
            }
            st.assembly.Touch();
            st.selected = id;
            creating.preview = id;
        }
        return;
    }
    const auto fixed = [&](ObjectId c) { return st.assembly.IsFixed(c); };
    if (!assembly::IsTransmission(made.type) && fixed(made.first.component) && fixed(made.second.component))
        app.notes.Info(Tr("Оба компонента закреплены — зависимость их не сдвинет"));
    st.Checkpoint(assembly::IsJoint(made.type) ? N_("Соединение") : N_("Зависимость"));
    const ObjectId id = st.assembly.AddConstraint(made).id;
    st.selected = id;
    creating.preview = id;
}

} // namespace

bool ApplyConstraint(AppContext& app, Session& session, bool close) {
    (void)app;
    AssemblyState& st = *session.assembly;
    auto& creating = st.creating;
    if (!creating.active || !creating.preview) return false;
    // Kept as it is now; the next one of the same kind is ready, as in Inventor.
    const assembly::Constraint keep = creating.constraint;
    const bool redefining = creating.redefine.IsValid();
    creating = {};
    if (close || redefining) return true;
    creating.active = true;
    creating.constraint.type = keep.type;
    creating.constraint.value = keep.value;
    creating.constraint.flipped = keep.flipped;
    creating.constraint.ratio = keep.ratio;
    creating.constraint.limits = keep.limits;
    return true;
}

void CancelConstraint(Session& session) {
    AssemblyState& st = *session.assembly;
    DropPreview(st);
    st.creating = {};
}

bool TakeConstraintPick(AppContext& app, Session& session, const assembly::GeometryRef& g) {
    AssemblyState& st = *session.assembly;
    auto& creating = st.creating;
    if (!creating.active || assembly::IsTransmission(creating.constraint.type)) return false;
    const assembly::ConstraintType type = creating.constraint.type;
    const assembly::GeometryRef* first = creating.picked >= 1 ? &creating.constraint.first : nullptr;
    if (const char* why = Unfit(type, g, first, creating.picked)) {
        app.notes.Info(Tr(why));
        return true;
    }
    // All picked already: a new pick replaces the last one (a better face for the same constraint).
    if (creating.picked >= PicksNeeded(type)) {
        DropPreview(st);
        creating.picked = PicksNeeded(type) - 1;
        if (const char* why = Unfit(type, g, &creating.constraint.first, creating.picked)) {
            app.notes.Info(Tr(why));
            ShowPreview(app, st);
            creating.picked = PicksNeeded(type);
            return true;
        }
    }
    if (creating.picked == 1 && first->component == g.component) {
        app.notes.Info(Tr("Вторую выберите на другом компоненте"));
        return true;
    }
    if (creating.picked == 0) {
        creating.constraint.first = g;
        creating.picked = 1;
        return true;
    }
    if (creating.picked == 1) {
        creating.constraint.second = g;
        if (type == assembly::ConstraintType::Symmetry) {
            creating.picked = 2;   // the plane of symmetry next
            return true;
        }
    } else {
        creating.constraint.third = g;
    }
    creating.picked = PicksNeeded(type);
    ShowPreview(app, st);
    return true;
}

bool TakeJointPick(AppContext& app, Session& session, ObjectId joint) {
    AssemblyState& st = *session.assembly;
    auto& creating = st.creating;
    if (!creating.active || !assembly::IsTransmission(creating.constraint.type)) return false;
    const assembly::Constraint* j = st.assembly.FindConstraint(joint);
    if (!j) return true;
    const bool wantSlider = creating.constraint.type == assembly::ConstraintType::Screw && creating.picked == 1;
    const assembly::ConstraintType want = wantSlider ? assembly::ConstraintType::Slider : assembly::ConstraintType::Rotational;
    if (j->type != want) {
        app.notes.Info(wantSlider ? Tr("Здесь нужно соединение «Ползун»") : Tr("Здесь нужно соединение «Вращение»"));
        return true;
    }
    if (creating.picked == 0 || creating.preview) {
        if (creating.preview) {   // all picked: start over from the drive
            DropPreview(st);
            creating.picked = 0;
        }
        creating.constraint.drive = joint;
        creating.picked = 1;
        return true;
    }
    if (joint == creating.constraint.drive) {
        app.notes.Info(Tr("Выберите другое соединение"));
        return true;
    }
    DropPreview(st);
    creating.constraint.driven = joint;
    creating.picked = 2;
    ShowPreview(app, st);
    return true;
}

bool TakeElementPick(AppContext& app, Session& session, ObjectId component, ObjectId element) {
    AssemblyState& st = *session.assembly;
    if (st.newPart.active) return CreatePartPickElement(app, session, component, element);
    if (!st.creating.active) return false;
    const std::optional<assembly::GeometryRef> g = st.geometry.ElementGeometry(component, element);
    if (!g) {
        app.notes.Info(Tr("Этот элемент сейчас не построен"));
        return true;
    }
    return TakeConstraintPick(app, session, *g);
}

void StartRedefine(AppContext& app, ObjectId constraint, std::optional<assembly::ConstraintType> type) {
    Session* s = ActiveAssembly(app);
    if (!s) return;
    AssemblyState& st = *s->assembly;
    const assembly::Constraint* c = st.assembly.FindConstraint(constraint);
    if (!c) return;
    StopEditing(app);
    if (st.creating.preview) st.Discard();
    st.creating = {};
    st.creating.active = true;
    st.creating.redefine = constraint;
    st.creating.constraint = *c;   // its options stay; the geometry is picked again
    if (type) st.creating.constraint.type = *type;
    st.creating.constraint.first = {};
    st.creating.constraint.second = {};
    st.creating.constraint.third = {};
    st.drag = {};
    app.shell.leftTab = LeftTab::Mate;
}

void DrawConstraintPanel(AppContext& app, Session& session, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    AssemblyState& st = *session.assembly;
    auto& creating = st.creating;
    // Undone from the menu while shown: the picks are there, the trial is not.
    if (creating.preview && !st.assembly.FindConstraint(creating.preview)) {
        creating.preview = {};
        creating.picked = std::max(0, creating.picked - 1);
    }
    const assembly::ConstraintType type = creating.constraint.type;
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(48);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ui.icons.Draw(dl, ConstraintIcon(type), {p.x + ui.S(14), p.y + (h - ui.S(22)) / 2}, ui.S(22), {1, 1, 1, 1});
        const std::string title = Tr(std::string(assembly::ConstraintTitle(type)));
        dl->AddText(ui.fonts.title, ui.S(14), {p.x + ui.S(46), p.y + (h - ui.S(14)) / 2 - ui.S(1)}, ui::ToU32(c.text), title.c_str());
        ImGui::SetCursorScreenPos({p.x + width - ui.S(38), p.y + ui.S(9)});
        if (ui::IconButton(ui, "##con-close", "ui-close", 30, 14, Tr("Отмена (Esc)"))) app.Later([&session] { CancelConstraint(session); });
        ImGui::SetCursorScreenPos({p.x, p.y + h});
        ui::Divider(ui, c.dividerSoft);
    }
    const float inner = width - ui.S(28);
    ImGui::SetCursorPos({ui.S(14), ImGui::GetCursorPosY() + ui.S(14)});
    ImGui::BeginGroup();
    if (creating.redefine)
        if (const assembly::Constraint* old = st.assembly.FindConstraint(creating.redefine))
            LabelText(ui, Trf("Переопределение: {}", old->name), c.accent);

    // The kind, switchable while picking (the picks stay when they suit it), as Inventor's Place Constraint.
    static constexpr std::array<assembly::ConstraintType, 5> kinds{assembly::ConstraintType::Mate, assembly::ConstraintType::Angle,
                                                                  assembly::ConstraintType::Tangent, assembly::ConstraintType::Insert,
                                                                  assembly::ConstraintType::Symmetry};
    if (std::find(kinds.begin(), kinds.end(), type) != kinds.end()) {
        ui::FieldLabel(ui, Tr("Тип"));
        const float bw = (inner - ui.S(4) * 4) / 5;
        for (std::size_t i = 0; i < kinds.size(); ++i) {
            if (i) ImGui::SameLine(0, ui.S(4));
            const std::string tip = Tr(std::string(assembly::ConstraintTitle(kinds[i])));
            ImGui::PushID(static_cast<int>(i));
            if (ui::IconButton(ui, "##kind", ConstraintIcon(kinds[i]), bw / ui.S(1), 20, tip.c_str(), true, kinds[i] == type) &&
                kinds[i] != type) {
                const assembly::ConstraintType to = kinds[i];
                app.Later([&app, &st, to] {
                    auto& cr = st.creating;
                    DropPreview(st);
                    cr.constraint.type = to;
                    cr.constraint.value = to == assembly::ConstraintType::Angle ? kPi / 2 : 0.0;
                    cr.constraint.limits = {};
                    // Picks kept as far as they suit the new kind.
                    const int had = std::min(cr.picked, PicksNeeded(to));
                    cr.picked = 0;
                    if (had >= 1 && !Unfit(to, cr.constraint.first, nullptr, 0)) cr.picked = 1;
                    if (had >= 2 && cr.picked == 1 && !Unfit(to, cr.constraint.second, &cr.constraint.first, 1)) cr.picked = 2;
                    if (cr.picked >= PicksNeeded(to)) ShowPreview(app, st);
                });
            }
            ImGui::PopID();
        }
        ImGui::Dummy({0, ui.S(8)});
    }

    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::ToU32(c.textSecondary));
    ImGui::TextUnformatted(creating.preview ? Tr("Детали встали, как будет. Проверьте направление и зазор, затем «Применить» или «Готово».")
                                            : Tr(PickPrompt(type, creating.picked)));
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::Dummy({0, ui.S(6)});

    // What is picked: a field per pick (the one waiting is outlined); ✕ picks it again.
    const auto field = [&](int index, const char* label, const std::string& value) {
        ui::FieldLabel(ui, Tr(label));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(30);
        const bool waiting = creating.picked == index && !creating.preview;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, {p.x + inner, p.y + h}, ui::ToU32(c.field), ui.S(6));
        dl->AddRect(p, {p.x + inner, p.y + h}, ui::ToU32(waiting ? c.accent : c.fieldBorder), ui.S(6), 0, waiting ? ui.S(2) : 1.0f);
        const std::string text = value.empty() ? std::string(Tr("не выбрано")) : value;
        const ImVec4 clip{p.x + ui.S(10), p.y, p.x + inner - ui.S(30), p.y + h};
        dl->AddText(ui.fonts.body, ui.S(13), {p.x + ui.S(10), p.y + (h - ui.S(13)) / 2 - ui.S(1)},
                    ui::ToU32(value.empty() ? c.textMuted : c.text), text.c_str(), nullptr, 0, &clip);
        ImGui::Dummy({inner, h});
        if (!value.empty()) {
            ImGui::SetCursorScreenPos({p.x + inner - ui.S(28), p.y + ui.S(3)});
            ImGui::PushID(index);
            if (ui::IconButton(ui, "##again", "ui-close", 24, 12, Tr("Выбрать заново")))
                app.Later([&st, index] {
                    DropPreview(st);
                    st.creating.picked = index;
                });
            ImGui::PopID();
            ImGui::SetCursorScreenPos({p.x, p.y + h});
        }
        ImGui::Dummy({0, ui.S(6)});
    };
    if (assembly::IsTransmission(type)) {
        const assembly::Constraint* drive = creating.picked >= 1 ? st.assembly.FindConstraint(creating.constraint.drive) : nullptr;
        const assembly::Constraint* driven = creating.picked >= 2 ? st.assembly.FindConstraint(creating.constraint.driven) : nullptr;
        field(0, N_("Ведущее"), drive ? drive->name : std::string());
        field(1, N_("Ведомое"), driven ? driven->name : std::string());
    } else {
        field(0, N_("Первое"), creating.picked >= 1 ? PickTitle(st, creating.constraint.first) : std::string());
        field(1, N_("Второе"), creating.picked >= 2 ? PickTitle(st, creating.constraint.second) : std::string());
        if (type == assembly::ConstraintType::Symmetry)
            field(2, N_("Плоскость симметрии"), creating.picked >= 3 ? PickTitle(st, creating.constraint.third) : std::string());
    }
    ImGui::Dummy({0, ui.S(6)});

    // Direction: the two ways the parts can meet, as pictures with words.
    const char* ways[2] = {nullptr, nullptr};
    switch (type) {
        case assembly::ConstraintType::Mate:   ways[0] = N_("Навстречу"), ways[1] = N_("Заподлицо"); break;
        case assembly::ConstraintType::Insert: ways[0] = N_("Навстречу"), ways[1] = N_("В одну сторону"); break;
        case assembly::ConstraintType::Tangent: ways[0] = N_("Снаружи"), ways[1] = N_("Внутри"); break;
        case assembly::ConstraintType::Symmetry: ways[0] = N_("Друг от друга"), ways[1] = N_("Друг на друга"); break;
        default:
            if (assembly::IsJoint(type) && type != assembly::ConstraintType::Ball) ways[0] = N_("Оси навстречу"), ways[1] = N_("Оси в одну сторону");
            break;
    }
    bool changed = false;
    if (ways[0]) {
        ui::FieldLabel(ui, Tr("Направление"));
        const std::array<const char*, 2> labels{Tr(ways[0]), Tr(ways[1])};
        const int now = creating.constraint.flipped ? 1 : 0;
        if (const int v = ui::Segmented(ui, "con-way", labels, now, 2, inner); v != now) {
            creating.constraint.flipped = v == 1;
            changed = true;
        }
        ImGui::Dummy({0, ui.S(10)});
    }
    // The rest of the options (gap, angle, limits, ratio); the direction is above.
    {
        const bool flipped = creating.constraint.flipped;
        if (ConstraintOptions(app, creating.constraint, inner, [] {}, false)) changed = true;
        creating.constraint.flipped = flipped;
    }
    if (changed && creating.preview) app.Later([&app, &st] { ShowPreview(app, st); });

    // Применить keeps it and stays for the next; Готово keeps it and closes (Inventor's Apply / OK).
    const float half = (inner - ui.S(8)) / 2;
    const bool ready = creating.preview.IsValid();
    if (ui::PrimaryButton(ui, "##con-apply", Tr("Применить"), "Enter", {half, ui.S(34)}, ready))
        app.Later([&app, &session] { ApplyConstraint(app, session, false); });
    ImGui::SameLine(0, ui.S(8));
    if (ui::SecondaryButton(ui, "##con-done", Tr("Готово"), nullptr, {half, ui.S(34)}, ready))
        app.Later([&app, &session] { ApplyConstraint(app, session, true); });
    ImGui::Dummy({0, ui.S(4)});
    if (ui::SecondaryButton(ui, "##con-cancel", Tr("Отмена"), "Esc", {inner, ui.S(32)})) app.Later([&session] { CancelConstraint(session); });
    ImGui::EndGroup();
    ImGui::Dummy({0, ui.S(12)});
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
    if (st.newPart.active) {
        st.newPart = {};
        return true;
    }
    if (st.creating.active) {
        // Back one step: the trial and the last pick, then the command.
        if (st.creating.preview) DropPreview(st);
        if (st.creating.picked > 0) --st.creating.picked;
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
        if (assembly::IsTransmission(con->type)) {
            const assembly::Constraint* d = st.assembly.FindConstraint(con->drive);
            const assembly::Constraint* n = st.assembly.FindConstraint(con->driven);
            LabelText(ui, Trf("{} → {}", d ? d->name : "?", n ? n->name : "?"), ui.C().textSecondary);
        } else {
            LabelText(ui, Trf("{} — {}", Tr(st.assembly.NameOf(con->first.component)), Tr(st.assembly.NameOf(con->second.component))),
                      ui.C().textSecondary);
            LabelText(ui, Trf("{} · {}", PickTitle(st, con->first), PickTitle(st, con->second)), ui.C().textSecondary);
            if (con->type == assembly::ConstraintType::Symmetry)
                LabelText(ui, Trf("Плоскость: {}", PickTitle(st, con->third)), ui.C().textSecondary);
        }
        ImGui::Dummy({0, ui.S(8)});
        if (ConstraintOptions(app, *con, inner, [&st] { st.Checkpoint(N_("Изменение зависимости")); })) st.assembly.Touch();
        JointMotionOptions(app, st, *con, inner);
        // Inventor's "Переопределить": the same constraint, its geometry picked
        // again — or another type (a mate that should have been an angle).
        if (!assembly::IsTransmission(con->type)) {
            const ObjectId cid = con->id;
            if (!assembly::IsJoint(con->type)) {
                static constexpr std::array<assembly::ConstraintType, 6> kTypes{
                    assembly::ConstraintType::Mate,   assembly::ConstraintType::Angle,    assembly::ConstraintType::Tangent,
                    assembly::ConstraintType::Insert, assembly::ConstraintType::AxisMate, assembly::ConstraintType::Symmetry};
                static constexpr std::array<const char*, 6> kLabels{N_("Совмещ."), N_("Угол"), N_("Касание"),
                                                                    N_("Вставка"), N_("Оси"),  N_("Симметр.")};
                int current = 0;
                for (std::size_t k = 0; k < kTypes.size(); ++k)
                    if (kTypes[k] == con->type) current = static_cast<int>(k);
                ui::FieldLabel(ui, Tr("Тип (другой — выбрать геометрию заново)"));
                if (const int k = ui::Segmented(ui, "con-type", kLabels, current, 3, inner); k != current) {
                    const assembly::ConstraintType type = kTypes[static_cast<std::size_t>(k)];
                    app.Later([&app, cid, type] { StartRedefine(app, cid, type); });
                }
                ImGui::Dummy({0, ui.S(10)});
            }
            if (ui::SecondaryButton(ui, "##con-redefine", Tr("Переопределить"), nullptr, {inner, ui.S(32)}))
                app.Later([&app, cid] { StartRedefine(app, cid); });
            ui::ItemTooltip(ui, Tr("Выбрать грани (рёбра, вершины, плоскости) этой зависимости заново; её значение и настройки остаются"));
            ImGui::Dummy({0, ui.S(10)});
        }
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

    // An element pointed at in the tree, and what the constraint being made has picked so far.
    if (const auto& e = app.shell.hoveredElement; e.element)
        if (const auto g = st.geometry.ElementGeometry(e.component, e.element)) DrawGeometryMark(app, vf, st, *g, true);
    if (st.creating.active && !assembly::IsTransmission(st.creating.constraint.type)) {
        if (st.creating.picked >= 1) DrawGeometryMark(app, vf, st, st.creating.constraint.first, false);
        if (st.creating.picked >= 2) DrawGeometryMark(app, vf, st, st.creating.constraint.second, false);
    }
    // The selected constraint: what it ties.
    if (const assembly::Constraint* con = st.assembly.FindConstraint(st.selected); con && !st.creating.active &&
                                                                                    !assembly::IsTransmission(con->type)) {
        DrawGeometryMark(app, vf, st, con->first, true);
        DrawGeometryMark(app, vf, st, con->second, true);
        if (con->type == assembly::ConstraintType::Symmetry) DrawGeometryMark(app, vf, st, con->third, false);
    }

    // "Создать": a click on a flat face starts the new part there.
    if (CreatePartInViewport(app, session, vf)) {
        for (const std::string& p : st.problems) app.notes.Error(Tr(p));
        st.problems.clear();
        return;
    }

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
        Prompt(ui, vf, creating.preview ? Tr("Проверьте, как встали детали: Enter или щелчок колесом — применить, Esc — отмена")
                                          : Tr(PickPrompt(creating.constraint.type, creating.picked)));
        if (vf.hovered && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (assembly::IsTransmission(creating.constraint.type)) {
                // A click on a part takes its joint of the kind wanted.
                const bool slider = creating.constraint.type == assembly::ConstraintType::Screw && creating.picked == 1;
                ObjectId found;
                int count = 0;
                for (const assembly::Constraint& j : st.assembly.constraints)
                    if ((slider ? j.type == assembly::ConstraintType::Slider : j.type == assembly::ConstraintType::Rotational) &&
                        (j.second.component == hovered || j.first.component == hovered)) {
                        found = j.id;
                        ++count;
                    }
                if (count == 1) TakeJointPick(app, session, found);
                else if (count == 0)
                    app.notes.Info(slider ? Tr("У этой детали нет соединения «Ползун»") : Tr("У этой детали нет соединения «Вращение»"));
                else app.notes.Info(Tr("У этой детали несколько соединений — выберите нужное в дереве"));
            } else if (std::optional<assembly::GeometryRef> g = st.geometry.GeometryAt(
                           st.assembly, hovered, hit->point,
                           creating.constraint.type == assembly::ConstraintType::Symmetry && creating.picked == 2
                               ? kernel::AssemblyPick::Face
                               : PickFor(creating.constraint.type),
                           PickTolerance(vf))) {
                TakeConstraintPick(app, session, *g);
            } else {
                app.notes.Info(Tr(creating.constraint.type == assembly::ConstraintType::Insert
                                      ? "Здесь нет круглой кромки"
                                      : creating.constraint.type == assembly::ConstraintType::AxisMate
                                            ? "Здесь нет оси — выберите цилиндр, конус или прямое ребро"
                                            : "Эту грань нельзя использовать"));
            }
        }
        for (const std::string& p : st.problems) app.notes.Error(Tr(p));
        st.problems.clear();
        return;
    }
    // Double click on a component: its part edited right here, in place.
    if (vf.hovered && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        drag = {};
        const ObjectId id = hovered;
        app.Later([&app, &session, id] { EditInPlace(app, session, id); });
        return;
    }
    if (vf.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        drag = {};
        if (hovered) {
            st.selected = hovered;
            if (const Component* c = st.assembly.FindComponent(hovered); c && !st.assembly.IsFixed(c->id)) {
                drag.armed = true;
                drag.component = hovered;
                drag.pressAt = pixel;
                drag.grab = hit->point;
                drag.start = c->placement;
                drag.local = c->placement.Inverse().Point(hit->point);
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
                        // Shifted there first (a part held by nothing just
                        // follows), then pulled there by the solver as far
                        // as its constraints let it: on an axle it turns.
                        c->placement.translation = drag.start.translation + (at - drag.grab);
                        drag.target = at;
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
