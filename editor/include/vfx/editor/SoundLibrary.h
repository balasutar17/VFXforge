// VFX Forge editor: the built-in sound library.
//
// Every library sound is made by code, from sine waves, noise, filters and
// envelopes: original sounds, free of anyone else's rights, the same on every
// machine, and costing nothing to ship. An effect that uses one refers to it
// as "sounds/lib-<id>.wav"; the file is written into the project folder when
// it is first needed, and made again if it is ever deleted.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "vfx/Result.h"
#include "vfx/editor/Audio.h"

namespace vfx::editor {

struct LibrarySound {
    std::string id;
    std::string name;
    std::string category;
    std::string description;
    bool loops = false;  // made to repeat seamlessly
};

const std::vector<LibrarySound>& librarySounds();
const LibrarySound* findLibrarySound(std::string_view id);

// Makes the sound. Fails for an unknown ID.
Result<Sound> makeLibrarySound(std::string_view id, int sampleRate = 48000);

// "sounds/lib-<id>.wav", and the other way round ("" when it is not one).
std::string librarySoundPath(std::string_view id);
std::string librarySoundId(std::string_view assetPath);

}  // namespace vfx::editor
