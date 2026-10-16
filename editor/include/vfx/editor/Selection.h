// VFX Forge editor: what is selected, and what a change to it reaches.
//
// A selection is any mix of single layers and groups. Groups nest: a layer's
// group is a path, "Fire Explosion/Sparks" being the group Sparks inside the
// group Fire Explosion, and selecting a group selects everything inside it,
// at any depth.
//
// The handles (the gizmo) only ever change the resolved selection: the
// selected layers and the layers of the selected groups, each once however
// many ways it was selected (a group and one of its own layers do not move
// that layer twice), without the locked ones. Nothing else in the effect is
// touched.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "vfx/Effect.h"
#include "vfx/Value.h"

namespace vfx::editor {

struct Selection {
    std::vector<Id> layers;           // selected one by one
    std::vector<std::string> groups;  // selected whole, by path
    Id active;                        // the one last clicked: its settings are shown

    bool empty() const { return layers.empty() && groups.empty(); }
};

// What Turn and Size happen about.
enum class PivotMode {
    Center,      // the middle of everything selected
    Active,      // the active layer's Position
    Individual,  // each layer about its own Position
};

struct ResolvedSelection {
    std::vector<Id> targets;  // in the effect's order, each once
    std::vector<Id> locked;   // selected but locked: left alone
    bool empty() const { return targets.empty(); }
};

ResolvedSelection resolveSelection(const Effect& effect, const Selection& selection);

// Group paths.
// Whether the layer is in the group, directly or inside a group within it.
bool inGroup(const Layer& layer, std::string_view path);
// "A/B/C" -> "A/B"; "A" -> "".
std::string parentGroup(std::string_view path);
// "A/B/C" -> "C".
std::string groupName(std::string_view path);
// How deep: "A" is 1, "A/B" is 2.
int groupDepth(std::string_view path);
// Every group in the effect, parents before their children, in the order
// their first layers come.
std::vector<std::string> groupPaths(const Effect& effect);
// A name made safe to be one step of a path.
std::string cleanGroupName(std::string_view name);

}  // namespace vfx::editor
