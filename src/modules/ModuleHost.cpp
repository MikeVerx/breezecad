#include "modules/ModuleHost.h"

#include "core/Log.h"
#include "core/Version.h"
#include "kernel/AssemblyReadApi.h"

#include <string>
#include <vector>

namespace bz::modules {

namespace {

const kernel::AssemblyReader* gReader = nullptr;
/// Strings handed out by component_at live here until the next call.
std::vector<kernel::ComponentRecord> gComponents;
bool gComponentsFresh = false;

const std::vector<kernel::ComponentRecord>* Components() {
    if (!gReader) return nullptr;
    if (!gComponentsFresh) {
        gComponents      = gReader->Components();
        gComponentsFresh = true;
    }
    return &gComponents;
}

void Copy(const Vec3& v, double out[3]) {
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
}

const char* HostVersion() { return Version(); }

void Log(int32_t level, const char* message) {
    if (!message) return;
    const std::string text = std::string("Модуль: ") + message;
    switch (level) {
        case BZ_LOG_DEBUG: log::Write(log::Level::Debug, text); break;
        case BZ_LOG_WARNING: log::Write(log::Level::Warning, text); break;
        case BZ_LOG_ERROR: log::Write(log::Level::Error, text); break;
        default: log::Write(log::Level::Info, text); break;
    }
}

int32_t AssemblyName(const char** name, const char** designation) {
    if (!gReader) return BZ_ENODATA;
    if (name) *name = gReader->Info().name.c_str();
    if (designation) *designation = gReader->Info().designation.c_str();
    return BZ_OK;
}

int32_t ComponentCount(int32_t* count) {
    if (!count) return BZ_EARGUMENT;
    const auto* all = Components();
    if (!all) return BZ_ENODATA;
    *count = static_cast<int32_t>(all->size());
    return BZ_OK;
}

int32_t ComponentAt(int32_t index, BzComponentInfo* out) {
    if (!out) return BZ_EARGUMENT;
    const auto* all = Components();
    if (!all) return BZ_ENODATA;
    if (index < 0 || static_cast<size_t>(index) >= all->size()) return BZ_ENOTFOUND;
    const kernel::ComponentRecord& c = (*all)[static_cast<size_t>(index)];
    out->id           = c.id.value;
    out->tree_name    = c.treeName.c_str();
    out->path         = c.path.c_str();
    out->designation  = c.designation.c_str();
    out->name         = c.name.c_str();
    out->material     = c.material.c_str();
    out->quantity     = c.quantity;
    out->grounded     = c.grounded ? 1 : 0;
    out->visible      = c.visible ? 1 : 0;
    out->has_geometry = c.hasGeometry ? 1 : 0;
    Copy(c.placement.translation, out->origin);
    Copy(c.placement.rotation.col[0], out->x_axis);
    Copy(c.placement.rotation.col[2], out->z_axis);
    return BZ_OK;
}

int32_t ComponentBounds(uint64_t id, double minXyz[3], double maxXyz[3]) {
    if (!minXyz || !maxXyz) return BZ_EARGUMENT;
    if (!gReader) return BZ_ENODATA;
    if (!gReader->Component(ObjectId{id})) return BZ_ENOTFOUND;
    const auto box = gReader->BoundsOf(ObjectId{id});
    if (!box) return BZ_ENODATA;
    Copy(box->min, minXyz);
    Copy(box->max, maxXyz);
    return BZ_OK;
}

const BzHostApi kHost{
    BZ_MODULE_ABI_VERSION,
    sizeof(BzHostApi),
    &HostVersion,
    &Log,
    &AssemblyName,
    &ComponentCount,
    &ComponentAt,
    &ComponentBounds,
};

} // namespace

const BzHostApi* HostApi() { return &kHost; }

void SetActiveAssembly(const kernel::AssemblyReader* reader) {
    gReader          = reader;
    gComponentsFresh = false;
    gComponents.clear();
}

} // namespace bz::modules
