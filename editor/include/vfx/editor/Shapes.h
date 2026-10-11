// VFX Forge editor: the built-in particle pictures.
//
// Each shape is a small formula, not an image, so it stays crisp at any size.
// The same formulas are written twice: here, for the picture renderer and
// the tests, and in app/shaders/sprite.frag for the graphics card. If you
// change one, change the other.
#pragma once

#include <cmath>

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

// Dissolve: a particle eaten away in ragged patches as it dies, the way a
// burning leaf or a wisp of smoke goes, where a plain fade thins it evenly
// all over. Written three times with the same sums: here, and in
// app/shaders/sprite.frag and image.frag. If you change one, change them all.
//
// A number from 0 to 1 that depends only on which square of a grid a point
// is in. Plain arithmetic, so every graphics card gets the same answer.
inline float dissolveSpeck(float cx, float cy) {
    const auto part = [](float v) { return v - std::floor(v); };
    float qx = part(cx * 0.1031f), qy = part(cy * 0.1031f), qz = qx;
    const float d = qx * (qy + 33.33f) + qy * (qz + 33.33f) + qz * (qx + 33.33f);
    qx += d;
    qy += d;
    qz += d;
    return part((qx + qy) * qz);
}

// Soft lumps about one grid square across.
inline float dissolveLumps(float x, float y) {
    const float ix = std::floor(x), iy = std::floor(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    const float a = dissolveSpeck(ix, iy), b = dissolveSpeck(ix + 1.0f, iy);
    const float c = dissolveSpeck(ix, iy + 1.0f), d = dissolveSpeck(ix + 1.0f, iy + 1.0f);
    const float low = a + (b - a) * fx, high = c + (d - c) * fx;
    return low + (high - low) * fy;
}

// When each point of a particle goes, from 0 (first) to 1 (last). (x, y) is
// the particle's own -1..1 square; seed is the particle's own random number,
// so no two are eaten the same way. Three sizes of lump, each turned against
// the others so no grid shows; the outside tends to go a little before the
// middle; and the result is spread out so that about as much goes in each
// tenth of the dissolve as in any other.
inline float dissolvePattern(float x, float y, float seed) {
    const float qx = x * 1.7f + seed * 37.0f, qy = y * 1.7f + seed * 91.0f;
    const float rx = (0.8f * qx - 0.6f * qy) * 2.1f + 5.3f, ry = (0.6f * qx + 0.8f * qy) * 2.1f + 1.7f;
    const float sx = (0.28f * qx + 0.96f * qy) * 4.3f + 9.1f, sy = (-0.96f * qx + 0.28f * qy) * 4.3f + 3.3f;
    float n = 0.5f * dissolveLumps(qx, qy) + 0.3f * dissolveLumps(rx, ry) + 0.2f * dissolveLumps(sx, sy);
    float r = std::sqrt(x * x + y * y);
    r = r > 1.0f ? 1.0f : r;
    n += 0.1f * (0.5f - r);
    n = (n - 0.2033f) / (0.7556f - 0.2033f);
    n = n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    return n * n * (3.0f - 2.0f * n);
}

// What is left of a point when the particle is `amount` of the way through
// dissolving (0 whole, 1 gone), and how brightly it glows for being near the
// eaten edge. soft and edge are the layer's Dissolve Softness and Dissolve
// Edge Glow, each 0 to 1.
struct Dissolved {
    float keep = 1;
    float rim = 0;
};
inline constexpr float kDissolveBand = 0.2f;  // how far in from the edge the glow reaches
inline Dissolved dissolveAt(float x, float y, float amount, float seed, float soft, float edge) {
    Dissolved out;
    if (!(amount > 0.0f)) {
        return out;
    }
    const float width = 0.03f + 0.6f * soft;
    const float n = dissolvePattern(x, y, seed);
    const float past = n - (amount * (1.0f + width) - width);
    float keep = past / width;
    keep = keep < 0.0f ? 0.0f : (keep > 1.0f ? 1.0f : keep);
    out.keep = keep;
    if (edge > 0.0f) {
        float inner = (past - width) / kDissolveBand;
        inner = inner < 0.0f ? 0.0f : (inner > 1.0f ? 1.0f : inner);
        const float start = amount * 6.0f;  // no glow before anything is eaten
        out.rim = edge * (1.0f - inner) * (1.0f - inner) * (start > 1.0f ? 1.0f : start);
    }
    return out;
}

const char* shapeName(SpriteShape shape);

}  // namespace vfx::editor
