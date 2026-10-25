#include "document/ThreadFeature.h"

#include "document/JsonIO.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <utility>

namespace bz::doc {

using json_io::Json;
using json_io::ReadMember;

namespace {

// ISO 261 coarse series: nominal diameter, pitch.
constexpr std::array<std::pair<double, double>, 22> kSeries{{
    {1.6, 0.35}, {2, 0.4},   {2.5, 0.45}, {3, 0.5},   {3.5, 0.6}, {4, 0.7},  {5, 0.8},   {6, 1.0},
    {8, 1.25},   {10, 1.5},  {12, 1.75},  {14, 2.0},  {16, 2.0},  {18, 2.5}, {20, 2.5},  {22, 2.5},
    {24, 3.0},   {27, 3.0},  {30, 3.5},   {36, 4.0},  {42, 4.5},  {48, 5.0},
}};

const std::pair<double, double>& Nearest(double d) {
    const auto* best = &kSeries[0];
    for (const auto& size : kSeries)
        if (std::abs(size.first - d) < std::abs(best->first - d)) best = &size;
    return *best;
}

} // namespace

namespace {

// ISO 228-1 (ГОСТ 6357): name, threads per inch, major diameter.
constexpr double kInch = 25.4;
constexpr std::array<ThreadSize, 14> kPipe{{
    {7.723, kInch / 28, "G1/16"},    {9.728, kInch / 28, "G1/8"},     {13.157, kInch / 19, "G1/4"},
    {16.662, kInch / 19, "G3/8"},    {20.955, kInch / 14, "G1/2"},    {22.911, kInch / 14, "G5/8"},
    {26.441, kInch / 14, "G3/4"},    {30.201, kInch / 14, "G7/8"},    {33.249, kInch / 11, "G1"},
    {37.897, kInch / 11, "G1 1/8"},  {41.910, kInch / 11, "G1 1/4"},  {47.803, kInch / 11, "G1 1/2"},
    {53.746, kInch / 11, "G1 3/4"},  {59.614, kInch / 11, "G2"},
}};

// ISO 2904 / ГОСТ 24738, the preferred pitch of each diameter.
constexpr std::array<ThreadSize, 18> kTrapezoidal{{
    {8, 1.5, nullptr},  {10, 2, nullptr},  {12, 3, nullptr},  {14, 3, nullptr},  {16, 4, nullptr},  {18, 4, nullptr},
    {20, 4, nullptr},   {22, 5, nullptr},  {24, 5, nullptr},  {26, 5, nullptr},  {28, 5, nullptr},  {30, 6, nullptr},
    {32, 6, nullptr},   {36, 6, nullptr},  {40, 7, nullptr},  {44, 7, nullptr},  {48, 8, nullptr},  {52, 8, nullptr},
}};

const std::array<ThreadSize, 22>& MetricSizes() {
    static const std::array<ThreadSize, 22> sizes = [] {
        std::array<ThreadSize, 22> out{};
        for (std::size_t i = 0; i < kSeries.size(); ++i) out[i] = {kSeries[i].first, kSeries[i].second, nullptr};
        return out;
    }();
    return sizes;
}

const ThreadSize& NearestOf(ThreadStandard standard, double d) {
    const std::span<const ThreadSize> sizes = ThreadSizes(standard);
    const ThreadSize* best = &sizes.front();
    for (const ThreadSize& s : sizes)
        if (std::abs(s.nominal - d) < std::abs(best->nominal - d)) best = &s;
    return *best;
}

/// Trapezoidal: the radial clearance at crest and root, ISO 2903.
double TrapezoidalClearance(double pitch) {
    if (pitch <= 1.5 + 1e-9) return 0.15;
    if (pitch <= 5 + 1e-9) return 0.25;
    if (pitch <= 12 + 1e-9) return 0.5;
    return 1.0;
}

constexpr std::array<std::pair<ThreadStandard, std::string_view>, 3> kStandardIds{{
    {ThreadStandard::Metric, "metric"}, {ThreadStandard::PipeG, "pipeG"}, {ThreadStandard::Trapezoidal, "trapezoidal"}}};
constexpr std::array<std::pair<ThreadFit, std::string_view>, 5> kFitIds{{
    {ThreadFit::Nominal, "nominal"}, {ThreadFit::Free, "free"}, {ThreadFit::Snug, "snug"},
    {ThreadFit::Metal, "metal"},     {ThreadFit::Custom, "custom"}}};

} // namespace

std::span<const ThreadSize> ThreadSizes(ThreadStandard standard) {
    switch (standard) {
        case ThreadStandard::PipeG:       return kPipe;
        case ThreadStandard::Trapezoidal: return kTrapezoidal;
        case ThreadStandard::Metric:      break;
    }
    return MetricSizes();
}

double StandardPitch(ThreadStandard standard, double nominal) { return NearestOf(standard, nominal).pitch; }

double NearestStandardSize(ThreadStandard standard, double d) {
    const double nominal = NearestOf(standard, d).nominal;
    return std::abs(nominal - d) <= 0.05 * nominal ? nominal : d;
}

std::string PipeSizeName(double nominal) {
    for (const ThreadSize& s : kPipe)
        if (std::abs(s.nominal - nominal) < 1e-6) return s.name;
    return {};
}

ThreadProfile BasicProfile(ThreadStandard standard, double p, bool external) {
    ThreadProfile out;
    switch (standard) {
        case ThreadStandard::Metric:
            // ISO 68-1: H = 0.866P. The groove is 7/8 P wide at the shaft's
            // major diameter and 17/24 H deep; 3/4 P at the hole's minor
            // diameter and 5/8 H deep (to the major one).
            out.halfAngle = kPi / 6;
            out.mouth = (external ? 0.875 : 0.75) * p;
            out.depth = (external ? 0.6134 : 0.5413) * p;
            out.minorOffset = 0.5413 * p;
            break;
        case ThreadStandard::PipeG:
            // ISO 228-1: H = 0.960491P, working depth h = 0.640327P, crests and
            // roots rounded (taken as flats of the same height here: H/6 off
            // the sharp triangle, P/6 wide). Bolt and nut alike.
            out.halfAngle = 27.5 * kPi / 180;
            out.mouth = p - 2 * (0.960491 * p / 6) * std::tan(out.halfAngle);
            out.depth = 0.640327 * p;
            out.minorOffset = 0.640327 * p;
            break;
        case ThreadStandard::Trapezoidal: {
            // ISO 2904: 30°, working depth P/2, crest flats 0.366P; screw and
            // nut get the radial clearance ac at crest and root.
            const double ac = TrapezoidalClearance(p);
            out.halfAngle = kPi / 12;
            out.mouth = p - 0.366 * p;
            out.depth = 0.5 * p + ac;
            out.minorOffset = 0.5 * p;
            break;
        }
    }
    return out;
}

FitAllowance AllowanceOf(const ThreadSpec& spec) {
    // Starting values for FDM (a 0.4 mm nozzle), set as the gap across the
    // flanks each part gives (a radial shift δ opens a flank by δ·sin of the
    // half angle: a 60° metric thread needs half the shift of a 30° Tr):
    // free 0.075 mm a part (0.15 between two printed parts), snug 0.0375
    // (0.075), to metal 0.05 on the printed side. Calibrate with a test
    // print; «Свой зазор» takes any clearance on the pitch diameter.
    const double s = std::sin(BasicProfile(spec.standard, 1.0, true).halfAngle);
    switch (spec.fit) {
        case ThreadFit::Nominal: return {};
        case ThreadFit::Free:    return {0.075 / s, 0.10, 0.10};
        case ThreadFit::Snug:    return {0.0375 / s, 0.05, 0.05};
        case ThreadFit::Metal:   return {0.05 / s, 0.05, 0.10};
        case ThreadFit::Custom: {
            const double c = std::max(spec.clearance.value, 0.0);
            return c > 0 ? FitAllowance{c / 2, 0.05, 0.05} : FitAllowance{};
        }
    }
    return {};
}

ThreadCut CutOf(const ThreadSpec& spec, double nominal, double pitch, bool external, std::optional<double> basicSurface) {
    const ThreadProfile basic = BasicProfile(spec.standard, pitch, external);
    const FitAllowance a = AllowanceOf(spec);
    // Away from the counterpart: a shaft gets thinner, a hole wider.
    const double away = external ? -1.0 : 1.0;
    ThreadCut out;
    out.halfAngle = basic.halfAngle;
    out.mouth = basic.mouth;
    const double surface = basicSurface ? *basicSurface : (external ? nominal / 2 : nominal / 2 - basic.minorOffset);
    out.anchor = surface + away * a.shift;
    out.surface = out.anchor + away * a.truncation;
    // Deeper roots, but the root keeps a flat (2% of the pitch at least).
    const double tan = std::tan(basic.halfAngle);
    const double deepest = (basic.mouth / 2 - 0.02 * pitch) / tan;
    out.depth = std::min(basic.depth + a.root, std::max(deepest, basic.depth));
    return out;
}

nlohmann::json ToJson(const ThreadSpec& spec) {
    Json out{{"standard", json_io::IdOf(kStandardIds, spec.standard)}, {"fit", json_io::IdOf(kFitIds, spec.fit)},
             {"leadChamfer", spec.leadChamfer}};
    if (spec.fit == ThreadFit::Custom) out["clearance"] = json_io::ToJson(spec.clearance);
    return out;
}

bool ReadThreadSpec(const nlohmann::json& in, ThreadSpec& spec) {
    ThreadSpec s;
    s.leadChamfer = false;   // threads of 0.2.4.1 and older had none
    if (!in.is_object()) return false;
    std::string standard = "metric", fit = "nominal";
    if (!ReadMember(in, "standard", standard) || !ReadMember(in, "fit", fit) || !ReadMember(in, "leadChamfer", s.leadChamfer) ||
        !ReadMember(in, "clearance", s.clearance))
        return false;
    if (!json_io::FromId(kStandardIds, standard, s.standard) || !json_io::FromId(kFitIds, fit, s.fit)) return false;
    spec = std::move(s);
    return true;
}

double CoarsePitch(double d) { return Nearest(d).second; }

std::vector<double> StandardSizes() {
    std::vector<double> out;
    for (const auto& size : kSeries) out.push_back(size.first);
    return out;
}

double StandardSize(double d) {
    const double nominal = Nearest(d).first;
    return std::abs(nominal - d) <= 0.05 * nominal ? nominal : d;
}

void ThreadFeature::SaveFields(Json& out) const {
    if (face)
        out["face"] = {{"point", json_io::ToJson(face->axisPoint)}, {"axis", json_io::ToJson(face->axisDir)},
                       {"radius", face->radius}};
    out["pitch"] = json_io::ToJson(pitch);
    out["fullLength"] = fullLength;
    out["length"] = json_io::ToJson(length);
    out["fromOtherEnd"] = fromOtherEnd;
    out["leftHanded"] = leftHanded;
    if (size > 0) out["size"] = size;
    if (cosmetic) out["cosmetic"] = true;
    out["thread"] = ToJson(spec);
}

Status ThreadFeature::LoadFields(const Json& in) {
    Parameter p = pitch, l = length;
    bool full = true, other = false, left = false;
    if (!ReadMember(in, "pitch", p) || !ReadMember(in, "fullLength", full) || !ReadMember(in, "length", l) ||
        !ReadMember(in, "fromOtherEnd", other) || !ReadMember(in, "leftHanded", left))
        return Error{"Резьба: повреждены параметры"};
    std::optional<CylinderTouch> f;
    if (const auto it = in.find("face"); it != in.end()) {
        CylinderTouch t;
        if (!it->is_object() || !json_io::Read(it->value("point", Json()), t.axisPoint) ||
            !json_io::Read(it->value("axis", Json()), t.axisDir) || !it->contains("radius") || !(*it)["radius"].is_number())
            return Error{"Резьба: повреждена грань"};
        t.radius = (*it)["radius"].get<double>();
        f = t;
    }
    face = f;
    pitch = std::move(p);
    length = std::move(l);
    fullLength = full;
    fromOtherEnd = other;
    leftHanded = left;
    ThreadSpec s;
    s.leadChamfer = false;   // a file without "thread": a metric thread of 0.2.4.1, no chamfer
    if (const auto it = in.find("thread"); it != in.end() && !ReadThreadSpec(*it, s)) return Error{"Резьба: повреждён вид резьбы"};
    spec = std::move(s);
    size = in.value("size", 0.0);
    cosmetic = in.value("cosmetic", false);
    return Status::Ok();
}

} // namespace bz::doc
