#include "document/Document.h"

#include "core/Expression.h"
#include "document/Origin.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <functional>
#include <unordered_map>

namespace bz::doc {

Document::Document() { myIds.Reserve(ObjectId{origin::kFirstUserId - 1}); }

Document Document::Clone() const {
    Document copy;
    copy.info = info;
    copy.units = units;
    copy.variables = variables;
    copy.myFeatures.reserve(myFeatures.size());
    for (const auto& f : myFeatures) copy.myFeatures.push_back(f->Clone());
    copy.myIds = myIds;
    copy.myEndMarker = myEndMarker;
    copy.myRevision = myRevision;
    copy.myDirtyFrom = myDirtyFrom;
    return copy;
}

void Document::TakeRebuilt(const Document& rebuilt) {
    if (rebuilt.myFeatures.size() != myFeatures.size() || rebuilt.variables.size() != variables.size()) return;
    for (std::size_t i = 0; i < variables.size(); ++i) variables[i] = rebuilt.variables[i];
    for (std::size_t i = 0; i < myFeatures.size(); ++i)
        if (myFeatures[i]->id == rebuilt.myFeatures[i]->id) myFeatures[i]->TakeRebuilt(*rebuilt.myFeatures[i]);
    myDirtyFrom = rebuilt.myDirtyFrom;
}

Feature* Document::Find(ObjectId id) {
    for (auto& f : myFeatures)
        if (f->id == id) return f.get();
    return nullptr;
}

const Feature* Document::Find(ObjectId id) const {
    for (const auto& f : myFeatures)
        if (f->id == id) return f.get();
    return nullptr;
}

int Document::IndexOf(ObjectId id) const {
    for (std::size_t i = 0; i < myFeatures.size(); ++i)
        if (myFeatures[i]->id == id) return static_cast<int>(i);
    return -1;
}

void Document::Insert(std::unique_ptr<Feature> feature, std::size_t index) {
    index = std::min(index, myFeatures.size());
    myIds.Reserve(feature->id);
    myFeatures.insert(myFeatures.begin() + static_cast<std::ptrdiff_t>(index), std::move(feature));
    if (index <= myEndMarker) ++myEndMarker;
    MarkChanged(index);
}

std::unique_ptr<Feature> Document::Take(ObjectId id) {
    const int index = IndexOf(id);
    if (index < 0) return nullptr;
    const auto i = static_cast<std::size_t>(index);
    std::unique_ptr<Feature> taken = std::move(myFeatures[i]);
    myFeatures.erase(myFeatures.begin() + index);
    if (i < myEndMarker) --myEndMarker;
    MarkChanged(i);
    return taken;
}

std::unique_ptr<Feature> Document::Replace(std::unique_ptr<Feature> feature) {
    const int index = IndexOf(feature->id);
    if (index < 0) return nullptr;
    const auto i = static_cast<std::size_t>(index);
    std::swap(myFeatures[i], feature);
    MarkChanged(i);
    return feature;
}

void Document::Move(ObjectId id, std::size_t newIndex) {
    const int index = IndexOf(id);
    if (index < 0) return;
    const bool wasActive = IsActive(static_cast<std::size_t>(index));
    std::unique_ptr<Feature> f = Take(id);
    newIndex = std::min(newIndex, myFeatures.size());
    myFeatures.insert(myFeatures.begin() + static_cast<std::ptrdiff_t>(newIndex), std::move(f));
    if (newIndex < myEndMarker || (wasActive && newIndex == myEndMarker)) ++myEndMarker;
    MarkChanged(std::min(static_cast<std::size_t>(index), newIndex));
}

void Document::SetEndMarker(std::size_t index) {
    index = std::min(index, myFeatures.size());
    if (index == myEndMarker) return;
    const std::size_t from = std::min(index, myEndMarker);
    myEndMarker = index;
    MarkChanged(from);
}

std::string Document::UniqueFeatureName(std::string_view base) const {
    for (int n = 1;; ++n) {
        std::string candidate = std::string(base) + std::to_string(n);
        const bool taken = std::any_of(myFeatures.begin(), myFeatures.end(),
                                       [&](const auto& f) { return f->name == candidate; });
        if (!taken) return candidate;
    }
}

std::string Document::NextParameterName() const {
    // One past the largest "dN" in use, like Inventor: numbers are not reused,
    // so a deleted d3 does not come back with another meaning.
    int largest = 0;
    for (const Parameter* p : AllParameters()) {
        if (p->name.size() < 2 || p->name[0] != 'd') continue;
        int n = 0;
        const char* begin = p->name.data() + 1;
        const char* end = p->name.data() + p->name.size();
        const auto [ptr, ec] = std::from_chars(begin, end, n);
        if (ec == std::errc{} && ptr == end) largest = std::max(largest, n);
    }
    return "d" + std::to_string(largest + 1);
}

std::vector<Parameter*> Document::AllParameters() {
    std::vector<Parameter*> out;
    for (auto& v : variables) out.push_back(&v);
    for (auto& f : myFeatures) f->CollectParameters(out);
    return out;
}

std::vector<const Parameter*> Document::AllParameters() const {
    std::vector<const Parameter*> out;
    for (const auto& v : variables) out.push_back(&v);
    for (const auto& f : myFeatures)
        for (const Parameter* p : f->Parameters()) out.push_back(p);
    return out;
}

int Document::EvaluateParameters() {
    const std::vector<Parameter*> params = AllParameters();

    std::unordered_map<std::string, Parameter*> byName;
    for (Parameter* p : params) {
        p->error.clear();
        if (p->name.empty()) continue;
        if (!byName.emplace(p->name, p).second) p->error = std::format("Имя «{}» уже занято", p->name);
    }

    enum class Mark : unsigned char { Unvisited, InProgress, Done };
    std::unordered_map<const Parameter*, Mark> marks;

    // Evaluates `p` after the parameters it references (depth-first).
    std::function<void(Parameter*)> evaluate = [&](Parameter* p) {
        Mark& mark = marks[p];
        if (mark == Mark::Done) return;
        if (mark == Mark::InProgress) {
            p->error = std::format("Циклическая ссылка через «{}»", p->name);
            return;
        }
        mark = Mark::InProgress;

        const NameLookup lookup = [&](std::string_view name) -> std::optional<NamedValue> {
            const auto it = byName.find(std::string(name));
            if (it == byName.end() || it->second == p) return std::nullopt;
            evaluate(it->second);
            if (marks[it->second] != Mark::Done) return std::nullopt;   // part of a cycle
            return NamedValue{it->second->value, it->second->quantity};
        };
        Expected<double> result = EvaluateExpression(p->expression, p->quantity, units, lookup);
        if (result) {
            if (p->error.empty()) p->value = *result;
        } else if (p->error.empty()) {
            p->error = result.GetError().message;
        }
        marks[p] = Mark::Done;
    };

    int errors = 0;
    for (Parameter* p : params) {
        evaluate(p);
        if (p->HasError()) ++errors;
    }
    return errors;
}

void Document::MarkChanged(std::size_t fromIndex) {
    ++myRevision;
    myDirtyFrom = std::min(myDirtyFrom, fromIndex);
}

} // namespace bz::doc
