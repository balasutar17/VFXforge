#include "vfx/editor/Shapes.h"

#include <algorithm>
#include <cmath>

namespace vfx::editor {

namespace {

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
float sq(float v) { return v * v; }

// Coverage of a hard edge, given the signed distance to it (negative inside)
// and the size of a pixel.
float edge(float distance, float pixel) { return clamp01(0.5f - distance / pixel); }

// A soft round blob centred at (cx, cy).
float blob(float x, float y, float cx, float cy, float radius) {
    return sq(clamp01(1.0f - (sq(x - cx) + sq(y - cy)) / sq(radius)));
}

// Signed distance to a five-pointed star with one point up.
// After Inigo Quilez's sdStar5.
float star5(float x, float y, float radius, float inner) {
    const float k1x = 0.809016994f, k1y = -0.587785252f;
    const float k2x = -k1x, k2y = k1y;
    x = std::fabs(x);
    float d = std::max(k1x * x + k1y * y, 0.0f);
    x -= 2.0f * d * k1x;
    y -= 2.0f * d * k1y;
    d = std::max(k2x * x + k2y * y, 0.0f);
    x -= 2.0f * d * k2x;
    y -= 2.0f * d * k2y;
    x = std::fabs(x);
    y -= radius;
    const float bax = inner * -k1y, bay = inner * k1x - 1.0f;
    const float h = std::clamp((x * bax + y * bay) / (bax * bax + bay * bay), 0.0f, radius);
    const float ex = x - bax * h, ey = y - bay * h;
    const float side = y * bax - x * bay;
    return std::sqrt(ex * ex + ey * ey) * (side > 0.0f ? 1.0f : -1.0f);
}

// Signed distance to a heart whose tip is at the origin and whose top is
// near y = 1.1. After Inigo Quilez's sdHeart.
float heart(float x, float y) {
    x = std::fabs(x);
    if (x + y > 1.0f) {
        return std::sqrt(sq(x - 0.25f) + sq(y - 0.75f)) - 0.353553391f;
    }
    const float m = 0.5f * std::max(x + y, 0.0f);
    const float a = sq(x) + sq(y - 1.0f);
    const float b = sq(x - m) + sq(y - m);
    return std::sqrt(std::min(a, b)) * (x > y ? 1.0f : -1.0f);
}

// The union of round lobes that makes a toon cloud, as a signed distance.
float cloud(float x, float y) {
    float d = std::sqrt(sq(x) + sq(y + 0.05f)) - 0.56f;
    d = std::min(d, std::sqrt(sq(x + 0.44f) + sq(y + 0.12f)) - 0.42f);
    d = std::min(d, std::sqrt(sq(x - 0.46f) + sq(y + 0.16f)) - 0.40f);
    d = std::min(d, std::sqrt(sq(x + 0.20f) + sq(y - 0.36f)) - 0.44f);
    d = std::min(d, std::sqrt(sq(x - 0.30f) + sq(y - 0.30f)) - 0.42f);
    return d;
}

// How far out the edge of a spiky burst is in a given direction. Ten
// spikes, every other one shorter.
float spikes(float x, float y, float reach) {
    const float turn = std::atan2(y, x + 1e-6f) / 6.2831853f + 0.5f;  // 0..1 around
    const float along = turn * 10.0f;
    const float cell = std::floor(along);
    const float across = std::fabs(along - cell - 0.5f) * 2.0f;      // 0 at a tip, 1 between
    const float longer = std::fmod(cell, 2.0f) < 0.5f ? 1.0f : 0.72f;
    return reach * (0.38f + (longer - 0.38f) * std::pow(1.0f - across, 1.5f));
}

}  // namespace

ShapeSample sampleShape(SpriteShape shape, float x, float y, float aaX, float aaY) {
    const float pixel = std::max(std::max(aaX, aaY), 1e-4f);
    const float r2 = x * x + y * y;
    const float r = std::sqrt(r2);
    ShapeSample out;

    switch (shape) {
        case SpriteShape::Soft:
            out.cover = sq(clamp01(1.0f - r2));
            break;

        case SpriteShape::Disc:
            out.cover = edge(r - 0.9f, pixel);
            break;

        case SpriteShape::Ring:
            out.cover = edge(std::fabs(r - 0.85f) - 0.055f, pixel);
            break;

        case SpriteShape::Bubble: {
            const float inside = edge(r - 0.86f, pixel);
            const float rim = edge(std::fabs(r - 0.86f) - 0.05f, pixel);
            const float fill = inside * (0.10f + 0.22f * r2);
            const float shine = inside * (0.85f * blob(x, y, -0.36f, 0.40f, 0.24f) +
                                          0.35f * blob(x, y, 0.42f, -0.46f, 0.12f));
            out.cover = clamp01(std::max(rim, fill + shine));
            break;
        }

        case SpriteShape::Sparkle: {
            const float rays = clamp01(1.0f - (std::sqrt(std::fabs(x)) + std::sqrt(std::fabs(y))));
            const float core = sq(clamp01(1.0f - r2 / sq(0.42f)));
            out.cover = clamp01(2.4f * std::pow(rays, 1.5f) + 0.7f * core);
            break;
        }

        case SpriteShape::Star:
            out.cover = edge(star5(x, y, 0.82f, 0.45f) - 0.10f, pixel);
            // A smaller, lighter star inside.
            out.tone = edge(star5(x, y + 0.02f, 0.46f, 0.45f) - 0.06f, pixel);
            break;

        case SpriteShape::Smoke: {
            float clear = 1.0f;
            clear *= 1.0f - 0.6f * blob(x, y, 0.0f, 0.0f, 0.80f);
            clear *= 1.0f - 0.6f * blob(x, y, -0.30f, 0.20f, 0.56f);
            clear *= 1.0f - 0.6f * blob(x, y, 0.32f, 0.14f, 0.54f);
            clear *= 1.0f - 0.6f * blob(x, y, -0.14f, -0.30f, 0.52f);
            clear *= 1.0f - 0.6f * blob(x, y, 0.26f, -0.30f, 0.48f);
            clear *= 1.0f - 0.6f * blob(x, y, 0.04f, 0.38f, 0.50f);
            out.cover = 1.0f - clear;
            break;
        }

        case SpriteShape::Square: {
            const float qx = std::fabs(x) - 0.66f, qy = std::fabs(y) - 0.66f;
            const float outside = std::sqrt(sq(std::max(qx, 0.0f)) + sq(std::max(qy, 0.0f)));
            out.cover = edge(outside + std::min(std::max(qx, qy), 0.0f) - 0.12f, pixel);
            break;
        }

        case SpriteShape::Diamond:
            out.cover = edge((std::fabs(x) / 0.66f + std::fabs(y) / 0.92f - 1.0f) * 0.536f, pixel);
            // The upper-left facet catches the light.
            out.tone = clamp01((y - x) * 4.0f) * 0.8f *
                       edge((std::fabs(x) / 0.66f + std::fabs(y) / 0.92f - 0.72f) * 0.536f, pixel);
            break;

        case SpriteShape::Heart:
            out.cover = edge(heart(x * 0.64f, y * 0.64f + 0.55f) / 0.64f, pixel);
            // A small shine on the upper-left lobe.
            out.tone = edge(std::sqrt(sq((x + 0.42f) / 1.5f) + sq(y - 0.42f)) - 0.13f, pixel);
            break;

        case SpriteShape::Streak: {
            // A comet: a round bright head at +x, thinning and fading behind.
            const float t = clamp01((x + 1.0f) * 0.5f);
            const float nose = std::max(t - 0.75f, 0.0f) / 0.25f;
            const float width = 0.85f * std::pow(t, 0.7f) * std::sqrt(clamp01(1.0f - nose * nose));
            if (width > 1e-4f) {
                out.cover = sq(clamp01(1.0f - sq(y / width))) * std::pow(t, 1.2f);
            }
            break;
        }

        case SpriteShape::Flame: {
            // A teardrop: round at the bottom, pointed and dimmer at the top.
            const float t = clamp01((y + 1.0f) * 0.5f);
            const float base = (0.3f - std::min(t, 0.3f)) / 0.3f;
            const float taper = 1.0f - std::max(t - 0.3f, 0.0f) / 0.7f;
            const float width = 0.8f * std::sqrt(clamp01(1.0f - base * base)) * std::pow(taper, 0.8f);
            if (width > 1e-4f) {
                out.cover = sq(clamp01(1.0f - sq(x / width))) * (1.0f - 0.55f * t);
            }
            break;
        }

        case SpriteShape::Puff: {
            // A toon cloud: hard-edged lobes, lit from the upper left, with
            // a shadow along the lower right.
            const float d = cloud(x * 1.08f, y * 1.08f) / 1.08f;
            out.cover = edge(d, pixel);
            const float lit = edge(cloud((x + 0.13f) * 1.5f, (y - 0.15f) * 1.5f) / 1.5f, pixel);
            const float shade = 1.0f - edge(cloud((x + 0.07f) * 1.14f, (y - 0.08f) * 1.14f) / 1.14f, pixel);
            out.tone = lit - 0.6f * shade;
            break;
        }

        case SpriteShape::Burst: {
            // A spiky flash with a lighter spiky core.
            out.cover = edge(r - spikes(x, y, 0.95f), pixel);
            out.tone = edge(r - spikes(x, y, 0.52f), pixel);
            break;
        }

        case SpriteShape::Crescent: {
            // A slash: a disc with a bite taken from behind, so the thick
            // edge leads at +x. The leading edge is lighter.
            const float bite = std::sqrt(sq(x + 0.42f) + sq(y)) - 0.98f;
            out.cover = edge(std::max(r - 0.92f, -bite), pixel);
            out.tone = edge(std::max(r - 0.92f, -(std::sqrt(sq(x + 0.16f) + sq(y)) - 0.98f)), pixel);
            break;
        }

        case SpriteShape::Orb: {
            // A toon ball: a shine at the upper left and a shadow crescent
            // at the lower right.
            out.cover = edge(r - 0.9f, pixel);
            const float shine = edge(std::sqrt(sq(x + 0.34f) + sq(y - 0.36f)) - 0.24f, pixel);
            const float shade = 1.0f - edge(std::sqrt(sq(x + 0.16f) + sq(y - 0.16f)) - 0.84f, pixel);
            out.tone = shine - 0.7f * shade;
            break;
        }

        case SpriteShape::Glint: {
            // A flat four-pointed star with curved sides.
            const float s = std::sqrt(std::fabs(x)) + std::sqrt(std::fabs(y));
            out.cover = edge((s - 0.95f) * (0.5f * r + 0.12f), pixel);
            out.tone = edge((s - 0.6f) * (0.5f * r + 0.12f), pixel);
            break;
        }
    }
    return out;
}

float shapeCoverage(SpriteShape shape, float x, float y, float aaX, float aaY) {
    return sampleShape(shape, x, y, aaX, aaY).cover;
}

const char* shapeName(SpriteShape shape) {
    static const char* names[kSpriteShapeCount] = {
        "soft", "disc", "ring", "bubble", "sparkle", "star", "smoke", "square", "diamond",
        "heart", "streak", "flame", "puff", "burst", "crescent", "orb", "glint"};
    const auto i = static_cast<int>(shape);
    return i >= 0 && i < kSpriteShapeCount ? names[i] : "soft";
}

}  // namespace vfx::editor
