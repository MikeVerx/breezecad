#include "kernel/occt/Draft.h"

#include "kernel/occt/FaceMatch.h"

#include <BRepCheck_Analyzer.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <Standard_Failure.hxx>
#include <gp_Pln.hxx>

#include <cmath>

namespace bz::kernel::occt {

Expected<TopoDS_Shape> MakeDraft(const TopoDS_Shape& body, const doc::DraftFeature& f, const Frame& neutral) {
    if (f.faces.empty()) return Error{"Выберите грани, которым нужен уклон"};
    if (f.angle.HasError()) return Error{f.angle.error};
    if (std::abs(f.angle.value) < 1e-9 || std::abs(f.angle.value) >= kPi / 4) return Error{"Угол уклона — больше 0° и меньше 45°"};
    const Vec3 pull = f.reversed ? -neutral.normal : neutral.normal;
    const gp_Dir direction(pull.x, pull.y, pull.z);
    const gp_Pln plane(gp_Pnt(neutral.origin.x, neutral.origin.y, neutral.origin.z), direction);
    try {
        BRepOffsetAPI_DraftAngle draft(body);
        for (const doc::FaceReference& ref : f.faces) {
            const std::optional<TopoDS_Face> face = ResolveFace(body, ref);
            if (!face) return Error{"Грань уклона не найдена: модель изменилась слишком сильно"};
            draft.Add(*face, direction, f.angle.value, plane);
            if (!draft.AddDone()) return Error{"Этой грани нельзя задать уклон относительно выбранной плоскости"};
        }
        draft.Build();
        if (!draft.IsDone()) return Error{"Уклон не строится"};
        const TopoDS_Shape result = draft.Shape();
        if (!BRepCheck_Analyzer(result).IsValid()) return Error{"Уклон получился некорректным"};
        return result;
    } catch (const Standard_Failure& failure) {
        return Error{std::string("Ошибка уклона: ") + failure.GetMessageString()};
    }
}

} // namespace bz::kernel::occt
