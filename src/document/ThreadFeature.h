#pragma once
// A modelled thread ("Резьба"): an ISO metric 60° thread on a cylindrical
// face of the body. On a shaft the groove goes in from its surface
// (external thread); in a hole it goes out into the wall (internal thread,
// the hole being the minor diameter, as drilled for a tap).

#include "document/Feature.h"
#include "document/WorkPlaneFeature.h"

#include <optional>
#include <span>
#include <string>

namespace bz::doc {

// ---------------------------------------------------------------------------
// What a thread is (0.2.4.2): its kind, how it fits its counterpart, and
// whether its open ends get a lead-in chamfer. Shared by «Резьба» and the
// threaded hole.
// ---------------------------------------------------------------------------

enum class ThreadStandard : unsigned char {
    Metric,        ///< M, 60° (ГОСТ 24705 / ISO 68-1)
    PipeG,         ///< G, cylindrical pipe, 55° (ГОСТ 6357 / ISO 228-1)
    Trapezoidal,   ///< Tr, 30° (ГОСТ 24738 / ISO 2904)
};

/// How the thread is made against its counterpart. The basic profile has no
/// gap at all (a metal bolt and nut meet "in zero"); the printed fits move
/// the whole profile away from the counterpart (as an ISO allowance does),
/// cut the crests flat and deepen the roots, so print steps do not bind.
enum class ThreadFit : unsigned char {
    Nominal,   ///< the basic profile, as in the book (allowances set in the slicer by hand)
    Free,      ///< printed to printed, loose: sealed joints with tape, frequent screwing
    Snug,      ///< printed to printed, close: axial accuracy, no sealing
    Metal,     ///< printed to a metal part (bolt, nut): only the printed side is corrected
    Custom,    ///< the user's own clearance
};

struct ThreadSpec {
    ThreadStandard standard = ThreadStandard::Metric;
    ThreadFit      fit = ThreadFit::Nominal;
    /// Custom fit: clearance on the pitch diameter of this part, mm.
    Parameter      clearance{.name = "", .expression = "0.2", .quantity = Quantity::Length, .value = 0.2, .error = {}};
    /// A 45° chamfer at the open ends, one thread deep: the first turn starts
    /// clean (no elephant foot, no ragged half turn). Off for those who finish
    /// threads by hand.
    bool           leadChamfer = true;
};

/// One size of a standard series.
struct ThreadSize {
    double      nominal;   ///< major diameter, mm
    double      pitch;     ///< mm
    const char* name;      ///< "G1/2"; nullptr: "M8", "Tr16" made from the number
};

/// The standard sizes of a kind, smallest first.
std::span<const ThreadSize> ThreadSizes(ThreadStandard standard);
/// The standard pitch of the size nearest `nominal`.
double StandardPitch(ThreadStandard standard, double nominal);
/// The standard size `diameter` is within 5% of, or `diameter` itself.
double NearestStandardSize(ThreadStandard standard, double diameter);
/// "G1/2" for a pipe size; empty for the others (they are named by the number).
std::string PipeSizeName(double nominal);

/// The basic profile of a kind at a pitch: the groove cut into one part.
struct ThreadProfile {
    double halfAngle = 0;    ///< half the thread angle, radians
    double mouth = 0;        ///< groove width at the part's basic surface, mm
    double depth = 0;        ///< groove depth from that surface, mm
    double minorOffset = 0;  ///< nominal radius minus the internal thread's minor radius, mm
};
ThreadProfile BasicProfile(ThreadStandard standard, double pitch, bool external);

/// The allowances a fit applies to one part (radial, mm).
struct FitAllowance {
    double shift = 0;        ///< the whole profile, away from the counterpart (half the pitch-diameter clearance)
    double truncation = 0;   ///< crests cut flat by this much more
    double root = 0;         ///< roots this much deeper
};
FitAllowance AllowanceOf(const ThreadSpec& spec);

/// The thread as it is cut into one part.
struct ThreadCut {
    double halfAngle = 0;
    double anchor = 0;    ///< radius of the groove's mouth (the shifted basic surface)
    double surface = 0;   ///< radius the part is turned or bored to (crests truncated)
    double depth = 0;     ///< groove depth from `anchor`
    double mouth = 0;     ///< groove width at `anchor`
};
/// `basicSurface`: the part's basic surface radius when known from the face
/// (a shaft's major radius, a hole's minor one); else from `nominal`.
ThreadCut CutOf(const ThreadSpec& spec, double nominal, double pitch, bool external,
                std::optional<double> basicSurface = std::nullopt);

nlohmann::json ToJson(const ThreadSpec& spec);
/// Reads what ToJson wrote; absent keys keep the values of a 0.2.4.1 file
/// (metric, nominal, no chamfer). False when a key is damaged.
bool ReadThreadSpec(const nlohmann::json& in, ThreadSpec& spec);

class ThreadFeature final : public FeatureBase<ThreadFeature> {
public:
    static constexpr std::string_view kType = "thread";

    std::optional<CylinderTouch> face;        ///< the shaft or the hole (`radial` unused)
    Parameter pitch{.name = "", .expression = "1.25", .quantity = Quantity::Length, .value = 1.25, .error = {}};
    bool      fullLength = true;
    Parameter length{.name = "", .expression = "10", .quantity = Quantity::Length, .value = 10.0, .error = {}};
    bool      fromOtherEnd = false;   ///< start at the other end of the face
    bool      leftHanded = false;
    /// Standard size chosen by hand (nominal diameter, mm); 0 = from the
    /// face. A shaft or hole of another diameter is cut to this size.
    double    size = 0;
    /// Drawn on the face, not cut: the model stays light. The real thread
    /// is cut in exported files.
    bool      cosmetic = false;
    ThreadSpec spec;

    /// Runtime (set by the rebuild, not saved): what the face turned out to be.
    struct Site {
        bool   external = true;   ///< a shaft, not a hole
        double nominal = 0;       ///< nominal diameter, mm
        double length = 0;        ///< face length along the axis, mm
    };
    std::optional<Site> site;

    std::string_view Type() const override { return kType; }
    void CollectParameters(std::vector<Parameter*>& out) override {
        out.push_back(&pitch);
        out.push_back(&length);
        out.push_back(&spec.clearance);
    }
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

/// ISO metric coarse pitch for a nominal diameter (the nearest standard size).
double CoarsePitch(double nominalDiameter);

/// The ISO 261 coarse series, smallest first: nominal diameters, mm.
std::vector<double> StandardSizes();

/// The standard size `diameter` is within 5% of, or `diameter` itself.
double StandardSize(double diameter);

} // namespace bz::doc
