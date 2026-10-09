// VFX Forge editor: exporting an effect as animation frames.
//
// Each frame is drawn by the picture renderer, so it looks exactly as it
// does in the window. The frames are written as numbered PNG files, and
// optionally also as one sprite sheet. In Unity, select the numbered frames
// and drag them into a scene: Unity makes the animation itself.
#pragma once

#include <filesystem>
#include <string>

#include "vfx/Effect.h"
#include "vfx/Result.h"
#include "vfx/editor/SpriteMesh.h"

namespace vfx::editor {

struct FrameOptions {
    int width = 512;
    int height = 512;
    double framesPerSecond = 30;
    bool transparent = true;  // see-through background; otherwise black
    bool sheet = true;        // also write every frame into one picture
    View view;                // what part of the world to show; width and height are ignored
};

struct FramesWritten {
    int frames = 0;
    std::filesystem::path folder;  // the numbered frames
    std::filesystem::path sheet;   // empty when no sheet was written
    int columns = 0, rows = 0;     // of the sheet
};

// How many frames one pass of the effect takes at a frame rate.
int frameCount(const Effect& effect, double framesPerSecond);

// Writes "<stem> frames/<stem>_0000.png" and so on, and "<stem> sheet.png",
// inside the given folder.
Result<FramesWritten> exportFrames(const Effect& effect, const FrameOptions& options,
                                   const std::filesystem::path& folder, const std::string& stem);

}  // namespace vfx::editor
