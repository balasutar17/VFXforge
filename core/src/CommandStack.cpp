#include "vfx/CommandStack.h"

#include <algorithm>
#include <utility>

namespace vfx {

CommandStack::CommandStack(Document& document, std::size_t limit)
    : document_(document), limit_(std::max<std::size_t>(limit, 1)) {}

Status CommandStack::push(CommandPtr command) {
    if (!command) {
        return makeError("Nothing to do.", "null command pushed");
    }
    if (auto s = command->apply(document_); !s) {
        return s;
    }
    if (depth_ > 0) {
        if (!pending_.empty() && pending_.back()->canMerge(*command)) {
            pending_.back()->merge(*command);
        } else {
            pending_.push_back(std::move(command));
        }
        return {};
    }
    commit(std::move(command));
    return {};
}

void CommandStack::commit(CommandPtr command) {
    if (command->isNoOp()) {
        return;  // nothing changed, so nothing to undo and redo stays valid
    }
    if (index_ < steps_.size()) {
        // A new edit after an undo discards the redo branch. If the save
        // point was in that branch it can no longer be reached.
        steps_.erase(steps_.begin() + static_cast<std::ptrdiff_t>(index_), steps_.end());
        if (savedIndex_ > static_cast<std::ptrdiff_t>(index_)) {
            savedIndex_ = kUnreachable;
        }
    }
    steps_.push_back(std::move(command));
    ++index_;
    if (steps_.size() > limit_) {
        steps_.erase(steps_.begin());
        --index_;
        if (savedIndex_ == 0) {
            savedIndex_ = kUnreachable;
        } else if (savedIndex_ > 0) {
            --savedIndex_;
        }
    }
}

bool CommandStack::canUndo() const { return depth_ == 0 && index_ > 0; }
bool CommandStack::canRedo() const { return depth_ == 0 && index_ < steps_.size(); }

Status CommandStack::undo() {
    if (depth_ > 0) {
        return makeError("Finish the current edit before undoing.");
    }
    if (index_ == 0) {
        return makeError("There is nothing to undo.");
    }
    --index_;
    steps_[index_]->revert(document_);
    return {};
}

Status CommandStack::redo() {
    if (depth_ > 0) {
        return makeError("Finish the current edit before redoing.");
    }
    if (index_ >= steps_.size()) {
        return makeError("There is nothing to redo.");
    }
    if (auto s = steps_[index_]->apply(document_); !s) {
        return s;
    }
    ++index_;
    return {};
}

std::string CommandStack::undoName() const {
    return canUndo() ? steps_[index_ - 1]->name() : std::string();
}

std::string CommandStack::redoName() const {
    return canRedo() ? steps_[index_]->name() : std::string();
}

void CommandStack::beginTransaction(std::string name) {
    if (depth_++ == 0) {
        transactionName_ = std::move(name);
        pending_.clear();
    }
}

void CommandStack::endTransaction() {
    if (depth_ == 0 || --depth_ > 0) {
        return;
    }
    if (pending_.size() == 1) {
        commit(std::move(pending_.front()));
    } else if (pending_.size() > 1) {
        commit(std::make_unique<CompositeCommand>(transactionName_, std::move(pending_)));
    }
    pending_.clear();
}

void CommandStack::cancelTransaction() {
    if (depth_ == 0) {
        return;
    }
    for (auto it = pending_.rbegin(); it != pending_.rend(); ++it) {
        (*it)->revert(document_);
    }
    pending_.clear();
    depth_ = 0;
}

void CommandStack::markSaved() { savedIndex_ = static_cast<std::ptrdiff_t>(index_); }

bool CommandStack::isDirty() const {
    return !pending_.empty() || savedIndex_ != static_cast<std::ptrdiff_t>(index_);
}

void CommandStack::clear() {
    const bool dirty = isDirty();
    steps_.clear();
    pending_.clear();
    index_ = 0;
    depth_ = 0;
    savedIndex_ = dirty ? kUnreachable : 0;
}

}  // namespace vfx
