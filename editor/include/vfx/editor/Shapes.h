// VFX Forge editor: the built-in particle pictures.
//
// Each shape is a small formula, not an image, so it stays crisp at any size.
// The same formulas are written twice: here, for the picture renderer and
// the tests, and in app/shaders/sprite.frag for the graphics card. If you
// change one, change the other.
#pragma once

#include "vfx/Program.h"

namespace vfx::editor {

// What a shape looks like at one point on the particle.
struct ShapeSample {
    // How much of the particle's colour shows here, from 0 to 1.
    float cover = 0;
    // Toon shading: 0 is the particle's own colour, toward +1 a lighter
    // tone of it (a highlight), toward -1 a darker one (a shadow).
    float tone = 0;
};

// (x, y) runs from -1 to 1 across the particle with y pointing up. For a
// particle that follows its movement, +x is the way it is going. (aaX, aaY)
// is the size of one screen pixel in those units, used to soften hard edges
// by exactly one pixel.
ShapeSample sampleShape(SpriteShape shape, float x, float y, float aaX, float aaY);

// Just the coverage part.
float shapeCoverage(SpriteShape shape, float x, float y, float aaX, float aaY);

// Applies a tone to a colour that is ready for the screen (premultiplied).
// A highlight moves the colour toward white at the particle's own
// brightness and lifts it a little; a shadow dims it.
inline void applyTone(float& r, float& g, float& b, float tone) {
    if (tone > 0.0f) {
        const float most = r > g ? (r > b ? r : b) : (g > b ? g : b);
        const float lift = 1.0f + 0.3f * tone;
        r = (r + (most - r) * 0.5f * tone) * lift;
        g = (g + (most - g) * 0.5f * tone) * lift;
        b = (b + (most - b) * 0.5f * tone) * lift;
    } else if (tone < 0.0f) {
        const float dim = 1.0f + 0.5f * tone;
        r *= dim;
        g *= dim;
        b *= dim;
    }
}

const char* shapeName(SpriteShape shape);

}  // namespace vfx::editor
