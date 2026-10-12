#pragma once
// The command table behind scripting::Run and the helpers the commands
// share: reading arguments, describing them (JSON schema), describing
// features in replies. Each group of commands lives in its own file and
// adds itself with AddXxxCommands().

#include "core/Id.h"
#include "core/Math.h"
#include "document/EdgeReference.h"
#include "document/FaceReference.h"
#include "kernel/Topology.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace bz::doc {
class Document;
class Feature;
} // namespace bz::doc

namespace bz::app {

struct AppContext;
class Session;

namespace scripting {

using Json = nlohmann::json;

/// What a command throws when it cannot do what was asked; the text goes
/// back to the caller as is (Russian, as the program's own messages).
struct Failure {
    std::string message;
};
[[noreturn]] void Fail(std::string message);

struct Command {
    std::string name;          ///< "part.tree"
    std::string description;   ///< for the caller (an AI reads it): what it does, what it returns
    Json        schema;        ///< JSON schema of "args" (an object schema)
    bool        changes = false;   ///< changes a document: refused while the user edits
    std::function<Json(AppContext&, const Json& args)> run;
};
using Commands = std::vector<Command>;

void AddDocumentCommands(Commands& out);   // DocumentCommands.cpp: open, save, export, undo
void AddPartCommands(Commands& out);       // PartCommands.cpp: tree, parameters, properties, edits
void AddSketchCommands(Commands& out);     // SketchCommands.cpp: sketches and their geometry
void AddSolidCommands(Commands& out);      // SolidCommands.cpp: extrude, revolve, thread
void AddTopologyCommands(Commands& out);   // TopologyCommands.cpp: faces, edges, measure
void AddGenericCommands(Commands& out);    // GenericCommands.cpp: any feature by its fields, the tree
void AddFeatureCommands(Commands& out);    // FeatureCommands.cpp: hole, fillet, shell, patterns, mirror, loft

// --- schemas ----------------------------------------------------------------

/// {"type": "object", "properties": {...}, "required": [...]}
Json Schema(Json properties = Json::object(), std::vector<std::string> required = {});
Json NumberArg(std::string description);
Json IntegerArg(std::string description);
Json StringArg(std::string description);
Json BoolArg(std::string description);
Json EnumArg(std::string description, std::vector<std::string> values);
/// A value or an expression: 25, "25", "w / 2", "30 deg".
Json ExpressionArg(std::string description);
/// [x, y] in sketch millimetres.
Json PointArg(std::string description);
Json IdArg(std::string description);

// --- arguments --------------------------------------------------------------

const Json&         Required(const Json& args, const char* key);
bool                Has(const Json& args, const char* key);
double              Number(const Json& args, const char* key, std::optional<double> fallback = {});
int                 Integer(const Json& args, const char* key, std::optional<int> fallback = {});
std::string         Text(const Json& args, const char* key, std::optional<std::string> fallback = {});
bool                Flag(const Json& args, const char* key, bool fallback);
ObjectId            Id(const Json& args, const char* key);
Vec2                Point(const Json& value, const char* what);
Vec2                Point(const Json& args, const char* key, std::optional<Vec2> fallback);
/// A number or a string, as the text of an expression ("25", "w / 2").
std::string         Expression(const Json& args, const char* key, std::optional<std::string> fallback = {});
/// One of `values`; the index of the one given.
std::size_t         Choice(const Json& args, const char* key, const std::vector<std::string>& values, std::size_t fallback);

// --- documents --------------------------------------------------------------

/// The active document; fails when none is open.
Session& Active(AppContext& app);
/// The active document when it is a part; fails otherwise.
Session& ActivePart(AppContext& app);
/// A feature of the part by id; fails when there is none.
doc::Feature& FeatureOf(Session& part, ObjectId id);

// --- geometry of the bodies (kernel/Topology.h) -----------------------------

/// Faces and edges of the part as it is now (kept until the model changes).
const kernel::Topology& TopologyOf(Session& part);
/// A face by its number in part.faces.
const kernel::FaceInfo& FaceArg(Session& part, const Json& value, const char* what);
/// A list of edge numbers (part.edges), as references to store.
std::vector<doc::EdgeReference> EdgeRefs(Session& part, const Json& args, const char* key);
/// A list of face numbers (part.faces), as references to store.
std::vector<doc::FaceReference> FaceRefs(Session& part, const Json& args, const char* key);
/// [x, y, z] in model mm.
Vec3 Point3(const Json& value, const char* what);
Json ToJson3(Vec3 v);
Json Point3Arg(std::string description);
Json IndexListArg(std::string description);

/// {id, name, type, state, message, suppressed}
Json Describe(const doc::Feature& feature);
/// Brings the geometry up to date (the user sees it at once) and describes
/// `feature` after it: what the caller just made or changed.
Json AfterChange(Session& part, ObjectId feature);
/// A feature just added or edited that failed to build: the step is taken
/// back (undo) and the caller gets the reason as an error — no broken item
/// is left in the tree. Nothing happens for one that built (or warned).
void RollBackIfFailed(Session& part, ObjectId feature);

} // namespace scripting
} // namespace bz::app
