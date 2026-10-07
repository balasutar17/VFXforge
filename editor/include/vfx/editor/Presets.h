// VFX Forge editor: the built-in effect library.
//
// Every preset is an ordinary effect made from ordinary modules, so anything
// in it can be changed after it is opened. Nothing here is special-cased by
// the simulation or the renderer.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "vfx/Effect.h"
#include "vfx/Result.h"
#include "vfx/Templates.h"

namespace vfx::editor {

struct PresetInfo {
    std::string id;        // stable, lower-case, used in code and tests
    std::string name;      // shown to artists
    std::string category;  // shown to artists
    std::string description;

    // The flat camera that frames the effect well: the world point in the
    // middle of the picture, and how many units fit top to bottom.
    float viewX = 0, viewY = 0, viewHeight = 6;

    // A moment, in seconds, at which the effect looks like itself.
    double previewTime = 0.5;
};

// Every preset, grouped by category in the order the library shows them.
const std::vector<PresetInfo>& presets();

// The categories, in display order.
std::vector<std::string> presetCategories();

const PresetInfo* findPreset(std::string_view id);

// Builds a preset as a new effect with fresh IDs.
Result<Effect> makePreset(std::string_view id, const IdSource& newId);

}  // namespace vfx::editor
