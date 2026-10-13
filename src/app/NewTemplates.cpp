#include "app/NewTemplates.h"

#include <algorithm>

namespace bz::app {

bool NewGroup::Available() const {
    return std::ranges::any_of(templates, [](const NewTemplate& t) { return t.available; });
}

std::vector<NewGroup> NewDocumentGroups(const doc::DocumentTypeRegistry& registry) {
    std::vector<NewGroup> groups;
    for (const doc::DocumentTypeInfo& type : registry.All()) {
        const std::string name = type.group.empty() ? type.title : type.group;
        auto group = std::ranges::find(groups, name, &NewGroup::name);
        if (group == groups.end()) {
            groups.push_back({name, {}});
            group = groups.end() - 1;
        }
        NewTemplate base{.id = type.id, .type = type.id, .title = type.title, .note = {}, .icon = type.icon,
                         .available = type.canCreate, .length = std::nullopt};
        if (type.id == doc::kPartType) {
            // A part carries its length units: the two that make sense for DIY.
            NewTemplate mm = base, inch = base;
            mm.id = "part.mm";
            mm.note = "мм";
            mm.length = LengthUnit::Millimeter;
            inch.id = "part.inch";
            inch.note = "дюймы";
            inch.length = LengthUnit::Inch;
            group->templates.push_back(std::move(mm));
            group->templates.push_back(std::move(inch));
        } else {
            group->templates.push_back(std::move(base));
        }
    }
    return groups;
}

const NewTemplate* FindTemplate(const std::vector<NewGroup>& groups, std::string_view id) {
    for (const NewGroup& g : groups)
        for (const NewTemplate& t : g.templates)
            if (t.id == id) return &t;
    return nullptr;
}

const NewTemplate* InitialTemplate(const std::vector<NewGroup>& groups, std::string_view last, LengthUnit defaultLength) {
    if (const NewTemplate* t = FindTemplate(groups, last); t && t->available) return t;
    for (const NewGroup& g : groups)
        for (const NewTemplate& t : g.templates)
            if (t.available && t.type == doc::kPartType && t.length == defaultLength) return &t;
    for (const NewGroup& g : groups)
        for (const NewTemplate& t : g.templates)
            if (t.available) return &t;
    return nullptr;
}

} // namespace bz::app
