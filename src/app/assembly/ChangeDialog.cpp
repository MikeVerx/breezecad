// «Обнаружено изменение, затрагивающее другие детали» (see ChangeDialog.h).
#include "app/assembly/ChangeDialog.h"

#include "app/AssemblyState.h"
#include "app/Language.h"
#include "app/Session.h"
#include "app/assembly/InPlace.h"
#include "app/sketch/SketchEditor.h"
#include "core/Monitor.h"
#include "core/PathText.h"
#include "document/SketchFeature.h"
#include "kernel/ChangePreview.h"
#include "ui/Theme.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <future>

namespace bz::app {

using kernel::ChangeChoice;

namespace {

constexpr double kDeflection = 0.002;   // a share of the part's size

/// One choice as the preview shows it.
struct VariantPreview {
    kernel::PreviewMesh part;                 ///< the part after the choice
    kernel::PreviewMesh haze;                 ///< the volume that changed
    std::vector<Vec3>   sketch;               ///< segments: the sketches drawn (red or yellow)
    std::vector<Vec3>   errors;               ///< where the sketches went wrong
    std::string         error;                ///< the choice cannot be made (Russian)
};

/// The preview of one variant: made on a background thread from copies.
struct Pending {
    std::future<VariantPreview> work;
    std::optional<VariantPreview> ready;
    std::string key;   ///< what it was made for: the change, the gap
};

} // namespace

struct ChangeDialogState {
    std::size_t                current = 0;   ///< the change shown
    std::vector<ChangeChoice>  choices;       ///< per change of AssemblyState::changes
    std::string                gap = "0,2";   ///< «Зазор», as typed
    bool                       rollback = true;
    bool                       sameForAll = false;
    /// Preview: per variant; the source part and its new contour.
    std::array<Pending, 4>     variants;
    std::future<kernel::PreviewMesh> sourceWork;
    std::optional<kernel::PreviewMesh> source;
    std::vector<Vec3>          contour;       ///< the source's new contour, segments
    std::string                sourceKey;
    float                      yaw = -0.65f, pitch = 0.55f, zoom = 1.0f;
};

namespace {

double ParseGap(std::string text) {
    std::replace(text.begin(), text.end(), ',', '.');
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    return end == text.c_str() ? -1.0 : v;
}

std::size_t Index(ChangeChoice c) { return static_cast<std::size_t>(c); }

/// The curves of a sketch as 3D segments in its part.
void SketchSegments(const doc::SketchFeature& sk, const std::vector<ObjectId>* only, double offset, std::vector<Vec3>& out) {
    const sketch::Sketch& s = sk.sketch;
    const Frame& f = sk.placement.frame;
    const Vec3 y = f.YDir();
    const auto at = [&](Vec2 p) { return f.origin + f.xDir * p.x + y * p.y + f.normal * offset; };
    const auto wanted = [&](ObjectId id) { return !only || std::find(only->begin(), only->end(), id) != only->end(); };
    for (const sketch::Line& l : s.lines)
        if (!l.construction && wanted(l.id)) {
            out.push_back(at(s.PositionOf(l.start)));
            out.push_back(at(s.PositionOf(l.end)));
        }
    const auto arc = [&](Vec2 c, double r, double a0, double a1) {
        const int n = std::max(8, static_cast<int>(std::abs(a1 - a0) / 0.15));
        for (int i = 0; i < n; ++i) {
            const double t0 = a0 + (a1 - a0) * i / n, t1 = a0 + (a1 - a0) * (i + 1) / n;
            out.push_back(at(c + Vec2{std::cos(t0), std::sin(t0)} * r));
            out.push_back(at(c + Vec2{std::cos(t1), std::sin(t1)} * r));
        }
    };
    for (const sketch::Circle& c : s.circles)
        if (!c.construction && wanted(c.id)) arc(s.PositionOf(c.center), c.radius, 0, 2 * 3.14159265358979);
    for (const sketch::Arc& a : s.arcs)
        if (!a.construction && wanted(a.id)) {
            const Vec2 c = s.PositionOf(a.center), p = s.PositionOf(a.start) - c, q = s.PositionOf(a.end) - c;
            double a0 = std::atan2(p.y, p.x), a1 = std::atan2(q.y, q.x);
            if (a1 <= a0) a1 += 2 * 3.14159265358979;
            arc(c, p.Length(), a0, a1);
        }
}

/// Where a sketch went wrong: failing constraints and broken projections.
void SketchErrors(const doc::SketchFeature& sk, std::vector<Vec3>& out) {
    const sketch::Sketch& s = sk.sketch;
    const Frame& f = sk.placement.frame;
    const auto at = [&](Vec2 p) { return f.origin + f.xDir * p.x + f.YDir() * p.y; };
    const auto middle = [&](ObjectId id) -> std::optional<Vec2> {
        if (s.FindPoint(id)) return s.PositionOf(id);
        if (const sketch::Line* l = s.FindLine(id)) return (s.PositionOf(l->start) + s.PositionOf(l->end)) * 0.5;
        if (const sketch::Circle* c = s.FindCircle(id)) return s.PositionOf(c->center) + Vec2{c->radius, 0};
        if (const sketch::Arc* a = s.FindArc(id)) return s.PositionOf(a->start);
        return std::nullopt;
    };
    for (ObjectId id : sk.analysis.failing)
        if (const sketch::Constraint* c = s.FindConstraint(id))
            for (ObjectId r : c->refs)
                if (const auto p = middle(r)) {
                    out.push_back(at(*p));
                    break;
                }
    for (ObjectId curve : sk.BrokenProjections())
        if (const auto p = middle(curve)) out.push_back(at(*p));
}

/// Starts (or keeps) the previews of the change shown.
void MakePreviews(ChangeDialogState& d, const AssemblyState& st, std::string_view name) {
    const AssemblyState::SourceChange& change = st.changes[d.current];
    const doc::Document* part = st.geometry.PartOf(change.component);
    if (!part) return;
    const std::string base = std::format("{}#{}", ToUtf8(change.partFile), d.current);
    for (std::size_t i = 0; i < 4; ++i) {
        const auto choice = static_cast<ChangeChoice>(i);
        const std::string key = choice == ChangeChoice::Mimic ? base + "#" + d.gap : base;
        Pending& p = d.variants[i];
        if (p.key == key) continue;
        p.key = key;
        p.ready.reset();
        VariantPreview overlay;
        Expected<doc::Document> after = st.TryChange(d.current, choice, ParseGap(d.gap), name);
        if (!after) {
            overlay.error = after.GetError().message;
            p.work = {};
            p.ready = std::move(overlay);
            continue;
        }
        // The sketches, as the choice leaves them (cheap: read here).
        for (const kernel::AffectedSketch& hit : change.sketches) {
            const auto* sk = after->FindAs<doc::SketchFeature>(hit.sketch);
            if (!sk) continue;
            if (choice == ChangeChoice::Ignore) {
                SketchSegments(*sk, &hit.curves, 0, overlay.sketch);
            } else if (choice != ChangeChoice::Mimic) {
                SketchSegments(*sk, nullptr, 0, overlay.sketch);
                SketchErrors(*sk, overlay.errors);
            }
        }
        // The bodies: rebuilt and meshed on a background thread, from copies.
        p.work = std::async(std::launch::async, [before = part->Clone(), after = std::move(*after), overlay = std::move(overlay),
                                                 choice]() mutable {
            kernel::Model was, is;
            was.Rebuild(before);
            is.Rebuild(after);
            overlay.part = kernel::MeshOf(is, kDeflection);
            if (choice != ChangeChoice::Ignore && choice != ChangeChoice::Manual)
                overlay.haze = kernel::ChangedVolumeOf(was, is, kDeflection);
            return std::move(overlay);
        });
    }
    // The source part, where it stands around the part, and its new contour.
    if (d.sourceKey != base) {
        d.sourceKey = base;
        d.source.reset();
        d.contour.clear();
        const kernel::AffectedSketch& hit = change.sketches.front();
        const doc::Document* from = st.geometry.PartOf(hit.sourceComponent);
        if (from && st.assembly.FindComponent(hit.sourceComponent) && st.assembly.FindComponent(change.component)) {
            const assembly::Placement where =
                st.assembly.PlacementOf(change.component).Inverse() * st.assembly.PlacementOf(hit.sourceComponent);
            double offset = 0;
            if (const auto* sk = part->FindAs<doc::SketchFeature>(hit.sketch))
                for (const doc::ContextProjection& p : sk->contextProjections)
                    if (p.component == hit.sourceComponent && p.source.sketch == hit.sourceSketch) offset = p.source.offset;
            if (const auto* src = from->FindAs<doc::SketchFeature>(hit.sourceSketch)) SketchSegments(*src, nullptr, offset, d.contour);
            for (Vec3& v : d.contour) v = where.Point(v);
            d.sourceWork = std::async(std::launch::async, [doc = from->Clone(), where]() mutable {
                kernel::Model m;
                m.Rebuild(doc);
                kernel::PreviewMesh mesh = kernel::MeshOf(m, kDeflection);
                for (Vec3& v : mesh.triangles) v = where.Point(v);
                for (Vec3& v : mesh.segments) v = where.Point(v);
                return mesh;
            });
        }
    }
}

template <class T>
bool Ready(std::future<T>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

/// The read-only view: the part turned by yaw and pitch, fitted into the rect.
struct View {
    float  yaw, pitch;
    Vec3   centre;
    double scale;
    ImVec2 mid;
    Vec3 Turn(Vec3 v) const {
        const double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
        const double x1 = v.x * cy - v.y * sy, y1 = v.x * sy + v.y * cy;
        return {x1, y1 * cp - v.z * sp, y1 * sp + v.z * cp};   // x right, y depth (away), z up
    }
    ImVec2 Screen(Vec3 v, double* depth = nullptr) const {
        const Vec3 t = Turn(v - centre);
        if (depth) *depth = t.y;
        return {mid.x + static_cast<float>(t.x * scale), mid.y - static_cast<float>(t.z * scale)};
    }
};

void DrawPreview(ui::Ui& ui, ImDrawList* dl, ImVec2 p0, ImVec2 p1, ChangeDialogState& d, const VariantPreview* v) {
    const ui::ThemeColors& c = ui.C();
    dl->AddRectFilledMultiColor(p0, p1, ui::ToU32(c.viewportTop), ui::ToU32(c.viewportTop), ui::ToU32(c.viewportBottom),
                                ui::ToU32(c.viewportBottom));
    if (!v) {
        const char* wait = Tr("Считаю превью…");
        const ImVec2 s = ui.fonts.body->CalcTextSizeA(ui.S(13), FLT_MAX, 0, wait);
        dl->AddText(ui.fonts.body, ui.S(13), {(p0.x + p1.x - s.x) / 2, (p0.y + p1.y - s.y) / 2}, ui::ToU32(c.textMuted), wait);
        return;
    }
    // Fit the part and the source.
    Vec3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    const auto grow = [&](const std::vector<Vec3>& pts) {
        for (const Vec3& q : pts) {
            lo = {std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z)};
            hi = {std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z)};
        }
    };
    grow(v->part.triangles);
    grow(d.contour);
    if (lo.x > hi.x) return;
    const double size = std::max(1e-6, (hi - lo).Length());
    View view{d.yaw, d.pitch, (lo + hi) * 0.5, 0.0, {(p0.x + p1.x) / 2, (p0.y + p1.y) / 2 + ui.S(10)}};
    view.scale = std::min(p1.x - p0.x, p1.y - p0.y) * 0.78 / size * d.zoom;

