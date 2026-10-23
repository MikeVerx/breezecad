// Any feature by its fields: the same JSON a .bzc file keeps (Feature::
// SaveFields / LoadFields). Covers every feature type the program has —
// and ones modules register later — without a command of its own:
//   part.feature_types     the types and their fields with default values;
//   part.create_feature    a new one from fields (missing fields: defaults);
//   part.edit_feature      change some fields of an existing one.
// Plus the tree itself: move a feature, the end-of-part marker, close.

#include "app/AppContext.h"
#include "app/Operations.h"
#include "app/AssemblyState.h"
#include "app/Recovery.h"
#include "app/Session.h"
#include "app/Workspace.h"
#include "app/scripting/Commands.h"
#include "core/I18n.h"
#include "document/Commands.h"
#include "document/FeatureRegistry.h"

#include <cstdlib>
#include <format>
#include <stdexcept>

namespace bz::app::scripting {

namespace {

/// Shorthands a caller (an AI) is likely to write, made into file form:
/// a number or text where the file has a parameter object becomes its
/// expression ("depth": 12 → {"expression": "12", …}).
void Normalize(Json& patch, const Json& base) {
    if (!patch.is_object() || !base.is_object()) return;
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        const auto b = base.find(it.key());
        if (b == base.end()) continue;
        if (b->is_object() && b->contains("expression") && (it->is_number() || it->is_string())) {
            Json p = *b;
            p["expression"] = it->is_number() ? std::format("{}", it->get<double>()) : it->get<std::string>();
            *it = std::move(p);
        } else if (it->is_object() && b->is_object()) {
            Normalize(*it, *b);
        }
    }
}

/// `base` with `patch` laid over (RFC 7386: objects merge, null removes).
Json Merged(const Json& base, Json patch) {
    Normalize(patch, base);
    Json out = base;
    out.merge_patch(patch);
    return out;
}

/// Parameters a new feature brings without a name get one ("d12"), so they
/// show in the variables table and can be changed by name.
void NameParameters(doc::Document& d, doc::Feature& f) {
    std::vector<Parameter*> params;
    f.CollectParameters(params);
    int next = std::stoi(d.NextParameterName().substr(1));
    for (Parameter* p : params)
        if (p->name.empty()) p->name = "d" + std::to_string(next++);
}

Json Types(AppContext&, const Json& args) {
    const doc::FeatureRegistry& r = doc::FeatureRegistry::Default();
    const std::string only = Text(args, "type", "");
    Json out = Json::array();
    for (const std::string& type : r.Types()) {
        if (!only.empty() && type != only) continue;
        auto f = r.Create(type);
        Json fields;
        f->SaveFields(fields);
        out.push_back({{"type", type}, {"default_fields", std::move(fields)}});
    }
    if (out.empty()) Fail(std::format("Нет типа «{}»", only));
    return out;
}

Json Create(AppContext& app, const Json& args) {
    Session&          s    = ActivePart(app);
    const std::string type = Text(args, "type");
    if (type == "sketch") Fail("Эскизы создаются командами sketch.create / sketch.add");
    auto f = doc::FeatureRegistry::Default().Create(type);
    if (!f) Fail(std::format("Нет типа операции «{}» (список — part.feature_types)", type));
    Json base;
    f->SaveFields(base);
    const Json fields = Merged(base, Has(args, "fields") ? args["fields"] : Json::object());
    if (const Status st = f->LoadFields(fields); !st) Fail(std::format("Поля не подходят: {}", st.GetError().message));
    f->id   = s.doc.Ids().Next();
    f->name = Has(args, "name") ? Text(args, "name") : s.doc.UniqueFeatureName(std::string(type));
    if (Flag(args, "name_parameters", true)) NameParameters(s.doc, *f);
    f->ReserveIds(s.doc.Ids());
    std::size_t index = s.doc.EndMarker();
    if (Has(args, "index")) index = static_cast<std::size_t>(std::clamp(Integer(args, "index"), 0, static_cast<int>(s.doc.FeatureCount())));
    const ObjectId id = f->id;
    s.Execute(doc::AddFeature(std::move(f), index));
    Json out = AfterChange(s, id);
    RollBackIfFailed(s, id);
    for (const Parameter* p : s.doc.Find(id)->Parameters())
        if (!p->name.empty()) out["parameters"].push_back({{"name", p->name}, {"expression", p->expression}});
    return out;
}

Json Edit(AppContext& app, const Json& args) {
    Session&      s       = ActivePart(app);
    doc::Feature& current = FeatureOf(s, Id(args, "id"));
    auto          edited  = current.Clone();
    if (Has(args, "fields")) {
        Json base;
        current.SaveFields(base);
        if (const Status st = edited->LoadFields(Merged(base, args["fields"])); !st)
            Fail(std::format("Поля не подходят: {}", st.GetError().message));
    }
    if (Has(args, "name")) edited->name = Text(args, "name");
    if (Has(args, "suppressed")) edited->suppressed = Flag(args, "suppressed", false);
    edited->ReserveIds(s.doc.Ids());
    const ObjectId id = current.id;
    s.Execute(doc::ReplaceFeature(std::move(edited), Trf("Изменить {}", current.name)));
    Json out = AfterChange(s, id);
    RollBackIfFailed(s, id);
    return out;
}

Json Move(AppContext& app, const Json& args) {
    Session&            s = ActivePart(app);
    const doc::Feature& f = FeatureOf(s, Id(args, "id"));
    const int           n = static_cast<int>(s.doc.FeatureCount());
    const int           to = Integer(args, "index");
    if (to < 0 || to >= n) Fail(std::format("«index» — от 0 до {}", n - 1));
    const ObjectId id = f.id;
    s.Execute(doc::MoveFeature(s.doc, id, static_cast<std::size_t>(to)));
    Json out = AfterChange(s, id);
    out["index"] = s.doc.IndexOf(id);
    return out;
}

Json EndMarker(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    const int n = static_cast<int>(s.doc.FeatureCount());
    const int at = Has(args, "index") ? Integer(args, "index") : n;
    if (at < 0 || at > n) Fail(std::format("«index» — от 0 до {} ({} — в конец)", n, n));
    s.Execute(doc::SetEndMarker(s.doc, static_cast<std::size_t>(at)));
    s.RebuildNow();
    return {{"end_marker", s.doc.EndMarker()}, {"features", n},
            {"note", "Операции с номером ≥ end_marker откачены (не вычисляются), как маркер «Конец детали»."}};
}

Json Close(AppContext& app, const Json& args) {
    Session& s = Active(app);
    if (s.IsModified() && !Flag(args, "discard_changes", false))
        Fail("В документе несохранённые изменения: сохраните (doc.save) или передайте discard_changes: true");
    const std::string title = s.Title();
    app.workspace.Close(app.workspace.ActiveIndex());
    return {{"closed", title}, {"open_documents", app.workspace.sessions.size()}};
}

} // namespace

