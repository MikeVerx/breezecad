#include "app/features/SheetMetalEditors.h"
#include "app/features/DxfEditor.h"
#include "app/features/ToolChoice.h"

#include "app/Format.h"
#include "app/Materials.h"
#include "app/shell/Shell.h"
#include "core/I18n.h"
#include "core/PathText.h"
#include "platform/FileDialogs.h"
#include "platform/Paths.h"
#include "sheetmetal/Dxf.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <array>
#include <optional>
#include <cmath>
#include <fstream>

namespace bz::app {
namespace {

std::string Number(double v, int decimals = 2) { return doc::SheetNumber(v, decimals); }

/// "Авто" / "Вручную" for one rule; true when switched.
bool AutoChoice(AppContext& app, const char* id, bool& automatic, float width) {
    static constexpr std::array<const char*, 2> labels{N_("Авто"), N_("Вручную")};
    std::array<const char*, 2> shown{Tr(labels[0]), Tr(labels[1])};
    const int current = automatic ? 0 : 1;
    const int chosen = ui::Segmented(app.ui, id, shown, current, 2, width);
    if (chosen == current || chosen < 0) return false;
    automatic = chosen == 0;
    return true;
}

/// Points along a segment of the flat pattern, for drawing.
void Sample(const sheet::Segment& s, std::vector<Vec2>& out) {
    if (!s.arc) {
        out.push_back(s.a);
        out.push_back(s.b);
        return;
    }
    const double r = s.Radius(), a0 = std::atan2(s.a.y - s.center.y, s.a.x - s.center.x), sweep = s.Sweep();
    const int n = std::max(8, static_cast<int>(std::abs(sweep) / (2 * kPi) * 64));
    for (int i = 0; i <= n; ++i) {
        const double t = a0 + sweep * i / n;
        out.push_back(s.center + Vec2{std::cos(t), std::sin(t)} * r);
    }
}

} // namespace

ObjectId LastSheet(const doc::Document& d) {
    for (std::size_t i = d.EndMarker(); i-- > 0;)
        if (const auto* s = dynamic_cast<const doc::SheetMetalFeature*>(d.Features()[i].get()); s && !s->suppressed) return s->id;
    return {};
}

// --- Лист -------------------------------------------------------------------------

bool SheetEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::SheetMetalFeature*>(&f) != nullptr; }

std::string SheetEditor::Hint() const {
    return Tr("Щёлкните замкнутый контур эскиза и впишите толщину — радиус гиба и K-фактор подтянутся сами. Enter — готово, Esc — отмена");
}

std::unique_ptr<doc::SketchSolidFeature> SheetEditor::Create(Session& session, ObjectId sketch) const {
    (void)sketch;
    auto f = std::make_unique<doc::SheetMetalFeature>();
    f->thickness.name = session.doc.NextParameterName();
    f->thickness.expression = "2";
    f->ApplyAuto();
    return f;
}

void SheetEditor::LoadTexts(const doc::Feature& feature) {
    const auto& f = static_cast<const doc::SheetMetalFeature&>(feature);
    myThickness = f.thickness.expression;
    myRadius = f.radius.expression;
    myK = f.kFactor.expression;
    myFocusFirstField = true;
}

void SheetEditor::DrawOptions(AppContext& app, Session& session, doc::SketchSolidFeature& feature, float width) {
    auto& f = static_cast<doc::SheetMetalFeature&>(feature);
    const sheet::Rules rules = f.Rules();
    const std::string mm = UnitLabel(Quantity::Length, session.doc.units);

    Section(app, Tr("Толщина листа"));
    ParameterField(app, session, "thickness", myThickness, f.thickness, width, myFocusFirstField);
    myFocusFirstField = false;
    Caption(app, Tr("Впишите толщину — остальное подстроится под неё."), width);
    Gap(app);

    Section(app, Tr("Радиус гиба (внутренний)"));
    if (AutoChoice(app, "radius-auto", f.radiusAuto, width)) {
        if (!f.radiusAuto) myRadius = Number(rules.radius, 3), f.radius.expression = myRadius;
        Changed(session);
    }
    if (f.radiusAuto)
        Caption(app, Trf("R = {} {} — равен толщине", Number(rules.radius), mm).c_str(), width);
    else
        ParameterField(app, session, "radius", myRadius, f.radius, width, false);
    Gap(app);

    Section(app, Tr("K-фактор"));
    if (AutoChoice(app, "k-auto", f.kAuto, width)) {
        if (!f.kAuto) myK = Number(rules.kFactor, 3), f.kFactor.expression = myK;
        Changed(session);
    }
    if (f.kAuto)
        Caption(app, Trf("K = {} — по DIN 6935 для R/s = {}", Number(rules.kFactor, 3), Number(rules.radius / rules.thickness)).c_str(), width);
    else
        ParameterField(app, session, "k", myK, f.kFactor, width, false, false);
    Caption(app, Tr("Где нейтральный слой: доля толщины от внутренней стороны сгиба. Влияет на длину развёртки."), width);
    Gap(app);

    static constexpr std::array<ChoiceItem, 2> sides{{{"dir-up", N_("Вверх от эскиза")}, {"dir-down", N_("Вниз от эскиза")}}};
    const int current = f.reversed ? 1 : 0;
    if (const int chosen = ToolChoice(app, "side", N_("Толщина откладывается"), sides, current, width); chosen != current && chosen >= 0) {
        f.reversed = chosen == 1;
        Changed(session);
    }
}

