#include "kernel/AssemblyReadApi.h"

#include "document/Document.h"

namespace bz::kernel {

namespace {

/// "plate" for "parts/plate.bzc".
std::string FileStem(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    if (const std::size_t dot = name.find_last_of('.'); dot != std::string::npos && dot > 0) name.resize(dot);
    return name;
}

const std::string& Either(const std::string& own, const std::string& fromPart) {
    return own.empty() ? fromPart : own;
}

} // namespace

ComponentRecord AssemblyReader::Record(const assembly::Component& c) const {
    ComponentRecord r;
    r.id        = c.id;
    r.treeName  = c.name;
    r.path      = c.path;
    r.placement = c.placement;
    r.grounded  = c.grounded;
    r.visible   = c.visible;
    r.quantity  = c.properties.quantity;

    static const doc::PartInfo none;
    const doc::Document*       part = myGeometry.PartOf(c.id);
    const doc::PartInfo&       info = part ? part->info : none;
    r.designation = Either(c.properties.designation, info.designation);
    r.material    = Either(c.properties.material, info.material);
    r.name        = Either(c.properties.name, info.name);
    if (r.name.empty()) r.name = FileStem(c.path);

    r.problem     = myGeometry.ProblemOf(c.id);
    const Model* m = myGeometry.PartModelOf(c.id);
    r.hasGeometry = m && !m->Bodies().empty();
    return r;
}

std::vector<ComponentRecord> AssemblyReader::Components() const {
    std::vector<ComponentRecord> out;
    out.reserve(myAssembly.components.size());
    for (const assembly::Component& c : myAssembly.components) out.push_back(Record(c));
    return out;
}

std::optional<ComponentRecord> AssemblyReader::Component(ObjectId id) const {
    if (const assembly::Component* c = myAssembly.FindComponent(id)) return Record(*c);
    return std::nullopt;
}

std::optional<Model> AssemblyReader::BodiesOf(ObjectId id) const {
    const assembly::Component* c = myAssembly.FindComponent(id);
    if (!c) return std::nullopt;
    return myGeometry.PlacedModelOf(id, c->placement);
}

} // namespace bz::kernel
