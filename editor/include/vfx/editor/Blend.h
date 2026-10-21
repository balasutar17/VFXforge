// VFX Forge editor: laying a particle's colour over a pixel in each of
// Photoshop's blend modes.
//
// The picture renderer (and so exported frames and pictures) uses this for
// every mode. The live preview lets the graphics card do the five that need
// only the particle and the pixel under it (see blendByCard), and these
// formulas are written to give the same pixels as the card does for those.
#pragma once

#include "vfx/Program.h"

namespace vfx::editor {

// Lays a colour over a pixel. (r, g, b) is the particle's colour at this
// pixel, already multiplied by its alpha (screen colours, so brighter than
// 1 means glowing), a is its alpha (0 for light that is only added), and
// pixel points at the pixel's red, green and blue, also premultiplied.
//
// noise is a number from 0 to 1 that differs from pixel to pixel: Dissolve
// shows the particle where it is below its alpha, and not where above, so
// a faint particle breaks up into dots instead of fading.
//
// Normal and Linear Dodge (Add) are source + pixel * (1 - alpha). The
// other modes do their sums with the particle's own colour (straight, not
// multiplied by alpha) and the pixel, and mix the answer in by alpha, which
// is how a layer's opacity works in Photoshop.
void blendOver(BlendMode mode, float r, float g, float b, float a, float* pixel, float noise);

// A number from 0 to 1 for a pixel, different from its neighbours'.
float blendNoise(int x, int y);

}  // namespace vfx::editor
