#pragma once
// Read-only view of an assembly for modules (a parts list, drawings,
// calculations...). A module walks the components, reads their properties
// as the user sees them (an empty field of the component takes the value
// from its part) and gets their bodies in assembly coordinates. Nothing here
// changes the assembly. Free of OCCT types, like Model.h.
//
// The reader holds references: the assembly and its geometry must outlive
// it, and AssemblyModel::Update must have run for the current assembly.

#include "assembly/Assembly.h"
#include "kernel/AssemblyModel.h"
#include "kernel/Model.h"

#include <optional>
#include <string>
#include <vector>

namespace bz::kernel {

/// One component as a module sees it.
struct ComponentRecord {
    ObjectId            id;
    std::string         treeName;      ///< "Кронштейн:1"
    std::string         path;          ///< part file, relative to the assembly, '/' separators
    assembly::Placement placement;     ///< part coordinates -> assembly coordinates
    bool                grounded = false;
    bool                visible  = true;

    // Properties with the part's values filled in where the component leaves them empty.
    std::string designation;
    std::string name;       ///< component, else part name, else the file name
    std::string material;
    int         quantity = 1;

    bool        hasGeometry = false;   ///< its part was read and rebuilt
    std::string problem;               ///< why not ("Файл не найден: …"), Russian
};

class AssemblyReader {
public:
    AssemblyReader(const assembly::Assembly& assembly, const AssemblyModel& geometry)
        : myAssembly(assembly), myGeometry(geometry) {}

    const assembly::AssemblyInfo& Info() const { return myAssembly.info; }

    /// Every component, in tree order.
    std::vector<ComponentRecord> Components() const;
    /// One component; nullopt for an unknown id.
    std::optional<ComponentRecord> Component(ObjectId id) const;

    /// The component's bodies in assembly coordinates (volume, box, mass
    /// properties, export through Model). nullopt without geometry.
    std::optional<Model> BodiesOf(ObjectId id) const;
    /// Box of the component in the assembly; nullopt without geometry or when hidden.
    std::optional<Bounds> BoundsOf(ObjectId id) const { return myGeometry.ComponentBounds(id); }

    /// The constraints, read-only (state as last solved).
    const std::vector<assembly::Constraint>& Constraints() const { return myAssembly.constraints; }

private:
    ComponentRecord Record(const assembly::Component& c) const;

    const assembly::Assembly& myAssembly;
    const AssemblyModel&      myGeometry;
};

} // namespace bz::kernel
