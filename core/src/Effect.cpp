#include "vfx/Effect.h"

namespace vfx {

const Value* Module::find(std::string_view key) const {
    if (!desc) {
        return nullptr;
    }
    const int i = desc->indexOf(key);
    if (i < 0 || static_cast<std::size_t>(i) >= values.size()) {
        return nullptr;
    }
    return &values[static_cast<std::size_t>(i)];
}

Value* Module::find(std::string_view key) {
    return const_cast<Value*>(static_cast<const Module*>(this)->find(key));
}

Module makeModule(const ModuleTypeDesc& desc, Id id) {
    Module m;
    m.id = id;
    m.type = desc.type;
    m.desc = &desc;
    m.values.reserve(desc.properties.size());
    for (const auto& p : desc.properties) {
        m.values.push_back(p.defaultValue);
    }
    return m;
}

int layerIndex(const Effect& effect, Id id) {
    for (std::size_t i = 0; i < effect.layers.size(); ++i) {
        if (effect.layers[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int moduleIndex(const Layer& layer, Id id) {
    for (std::size_t i = 0; i < layer.modules.size(); ++i) {
        if (layer.modules[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int assetIndex(const Effect& effect, Id id) {
    for (std::size_t i = 0; i < effect.assets.size(); ++i) {
        if (effect.assets[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const Layer* findLayer(const Effect& effect, Id id) {
    const int i = layerIndex(effect, id);
    return i < 0 ? nullptr : &effect.layers[static_cast<std::size_t>(i)];
}

Layer* findLayer(Effect& effect, Id id) {
    const int i = layerIndex(effect, id);
    return i < 0 ? nullptr : &effect.layers[static_cast<std::size_t>(i)];
}

const Module* findModule(const Layer& layer, Id id) {
    const int i = moduleIndex(layer, id);
    return i < 0 ? nullptr : &layer.modules[static_cast<std::size_t>(i)];
}

Module* findModule(Layer& layer, Id id) {
    const int i = moduleIndex(layer, id);
    return i < 0 ? nullptr : &layer.modules[static_cast<std::size_t>(i)];
}

const Asset* findAsset(const Effect& effect, Id id) {
    const int i = assetIndex(effect, id);
    return i < 0 ? nullptr : &effect.assets[static_cast<std::size_t>(i)];
}

std::vector<Id> collectIds(const Effect& effect) {
    std::vector<Id> ids;
    ids.push_back(effect.id);
    for (const auto& a : effect.assets) {
        ids.push_back(a.id);
    }
    for (const auto& l : effect.layers) {
        ids.push_back(l.id);
        for (const auto& c : l.controls) {
            ids.push_back(c.id);
        }
        for (const auto& m : l.modules) {
            ids.push_back(m.id);
        }
    }
    return ids;
}

bool isAssetReferenced(const Effect& effect, Id asset) {
    for (const auto& l : effect.layers) {
        for (const auto& m : l.modules) {
            for (const auto& v : m.values) {
                if (const auto* ref = std::get_if<AssetRef>(&v); ref && ref->id == asset) {
                    return true;
                }
            }
        }
    }
    return false;
}

}  // namespace vfx
