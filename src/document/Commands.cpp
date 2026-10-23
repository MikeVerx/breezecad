#include "document/Commands.h"

#include "core/Expression.h"

#include <format>
#include <utility>
#include <unordered_map>
#include <optional>

namespace bz::doc {

// --- History ----------------------------------------------------------------

void History::Execute(Document& doc, std::unique_ptr<Command> command) {
    command->Do(doc);
    myCommands.resize(myPosition);   // drop redo tail
    if (mySavedPosition > myPosition) mySavedPosition = kNever;
    myCommands.push_back(std::move(command));
    ++myPosition;

    if (myCommands.size() > kLimit) {
        myCommands.erase(myCommands.begin());
        --myPosition;
        mySavedPosition = mySavedPosition == 0 || mySavedPosition == kNever ? kNever : mySavedPosition - 1;
    }
}

std::string History::UndoLabel() const { return CanUndo() ? myCommands[myPosition - 1]->Label() : std::string{}; }
std::string History::RedoLabel() const { return CanRedo() ? myCommands[myPosition]->Label() : std::string{}; }

void History::Undo(Document& doc) {
    if (!CanUndo()) return;
    --myPosition;
    myCommands[myPosition]->Undo(doc);
}

void History::Redo(Document& doc) {
    if (!CanRedo()) return;
    myCommands[myPosition]->Do(doc);
    ++myPosition;
}

History::Mark History::GetMark() const {
    Mark m;
    m.position = myPosition;
    m.size = myCommands.size();
    m.saved = mySavedPosition;
    m.first = myCommands.empty() ? nullptr : myCommands.front().get();
    m.last = myPosition > 0 ? myCommands[myPosition - 1].get() : nullptr;
    m.next = myPosition < myCommands.size() ? myCommands[myPosition].get() : nullptr;
    return m;
}

void History::RollBack(const Mark& mark) {
    if (GetMark() == mark) return;
    const bool prefixKept = myCommands.size() >= mark.position &&
                            (mark.position == 0 || myCommands[mark.position - 1].get() == mark.last) &&
                            (mark.first == nullptr || (!myCommands.empty() && myCommands.front().get() == mark.first));
    if (!prefixKept) {
        Clear();
        MarkUnsaved();
        return;
    }
    // A step made since stands where redo would have gone: drop it (and
    // whatever followed).
    if (myCommands.size() > mark.position && myCommands[mark.position].get() != mark.next) myCommands.resize(mark.position);
    myPosition = mark.position;
    mySavedPosition = mark.saved;
}

void History::Clear() {
    myCommands.clear();
    myPosition = 0;
    mySavedPosition = 0;
}

// --- commands ---------------------------------------------------------------
namespace {

class AddFeatureCommand final : public Command {
public:
    AddFeatureCommand(std::unique_ptr<Feature> feature, std::size_t index)
        : myFeature(std::move(feature)), myId(myFeature->id), myIndex(index), myLabel(myFeature->name) {}

    std::string Label() const override { return myLabel; }
    void Do(Document& doc) override { doc.Insert(std::move(myFeature), myIndex); }
    void Undo(Document& doc) override { myFeature = doc.Take(myId); }

private:
    std::unique_ptr<Feature> myFeature;   // owned while not in the document
    ObjectId                 myId;
    std::size_t              myIndex;
    std::string              myLabel;
};

class RemoveFeatureCommand final : public Command {
public:
    RemoveFeatureCommand(ObjectId id, std::size_t index, std::string label)
        : myId(id), myIndex(index), myLabel(std::move(label)) {}

    std::string Label() const override { return std::format("Удалить {}", myLabel); }
    void Do(Document& doc) override { myFeature = doc.Take(myId); }
    void Undo(Document& doc) override { doc.Insert(std::move(myFeature), myIndex); }

private:
    std::unique_ptr<Feature> myFeature;
    ObjectId                 myId;
    std::size_t              myIndex;
    std::string              myLabel;
};

class ReplaceFeatureCommand final : public Command {
public:
    ReplaceFeatureCommand(std::unique_ptr<Feature> other, std::string label)
        : myOther(std::move(other)), myLabel(std::move(label)) {}

    std::string Label() const override { return myLabel; }
    // Swapping both ways: Do and Undo are the same operation.
    void Do(Document& doc) override { myOther = doc.Replace(std::move(myOther)); }
    void Undo(Document& doc) override { myOther = doc.Replace(std::move(myOther)); }

private:
    std::unique_ptr<Feature> myOther;   // the version not currently in the document
    std::string              myLabel;
};

class MoveFeatureCommand final : public Command {
public:
    MoveFeatureCommand(ObjectId id, std::size_t from, std::size_t to, std::string label)
        : myId(id), myFrom(from), myTo(to), myLabel(std::move(label)) {}

