#include "kernel/occt/Text.h"

#include "kernel/TextGeometry.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <Font_BRepFont.hxx>
#include <Font_BRepTextBuilder.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>

#include <algorithm>
#include <map>
#include <mutex>

namespace bz::kernel {
namespace {

std::mutex                                                      gMutex;   // the model may rebuild on another thread
std::string                                                     gFont;
std::map<std::pair<std::string, double>, TopoDS_Shape>          gFaces;
std::map<std::pair<std::string, double>, std::vector<std::vector<Vec2>>> gOutlines;

TopoDS_Shape FacesLocked(const std::string& text, double height) {
    const auto key = std::make_pair(text, height);
    if (const auto it = gFaces.find(key); it != gFaces.end()) return it->second;
    TopoDS_Shape shape;
    if (!gFont.empty() && height > 0 && text.find_first_not_of(" \t") != std::string::npos) {
        try {
            Font_BRepFont font(NCollection_String(gFont.c_str()), height);
            Font_BRepTextBuilder builder;
            shape = builder.Perform(font, NCollection_String(text.c_str()));
        } catch (const Standard_Failure&) {
            shape.Nullify();
        }
    }
    if (gFaces.size() > 256) gFaces.clear();
    gFaces[key] = shape;
    return shape;
}

} // namespace

void SetTextFont(const std::filesystem::path& file) {
    const std::lock_guard lock(gMutex);
    gFont = file.string();
    gFaces.clear();
    gOutlines.clear();
}

const std::vector<std::vector<Vec2>>& TextOutlines(const std::string& text, double height) {
    const std::lock_guard lock(gMutex);
    const auto key = std::make_pair(text, height);
    if (const auto it = gOutlines.find(key); it != gOutlines.end()) return it->second;
    if (gOutlines.size() > 256) gOutlines.clear();
    std::vector<std::vector<Vec2>>& out = gOutlines[key];
    const TopoDS_Shape faces = FacesLocked(text, height);
    if (faces.IsNull()) return out;
    for (TopExp_Explorer f(faces, TopAbs_FACE); f.More(); f.Next())
        for (TopExp_Explorer w(f.Current(), TopAbs_WIRE); w.More(); w.Next()) {
            std::vector<Vec2> loop;
            for (BRepTools_WireExplorer e(TopoDS::Wire(w.Current()), TopoDS::Face(f.Current())); e.More(); e.Next()) {
                const BRepAdaptor_Curve curve(e.Current());
                const GCPnts_TangentialDeflection sampler(curve, 0.1, height * 0.01);
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

namespace occt {

TopoDS_Shape TextFaces(const std::string& text, double height) {
    const std::lock_guard lock(gMutex);
    return FacesLocked(text, height);
}

} // namespace occt
} // namespace bz::kernel
