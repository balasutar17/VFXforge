#include "vfx/Document.h"

#include <algorithm>
#include <utility>

#include "Access.h"

namespace vfx {

namespace detail {

const PropertyDesc* describePath(const Effect& effect, const Path& path) {
    const auto& registry = Registry::builtin();
    if (path.isEffectField()) {
        return registry.effectFields().find(path.field);
    }
    const Layer* layer = findLayer(effect, path.layer);
    if (!layer) {
        return nullptr;
    }
    if (path.isLayerField()) {
        return registry.layerFields().find(path.field);
    }
    const Module* module = findModule(*layer, path.module);
    if (!module || !module->desc) {
        return nullptr;
    }
    return module->desc->find(path.field);
}

namespace {

// Effect and layer fields live in plain struct members. These two helpers
// are the single place that maps a field name onto a member.
template <class EffectT, class Fn>
bool visitEffectField(EffectT& e, std::string_view field, Fn&& fn) {
    if (field == "name") { fn(e.name); return true; }
    if (field == "space") { fn(e.space); return true; }
    if (field == "seed") { fn(e.seed); return true; }
    if (field == "duration") { fn(e.duration); return true; }
    if (field == "loop") { fn(e.loop); return true; }
    if (field == "frameRate") { fn(e.frameRate); return true; }
    return false;
}

template <class LayerT, class Fn>
bool visitLayerField(LayerT& l, std::string_view field, Fn&& fn) {
    if (field == "name") { fn(l.name); return true; }
    if (field == "enabled") { fn(l.enabled); return true; }
    if (field == "start") { fn(l.start); return true; }
    if (field == "duration") { fn(l.duration); return true; }
    if (field == "role") { fn(l.role); return true; }
    if (field == "locked") { fn(l.locked); return true; }
    return false;
}

struct Reader {
    Value* out;
    template <class T>
    void operator()(const T& member) const { *out = Value(std::in_place_type<T>, member); }
};

struct Writer {
    const Value* in;
    bool* ok;
    template <class T>
    void operator()(T& member) const {
        if (const auto* v = std::get_if<T>(in)) {
            member = *v;
            *ok = true;
        }
    }
};

}  // namespace

bool readValue(const Effect& effect, const Path& path, Value& out) {
    if (path.isEffectField()) {
        return visitEffectField(effect, path.field, Reader{&out});
    }
    const Layer* layer = findLayer(effect, path.layer);
    if (!layer) {
        return false;
    }
    if (path.isLayerField()) {
        return visitLayerField(*layer, path.field, Reader{&out});
    }
    const Module* module = findModule(*layer, path.module);
    if (!module) {
        return false;
    }
    const Value* value = module->find(path.field);
    if (!value) {
        return false;
    }
    out = *value;
    return true;
}

bool writeValue(Effect& effect, const Path& path, const Value& value) {
    bool ok = false;
    if (path.isEffectField()) {
        visitEffectField(effect, path.field, Writer{&value, &ok});
        return ok;
    }
    Layer* layer = findLayer(effect, path.layer);
    if (!layer) {
        return false;
    }
    if (path.isLayerField()) {
        visitLayerField(*layer, path.field, Writer{&value, &ok});
        return ok;
    }
    Module* module = findModule(*layer, path.module);
    if (!module) {
        return false;
    }
    Value* slot = module->find(path.field);
    if (!slot || slot->index() != value.index()) {
        return false;
    }
    *slot = value;
    return true;
}

}  // namespace detail

Document::Document(Effect effect, IdGenerator ids) : effect_(std::move(effect)), ids_(ids) {}

Result<Value> Document::get(const Path& path) const {
    Value value;
    if (!detail::readValue(effect_, path, value)) {
        return makeError("There is no property at \"" + path.str() + "\".");
    }
    return value;
}

const PropertyDesc* Document::describe(const Path& path) const {
    return detail::describePath(effect_, path);
}

Id Document::newId() {
    const std::vector<Id> used = collectIds(effect_);
    for (;;) {
        const Id candidate = ids_.next();
        if (std::find(used.begin(), used.end(), candidate) == used.end()) {
            return candidate;
        }
    }
}

int Document::subscribe(Listener listener) {
    const int token = nextToken_++;
    listeners_.emplace_back(token, std::move(listener));
    return token;
}

void Document::unsubscribe(int token) {
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                    [token](const auto& entry) { return entry.first == token; }),
                     listeners_.end());
}

void Document::notify(const Change& change) const {
    // Copy first so a listener may subscribe or unsubscribe while being called.
    const auto snapshot = listeners_;
    for (const auto& entry : snapshot) {
        entry.second(change);
    }
}

}  // namespace vfx
