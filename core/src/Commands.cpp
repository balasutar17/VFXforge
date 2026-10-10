#include "vfx/Command.h"

#include <algorithm>
#include <utility>

#include "Access.h"
#include "Checks.h"
#include "Utf8.h"

namespace vfx {

namespace detail {

bool isPortableRelativePath(std::string_view path) {
    if (path.empty() || path.size() > kMaxTextLength || !isValidUtf8(path)) {
        return false;
    }
    if (path.front() == '/' || path.back() == '/') {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find('/', start);
        if (end == std::string_view::npos) {
            end = path.size();
        }
        const std::string_view part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        for (char c : part) {
            // Backslashes and colons would make the path mean different
            // things on Windows and macOS. Control characters are never valid.
            if (c == '\\' || c == ':' || static_cast<unsigned char>(c) < 0x20) {
                return false;
            }
        }
        start = end + 1;
    }
    return true;
}

Status validateAsset(const Asset& asset) {
    if (!asset.id.valid()) {
        return makeError("The asset has no ID.");
    }
    if (asset.kind.empty() || asset.kind.size() > kMaxTextLength || !isValidUtf8(asset.kind) ||
        asset.hash.size() > kMaxTextLength || !isValidUtf8(asset.hash)) {
        return makeError("The asset description is not valid.");
    }
    if (!isPortableRelativePath(asset.path)) {
        return makeError("\"" + asset.path + "\" is not a usable asset location.",
                         "asset paths must be relative to the project folder, use forward "
                         "slashes, and contain no '..' parts");
    }
    return {};
}

Status validateModule(const Module& module) {
    if (!module.id.valid()) {
        return makeError("The module has no ID.");
    }
    if (module.type.empty() || module.type.size() > kMaxTextLength || !isValidUtf8(module.type)) {
        return makeError("The module has no valid type.");
    }
    if (!module.desc) {
        // Unknown to this version: carried along untouched, never edited.
        if (!module.values.empty()) {
            return makeError("The module is not valid.", "unknown module type with values");
        }
        return {};
    }
    if (module.desc != Registry::builtin().findModule(module.type) ||
        module.values.size() != module.desc->properties.size()) {
        return makeError("The module is not valid.", "type and property table disagree");
    }
    for (std::size_t i = 0; i < module.values.size(); ++i) {
        if (auto s = validateValue(module.desc->properties[i], module.values[i]); !s) {
            return s;
        }
    }
    return {};
}

Status validateLayer(const Layer& layer) {
    if (!layer.id.valid()) {
        return makeError("The layer has no ID.");
    }
    const auto& fields = Registry::builtin().layerFields();
    if (auto s = validateValue(*fields.find("name"), Value(layer.name)); !s) {
        return s;
    }
    if (auto s = validateValue(*fields.find("start"), Value(layer.start)); !s) {
        return s;
    }
    if (auto s = validateValue(*fields.find("duration"), Value(layer.duration)); !s) {
        return s;
    }
    if (auto s = validateValue(*fields.find("role"), Value(layer.role)); !s) {
        return s;
    }
    for (const auto& control : layer.controls) {
        if (!control.id.valid() || control.label.size() > kMaxTextLength ||
            !isValidUtf8(control.label)) {
            return makeError("The layer has a control that is not valid.");
        }
        for (const auto& target : control.targets) {
            if (!target.module.valid() || target.property.empty() ||
                target.property.size() > kMaxTextLength || !isValidUtf8(target.property) ||
                target.property.find('/') != std::string::npos) {
                return makeError("The layer has a control that is not valid.");
            }
        }
    }
    for (const auto& module : layer.modules) {
        if (auto s = validateModule(module); !s) {
            return s;
        }
    }
    return {};
}

}  // namespace detail

namespace {

bool contains(const std::vector<Id>& ids, Id id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

Error missingLayer() { return makeError("That layer no longer exists."); }
Error missingModule() { return makeError("That module no longer exists."); }

template <class T>
void moveItem(std::vector<T>& items, int from, int to) {
    if (from == to) {
        return;
    }
    T item = std::move(items[static_cast<std::size_t>(from)]);
    items.erase(items.begin() + from);
    items.insert(items.begin() + to, std::move(item));
}

}  // namespace

// ---------------------------------------------------------------- Command

bool Command::canMerge(const Command&) const { return false; }
void Command::merge(const Command&) {}
Effect& Command::edit(Document& document) { return document.effect_; }
void Command::notify(Document& document, const Change& change) { document.notify(change); }

// ----------------------------------------------------- SetPropertyCommand

SetPropertyCommand::SetPropertyCommand(Path path, Value value)
    : path_(std::move(path)), newValue_(std::move(value)) {}

std::string SetPropertyCommand::name() const {
    return label_.empty() ? "Change Property" : "Change " + label_;
}

Status SetPropertyCommand::apply(Document& document) {
    const PropertyDesc* desc = document.describe(path_);
    if (!desc) {
        return makeError("There is no property at \"" + path_.str() + "\".");
    }
    if (auto s = validateValue(*desc, newValue_); !s) {
        return s;
    }
    if (const auto* ref = std::get_if<AssetRef>(&newValue_)) {
        if (ref->id.valid() && !findAsset(document.effect(), ref->id)) {
            return makeError("That asset is not part of this effect.");
        }
    }
    Value old;
    if (!detail::readValue(document.effect(), path_, old)) {
        return makeError("There is no property at \"" + path_.str() + "\".");
    }
    if (!detail::writeValue(edit(document), path_, newValue_)) {
        return makeError(desc->label + " could not be changed.", "write rejected at " + path_.str());
    }
    oldValue_ = std::move(old);
    label_ = desc->label;
    notify(document, Change{Change::Kind::Property, path_.layer, path_.module, {}, path_.field});
    return {};
}

void SetPropertyCommand::revert(Document& document) {
    detail::writeValue(edit(document), path_, oldValue_);
    notify(document, Change{Change::Kind::Property, path_.layer, path_.module, {}, path_.field});
}

bool SetPropertyCommand::canMerge(const Command& later) const {
    const auto* other = dynamic_cast<const SetPropertyCommand*>(&later);
    return other && other->path_ == path_;
}

void SetPropertyCommand::merge(const Command& later) {
    newValue_ = static_cast<const SetPropertyCommand&>(later).newValue_;
}

bool SetPropertyCommand::isNoOp() const { return oldValue_ == newValue_; }

// -------------------------------------------------------- AddLayerCommand

AddLayerCommand::AddLayerCommand(Layer layer, int index)
    : layer_(std::move(layer)), index_(index) {}

std::string AddLayerCommand::name() const { return "Add Layer"; }

Status AddLayerCommand::apply(Document& document) {
    if (auto s = detail::validateLayer(layer_); !s) {
        return s;
    }
    std::vector<Id> used = collectIds(document.effect());
    auto claim = [&used](Id id) {
        if (contains(used, id)) {
            return false;
        }
        used.push_back(id);
        return true;
    };
    bool unique = claim(layer_.id);
    for (const auto& c : layer_.controls) {
        unique = unique && claim(c.id);
    }
    for (const auto& m : layer_.modules) {
        unique = unique && claim(m.id);
    }
    if (!unique) {
        return makeError("The layer could not be added.", "an ID in the layer is already in use");
    }

    auto& layers = edit(document).layers;
    const int count = static_cast<int>(layers.size());
    if (index_ < 0 || index_ > count) {
        index_ = count;
    }
    layers.insert(layers.begin() + index_, layer_);
    notify(document, Change{Change::Kind::LayerAdded, layer_.id, {}, {}, {}});
    return {};
}

void AddLayerCommand::revert(Document& document) {
    auto& effect = edit(document);
    const int i = layerIndex(effect, layer_.id);
    if (i >= 0) {
        effect.layers.erase(effect.layers.begin() + i);
        notify(document, Change{Change::Kind::LayerRemoved, layer_.id, {}, {}, {}});
    }
}

// ----------------------------------------------------- RemoveLayerCommand

RemoveLayerCommand::RemoveLayerCommand(Id layer) : id_(layer) {}

std::string RemoveLayerCommand::name() const { return "Delete Layer"; }

Status RemoveLayerCommand::apply(Document& document) {
    auto& effect = edit(document);
    const int i = layerIndex(effect, id_);
    if (i < 0) {
        return missingLayer();
    }
    index_ = i;
    removed_ = std::move(effect.layers[static_cast<std::size_t>(i)]);
    effect.layers.erase(effect.layers.begin() + i);
    notify(document, Change{Change::Kind::LayerRemoved, id_, {}, {}, {}});
    return {};
}

void RemoveLayerCommand::revert(Document& document) {
    auto& layers = edit(document).layers;
    const int at = std::min(index_, static_cast<int>(layers.size()));
    // The identical object goes back, IDs and all, so anything that referred
    // to it before the delete still does after the undo.
    layers.insert(layers.begin() + at, removed_);
    notify(document, Change{Change::Kind::LayerAdded, id_, {}, {}, {}});
}

// ------------------------------------------------------- MoveLayerCommand

MoveLayerCommand::MoveLayerCommand(Id layer, int newIndex) : id_(layer), newIndex_(newIndex) {}

std::string MoveLayerCommand::name() const { return "Reorder Layer"; }

Status MoveLayerCommand::apply(Document& document) {
    auto& effect = edit(document);
    const int i = layerIndex(effect, id_);
    if (i < 0) {
        return missingLayer();
    }
    oldIndex_ = i;
    newIndex_ = std::clamp(newIndex_, 0, static_cast<int>(effect.layers.size()) - 1);
    moveItem(effect.layers, oldIndex_, newIndex_);
    notify(document, Change{Change::Kind::LayerMoved, id_, {}, {}, {}});
    return {};
}

void MoveLayerCommand::revert(Document& document) {
    moveItem(edit(document).layers, newIndex_, oldIndex_);
    notify(document, Change{Change::Kind::LayerMoved, id_, {}, {}, {}});
}

// ------------------------------------------------------- AddModuleCommand

AddModuleCommand::AddModuleCommand(Id layer, Module module, int index)
    : layer_(layer), module_(std::move(module)), index_(index) {}

std::string AddModuleCommand::name() const {
    return module_.desc ? "Add " + module_.desc->label : "Add Module";
}

Status AddModuleCommand::apply(Document& document) {
    if (auto s = detail::validateModule(module_); !s) {
        return s;
    }
    Layer* layer = findLayer(edit(document), layer_);
    if (!layer) {
        return missingLayer();
    }
    if (contains(collectIds(document.effect()), module_.id)) {
        return makeError("The module could not be added.", "its ID is already in use");
    }
    if (module_.desc) {
        for (const auto& existing : layer->modules) {
            if (existing.type == module_.type) {
                return makeError("This layer already has " + module_.desc->label + ".");
            }
        }
    }
    const int count = static_cast<int>(layer->modules.size());
    if (index_ < 0 || index_ > count) {
        index_ = count;
    }
    layer->modules.insert(layer->modules.begin() + index_, module_);
    notify(document, Change{Change::Kind::ModuleAdded, layer_, module_.id, {}, {}});
    return {};
}

void AddModuleCommand::revert(Document& document) {
    Layer* layer = findLayer(edit(document), layer_);
    if (!layer) {
        return;
    }
    const int i = moduleIndex(*layer, module_.id);
    if (i >= 0) {
        layer->modules.erase(layer->modules.begin() + i);
        notify(document, Change{Change::Kind::ModuleRemoved, layer_, module_.id, {}, {}});
    }
}

// ---------------------------------------------------- RemoveModuleCommand

RemoveModuleCommand::RemoveModuleCommand(Id layer, Id module) : layer_(layer), id_(module) {}

std::string RemoveModuleCommand::name() const {
    return removed_.desc ? "Remove " + removed_.desc->label : "Remove Module";
}

Status RemoveModuleCommand::apply(Document& document) {
    Layer* layer = findLayer(edit(document), layer_);
    if (!layer) {
        return missingLayer();
    }
    const int i = moduleIndex(*layer, id_);
    if (i < 0) {
        return missingModule();
    }
    index_ = i;
    removed_ = std::move(layer->modules[static_cast<std::size_t>(i)]);
    layer->modules.erase(layer->modules.begin() + i);
    notify(document, Change{Change::Kind::ModuleRemoved, layer_, id_, {}, {}});
    return {};
}

void RemoveModuleCommand::revert(Document& document) {
    Layer* layer = findLayer(edit(document), layer_);
    if (!layer) {
        return;
    }
    const int at = std::min(index_, static_cast<int>(layer->modules.size()));
    layer->modules.insert(layer->modules.begin() + at, removed_);
    notify(document, Change{Change::Kind::ModuleAdded, layer_, id_, {}, {}});
}

// ------------------------------------------------------ MoveModuleCommand

MoveModuleCommand::MoveModuleCommand(Id layer, Id module, int newIndex)
    : layer_(layer), id_(module), newIndex_(newIndex) {}

std::string MoveModuleCommand::name() const { return "Reorder Module"; }

Status MoveModuleCommand::apply(Document& document) {
    Layer* layer = findLayer(edit(document), layer_);
    if (!layer) {
        return missingLayer();
    }
    const int i = moduleIndex(*layer, id_);
    if (i < 0) {
        return missingModule();
    }
    oldIndex_ = i;
    newIndex_ = std::clamp(newIndex_, 0, static_cast<int>(layer->modules.size()) - 1);
    moveItem(layer->modules, oldIndex_, newIndex_);
    notify(document, Change{Change::Kind::ModuleMoved, layer_, id_, {}, {}});
    return {};
}

void MoveModuleCommand::revert(Document& document) {
    Layer* layer = findLayer(edit(document), layer_);
    if (!layer) {
        return;
    }
    moveItem(layer->modules, newIndex_, oldIndex_);
    notify(document, Change{Change::Kind::ModuleMoved, layer_, id_, {}, {}});
}

// -------------------------------------------------------- AddAssetCommand

AddAssetCommand::AddAssetCommand(Asset asset) : asset_(std::move(asset)) {}

std::string AddAssetCommand::name() const { return "Add Asset"; }

Status AddAssetCommand::apply(Document& document) {
    if (auto s = detail::validateAsset(asset_); !s) {
        return s;
    }
    if (contains(collectIds(document.effect()), asset_.id)) {
        return makeError("The asset could not be added.", "its ID is already in use");
    }
    edit(document).assets.push_back(asset_);
    notify(document, Change{Change::Kind::AssetAdded, {}, {}, asset_.id, {}});
    return {};
}

void AddAssetCommand::revert(Document& document) {
    auto& effect = edit(document);
    const int i = assetIndex(effect, asset_.id);
    if (i >= 0) {
        effect.assets.erase(effect.assets.begin() + i);
        notify(document, Change{Change::Kind::AssetRemoved, {}, {}, asset_.id, {}});
    }
}

// ----------------------------------------------------- RemoveAssetCommand

RemoveAssetCommand::RemoveAssetCommand(Id asset) : id_(asset) {}

std::string RemoveAssetCommand::name() const { return "Remove Asset"; }

Status RemoveAssetCommand::apply(Document& document) {
    auto& effect = edit(document);
    const int i = assetIndex(effect, id_);
    if (i < 0) {
        return makeError("That asset no longer exists.");
    }
    if (isAssetReferenced(effect, id_)) {
        return makeError("This asset is still in use by a layer.");
    }
    index_ = i;
    removed_ = std::move(effect.assets[static_cast<std::size_t>(i)]);
    effect.assets.erase(effect.assets.begin() + i);
    notify(document, Change{Change::Kind::AssetRemoved, {}, {}, id_, {}});
    return {};
}

void RemoveAssetCommand::revert(Document& document) {
    auto& assets = edit(document).assets;
    const int at = std::min(index_, static_cast<int>(assets.size()));
    assets.insert(assets.begin() + at, removed_);
    notify(document, Change{Change::Kind::AssetAdded, {}, {}, id_, {}});
}

// ------------------------------------------------------- CompositeCommand

CompositeCommand::CompositeCommand(std::string name, std::vector<CommandPtr> children)
    : name_(std::move(name)), children_(std::move(children)) {}

Status CompositeCommand::apply(Document& document) {
    for (std::size_t i = 0; i < children_.size(); ++i) {
        if (auto s = children_[i]->apply(document); !s) {
            while (i > 0) {
                children_[--i]->revert(document);
            }
            return s;
        }
    }
    return {};
}

void CompositeCommand::revert(Document& document) {
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        (*it)->revert(document);
    }
}

bool CompositeCommand::canMerge(const Command& later) const {
    const auto* other = dynamic_cast<const CompositeCommand*>(&later);
    if (!other || other->children_.size() != children_.size() || children_.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < children_.size(); ++i) {
        if (!children_[i]->canMerge(*other->children_[i])) {
            return false;
        }
    }
    return true;
}

void CompositeCommand::merge(const Command& later) {
    const auto& other = static_cast<const CompositeCommand&>(later);
    for (std::size_t i = 0; i < children_.size(); ++i) {
        children_[i]->merge(*other.children_[i]);
    }
}

bool CompositeCommand::isNoOp() const {
    for (const auto& child : children_) {
        if (!child->isNoOp()) {
            return false;
        }
    }
    return true;
}

}  // namespace vfx
