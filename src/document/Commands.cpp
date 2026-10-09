#include "document/Commands.h"

#include <format>
#include <utility>

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

class SetUnitsCommand final : public Command {
public:
    explicit SetUnitsCommand(UnitSystem other) : myOther(other) {}

    std::string Label() const override { return "Единицы измерения"; }
    void Do(Document& doc) override { Swap(doc); }
    void Undo(Document& doc) override { Swap(doc); }

private:
    void Swap(Document& doc) {
        std::swap(doc.units, myOther);
        doc.MarkChanged(0);   // plain numbers in expressions now mean other units
    }

    UnitSystem myOther;
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

std::unique_ptr<Command> SetUnits(UnitSystem units) {
    return std::make_unique<SetUnitsCommand>(units);
}

std::unique_ptr<Command> SetPartInfo(PartInfo info) {
    return std::make_unique<SetPartInfoCommand>(std::move(info));
}

std::unique_ptr<Command> Group(std::string label, std::vector<std::unique_ptr<Command>> commands) {
    return std::make_unique<GroupCommand>(std::move(label), std::move(commands));
}

} // namespace bz::doc
