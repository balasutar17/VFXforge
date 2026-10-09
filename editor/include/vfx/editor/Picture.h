// VFX Forge editor: drawing a frame into a picture without a graphics card.
//
// This is the reference renderer. It draws exactly the triangles the app
// sends to the graphics card, with the same shape formulas and the same
// blending, into plain memory. It is slow and simple on purpose. It exists
// so effects can be looked at and tested anywhere, and it is the basis for
// exporting image sequences later.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "vfx/Result.h"
#include "vfx/editor/SpriteMesh.h"

namespace vfx::editor {

struct Picture {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;  // four bytes a pixel, top row first

    const std::uint8_t* pixel(int x, int y) const {
        return &rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                      static_cast<std::size_t>(x)) * 4u];
    }
};

// A screen colour, 0 to 1, as a colour picker shows it.
struct ScreenColor {
    float r = 0.043f, g = 0.047f, b = 0.059f;  // the app's viewport background
};

// Draws a mesh over a plain background. The mesh's coordinates are pixels.
// Pictures for layers that draw the artist's own image come from images.
Picture drawPicture(const SpriteMesh& mesh, int width, int height, ScreenColor background = {},
                    const ImageSet* images = nullptr);

// Draws a mesh over nothing: where no particle is, the picture is fully
// see-through. Colours are stored straight (not premultiplied), as PNG
// expects. Light added by glowing particles counts toward coverage by its
// brightness, so it still shows when the picture is laid over a scene.
Picture drawPictureClear(const SpriteMesh& mesh, int width, int height,
                         const ImageSet* images = nullptr);

// Copies one picture into another at (left, top), clipped to fit.
void paste(Picture& onto, const Picture& piece, int left, int top);

// PNG encoding. The file is valid but not compressed.
std::string encodePng(const Picture& picture);
Status writePng(const std::filesystem::path& path, const Picture& picture);

}  // namespace vfx::editor
