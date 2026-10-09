#include "kernel/occt/Text.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <Bnd_Box.hxx>
#include <Font_BRepFont.hxx>
#include <Font_BRepTextBuilder.hxx>
#include <Font_FontMgr.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <Standard_Failure.hxx>
#include <TColStd_SequenceOfHAsciiString.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

namespace bz::kernel {
namespace {

std::mutex  gMutex;   // the model may rebuild on another thread
std::string gRegular, gBold;

struct Laid {
    TextFormat format;
    double     w = 0, h = 0;
    bool operator<(const Laid& o) const { return std::tie(format, w, h) < std::tie(o.format, o.w, o.h); }
};
std::map<Laid, TopoDS_Shape>                   gFaces;
std::map<Laid, std::vector<std::vector<Vec2>>> gOutlines;

double Round(double v) { return std::round(v * 1e6) / 1e6; }   // cache keys from solved positions

bool InitFont(Font_BRepFont& font, const TextFormat& f) {
    if (f.font.empty()) {
        const std::string& file = f.bold && !gBold.empty() ? gBold : gRegular;
        return !file.empty() && font.Init(NCollection_String(file.c_str()), f.height, 0);
    }
    const Font_FontAspect aspect = f.bold && f.italic ? Font_FontAspect_BoldItalic
                                 : f.bold             ? Font_FontAspect_Bold
                                 : f.italic           ? Font_FontAspect_Italic
                                                      : Font_FontAspect_Regular;
    return font.FindAndInit(TCollection_AsciiString(f.font.c_str()), aspect, f.height, Font_StrictLevel_Any);
}

TopoDS_Shape FacesLocked(const TextFormat& f, double w, double h) {
    const Laid key{f, Round(w), Round(h)};
    if (const auto it = gFaces.find(key); it != gFaces.end()) return it->second;
    TopoDS_Shape shape;
    if (f.height > 0 && f.text.find_first_not_of(" \t\n") != std::string::npos) {
        try {
            Font_BRepFont font;
            if (InitFont(font, f)) {
                // The pen sits on the box where the alignment says.
                const double x = f.hAlign == 1 ? w / 2 : f.hAlign == 2 ? w : 0.0;
                const double y = f.vAlign == 1 ? h / 2 : f.vAlign == 2 ? h : 0.0;
                const gp_Ax3 pen(gp_Pnt(x, y, 0), gp::DZ(), gp::DX());
                const Graphic3d_HorizontalTextAlignment ha = f.hAlign == 1 ? Graphic3d_HTA_CENTER
                                                          : f.hAlign == 2 ? Graphic3d_HTA_RIGHT
                                                                          : Graphic3d_HTA_LEFT;
                const Graphic3d_VerticalTextAlignment va = f.vAlign == 1 ? Graphic3d_VTA_CENTER
                                                        : f.vAlign == 2 ? Graphic3d_VTA_TOP
                                                                        : Graphic3d_VTA_BOTTOM;
                Font_BRepTextBuilder builder;
                shape = builder.Perform(font, NCollection_String(f.text.c_str()), pen, ha, va);
            }
        } catch (const Standard_Failure&) {
            shape.Nullify();
        }
    }
    if (gFaces.size() > 256) gFaces.clear();
    gFaces[key] = shape;
    return shape;
}

} // namespace

void SetTextFont(const std::filesystem::path& regular, const std::filesystem::path& bold) {
    const std::lock_guard lock(gMutex);
    gRegular = regular.string();
    gBold = bold.string();
    gFaces.clear();
    gOutlines.clear();
}

const std::vector<std::string>& AvailableFonts() {
    static const std::vector<std::string> fonts = [] {
        std::vector<std::string> out;
        try {
            Handle(Font_FontMgr) manager = Font_FontMgr::GetInstance();
            TColStd_SequenceOfHAsciiString names;
            manager->GetAvailableFontsNames(names);
            for (const auto& n : names) out.emplace_back(n->ToCString());
        } catch (const Standard_Failure&) {
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }();
    return fonts;
}

std::vector<std::vector<Vec2>> TextOutlines(const TextFormat& format, double w, double h) {
    const std::lock_guard lock(gMutex);
    const Laid key{format, Round(w), Round(h)};
    if (const auto it = gOutlines.find(key); it != gOutlines.end()) return it->second;
    if (gOutlines.size() > 256) gOutlines.clear();
    std::vector<std::vector<Vec2>>& out = gOutlines[key];
    const TopoDS_Shape faces = FacesLocked(format, w, h);
    if (faces.IsNull()) return out;
    for (TopExp_Explorer f(faces, TopAbs_FACE); f.More(); f.Next())
        for (TopExp_Explorer wi(f.Current(), TopAbs_WIRE); wi.More(); wi.Next()) {
            std::vector<Vec2> loop;
            for (BRepTools_WireExplorer e(TopoDS::Wire(wi.Current()), TopoDS::Face(f.Current())); e.More(); e.Next()) {
                const BRepAdaptor_Curve curve(e.Current());
                const GCPnts_TangentialDeflection sampler(curve, 0.1, format.height * 0.01);
                std::vector<Vec2> pts;
                for (int i = 1; i <= sampler.NbPoints(); ++i) pts.push_back({sampler.Value(i).X(), sampler.Value(i).Y()});
                if (e.Current().Orientation() == TopAbs_REVERSED) std::reverse(pts.begin(), pts.end());
                loop.insert(loop.end(), pts.begin(), pts.end());
            }
            if (loop.size() > 2) {
                loop.push_back(loop.front());
                out.push_back(std::move(loop));
            }
        }
    return out;
}

Vec2 TextExtent(const TextFormat& format) {
    TextFormat f = format;
    f.hAlign = 0;
    f.vAlign = 0;
    TopoDS_Shape faces;
    {
        const std::lock_guard lock(gMutex);
        faces = FacesLocked(f, 0, 0);
    }
    if (faces.IsNull()) return {};
    Bnd_Box box;
    BRepBndLib::Add(faces, box);
    if (box.IsVoid()) return {};
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    return {std::max(x1, 0.0), y1 - std::min(y0, 0.0)};
}

namespace occt {

TopoDS_Shape TextFaces(const TextFormat& format, double w, double h) {
    const std::lock_guard lock(gMutex);
    return FacesLocked(format, w, h);
}

} // namespace occt
} // namespace bz::kernel