    std::string Label() const override { return std::format("Переместить {}", myLabel); }
    void Do(Document& doc) override { doc.Move(myId, myTo); }
    void Undo(Document& doc) override { doc.Move(myId, myFrom); }

private:
    ObjectId    myId;
    std::size_t myFrom;
    std::size_t myTo;
    std::string myLabel;
};

class SetEndMarkerCommand final : public Command {
public:
    SetEndMarkerCommand(std::size_t from, std::size_t to) : myFrom(from), myTo(to) {}

    std::string Label() const override { return "Маркер конца"; }
    void Do(Document& doc) override { doc.SetEndMarker(myTo); }
    void Undo(Document& doc) override { doc.SetEndMarker(myFrom); }

private:
    std::size_t myFrom;
    std::size_t myTo;
};

class SetVariablesCommand final : public Command {
public:
    explicit SetVariablesCommand(std::vector<Parameter> other) : myOther(std::move(other)) {}

    std::string Label() const override { return "Переменные"; }
    void Do(Document& doc) override { Swap(doc); }
    void Undo(Document& doc) override { Swap(doc); }

private:
    void Swap(Document& doc) {
        std::swap(doc.variables, myOther);
        doc.MarkChanged(0);   // any feature may use a variable
    }

    std::vector<Parameter> myOther;
};

class RenameVariableCommand final : public Command {
public:
    RenameVariableCommand(std::size_t index, std::string name) : myIndex(index), myName(std::move(name)) {}

    std::string Label() const override { return "Переименование переменной"; }

    void Do(Document& doc) override {
        if (myIndex >= doc.variables.size()) return;
        Parameter& v = doc.variables[myIndex];
        myOld = v.name;
        v.name = myName;
        // Every expression that used the old name follows; the old texts are
        // kept by position (the same document state on undo, so the same order).
        myTexts.clear();
        const std::vector<Parameter*> all = doc.AllParameters();
        for (std::size_t i = 0; i < all.size(); ++i) {
            std::string renamed = RenameInExpression(all[i]->expression, myOld, myName);
            if (renamed == all[i]->expression) continue;
            myTexts.emplace_back(i, all[i]->expression);
            all[i]->expression = std::move(renamed);
        }
        doc.MarkChanged(0);
    }

    void Undo(Document& doc) override {
        if (myIndex >= doc.variables.size()) return;
        const std::vector<Parameter*> all = doc.AllParameters();
        for (const auto& [i, text] : myTexts)
            if (i < all.size()) all[i]->expression = text;
        doc.variables[myIndex].name = myOld;
        doc.MarkChanged(0);
    }

private:
    std::size_t myIndex;
    std::string myName, myOld;
    std::vector<std::pair<std::size_t, std::string>> myTexts;
};

class SetUnitsCommand final : public Command {
public:
    explicit SetUnitsCommand(UnitSystem other) : myOther(other) {}

    std::string Label() const override { return "Единицы измерения"; }

    void Do(Document& doc) override {
        // A plain number takes the document unit: "10" was 10 mm and would be
        // 10 in after the switch — the whole part 25.4 times bigger. Every
        // expression first gets the old unit written where it depends on it.
        myTexts.clear();
        std::unordered_map<std::string, NamedValue> named;
        for (const Parameter* p : std::as_const(doc).AllParameters())
            if (!p->name.empty()) named.emplace(p->name, NamedValue{p->value, p->quantity});
        const NameLookup lookup = [&](std::string_view name) -> std::optional<NamedValue> {
            const auto it = named.find(std::string(name));
            if (it == named.end()) return std::nullopt;
            return it->second;
        };
        const std::vector<Parameter*> all = doc.AllParameters();
        for (std::size_t i = 0; i < all.size(); ++i) {
            std::string text = ExpressionInUnits(all[i]->expression, all[i]->quantity, doc.units, myOther, lookup);
            if (text == all[i]->expression) continue;
            myTexts.emplace_back(i, all[i]->expression);
            all[i]->expression = std::move(text);
        }
        std::swap(doc.units, myOther);
        doc.MarkChanged(0);
    }

    void Undo(Document& doc) override {
        std::swap(doc.units, myOther);
        const std::vector<Parameter*> all = doc.AllParameters();
        for (const auto& [i, text] : myTexts)
            if (i < all.size()) all[i]->expression = text;
        doc.MarkChanged(0);
    }

private:
    UnitSystem myOther;
    std::vector<std::pair<std::size_t, std::string>> myTexts;   ///< by position in AllParameters, as before Do
};

class SetPartInfoCommand final : public Command {
public:
    explicit SetPartInfoCommand(PartInfo other) : myOther(std::move(other)) {}