    // Faces and edges sorted far to near (no depth buffer: the view is small).
    struct Prim {
        double depth;
        int    kind;   // 0 face, 1 edge, 2 source face
        ImVec2 a, b, cc;
        float  light;
    };
    std::vector<Prim> prims;
    const auto faces = [&](const std::vector<Vec3>& tris, int kind) {
        for (std::size_t i = 0; i + 2 < tris.size(); i += 3) {
            const Vec3 n = view.Turn((tris[i + 1] - tris[i]).Cross(tris[i + 2] - tris[i]));
            const double len = n.Length();
            if (len < 1e-12 || (kind == 0 && n.y > 0)) continue;   // facing away
            double d0 = 0, d1 = 0, d2 = 0;
            Prim pr{0, kind, view.Screen(tris[i], &d0), view.Screen(tris[i + 1], &d1), view.Screen(tris[i + 2], &d2), 0};
            pr.depth = (d0 + d1 + d2) / 3;
            pr.light = static_cast<float>(0.55 + 0.45 * std::abs((n.x * 0.35 - n.y * 0.75 + n.z * 0.55) / len));
            prims.push_back(pr);
        }
    };
    faces(v->part.triangles, 0);
    if (d.source) faces(d.source->triangles, 2);
    for (std::size_t i = 0; i + 1 < v->part.segments.size(); i += 2) {
        double d0 = 0, d1 = 0;
        Prim pr{0, 1, view.Screen(v->part.segments[i], &d0), view.Screen(v->part.segments[i + 1], &d1), {}, 0};
        pr.depth = (d0 + d1) / 2 - size * 0.004;   // over the faces it bounds
        prims.push_back(pr);
    }
    std::sort(prims.begin(), prims.end(), [](const Prim& x, const Prim& y) { return x.depth > y.depth; });
    dl->PushClipRect(p0, p1, true);
    const Color body = c.body, sourceTint = c.accent;
    for (const Prim& pr : prims) {
        if (pr.kind == 1) {
            dl->AddLine(pr.a, pr.b, ui::ToU32(c.bodyEdges.WithAlpha(0.85f)), ui.S(1.0f));
        } else if (pr.kind == 0) {
            const Color shade{body.r * pr.light, body.g * pr.light, body.b * pr.light, 1.0f};
            dl->AddTriangleFilled(pr.a, pr.b, pr.cc, ui::ToU32(shade));
        } else {
            dl->AddTriangleFilled(pr.a, pr.b, pr.cc, ui::ToU32(sourceTint.WithAlpha(0.06f)));
        }
    }
    // The source's new contour: blue dashes.
    for (std::size_t i = 0; i + 1 < d.contour.size(); i += 2) {
        const ImVec2 a = view.Screen(d.contour[i]), b = view.Screen(d.contour[i + 1]);
        const float len = std::hypot(b.x - a.x, b.y - a.y), dash = ui.S(5);
        for (float t = 0; t < len; t += 2 * dash) {
            const float u0 = t / len, u1 = std::min(1.0f, (t + dash) / len);
            dl->AddLine({a.x + (b.x - a.x) * u0, a.y + (b.y - a.y) * u0}, {a.x + (b.x - a.x) * u1, a.y + (b.y - a.y) * u1},
                        ui::ToU32(c.accent), ui.S(1.2f));
        }
    }
    // The changed volume: a red haze seen from anywhere (drawn over everything;
    // no anti-aliased fill: the seams of its triangles would show).
    const ImDrawListFlags flags = dl->Flags;
    dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    for (std::size_t i = 0; i + 2 < v->haze.triangles.size(); i += 3)
        dl->AddTriangleFilled(view.Screen(v->haze.triangles[i]), view.Screen(v->haze.triangles[i + 1]),
                              view.Screen(v->haze.triangles[i + 2]), ui::ToU32(c.danger.WithAlpha(0.10f)));
    dl->Flags = flags;
    // The sketches: red when the part stays as it is, yellow when they follow.
    const bool stays = v->errors.empty() && d.choices.size() > d.current && d.choices[d.current] == ChangeChoice::Ignore;
    const Color line = stays ? c.danger : c.warning;
    for (std::size_t i = 0; i + 1 < v->sketch.size(); i += 2)
        dl->AddLine(view.Screen(v->sketch[i]), view.Screen(v->sketch[i + 1]), ui::ToU32(line), ui.S(2.2f));
    for (const Vec3& e : v->errors) {
        const ImVec2 at = view.Screen(e);
        dl->AddCircleFilled(at, ui.S(8), ui::ToU32(c.danger));
        dl->AddText(ui.fonts.strong, ui.S(12), {at.x - ui.S(2), at.y - ui.S(7)}, ui::ToU32(c.textOnAccent), "!");
    }
    dl->PopClipRect();
}

/// The glyph of a variant, in a rounded square.
void VariantIcon(ui::Ui& ui, ImDrawList* dl, ImVec2 at, ChangeChoice choice, bool chosen) {
    const ui::ThemeColors& c = ui.C();
    const float s = ui.S(30);
    dl->AddRectFilled(at, {at.x + s, at.y + s}, ui::ToU32(chosen ? c.panel : c.segmentTrack), ui.S(7));
    const ImVec2 m{at.x + s / 2, at.y + s / 2};
    const ImU32 ink = ui::ToU32(c.accent);
    switch (choice) {
        case ChangeChoice::Ignore:
            dl->AddCircle(m, ui.S(7), ink, 0, ui.S(1.6f));
            dl->AddLine({m.x - ui.S(5), m.y - ui.S(5)}, {m.x + ui.S(5), m.y + ui.S(5)}, ink, ui.S(1.6f));
            break;
        case ChangeChoice::Adapt:
            dl->PathArcTo(m, ui.S(7), 3.6f, 6.0f);
            dl->PathStroke(ink, 0, ui.S(1.6f));
            dl->PathArcTo(m, ui.S(7), 0.5f, 2.9f);
            dl->PathStroke(ink, 0, ui.S(1.6f));
            dl->AddTriangleFilled({m.x + ui.S(7), m.y - ui.S(4)}, {m.x + ui.S(4), m.y + ui.S(1)}, {m.x + ui.S(9), m.y + ui.S(1)}, ink);
            dl->AddTriangleFilled({m.x - ui.S(7), m.y + ui.S(4)}, {m.x - ui.S(4), m.y - ui.S(1)}, {m.x - ui.S(9), m.y - ui.S(1)}, ink);
            break;
        case ChangeChoice::Mimic:
            ui.icons.Draw(dl, "sk-offset", {m.x - ui.S(9), m.y - ui.S(9)}, ui.S(18), c.accent);
            break;
        case ChangeChoice::Manual:
            ui.icons.Draw(dl, "sketch", {m.x - ui.S(9), m.y - ui.S(9)}, ui.S(18), c.accent);
            break;
    }
}

/// «Ручное редактирование»: opens the next sketch of the queue when none is
/// open in the part edited in place.
void LeadManualEdit(AppContext& app, Session& as) {
    AssemblyState& st = *as.assembly;
    if (!st.manual.component || st.manual.sketches.empty()) return;
    Session* part = app.Doc();
    if (st.inPlace != st.manual.component) {
        if (st.inPlace || part != &as) return;   // another part is being edited
        if (!EditInPlace(app, as, st.manual.component)) st.manual = {};
        return;
    }
    if (!part || part->host != &as || part->mode != EditMode::Model) return;   // a sketch is still open
    const ObjectId next = st.manual.sketches.front();
    st.manual.sketches.erase(st.manual.sketches.begin());
    auto* sk = part->doc.FindAs<doc::SketchFeature>(next);
    if (!sk) return;
    if (st.manual.rollback && sk->RestoreBeforeBreak(part->doc.Ids())) {
        part->doc.MarkChanged(static_cast<std::size_t>(part->doc.IndexOf(next)));
        part->RebuildNow();
    }
    app.sketch.Edit(app, *part, next);
    mon::Action("assembly", "ручная правка после изменения источника: " + sk->name);
    if (st.manual.sketches.empty()) st.manual = {};
}

const char* ChoiceTitle(ChangeChoice c) {
    switch (c) {
        case ChangeChoice::Ignore: return N_("Игнорировать изменения");
        case ChangeChoice::Adapt: return N_("Автоматическое адаптирование");
        case ChangeChoice::Mimic: return N_("Мимикрирование с зазором");
        case ChangeChoice::Manual: return N_("Ручное редактирование");
    }
    return "";
}
const char* ChoiceNote(ChangeChoice c) {
    switch (c) {
        case ChangeChoice::Ignore: return N_("Деталь остаётся как есть. Изменившиеся эскизы подсвечены красным");
        case ChangeChoice::Adapt: return N_("Эскизы идут за новым контуром, зависимости сохраняются. Видно, что и где изменится");
        case ChangeChoice::Mimic: return N_("Повторить новый контур эквидистантой на заданном расстоянии");
        case ChangeChoice::Manual: return N_("Сразу открыть первый эскиз, где навернулась геометрия");
    }
    return "";
}

} // namespace

