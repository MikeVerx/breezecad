#include "app/assembly/AssemblyUi.h"

#include "app/Format.h"
#include "app/Operations.h"
#include "app/features/OperationEditors.h"
#include "app/shell/Shell.h"
#include "app/sketch/SketchEditor.h"
#include "assembly/AssemblyFile.h"
#include "core/Expression.h"
#include "core/I18n.h"
#include "core/PathText.h"
#include "document/BzcFile.h"
#include "document/DocumentTypes.h"
#include "platform/FileDialogs.h"
#include "platform/Paths.h"
#include "ui/Menus.h"
#include "ui/Widgets.h"

#include <algorithm>
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

} // namespace

bool InAssembly(AppContext& app) { return ActiveAssembly(app) != nullptr; }

void NewAssembly(AppContext& app) {
    StopEditing(app);
    app.workspace.NewAssembly();
    app.shell.ribbonTab = RibbonTab::Model;
    app.shell.leftTab = LeftTab::Tree;
}

ObjectId PlaceFile(AppContext& app, Session& session, const std::filesystem::path& file) {
    AssemblyState& st = *session.assembly;
    if (!session.path.empty() && std::filesystem::equivalent(session.path, file)) {
        app.notes.Error(Tr("Сборку нельзя вставить в саму себя"));
        return {};
    }
    if (Expected<std::string> type = doc::ReadDocumentType(file); !type || *type != doc::kPartType) {
        app.notes.Error(Trf("«{}» — не деталь: {}", ToUtf8(file.filename()),
                            type ? Tr("подсборки появятся позже") : Tr(type.GetError().message)));
        return {};
    }
    st.Checkpoint(N_("Вставка компонента"));
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

bool PlaceStandardPart(AppContext& app, const doc::parts::Request& request) {
    Session* s = ActiveAssembly(app);
    if (!s) return false;
    const std::string designation = doc::parts::Designation(request);
    // Standard parts live in a folder next to the assembly (or among the
    // user's documents while it has no file): equal ones share one file.
    const std::filesystem::path base = s->path.empty() ? platform::DocumentsDir() / "BreezeCAD" : s->path.parent_path();
    const std::filesystem::path folder = base / PathFromUtf8("Стандартные изделия");
    const std::filesystem::path file = folder / PathFromUtf8(SafeFileName(designation) + ".bzc");
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        Expected<doc::parts::Made> made = doc::parts::MakePart(request);
        if (!made) {
            app.notes.Error(Tr(made.GetError().message));
            return false;
        }
        std::filesystem::create_directories(folder, ec);
        if (const Status saved = doc::SaveBzc(made->doc, file, "BreezeCAD"); !saved) {
            app.notes.Error(Trf("Не удалось сохранить деталь: {}", Tr(saved.GetError().message)));
            return false;
        }
    }
    StopEditing(app);
    const ObjectId id = PlaceFile(app, *s, file);
    if (!id) return false;
    // A standard part is listed by its designation.
    if (Component* c = s->assembly->assembly.FindComponent(id)) {
        if (c->properties.designation.empty()) c->properties.designation = designation;
    }
    app.notes.Success(Trf("Вставлено: {}", designation));
    return true;
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
    if (st.assembly.FindComponent(st.selected)) {
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
        const std::string title = c ? Trf("Компонент {}", c->name) : std::string(Tr("Свойства сборки"));
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
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) drag = {};
    if (drag.moving) app.shell.handleHot = true;   // no menus while dragging

    // Prompt for an empty assembly.
    if (st.assembly.components.empty()) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const char* text = Tr("Вставьте первый компонент: «Вставить» на ленте — деталь из файла или стандартное изделие");
        ImFont* font = ui.fonts.medium;
        const ImVec2 ts = font->CalcTextSizeA(ui.S(13), FLT_MAX, 0, text);
        const ImVec2 p{(vf.min.x + vf.max.x - ts.x) / 2 - ui.S(14), vf.min.y + ui.S(14)};
        dl->AddRectFilled(p, {p.x + ts.x + ui.S(28), p.y + ui.S(34)}, ui::ToU32(ui.C().panel.WithAlpha(0.94f)), ui.S(17));
        dl->AddText(font, ui.S(13), {p.x + ui.S(14), p.y + ui.S(9)}, ui::ToU32(ui.C().textSecondary), text);
    }
    // Problems with part files: once each.
    for (const std::string& p : st.problems) app.notes.Error(Tr(p));
    st.problems.clear();
}

} // namespace bz::app
