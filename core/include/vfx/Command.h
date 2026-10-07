// VFX Forge core: commands.
//
// Every change to a Document is a Command with an apply and a revert. The UI,
// the command-line tool, tests and (later) scripting and AI edits all go
// through this one door.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "vfx/Document.h"
#include "vfx/Effect.h"
#include "vfx/Path.h"
#include "vfx/Result.h"
#include "vfx/Value.h"

namespace vfx {

class Command {
public:
    virtual ~Command() = default;

    // Short label for the Edit menu, for example "Change Gravity".
    virtual std::string name() const = 0;

    // Makes the change. On failure the document must be left untouched.
    // Calling apply again after revert must reproduce the same state,
    // including the same IDs.
    virtual Status apply(Document& document) = 0;

    // Undoes a successful apply.
    virtual void revert(Document& document) = 0;

    // Coalescing, used while a transaction is open (a slider drag): when
    // canMerge is true, merge folds an already-applied later command into
    // this one so the pair undoes as a single step.
    virtual bool canMerge(const Command& later) const;
    virtual void merge(const Command& later);

    // True when the applied command left the document as it found it.
    virtual bool isNoOp() const { return false; }

protected:
    // The only route to mutable effect data.
    static Effect& edit(Document& document);
    static void notify(Document& document, const Change& change);
};

using CommandPtr = std::unique_ptr<Command>;

// Sets the value at a property path. Covers effect fields, layer fields and
// module properties, so renaming a layer, moving its start time and dragging
// a gravity slider are all this one command.
class SetPropertyCommand final : public Command {
public:
    SetPropertyCommand(Path path, Value value);

    std::string name() const override;
    Status apply(Document& document) override;
    void revert(Document& document) override;
    bool canMerge(const Command& later) const override;
    void merge(const Command& later) override;
    bool isNoOp() const override;

    const Path& path() const { return path_; }

private:
    Path path_;
    Value newValue_;
    Value oldValue_;
    std::string label_;
};

class AddLayerCommand final : public Command {
public:
    // index -1 appends.
    explicit AddLayerCommand(Layer layer, int index = -1);

    std::string name() const override;
    Status apply(Document& document) override;
    void revert(Document& document) override;

private:
    Layer layer_;
    int index_;
};

class RemoveLayerCommand final : public Command {
public:
    explicit RemoveLayerCommand(Id layer);

    std::string name() const override;
    Status apply(Document& document) override;
    void revert(Document& document) override;

private:
    Id id_;
    Layer removed_;
    int index_ = -1;
};

class MoveLayerCommand final : public Command {
public:
    MoveLayerCommand(Id layer, int newIndex);

    std::string name() const override;
    Status apply(Document& document) override;
    void revert(Document& document) override;
    bool isNoOp() const override { return oldIndex_ == newIndex_; }

private:
    Id id_;
    int newIndex_;
    int oldIndex_ = -1;
};

class AddModuleCommand final : public Command {
public:
    // index -1 appends.
    AddModuleCommand(Id layer, Module module, int index = -1);

    std::string name() const override;
    Status apply(Document& document) override;
    void revert(Document& document) override;

private:
    Id layer_;
    Module module_;
    int index_;
};

class RemoveModuleCommand final : public Command {
public:
    RemoveModuleCommand(Id layer, Id module);

    std::string name() const override;
    Status apply(Document& document) override;
    void revert(Document& document) override;

private:
    Id layer_;
    Id id_;
    Module removed_;
    int index_ = -1;
};

class MoveModuleCommand final : public Command {
public:
    MoveModuleCommand(Id layer, Id module, int newIndex);

    std::string name() const override;
    Status apply(Document& document) override;
    void revert(Document& document) override;
    bool isNoOp() const override { return oldIndex_ == newIndex_; }

private:
    Id layer_;
    Id id_;
    int newIndex_;
    int oldIndex_ = -1;
};

class AddAssetCommand final : public Command {
public:
    explicit AddAssetCommand(Asset asset);

    std::string name() const override;
    Status apply(Document& document) override;
    void revert(Document& document) override;

private:
    Asset asset_;
};

// Fails while any property still refers to the asset.
class RemoveAssetCommand final : public Command {
public:
    explicit RemoveAssetCommand(Id asset);

    std::string name() const override;
    Status apply(Document& document) override;
    void revert(Document& document) override;

private:
    Id id_;
    Asset removed_;
    int index_ = -1;
};

// Several commands that apply and undo as one step. If a child fails, the
// children already applied are reverted and the composite fails as a whole.
class CompositeCommand final : public Command {
public:
    CompositeCommand(std::string name, std::vector<CommandPtr> children);

    std::string name() const override { return name_; }
    Status apply(Document& document) override;
    void revert(Document& document) override;
    bool canMerge(const Command& later) const override;
    void merge(const Command& later) override;
    bool isNoOp() const override;

private:
    std::string name_;
    std::vector<CommandPtr> children_;
};

}  // namespace vfx
