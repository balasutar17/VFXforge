// VFX Forge editor: exporting an effect to Unreal Engine.
//
// Unreal gets the effect as VFX Forge draws it: every frame, rendered by the
// same picture renderer as the window, packed into one flipbook picture,
// with a description of how to play it. A Python script that travels with it
// (unreal/VFXForge_Import.py, run in the Unreal editor) turns that into a
// texture, Paper2D sprites and flipbook, a material and a Blueprint actor
// that plays the effect. The look is VFX Forge's exactly; the layers are not
// separate in Unreal.
//
// What is written, inside the chosen folder:
//   VFXForge for Unreal/VFXForge_Import.py
//   VFXForge for Unreal/README.txt
//   VFXForge for Unreal/Effects/<name>/<name>_flipbook.png
//   VFXForge for Unreal/Effects/<name>/<name>.json
// Exporting more effects to the same folder adds them beside the others.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "vfx/Effect.h"
#include "vfx/Result.h"
#include "vfx/editor/SpriteMesh.h"

namespace vfx::editor {

struct UnrealFile {
    std::string path;  // relative to the export folder
    std::string text;
};

// The script and README, built into the program from unreal/.
const std::vector<UnrealFile>& unrealHelperSources();

struct UnrealOptions {
    int frameSize = 512;               // pixels along a frame's longer side (made smaller to fit)
    View view;                         // what the frames show; width and height are ignored
    const ImageSet* images = nullptr;  // the effect's own pictures, if it uses any
};

struct UnrealWritten {
    std::filesystem::path folder;  // "VFXForge for Unreal"
    std::filesystem::path effect;  // its Effects/<name> folder
    int frames = 0, columns = 0, rows = 0;
    int frameWidth = 0, frameHeight = 0, frameSize = 0;  // frameSize: the longer side
};

// The largest side of the flipbook picture.
inline constexpr int kUnrealSheetSide = 4096;

Result<UnrealWritten> exportToUnreal(const Effect& effect, const UnrealOptions& options,
                                     const std::filesystem::path& folder);

}  // namespace vfx::editor
