// VFX Forge core: the editable document.
//
// A Document owns one Effect. It can be read freely, but the only way to
// change it is to run a Command (see Command.h), which is what makes every
// edit undoable and observable.
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "vfx/Effect.h"
#include "vfx/Id.h"
#include "vfx/Metadata.h"
#include "vfx/Path.h"
#include "vfx/Result.h"
#include "vfx/Value.h"

namespace vfx {

// A small record of what one command changed. View-models and the compile
// step listen for these; nothing polls the document.
struct Change {
    enum class Kind {
        Property,
        LayerAdded,
        LayerRemoved,
        LayerMoved,
        ModuleAdded,
        ModuleRemoved,
        ModuleMoved,
        AssetAdded,
        AssetRemoved,
    };

    Kind kind = Kind::Property;
    Id layer;
    Id module;
    Id asset;
    std::string field;  // Property changes only
};

class Document {
public:
    using Listener = std::function<void(const Change&)>;

    explicit Document(Effect effect, IdGenerator ids = IdGenerator::fromEntropy());

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    const Effect& effect() const { return effect_; }

    // Reads the value at a path.
    Result<Value> get(const Path& path) const;

    // The metadata for a path, or null if the path does not name a known
    // property in this document.
    const PropertyDesc* describe(const Path& path) const;

    // A fresh ID that is not used anywhere in the effect.
    Id newId();

    // Listeners are called after each change, on the thread that made it.
    int subscribe(Listener listener);
    void unsubscribe(int token);

private:
    friend class Command;

    void notify(const Change& change) const;

    Effect effect_;
    IdGenerator ids_;
    std::vector<std::pair<int, Listener>> listeners_;
    int nextToken_ = 1;
};

}  // namespace vfx
