// VFX Forge editor: the artist's own effects, kept beside the built-in
// library.
//
// An effect saved here is an ordinary effect file in a folder of its own,
// with the pictures and sounds it uses next to it, so it keeps working
// wherever the artist's other files are. The library shows these with the
// built-in presets, and they open or are added to an effect the same way.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "vfx/Effect.h"
#include "vfx/Result.h"
#include "vfx/Templates.h"

namespace vfx::editor {

// What the library shows on an effect's card.
struct UserEffectInfo {
    std::string id;     // the folder's name: stable, lower-case
    std::string name;   // shown to artists
    std::string space;  // "2d" or "3d"
    double duration = 2.0;
    int layers = 0;

    // The flat camera that frames the card's picture, and the moment at
    // which it looks like itself.
    float viewX = 0, viewY = 0, viewHeight = 6;
    double previewTime = 0.5;
};

// Where an effect is kept, inside the library folder.
std::filesystem::path userEffectFile(const std::filesystem::path& root, const std::string& id);
std::filesystem::path userEffectFolder(const std::filesystem::path& root, const std::string& id);

// Saves the effect into the library under its own name; an effect with the
// same name is replaced. projectFolder is where its pictures and sounds are
// kept now. Returns the entry's ID.
struct SavedUserEffect {
    std::string id;
    bool replaced = false;
};
Result<SavedUserEffect> saveUserEffect(const std::filesystem::path& root, const Effect& effect,
                                       const std::filesystem::path& projectFolder, float viewX, float viewY,
                                       float viewHeight, double previewTime);

// One entry's card.
Result<UserEffectInfo> userEffectInfo(const std::filesystem::path& root, const std::string& id);

// Every saved effect, by name. Entries that cannot be read are left out.
std::vector<UserEffectInfo> listUserEffects(const std::filesystem::path& root);

// Reads one back. Pictures and sounds are in userEffectFolder().
Result<Effect> loadUserEffect(const std::filesystem::path& root, const std::string& id);

// Takes an effect out of the library, with its files.
Status removeUserEffect(const std::filesystem::path& root, const std::string& id);

// A copy of the effect in which every layer, module, control and asset has
// a new ID from newId, with the references between them kept. Used to add a
// saved effect to another without clashing.
Effect withFreshIds(Effect effect, const IdSource& newId);

// A name made into a folder name: lower-case letters, digits and dashes.
std::string userEffectSlug(const std::string& name);

}  // namespace vfx::editor