void DrawChangeDialog(AppContext& app) {
    Session* as = app.Doc();
    if (!as || !as->assembly) return;
    AssemblyState& st = *as->assembly;
    LeadManualEdit(app, *as);
    const char* id = "##change-dialog";
    if (st.changes.empty() || st.inPlace || app.workspace.ShowsHome()) {
        app.shell.changeDialog.reset();
        return;
    }
    if (!app.shell.changeDialog) app.shell.changeDialog = std::make_shared<ChangeDialogState>();
    ChangeDialogState& d = *app.shell.changeDialog;
    d.choices.resize(st.changes.size(), ChangeChoice::Adapt);
    d.current = std::min(d.current, st.changes.size() - 1);
    const std::string name = as->ContextName();
    MakePreviews(d, st, name);
    for (Pending& p : d.variants)
        if (Ready(p.work)) p.ready = p.work.get();
    if (Ready(d.sourceWork)) d.source = d.sourceWork.get();

    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    if (!ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = std::min(ui.S(1060), display.x - ui.S(24)), height = std::min(ui.S(660), display.y - ui.S(24));
    ImGui::SetNextWindowPos({display.x / 2, display.y / 2}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({width, height});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui.S(14));
    const bool visible = ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    if (!visible) return;
    const AssemblyState::SourceChange& change = st.changes[d.current];
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetWindowPos();
    const float pad = ui.S(24), headH = ui.S(80), footH = ui.S(66), leftW = std::min(ui.S(436), width * 0.42f);
    const ChangeChoice choice = d.choices[d.current];

    // --- head ---------------------------------------------------------------------
    dl->AddRectFilled({o.x + pad, o.y + ui.S(22)}, {o.x + pad + ui.S(36), o.y + ui.S(58)}, ui::ToU32(c.warning.WithAlpha(0.16f)),
                      ui.S(9));
    ui.icons.Draw(dl, "status-warning", {o.x + pad + ui.S(8), o.y + ui.S(30)}, ui.S(20), c.warning);
    const float textX = o.x + pad + ui.S(50), counterW = ui.S(90);
    ui::FittedText(dl, ui.fonts.display, ui.S(18), {textX, o.y + ui.S(22)}, o.x + width - pad - counterW, ui::ToU32(c.text),
                   Tr("Обнаружено изменение, затрагивающее другие детали"));
    const std::string what =
        change.sourceSketch.empty()
            ? Trf("Деталь «{}» изменилась. От неё построены детали сборки — выберите, что с ними делать.", change.source)
            : Trf("Деталь «{}»: изменён «{}». От него построены детали сборки — выберите, что с ними делать.", change.source,
                  change.sourceSketch);
    ui::FittedText(dl, ui.fonts.bodySmall, ui.S(12), {textX, o.y + ui.S(48)}, o.x + width - pad - counterW, ui::ToU32(c.textSecondary),
                   what.c_str());
    {
        const char* label = Tr("Деталь");
        const ImVec2 ls = ui.fonts.caption->CalcTextSizeA(ui.S(11), FLT_MAX, 0, label);
        dl->AddText(ui.fonts.caption, ui.S(11), {o.x + width - pad - ls.x, o.y + ui.S(20)}, ui::ToU32(c.textMuted), label);
        const std::string n = std::to_string(d.current + 1), total = std::format(" / {}", st.changes.size());
        const ImVec2 ts = ui.fonts.display->CalcTextSizeA(ui.S(20), FLT_MAX, 0, total.c_str());
        const ImVec2 ns = ui.fonts.display->CalcTextSizeA(ui.S(20), FLT_MAX, 0, n.c_str());
        const float right = o.x + width - pad;
        dl->AddText(ui.fonts.display, ui.S(20), {right - ts.x, o.y + ui.S(36)}, ui::ToU32(c.textMuted), total.c_str());
        dl->AddText(ui.fonts.display, ui.S(20), {right - ts.x - ns.x, o.y + ui.S(36)}, ui::ToU32(c.text), n.c_str());
    }
    dl->AddLine({o.x, o.y + headH}, {o.x + width, o.y + headH}, ui::ToU32(c.dividerSoft));
    dl->AddLine({o.x, o.y + height - footH}, {o.x + width, o.y + height - footH}, ui::ToU32(c.dividerSoft));
    dl->AddLine({o.x + leftW, o.y + headH}, {o.x + leftW, o.y + height - footH}, ui::ToU32(c.dividerSoft));

    // --- left: the part and the four variants -------------------------------------------
    const float lx = o.x + pad, lw = leftW - 2 * pad;
    float y = o.y + headH + ui.S(18);
    dl->AddText(ui.fonts.title, ui.S(15), {lx, y}, ui::ToU32(c.text), std::format("«{}»", change.part).c_str());
    {
        const std::string n = Trf("затронуто эскизов: {}", change.sketches.size());
        const ImVec2 s = ui.fonts.caption->CalcTextSizeA(ui.S(11), FLT_MAX, 0, n.c_str());
        dl->AddText(ui.fonts.caption, ui.S(11), {lx + lw - s.x, y + ui.S(3)}, ui::ToU32(c.textMuted), n.c_str());
    }
    y += ui.S(30);
    for (std::size_t i = 0; i < 4; ++i) {
        const auto ch = static_cast<ChangeChoice>(i);
        const bool chosen = choice == ch;
        const std::string note = Tr(ChoiceNote(ch));
        const float noteH = ui.fonts.bodySmall->CalcTextSizeA(ui.S(12), FLT_MAX, lw - ui.S(58), note.c_str()).y;
        const float h = ui.S(30) + noteH;
        ImGui::SetCursorScreenPos({lx - ui.S(4), y});
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::InvisibleButton("##variant", {lw + ui.S(8), h})) d.choices[d.current] = ch;
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        if (chosen || hovered)
            dl->AddRectFilled({lx - ui.S(4), y}, {lx + lw + ui.S(4), y + h}, ui::ToU32(chosen ? c.accentSoft : c.hover), ui.S(10));
        if (chosen) dl->AddRect({lx - ui.S(4), y}, {lx + lw + ui.S(4), y + h}, ui::ToU32(c.accentSoftBorder), ui.S(10));
        VariantIcon(ui, dl, {lx + ui.S(8), y + ui.S(10)}, ch, chosen);
        dl->AddText(ui.fonts.strong, ui.S(13), {lx + ui.S(52), y + ui.S(9)}, ui::ToU32(c.text), Tr(ChoiceTitle(ch)));
        dl->AddText(ui.fonts.bodySmall, ui.S(12), {lx + ui.S(52), y + ui.S(28)}, ui::ToU32(c.textSecondary), note.c_str(), nullptr,
                    lw - ui.S(58));
        y += h + ui.S(6);
        if (chosen && ch == ChangeChoice::Mimic) {
            ImGui::SetCursorScreenPos({lx + ui.S(52), y + ui.S(2)});
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(Tr("Зазор"));
            ImGui::SameLine(0, ui.S(8));
            ui::TextField(ui, "##gap", d.gap, ui.S(90));
            ImGui::SameLine(0, ui.S(6));
            ImGui::TextUnformatted(Tr("мм от нового контура"));
            y += ui.S(40);
        }
        if (chosen && ch == ChangeChoice::Manual) {
            dl->AddText(ui.fonts.caption, ui.S(11), {lx + ui.S(52), y}, ui::ToU32(c.textMuted), Tr("Откроются по порядку:"));
            y += ui.S(18);
            for (std::size_t k = 0; k < change.sketches.size() && k < 4; ++k) {
                const auto& s = change.sketches[k];
                const std::string row = Trf("{}. {} — кривых: {}", k + 1, s.name, s.curves.size());
                dl->AddText(ui.fonts.bodySmall, ui.S(12), {lx + ui.S(52), y}, ui::ToU32(c.text), row.c_str());
                y += ui.S(18);
            }
            ImGui::SetCursorScreenPos({lx + ui.S(48), y + ui.S(2)});
            ImGui::Checkbox(Tr("Откатить зависимости эскиза до изменения"), &d.rollback);
            y += ui.S(32);
        }
    }
    ImGui::SetCursorScreenPos({lx, o.y + height - footH - ui.S(40)});
    ImGui::Checkbox(Tr("Так же для всех затронутых деталей"), &d.sameForAll);

    // --- right: the preview -----------------------------------------------------------
    const ImVec2 p0{o.x + leftW + ui.S(16), o.y + headH + ui.S(16)}, p1{o.x + width - ui.S(20), o.y + height - footH - ui.S(16)};
    const VariantPreview* shown = d.variants[Index(choice)].ready ? &*d.variants[Index(choice)].ready : nullptr;
    ImGui::SetCursorScreenPos(p0);
    ImGui::InvisibleButton("##preview", {p1.x - p0.x, p1.y - p0.y});
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        d.yaw += delta.x * 0.01f;
        d.pitch = std::clamp(d.pitch + delta.y * 0.01f, -1.5f, 1.5f);
    }
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0)
        d.zoom = std::clamp(d.zoom * std::pow(1.15f, ImGui::GetIO().MouseWheel), 0.2f, 8.0f);
    if (shown && !shown->error.empty()) {
        DrawPreview(ui, dl, p0, p1, d, nullptr);
        ui::FittedText(dl, ui.fonts.body, ui.S(13), {p0.x + ui.S(14), p1.y - ui.S(60)}, p1.x - ui.S(10), ui::ToU32(c.danger),
                       Tr(shown->error).c_str());
    } else {
        DrawPreview(ui, dl, p0, p1, d, shown);
    }
    dl->AddRect(p0, p1, ui::ToU32(c.dividerSoft), ui.S(10));
    {
        std::string title;
        switch (choice) {
            case ChangeChoice::Ignore: title = Trf("«{}» без изменений", change.part); break;
            case ChangeChoice::Adapt: title = Trf("«{}» после адаптации", change.part); break;
            case ChangeChoice::Mimic: title = Trf("«{}» по новому контуру, зазор {} мм", change.part, d.gap); break;
            case ChangeChoice::Manual: title = Trf("«{}» — эскизы под правку", change.part); break;
        }
        const ImVec2 ts = ui.fonts.strong->CalcTextSizeA(ui.S(12), FLT_MAX, 0, title.c_str());
        dl->AddRectFilled({p0.x + ui.S(12), p0.y + ui.S(12)}, {p0.x + ui.S(22) + ts.x, p0.y + ui.S(34)}, ui::ToU32(c.panel), ui.S(6));
        dl->AddText(ui.fonts.strong, ui.S(12), {p0.x + ui.S(17), p0.y + ui.S(16)}, ui::ToU32(c.text), title.c_str());
        ui::FittedText(dl, ui.fonts.caption, ui.S(11), {p0.x + ui.S(32) + ts.x, p0.y + ui.S(17)}, p1.x - ui.S(10),
                       ui::ToU32(c.textMuted), Tr("Только просмотр · ЛКМ — вращать, колесо — масштаб"));
    }
    {   // the legend
        float lgx = p0.x + ui.S(16);
        const float lgy = p1.y - ui.S(26);
        const auto item = [&](const char* text, int kind) {
            const ImVec2 a{lgx, lgy + ui.S(4)};
            if (kind == 0) dl->AddRectFilled(a, {a.x + ui.S(14), a.y + ui.S(9)}, ui::ToU32(c.danger.WithAlpha(0.35f)), ui.S(2));
            else if (kind == 1) dl->AddLine({a.x, a.y + ui.S(4)}, {a.x + ui.S(14), a.y + ui.S(4)}, ui::ToU32(c.danger), ui.S(2.2f));
            else if (kind == 2) dl->AddLine({a.x, a.y + ui.S(4)}, {a.x + ui.S(14), a.y + ui.S(4)}, ui::ToU32(c.warning), ui.S(2.2f));
            else if (kind == 3) dl->AddCircleFilled({a.x + ui.S(6), a.y + ui.S(4)}, ui.S(6), ui::ToU32(c.danger));
            else {
                dl->AddLine({a.x, a.y + ui.S(4)}, {a.x + ui.S(5), a.y + ui.S(4)}, ui::ToU32(c.accent), ui.S(1.5f));
                dl->AddLine({a.x + ui.S(9), a.y + ui.S(4)}, {a.x + ui.S(14), a.y + ui.S(4)}, ui::ToU32(c.accent), ui.S(1.5f));
            }
            dl->AddText(ui.fonts.caption, ui.S(11), {lgx + ui.S(20), lgy + ui.S(2)}, ui::ToU32(c.textSecondary), text);
            lgx += ui.S(36) + ui.fonts.caption->CalcTextSizeA(ui.S(11), FLT_MAX, 0, text).x;
        };
        const std::string source = Trf("Деталь «{}» (новая)", change.source);
        switch (choice) {
            case ChangeChoice::Ignore: item(Tr("Изменившийся эскиз — деталь не меняется"), 1); break;
            case ChangeChoice::Adapt:
                item(Tr("Эскиз после адаптации"), 2);
                item(Tr("Ошибка эскиза"), 3);
                item(Tr("Изменившийся объём"), 0);
                break;
            case ChangeChoice::Mimic:
                item(Tr("Изменившийся объём"), 0);
                item(source.c_str(), 4);
                break;
            case ChangeChoice::Manual:
                item(Tr("Эскиз под правку"), 2);
                item(Tr("Где навернулась геометрия"), 3);
                break;
        }
    }

    // --- foot ---------------------------------------------------------------------------
    const float fy = o.y + height - footH / 2;
    const bool last = d.current + 1 >= st.changes.size();
    if (!last) {
        const std::string next = Trf("Затем — «{}» ({} / {})", st.changes[d.current + 1].part, d.current + 2, st.changes.size());
        dl->AddText(ui.fonts.bodySmall, ui.S(12), {o.x + pad, fy - ui.S(7)}, ui::ToU32(c.textSecondary), next.c_str());
    }
    std::string primary;
    switch (choice) {
        case ChangeChoice::Ignore: primary = last ? Tr("Оставить") : Tr("Оставить и далее →"); break;
        case ChangeChoice::Adapt: primary = last ? Tr("Адаптировать") : Tr("Адаптировать и далее →"); break;
        case ChangeChoice::Mimic: primary = last ? Tr("Повторить контур") : Tr("Повторить контур и далее →"); break;
        case ChangeChoice::Manual: primary = Trf("Открыть {}", change.sketches.front().name); break;
    }
    const float bh = ui.S(36), bw = std::max(ui.S(200), ui.fonts.medium->CalcTextSizeA(ui.S(13), FLT_MAX, 0, primary.c_str()).x + ui.S(40));
    const float pw = ui.S(140);
    ImGui::SetCursorScreenPos({o.x + width - pad - bw - ui.S(10) - pw, fy - bh / 2});
    if (ui::SecondaryButton(ui, "##change-previous", Tr("← Предыдущая"), nullptr, {pw, bh}, d.current > 0)) --d.current;
    ImGui::SetCursorScreenPos({o.x + width - pad - bw, fy - bh / 2});
    const double gap = ParseGap(d.gap);
    const bool can = !(choice == ChangeChoice::Mimic && gap < 0) && !(shown && !shown->error.empty());
    const bool go = ui::PrimaryButton(ui, "##change-apply", primary.c_str(), "Enter", {bw, bh}, can);
    if (go && can) {
        if (d.sameForAll)
            for (std::size_t k = d.current; k < d.choices.size(); ++k) d.choices[k] = choice;
        if (!last && !d.sameForAll && choice != ChangeChoice::Manual) {
            ++d.current;   // the next part; all are applied with the last one
        } else {
            // Applied in order, up to the part shown (all of them with «Так
            // же для всех»): the parts not seen yet wait for the window to
            // come back, and so do the manual ones after the first.
            const std::filesystem::path file = as->path;
            std::vector<ChangeChoice> choices = d.choices;
            std::size_t left = d.sameForAll || last ? choices.size() : d.current + 1;
            bool manual = false;
            std::size_t k = 0;
            std::string failed;
            for (; left > 0 && k < st.changes.size() && k < choices.size(); --left) {
                const ChangeChoice ch = choices[k];
                const std::string why = ch == ChangeChoice::Manual && manual ? std::string("-") : st.ResolveChange(k, ch, gap, d.rollback, file, name);
                if (!why.empty()) {
                    if (why != "-") failed = why;
                    ++k;   // stays in the list
                    continue;
                }
                manual = manual || ch == ChangeChoice::Manual;
                choices.erase(choices.begin() + static_cast<std::ptrdiff_t>(k));
            }
            if (!failed.empty()) app.notes.Error(Tr(failed));
            as->assembly->MarkUnsaved();
            app.shell.changeDialog.reset();
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

} // namespace bz::app