    std::string Label() const override { return "Свойства детали"; }
    void Do(Document& doc) override { std::swap(doc.info, myOther); }
    void Undo(Document& doc) override { std::swap(doc.info, myOther); }

private:
    PartInfo myOther;
};

class SetLookCommand final : public Command {
public:
    SetLookCommand(Look other, std::string label) : myOther(std::move(other)), myLabel(std::move(label)) {}

    std::string Label() const override { return myLabel; }
    void Do(Document& doc) override {
        std::swap(doc.look, myOther);
        doc.TouchLook();
    }
    void Undo(Document& doc) override { Do(doc); }

private:
    Look        myOther;
    std::string myLabel;
};

class SetHiddenCommand final : public Command {
public:
    SetHiddenCommand(ObjectId id, bool hidden, std::string label) : myId(id), myHidden(hidden), myLabel(std::move(label)) {}

    std::string Label() const override { return myLabel; }
    void Do(Document& doc) override {
        if (Feature* f = doc.Find(myId)) {
            myBefore = f->hidden;
            Set(doc, *f, myHidden);
        }
    }
    void Undo(Document& doc) override {
        if (Feature* f = doc.Find(myId)) Set(doc, *f, myBefore);
    }

private:
    static void Set(Document& doc, Feature& f, bool hidden) {
        f.hidden = hidden;
        doc.TouchLook();   // a new revision (saved state, view), no rebuild
    }

    ObjectId    myId;
    bool        myHidden;
    bool        myBefore = false;
    std::string myLabel;
};

class GroupCommand final : public Command {
public:
    GroupCommand(std::string label, std::vector<std::unique_ptr<Command>> commands)
        : myLabel(std::move(label)), myCommands(std::move(commands)) {}

    std::string Label() const override { return myLabel; }
    void Do(Document& doc) override {
        for (auto& c : myCommands) c->Do(doc);
    }
    void Undo(Document& doc) override {
        for (auto it = myCommands.rbegin(); it != myCommands.rend(); ++it) (*it)->Undo(doc);
    }

private:
    std::string                           myLabel;
    std::vector<std::unique_ptr<Command>> myCommands;
};

} // namespace

std::unique_ptr<Command> AddFeature(std::unique_ptr<Feature> feature, std::size_t index) {
    return std::make_unique<AddFeatureCommand>(std::move(feature), index);
}

std::unique_ptr<Command> RemoveFeature(const Document& doc, ObjectId id) {
    const Feature* f = doc.Find(id);
    const int index = doc.IndexOf(id);
    return std::make_unique<RemoveFeatureCommand>(id, index < 0 ? 0 : static_cast<std::size_t>(index),
                                                  f ? f->name : std::string{});
}

std::unique_ptr<Command> ReplaceFeature(std::unique_ptr<Feature> edited, std::string label) {
    return std::make_unique<ReplaceFeatureCommand>(std::move(edited), std::move(label));
}

std::unique_ptr<Command> MoveFeature(const Document& doc, ObjectId id, std::size_t newIndex) {
    const Feature* f = doc.Find(id);
    const int index = doc.IndexOf(id);
    return std::make_unique<MoveFeatureCommand>(id, index < 0 ? 0 : static_cast<std::size_t>(index), newIndex,
                                                f ? f->name : std::string{});
}

std::unique_ptr<Command> SetEndMarker(const Document& doc, std::size_t index) {
    return std::make_unique<SetEndMarkerCommand>(doc.EndMarker(), index);
}

std::unique_ptr<Command> SetVariables(std::vector<Parameter> variables) {
    return std::make_unique<SetVariablesCommand>(std::move(variables));
}

std::unique_ptr<Command> RenameVariable(std::size_t index, std::string name) {
    return std::make_unique<RenameVariableCommand>(index, std::move(name));
}

std::unique_ptr<Command> SetUnits(UnitSystem units) {
    return std::make_unique<SetUnitsCommand>(units);
}

std::unique_ptr<Command> SetPartInfo(PartInfo info) {
    return std::make_unique<SetPartInfoCommand>(std::move(info));
}

std::unique_ptr<Command> SetLook(Look look, std::string label) {
    return std::make_unique<SetLookCommand>(std::move(look), std::move(label));
}

std::unique_ptr<Command> SetHidden(ObjectId id, bool hidden, std::string label) {
    return std::make_unique<SetHiddenCommand>(id, hidden, std::move(label));
}

std::unique_ptr<Command> Group(std::string label, std::vector<std::unique_ptr<Command>> commands) {
    return std::make_unique<GroupCommand>(std::move(label), std::move(commands));
}

} // namespace bz::doc