// --- Отбортовка ---------------------------------------------------------------------

void FlangeEditor::StartNew(AppContext& app, Session& session) {
    const ObjectId sheet = LastSheet(session.doc);
    if (!sheet || !session.model.Sheet(session.doc, sheet)) {
        app.notes.Info(Tr("Сначала сделайте лист: эскиз с замкнутым контуром и «Лист»"));
        return;
    }
    auto f = std::make_unique<doc::FlangeFeature>();
    f->sheet = sheet;
    f->height.name = session.doc.NextParameterName();
    myMarked.reset();
    OpenNew(app, session, std::move(f));
    myFocusFirstField = true;
}

void FlangeEditor::StartNext(AppContext& app, Session& session, const doc::Feature& previous) {
    StartNew(app, session);
    auto* next = dynamic_cast<doc::FlangeFeature*>(Edited(session));
    const auto& prev = static_cast<const doc::FlangeFeature&>(previous);
    if (!next) return;
    next->height.expression = prev.height.expression;
    next->angle.expression = prev.angle.expression;
    next->down = prev.down;
    LoadTexts(*next);
    Changed(session);
}

bool FlangeEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::FlangeFeature*>(&f) != nullptr; }

bool FlangeEditor::ShowStatus(const doc::Feature& feature) const { return static_cast<const doc::FlangeFeature&>(feature).edgeSet; }

std::string FlangeEditor::Hint() const {
    return Tr("Щёлкните ребро листа или дальнее ребро полки, введите высоту и угол. Enter — готово, Esc — отмена");
}

void FlangeEditor::LoadTexts(const doc::Feature& feature) {
    const auto& f = static_cast<const doc::FlangeFeature&>(feature);
    myHeight = f.height.expression;
    myAngle = f.angle.expression;
    myOffset1 = f.offset1.expression;
    myOffset2 = f.offset2.expression;
    myWidth = f.width.expression;
}