/// debug.fault (BZ_FAULT_INJECTION only): makes a change, then fails, to
/// try the recovery (app/Recovery.h) end to end.
Json Fault(AppContext& app, const Json& args) {
    const std::string kind = args.value("kind", "crash");
    const std::string where = args.value("where", "here");
    const bool change = args.value("change", true);
    // The change and the failure in the same piece of work: recovery must
    // take the change back.
    const auto fail = [&app, kind, change] {
        Session* s = app.Doc();
        if (change && s) {
            if (s->assembly) {
                AssemblyState& st = *s->assembly;
                if (!st.assembly.components.empty()) {
                    st.Checkpoint("test");
                    st.assembly.components.back().placement.translation.x += 1000;
                    st.assembly.Touch();
                }
            } else if (s->doc.FeatureCount() > 0) {
                const doc::Feature& f = *s->doc.Features().front();
                s->Execute(doc::SetHidden(f.id, !f.hidden, "test"));
            }
        }
        if (kind == "throw") throw std::runtime_error("test: an exception in a tool");
        fault_test::Crash();
    };
    if (where == "later") app.Later(fail);
    else if (where == "interface") fault_test::RequestInInterface(fail);
    else fail();
    return {{"requested", kind}, {"where", where}};
}

void AddGenericCommands(Commands& out) {
    if (std::getenv("BZ_FAULT_INJECTION"))
        out.push_back({"debug.fault", "Тест восстановления: меняет документ и падает (kind: crash|throw; where: here|later|interface).",
                       Schema({{"kind", StringArg("crash | throw")}, {"where", StringArg("here | later | interface")},
                               {"change", BoolArg("Сначала изменить документ (по умолчанию да).")}}),
                       false, &Fault});
    out.push_back({"part.feature_types",
                   "Все типы операций программы и их поля со значениями по умолчанию — в том виде, в каком их принимают "
                   "part.create_feature и part.edit_feature (и хранит файл .bzc). Посмотрите здесь, прежде чем создавать "
                   "операцию, для которой нет отдельной команды.",
                   Schema({{"type", StringArg("Только этот тип (hole, fillet, pattern…).")}}), false, &Types});
    out.push_back({"part.create_feature",
                   "Создаёт операцию любого типа по полям (часть полей — остальные по умолчанию; параметр можно дать числом "
                   "или выражением: \"depth\": 12). Ссылка на грань или ребро — объект ref из part.faces/part.edges как есть. Для частых операций "
                   "удобнее отдельные команды feature.*. Возвращает состояние операции.",
                   Schema({{"type", StringArg("Тип из part.feature_types.")},
                           {"fields", {{"type", "object"}, {"description", "Поля операции (как в part.feature)."}}},
                           {"name", StringArg("Имя в дереве.")},
                           {"index", IntegerArg("Куда вставить в дерево; по умолчанию — перед маркером конца детали.")},
                           {"name_parameters", BoolArg("Дать параметрам имена dN (по умолчанию true).")}},
                          {"type"}),
                   true, &Create});
    out.push_back({"part.edit_feature",
                   "Меняет поля существующей операции (только переданные; вложенные объекты сливаются, null удаляет), "
                   "её имя или подавление — одним шагом отмены. Текущие поля — part.feature.",
                   Schema({{"id", IdArg("id операции.")},
                           {"fields", {{"type", "object"}, {"description", "Изменяемые поля."}}},
                           {"name", StringArg("Новое имя.")},
                           {"suppressed", BoolArg("Подавить / включить.")}},
                          {"id"}),
                   true, &Edit});
    out.push_back({"part.move_feature", "Переносит операцию на другое место в дереве (номер из part.tree).",
                   Schema({{"id", IdArg("id операции.")}, {"index", IntegerArg("Новый номер в дереве.")}}, {"id", "index"}), true,
                   &Move});
    out.push_back({"part.set_end_marker",
                   "Маркер «Конец детали»: операции начиная с index откатываются (как перетащить маркер в дереве). Без index — в конец.",
                   Schema({{"index", IntegerArg("Номер в дереве.")}}), true, &EndMarker});
    out.push_back({"doc.close", "Закрывает активный документ. С несохранёнными изменениями — только с discard_changes: true.",
                   Schema({{"discard_changes", BoolArg("Закрыть, потеряв несохранённые изменения.")}}), true, &Close});
}

} // namespace bz::app::scripting
