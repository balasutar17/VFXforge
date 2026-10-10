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
    // Gloss: toward 1 the colour turns white, at the particle's own
    // brightness. The white shine on candy, the white-hot core of a flare.
    float shine = 0;
};

// (x, y) runs from -1 to 1 across the particle with y pointing up. For a
// particle that follows its movement, +x is the way it is going. (aaX, aaY)
// is the size of one screen pixel in those units, used to soften hard edges
// by exactly one pixel.
ShapeSample sampleShape(SpriteShape shape, float x, float y, float aaX, float aaY);

// A trail ribbon at (along, across), each 0 to 1: solid in the middle,
// soft at the edges, white-hot along its centre near the particle.
inline ShapeSample ribbonSample(float along, float across) {
    const float c = across * 2.0f - 1.0f;
    const float d = c < 0.0f ? -c : c;
    ShapeSample s;
    const float body = (1.0f - d) * 2.2f;
    s.cover = body < 0.0f ? 0.0f : (body > 1.0f ? 1.0f : body);
    const float core = 1.0f - d * 2.5f;
    const float head = 1.0f - along;
    s.shine = (core > 0.0f ? core : 0.0f) * head * head * head;
    return s;
}

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

// Applies gloss to a premultiplied colour. alpha is the particle's own
// alpha: 0 for additive particles, which shine at their brightest channel.
inline void applyShine(float& r, float& g, float& b, float alpha, float shine) {
    if (shine > 0.0f) {
        float white = r > g ? (r > b ? r : b) : (g > b ? g : b);
        white = white > alpha ? white : alpha;
        r += (white - r) * shine;
        g += (white - g) * shine;
        b += (white - b) * shine;
    }
}

const char* shapeName(SpriteShape shape);

}  // namespace vfx::editor