void FlangeEditor::DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) {
    auto& f = static_cast<doc::FlangeFeature&>(feature);
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();

    Section(app, Tr("Ребро"));
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(34);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(c.accentSoft), ui.S(8));
        dl->AddRect(p, {p.x + width, p.y + h}, ui::ToU32(c.accentSoftBorder), ui.S(8));
        ui.icons.Draw(dl, f.edgeSet ? "ui-check" : "status-warning", {p.x + ui.S(10), p.y + (h - ui.S(14)) / 2}, ui.S(14), c.accent);
        std::string chip = f.edgeSet ? std::string(Tr("Ребро листа")) : std::string(Tr("Щёлкните ребро листа"));
        if (f.onFlange)
            if (const doc::Feature* under = session.doc.Find(f.onFlange))
                chip = f.onEdge == sheet::FlangeEdge::Far ? Trf("Дальнее ребро полки «{}»", under->name)
                                                          : Trf("Боковое ребро полки «{}»", under->name);
        dl->AddText(ui.fonts.medium, ui.S(13), {p.x + ui.S(32), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(c.text), chip.c_str());
        ImGui::Dummy({width, h});
    }
    Caption(app, f.onFlange && f.onEdge != sheet::FlangeEdge::Far
                     ? Tr("На боковом ребре полки: гнётся от самого ребра, полка под ней не укорачивается. «Вверх» — к её внутренней стороне.")
                 : f.onFlange ? Tr("Отбортовка на отбортовке. «Вверх» — в сторону внутренней стороны полки под ней (для полки 90° — к листу).")
                            : Tr("Прямое ребро плоского листа или ребро плоской части полки (дальнее или боковое), сверху или снизу — всё равно."),
            width);
    Gap(app);

    Section(app, Tr("Высота (по наружной стороне)"));
    ParameterField(app, session, "height", myHeight, f.height, width, myFocusFirstField);
    myFocusFirstField = false;
    Section(app, Tr("Угол"));
    ParameterField(app, session, "angle", myAngle, f.angle, width, false);
    Gap(app);

    static constexpr std::array<ChoiceItem, 2> sides{{{"dir-up", N_("Вверх")}, {"dir-down", N_("Вниз")}}};
    const int current = f.down ? 1 : 0;
    if (const int chosen = ToolChoice(app, "side", N_("Направление"), sides, current, width); chosen != current && chosen >= 0) {
        f.down = chosen == 1;
        Changed(session);
    }

    // How much of a plate edge.
    if (!f.onFlange) {
        Gap(app);
        Section(app, Tr("Ширина"));
        std::array<const char*, 3> modes{Tr("Всё ребро"), Tr("Отступы"), Tr("По центру")};
        const int mode = static_cast<int>(f.widthMode);
        if (const int chosen = ui::Segmented(app.ui, "width-mode", modes, mode, 3, width); chosen != mode && chosen >= 0) {
            f.widthMode = static_cast<doc::FlangeWidth>(chosen);
            Changed(session);
        }
        if (f.widthMode == doc::FlangeWidth::Offsets) {
            Section(app, Tr("От начала ребра"));
            ParameterField(app, session, "offset1", myOffset1, f.offset1, width, false);
            Section(app, Tr("От конца ребра"));
            ParameterField(app, session, "offset2", myOffset2, f.offset2, width, false);
        } else if (f.widthMode == doc::FlangeWidth::Centered) {
            Section(app, Tr("Ширина полки"));
            ParameterField(app, session, "width", myWidth, f.width, width, false);
        }
        if (f.widthMode != doc::FlangeWidth::Edge)
            if (const auto info = session.model.SheetAt(f.id))
                Caption(app, Trf("По краям — рельефы {} × {} мм за зоной сгиба, чтобы металл не рвался.",
                                 Number(sheet::Relief::For(info->part.rules).width), Number(sheet::Relief::For(info->part.rules).depth))
                                 .c_str(),
                        width);
    }

    if (const auto info = session.model.SheetAt(f.id); info && !info->layout.flanges.empty()) {
        const sheet::BentFlange& b = info->layout.flanges.back();
        const sheet::Rules& r = info->part.rules;
        Gap(app);
        Caption(app,
                Trf("Лист {} мм, R {} мм, K {}. Прямая часть полки {} мм, сгиб в развёртке {} мм.", Number(r.thickness),
                    Number(r.radius), Number(r.kFactor, 3), Number(b.straight), Number(b.allowance))
                    .c_str(),
                width);
    }
}

void FlangeEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    auto* f = dynamic_cast<doc::FlangeFeature*>(Edited(session));
    if (!f) return;
    const int index = session.doc.IndexOf(f->id);
    if (index < 0) return;
    app.viewport.BeginPickingOriginal(session.model, static_cast<std::size_t>(index), render::PickTarget::Edges);
    if (myMarked) app.viewport.SetMarkedEdges({*myMarked});
    else app.viewport.SetMarkedEdges({});

    if (!vf.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    const auto picked = app.viewport.PickEdge(vf.ToLocal(ImGui::GetMousePos()));
    if (!picked) return;
    // The part as the operation before this one left it: what the view shows to pick from.
    std::optional<kernel::SheetInfo> info;
    for (int k = index; k-- > 0 && !info;) {
        const doc::Feature& g = *session.doc.Features()[static_cast<std::size_t>(k)];
        const auto* fl = dynamic_cast<const doc::FlangeFeature*>(&g);
        const auto* cut = dynamic_cast<const doc::SheetCutFeature*>(&g);
        if ((g.id == f->sheet || (fl && fl->sheet == f->sheet) || (cut && cut->sheet == f->sheet)) && !g.suppressed) info = session.model.SheetAt(g.id);
    }
    if (!info) return;
    const Frame& frame = info->frame;
    const double s = info->part.rules.thickness, z0 = info->part.reversed ? -s : 0.0;
    const Vec2 p2 = frame.ToLocal(picked->middle);
    const Vec3 p{p2.x, p2.y, (picked->middle - frame.origin).Dot(frame.normal)};   // part coordinates
    const bool line = picked->curve == doc::CurveKind::Line;
    if (line && std::abs(picked->direction.Dot(frame.normal)) < 1e-3 && p.z > z0 - 1e-3 && p.z < z0 + s + 1e-3) {
        f->edge = picked->middle;   // an edge of the plate
        f->edgeSet = true;
        f->onFlange = {};
        myMarked = *picked;
        Changed(session);
        return;
    }
    // An edge of a flange's flat part: a flange on a flange — on its far edge or a side edge.
    const auto distance = [](Vec3 q, Vec3 a, Vec3 b) {
        const Vec3 d = b - a;
        const double t = std::clamp((q - a).Dot(d) / std::max(d.Dot(d), 1e-12), 0.0, 1.0);
        return (q - (a + d * t)).Length();
    };
    for (std::size_t k = 0; k < info->layout.flanges.size() && k < info->flangeIds.size(); ++k) {
        const sheet::BentFlange& b = info->layout.flanges[k];
        if (info->flangeIds[k] == f->id) continue;
        const Vec3 up = b.stripNormal * s;
        const Vec3 far0 = b.stripStart + b.stripOut * b.straight, far1 = far0 + b.along * b.width;
        const Vec3 s0 = b.stripStart, s1 = s0 + b.stripOut * b.straight;   // the side at the start
        const Vec3 e0 = s0 + b.along * b.width, e1 = e0 + b.stripOut * b.straight;   // the side at the end
        const auto close = [&](Vec3 a, Vec3 e) { return std::min(distance(p, a, e), distance(p, a + up, e + up)) < 0.05; };
        std::optional<sheet::FlangeEdge> which;
        if (close(far0, far1)) which = sheet::FlangeEdge::Far;
        else if (close(s0, s1)) which = sheet::FlangeEdge::Start;
        else if (close(e0, e1)) which = sheet::FlangeEdge::End;
        if (!which) continue;
        const int taken = *which == sheet::FlangeEdge::Far ? b.child : *which == sheet::FlangeEdge::Start ? b.sideStart : b.sideEnd;
        if (taken >= 0 && (static_cast<std::size_t>(taken) >= info->flangeIds.size() || info->flangeIds[static_cast<std::size_t>(taken)] != f->id)) {
            app.notes.Info(Tr("На этом ребре полки уже есть отбортовка"));
            return;
        }
        if (*which != sheet::FlangeEdge::Far && b.parent >= 0 && b.onEdge != sheet::FlangeEdge::Far) {
            app.notes.Info(Tr("На боковой полке отбортовки пока не ставятся"));
            return;
        }
        f->onFlange = info->flangeIds[k];
        f->onEdge = *which;
        f->widthMode = doc::FlangeWidth::Edge;
        f->edge = picked->middle;
        f->edgeSet = true;
        myMarked = *picked;
        Changed(session);
        return;
    }
    app.notes.Info(Tr("Выберите прямое ребро плоского листа или ребро плоской части полки"));
}

// --- Вырез ------------------------------------------------------------------------------

bool SheetCutEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::SheetCutFeature*>(&f) != nullptr; }

std::string SheetCutEditor::Hint() const {
    return Tr("Щёлкните замкнутые контуры эскиза, лежащего на грани листа или полки. Enter — готово, Esc — отмена");
}

std::unique_ptr<doc::SketchSolidFeature> SheetCutEditor::Create(Session& session, ObjectId sketch) const {
    (void)sketch;
    auto f = std::make_unique<doc::SheetCutFeature>();
    f->sheet = LastSheet(session.doc);
    f->operation = doc::BooleanOp::Cut;
    return f;
}

void SheetCutEditor::DrawOptions(AppContext& app, Session& session, doc::SketchSolidFeature& feature, float width) {
    auto& f = static_cast<doc::SheetCutFeature&>(feature);
    f.operation = doc::BooleanOp::Cut;
    if (!f.sheet) f.sheet = LastSheet(session.doc);
    Caption(app, Tr("Вырез сквозь лист по нормали к грани. Эскиз рисуйте на грани плоской части листа или полки — вырез попадёт и в развёртку. "
                    "Через сгиб и за край листа пока нельзя."),
            width);
}

