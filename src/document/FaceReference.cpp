#include "document/FaceReference.h"

#include <cmath>

namespace bz::doc {

Frame PlaneFrame(const FaceReference& face) {
    Frame f;
    f.normal = face.normal.Normalized();
    f.origin = f.normal * face.point.Dot(f.normal);   // foot of the model origin on the plane
    const Vec3 axis = std::abs(f.normal.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    f.xDir = (axis - f.normal * axis.Dot(f.normal)).Normalized();
    return f;
}

} // namespace bz::doc
