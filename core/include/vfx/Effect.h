// VFX Forge core: the authoring data model.
//
// One Effect is one .vfx file. Simple, Advanced and (later) Expert views all
// read and write this same structure; changing level never rebuilds it.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "vfx/Id.h"
#include "vfx/Metadata.h"
#include "vfx/Value.h"

namespace vfx {

// Fields this version does not understand, kept so a file written by a newer
// version survives a load and save here. Each entry is (key, JSON text).
using Extras = std::vector<std::pair<std::string, std::string>>;

struct Asset {
    Id id;
    std::string kind = "texture";
    std::string path;  // relative to the project folder, forward slashes
    std::string hash;  // "sha256:<hex>", or empty when not yet computed
    Extras extra;
};

struct Module {
    Id id;
    std::string type;

    // Null when the type is unknown to this version. Such a module is kept
    // and written back untouched, skipped by simulation, and shown as
    // unsupported.
    const ModuleTypeDesc* desc = nullptr;

    // One value per entry in desc->properties, in the same order.
    std::vector<Value> values;

    Extras extra;

    bool known() const { return desc != nullptr; }
    const Value* find(std::string_view key) const;
    Value* find(std::string_view key);
};

// Builds a module of a built-in type with every property at its default.
Module makeModule(const ModuleTypeDesc& desc, Id id);

// A Simple-mode control is a stored binding from one artist-facing name onto
// one or more module properties. It changes what is shown, never what is stored.
struct ControlTarget {
    Id module;
    std::string property;
};

struct SimpleControl {
    Id id;
    std::string label;
    std::vector<ControlTarget> targets;
    Extras extra;
};

struct Layer {
    Id id;
    std::string name = "Layer";
    bool enabled = true;
    double start = 0.0;     // seconds from the effect's start
    double duration = 2.0;  // seconds
    // What the layer is for, in one word ("glow", "sparks", "ring"...), when
    // something knows. Tools that adjust an effect as a whole use it to tell
    // the layers apart. Empty for a layer the artist built by hand.
    std::string role;
    // A locked layer is left alone by tools that adjust the whole effect.
    bool locked = false;
    std::vector<SimpleControl> controls;
    std::vector<Module> modules;
    Extras extra;
};

struct Effect {
    Id id;
    std::string name = "Untitled";
    std::string space = "2d";  // "2d" or "3d"
    std::int64_t seed = 1;
    double duration = 2.0;     // seconds
    std::string loop = "loop"; // "once" or "loop"
    double frameRate = 30.0;   // frames per second for the timeline and export
    std::vector<Asset> assets;
    std::vector<Layer> layers;
    Extras extra;
};

// Lookups. Index functions return -1 when the ID is absent.
int layerIndex(const Effect& effect, Id id);
int moduleIndex(const Layer& layer, Id id);
int assetIndex(const Effect& effect, Id id);

const Layer* findLayer(const Effect& effect, Id id);
Layer* findLayer(Effect& effect, Id id);
const Module* findModule(const Layer& layer, Id id);
Module* findModule(Layer& layer, Id id);
const Asset* findAsset(const Effect& effect, Id id);

// Every ID used anywhere in the effect.
std::vector<Id> collectIds(const Effect& effect);

// True when any property in the effect refers to this asset.
bool isAssetReferenced(const Effect& effect, Id asset);

}  // namespace vfx