// --- Развёртка ------------------------------------------------------------------------

void DrawFlatPatternWindow(AppContext& app) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    Session* s = app.Doc();
    if (!s) app.shell.showFlatPattern = false;
    if (!BeginDialog(ui, "##flat-pattern", app.shell.showFlatPattern, ui.S(640))) return;
    const auto close = [&] {
        app.shell.showFlatPattern = false;
        ImGui::CloseCurrentPopup();
    };
    const ObjectId base = s ? LastSheet(s->doc) : ObjectId{};
    const auto info = base ? s->model.Sheet(s->doc, base) : std::nullopt;
    {
        ui::FontScope font(ui.fonts.title);
        ImGui::TextUnformatted(Tr("Развёртка"));
    }
    if (!info) {
        ImGui::TextUnformatted(Tr("В детали нет листа: сделайте эскиз и операцию «Лист»."));
        if (ui::PrimaryButton(ui, "##flat-close0", Tr("Закрыть"), "Esc", {ImGui::GetContentRegionAvail().x, ui.S(36)}) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape))
            close();
        ImGui::EndPopup();
        return;
    }
    const sheet::FlatPattern flat = sheet::Flatten(info->part, info->layout);
    const Box2 box = flat.Bounds();
    const double w = box.max.x - box.min.x, h = box.max.y - box.min.y;

    // The blank, fitted into the window, y up.
    const float areaW = ImGui::GetContentRegionAvail().x, areaH = ui.S(320);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, {p0.x + areaW, p0.y + areaH}, ui::ToU32(c.field), ui.S(8));
    dl->AddRect(p0, {p0.x + areaW, p0.y + areaH}, ui::ToU32(c.fieldBorder), ui.S(8));
    const float margin = ui.S(36);
    const double scale = std::min((areaW - 2 * margin) / std::max(w, 1e-6), (areaH - 2 * margin) / std::max(h, 1e-6));
    const ImVec2 mid{p0.x + areaW / 2, p0.y + areaH / 2};
    const Vec2 centre = (box.min + box.max) * 0.5;
    const auto toScreen = [&](Vec2 v) {
        return ImVec2{mid.x + static_cast<float>((v.x - centre.x) * scale), mid.y - static_cast<float>((v.y - centre.y) * scale)};
    };
    const auto drawLoop = [&](const sheet::Loop& loop, ImU32 col, float thick) {
        std::vector<ImVec2> pts;
        for (const sheet::Segment& seg : loop) {
            std::vector<Vec2> sampled;
            Sample(seg, sampled);
            for (Vec2 v : sampled) {
                const ImVec2 q = toScreen(v);
                if (pts.empty() || std::abs(pts.back().x - q.x) + std::abs(pts.back().y - q.y) > 0.25f) pts.push_back(q);
            }
        }
        if (pts.size() > 2 && std::abs(pts.back().x - pts.front().x) + std::abs(pts.back().y - pts.front().y) <= 0.25f) pts.pop_back();
        if (pts.size() > 1) dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), col, ImDrawFlags_Closed, thick);
    };
    drawLoop(flat.outline.outer, ui::ToU32(c.text), ui.S(1.6f));
    for (const sheet::Loop& hole : flat.outline.holes) drawLoop(hole, ui::ToU32(c.text), ui.S(1.3f));
    const ImU32 bendColor = ui::ToU32(c.danger);   // bend lines: red, as in the DXF
    for (const sheet::FlatPattern::Bend& b : flat.bends) {
        const ImVec2 a = toScreen(b.a), e = toScreen(b.b);
        const float len = std::hypot(e.x - a.x, e.y - a.y), dash = ui.S(6), gap = ui.S(4);
        const ImVec2 d{(e.x - a.x) / std::max(len, 1.0f), (e.y - a.y) / std::max(len, 1.0f)};
        for (float t = 0; t < len; t += dash + gap) {
            const float t1 = std::min(len, t + dash);
            dl->AddLine({a.x + d.x * t, a.y + d.y * t}, {a.x + d.x * t1, a.y + d.y * t1}, bendColor, ui.S(1.2f));
        }
        const std::string note = sheet::BendNote(b);
        const ImVec2 ts = ui.fonts.caption->CalcTextSizeA(ui.S(12), FLT_MAX, 0, note.c_str());
        const ImVec2 m{(a.x + e.x) / 2, (a.y + e.y) / 2};
        // Above a lying line; beside a standing one, towards the middle of the blank.
        const bool standing = std::abs(e.y - a.y) > std::abs(e.x - a.x);
        const ImVec2 at = !standing ? ImVec2{m.x - ts.x / 2, m.y - ts.y - ui.S(2)}
                        : m.x > mid.x ? ImVec2{m.x - ts.x - ui.S(6), m.y - ts.y / 2}
                                      : ImVec2{m.x + ui.S(6), m.y - ts.y / 2};
        dl->AddText(ui.fonts.caption, ui.S(12), at, bendColor, note.c_str());
    }
    // Overall size under and beside the blank.
    {
        const std::string sw = Number(w) + " " + Tr("мм"), sh = Number(h) + " " + Tr("мм");
        const ImVec2 lo = toScreen(box.min), hi = toScreen(box.max);
        const ImU32 dim = ui::ToU32(c.textSecondary);
        const ImVec2 tw = ui.fonts.mono->CalcTextSizeA(ui.S(12), FLT_MAX, 0, sw.c_str());
        dl->AddLine({lo.x, lo.y + ui.S(14)}, {hi.x, lo.y + ui.S(14)}, dim, 1.0f);
        dl->AddText(ui.fonts.mono, ui.S(12), {(lo.x + hi.x) / 2 - tw.x / 2, lo.y + ui.S(16)}, dim, sw.c_str());
        dl->AddLine({hi.x + ui.S(14), lo.y}, {hi.x + ui.S(14), hi.y}, dim, 1.0f);
        dl->AddText(ui.fonts.mono, ui.S(12), {hi.x + ui.S(18), (lo.y + hi.y) / 2 - ui.S(6)}, dim, sh.c_str());
    }
    ImGui::Dummy({areaW, areaH});

    const sheet::Rules& r = info->part.rules;
    ImGui::TextUnformatted(Trf("Заготовка {} × {} мм · лист {} мм · R {} мм · K {} · сгибов: {}", Number(w), Number(h), Number(r.thickness),
                               Number(r.radius), Number(r.kFactor, 3), flat.bends.size())
                               .c_str());
    const double area = flat.Area();
    std::string mass = Trf("Площадь {} мм²", FormatGrouped(area, 1));
    if (const Material* m = FindMaterial(s->doc.info.material))
        mass += Trf(" · масса {} кг ({})", FormatGrouped(area * r.thickness / 1000.0 * m->density / 1000.0, 3), Tr(m->name));
    {
        ui::FontScope font(ui.fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
        ImGui::TextUnformatted(mass.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Dummy({0, ui.S(6)});

    const float bw = (ImGui::GetContentRegionAvail().x - ui.S(16)) / 3;
    if (ui::SecondaryButton(ui, "##flat-editor", Tr("Редактор DXF"), nullptr, {bw, ui.S(36)})) {
        OpenDxfEditor(app, sheet::FlatPatternDrawing(flat), s->Title() + Tr(" развёртка"));
        close();
        ImGui::EndPopup();
        return;
    }
    ImGui::SameLine(0, ui.S(8));
    if (ui::SecondaryButton(ui, "##flat-dxf", Tr("Сохранить DXF…"), nullptr, {bw, ui.S(36)})) {
        if (const std::string problem = platform::FileDialogProblem(); !problem.empty()) {
            app.notes.Error(problem);
        } else {
            const std::filesystem::path folder = s->path.empty() ? platform::DocumentsDir() : s->path.parent_path();
            if (const auto file = platform::ShowSaveDialog(Tr("Развёртка в DXF"), folder / PathFromUtf8(s->Title() + Tr(" развёртка") + ".dxf"),
                                                           {{"DXF", "*.dxf"}}, ".dxf")) {
                std::ofstream out(*file, std::ios::binary);
                out << sheet::FlatPatternDxf(flat);
                if (out && platform::FileWritten(*file)) app.notes.Info(Trf("Развёртка сохранена: {}", ToUtf8(file->filename())));
                else app.notes.Error(Tr("Не удалось записать файл развёртки"));
            }
        }
    }
    ImGui::SameLine(0, ui.S(8));
    if (ui::PrimaryButton(ui, "##flat-close", Tr("Закрыть"), "Esc", {bw, ui.S(36)}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) close();
    ImGui::EndPopup();
}

} // namespace bz::app
