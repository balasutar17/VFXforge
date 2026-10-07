// VFX Forge core: undo and redo.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "vfx/Command.h"
#include "vfx/Document.h"
#include "vfx/Result.h"

namespace vfx {

class CommandStack {
public:
    explicit CommandStack(Document& document, std::size_t limit = 1000);

    // Applies the command. On success it becomes the next undo step, or part
    // of the open transaction.
    Status push(CommandPtr command);

    bool canUndo() const;
    bool canRedo() const;
    Status undo();
    Status redo();

    // Labels for the Edit menu. Empty when there is nothing to undo or redo.
    std::string undoName() const;
    std::string redoName() const;

    // Groups every command pushed until the matching end into one undo step.
    // Consecutive edits of the same property coalesce, so a slider drag with
    // hundreds of intermediate values undoes in one go. Transactions nest;
    // only the outermost end commits.
    void beginTransaction(std::string name);
    void endTransaction();
    // Reverts everything pushed since the outermost begin and discards it.
    void cancelTransaction();
    bool inTransaction() const { return depth_ > 0; }

    // The save point. isDirty is false exactly when the document matches the
    // state it had at the last markSaved, including after undoing back to it.
    void markSaved();
    bool isDirty() const;

    std::size_t undoCount() const { return index_; }
    std::size_t redoCount() const { return steps_.size() - index_; }

    void clear();

private:
    void commit(CommandPtr command);

    static constexpr std::ptrdiff_t kUnreachable = -1;

    Document& document_;
    std::size_t limit_;
    std::vector<CommandPtr> steps_;  // steps_[0..index_) are undoable
    std::size_t index_ = 0;
    std::ptrdiff_t savedIndex_ = 0;

    int depth_ = 0;
    std::string transactionName_;
    std::vector<CommandPtr> pending_;
};

}  // namespace vfx
