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

// A hard-edged flame leaning to one side: negative inside, positive outside.
// Not a true distance, but close enough near the edge to soften it.
float blaze(float x, float y, float bottom, float top, float girth) {
    const float t = (y - bottom) / (top - bottom);
    if (t <= 0.0f || t >= 1.0f) {
        return 1.0f;
    }
    const float base = (0.3f - std::min(t, 0.3f)) / 0.3f;
    const float taper = 1.0f - std::max(t - 0.3f, 0.0f) / 0.7f;
    const float width = girth * std::sqrt(clamp01(1.0f - base * base)) * std::pow(taper, 0.9f);
    return (std::fabs(x - 0.16f * t * t) - width) * 0.8f;
}

// One thin ray along an axis: along runs out from the middle, across is the
// distance from the ray's centre line. Narrows and fades to its tip.
float ray(float along, float across, float length, float width) {
    if (along >= length) {
        return 0.0f;
    }
    const float left = 1.0f - along / length;
    return clamp01(1.0f - across / (width * left + 1e-4f)) * std::sqrt(left);
}

// Distance to the jagged line of a lightning bolt running along x, with one
// fork. Thinner toward its ends.
float boltDistance(float x, float y, float& thin) {
    static const float px[16] = {-0.95f, -0.8233f, -0.6967f, -0.57f, -0.4433f, -0.3167f, -0.19f, -0.0633f, 0.0633f, 0.19f, 0.3167f, 0.4433f, 0.57f, 0.6967f, 0.8233f, 0.95f};
    static const float py[16] = {0.0f, 0.08f, -0.05f, 0.12f, 0.02f, -0.1f, 0.06f, -0.02f, 0.14f, -0.06f, 0.04f, -0.12f, 0.03f, 0.09f, -0.04f, 0.0f};
    float best = 1e9f;
    auto segment = [&](float ax, float ay, float bx, float by) {
        const float ex = bx - ax, ey = by - ay;
        const float h = clamp01(((x - ax) * ex + (y - ay) * ey) / (ex * ex + ey * ey));
        best = std::min(best, std::sqrt(sq(x - ax - ex * h) + sq(y - ay - ey * h)));
    };
    for (int i = 0; i < 15; ++i) {
        segment(px[i], py[i], px[i + 1], py[i + 1]);
    }
    segment(-0.19f, 0.06f, -0.06f, -0.30f);  // the fork
    segment(-0.06f, -0.30f, 0.10f, -0.36f);
    segment(0.10f, -0.36f, 0.24f, -0.52f);
    thin = 1.0f - std::pow(std::fabs(x), 6.0f);
    return best;
}

// The hard white part of a star flash: an uneven core and nine sharp,
// tapering spikes of different lengths, as a signed distance.
float starflashDistance(float x, float y) {
    static const float angle[9] = {0.20f, 0.90f, 1.50f, 2.30f, 2.90f, 3.60f, 4.30f, 5.00f, 5.70f};
    static const float length[9] = {0.95f, 0.55f, 0.80f, 0.45f, 0.90f, 0.60f, 0.85f, 0.50f, 0.70f};
    static const float width[9] = {0.08f, 0.06f, 0.07f, 0.05f, 0.08f, 0.06f, 0.07f, 0.05f, 0.06f};
    const float r = std::sqrt(x * x + y * y);
    float d = r - (0.20f + 0.04f * std::sin(5.0f * std::atan2(y, x)));
    for (int k = 0; k < 9; ++k) {
        const float c = std::cos(angle[k]), s = std::sin(angle[k]);
        const float along = x * c + y * s;
        const float across = std::fabs(-x * s + y * c);
        if (along > 0.0f && along < length[k]) {
            d = std::min(d, (across - width[k] * (1.0f - along / length[k])) * 0.9f);
        }
    }
    return d;
}

// A rounded square (a "squircle"): the outline of a gumdrop or jelly candy.
// Close to a signed distance near the edge.
float squircle(float x, float y, float radius) {
    const float n = 2.6f;
    return std::pow(std::pow(std::fabs(x), n) + std::pow(std::fabs(y), n), 1.0f / n) - radius;
}

// Signed distance to a convex four-cornered fragment, corners in
// counter-clockwise order (the largest distance to any side's line).
float shardShape(float x, float y) {
    const float px[4] = {-0.85f, 0.10f, 0.90f, -0.20f};
    const float py[4] = {-0.35f, -0.90f, 0.15f, 0.85f};
    float d = -1e9f;
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        const float ex = px[j] - px[i], ey = py[j] - py[i];
        const float length = std::sqrt(ex * ex + ey * ey);
        d = std::max(d, ((x - px[i]) * ey - (y - py[i]) * ex) / length);
    }
    return d;
}

// A flying drop with its round head at +x and a tail to -x.
float dropShape(float x, float y) {
    const float head = std::sqrt(sq(x - 0.35f) + sq(y)) - 0.55f;
    if (x >= 0.35f || x <= -0.95f) {
        return head;
    }
    const float width = 0.55f * std::pow((x + 0.95f) / 1.3f, 0.9f);
    return std::min(head, (std::fabs(y) - width) * 0.85f);
}

// A splat of jelly: a round body, lobes around it and a few flung drops.
float splatShape(float x, float y) {
    float d = std::sqrt(sq(x) + sq(y)) - 0.52f;
    static const float lobes[6][3] = {{0.56f, 0.18f, 0.22f},   {0.10f, 0.60f, 0.20f},
                                      {-0.50f, 0.34f, 0.19f},  {-0.58f, -0.22f, 0.21f},
                                      {-0.06f, -0.60f, 0.18f}, {0.48f, -0.40f, 0.20f}};
    for (const auto& l : lobes) {
        d = std::min(d, std::sqrt(sq(x - l[0]) + sq(y - l[1])) - l[2]);
    }
    static const float drops[3][3] = {{0.86f, 0.52f, 0.09f}, {-0.84f, 0.66f, 0.07f}, {0.30f, -0.90f, 0.08f}};
    for (const auto& l : drops) {
        d = std::min(d, std::sqrt(sq(x - l[0]) + sq(y - l[1])) - l[2]);
    }
    return d;
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
            out.shine = 0.8f * out.tone;
            break;

        case SpriteShape::Streak: {
            // A comet: a round bright head at +x, thinning and fading behind.
            const float t = clamp01((x + 1.0f) * 0.5f);
            const float nose = std::max(t - 0.75f, 0.0f) / 0.25f;
            const float width = 0.85f * std::pow(t, 0.7f) * std::sqrt(clamp01(1.0f - nose * nose));
            if (width > 1e-4f) {
                out.cover = sq(clamp01(1.0f - sq(y / width))) * std::pow(t, 1.2f);
            }
            // The head burns white, the tail keeps the particle's colour.
            out.shine = sq(clamp01((t - 0.6f) / 0.35f)) * out.cover;
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
            out.shine = 0.8f * shine;
            break;
        }

        case SpriteShape::Glint: {
            // A flat four-pointed star with curved sides.
            const float s = std::sqrt(std::fabs(x)) + std::sqrt(std::fabs(y));
            out.cover = edge((s - 0.95f) * (0.5f * r + 0.12f), pixel);
            out.tone = edge((s - 0.6f) * (0.5f * r + 0.12f), pixel);
            break;
        }

        case SpriteShape::Blaze:
            // A toon flame with a lighter flame inside it.
            out.cover = edge(blaze(x, y, -0.92f, 0.95f, 0.62f), pixel);
            out.tone = edge(blaze(x + 0.02f, y, -0.74f, 0.42f, 0.36f), pixel);
            break;

        case SpriteShape::Candy: {
            // A glossy gumdrop: a long white shine at the upper left, a
            // small one beside it, and a shadow along the lower right.
            out.cover = edge(squircle(x, y, 0.86f), pixel);
            const float sx = (x + 0.30f) * 0.866f + (y - 0.40f) * 0.5f;
            const float sy = -(x + 0.30f) * 0.5f + (y - 0.40f) * 0.866f;
            const float longShine = edge((std::sqrt(sq(sx / 0.30f) + sq(sy / 0.13f)) - 1.0f) * 0.13f, pixel);
            const float spot = edge(std::sqrt(sq(x - 0.14f) + sq(y - 0.58f)) - 0.07f, pixel);
            const float shade = 1.0f - edge(squircle(x + 0.10f, y - 0.12f, 0.80f), pixel * 2.0f);
            out.tone = 0.35f * longShine - 0.55f * shade;
            out.shine = std::max(0.9f * longShine, spot);
            break;
        }

        case SpriteShape::Shard: {
            // A broken piece: two facets, one catching the light, and a
            // glint on its tip.
            out.cover = edge(shardShape(x * 1.05f, y * 1.05f) / 1.05f, pixel);
            const float side = (0.3f * (y - 0.85f) + 1.75f * (x + 0.2f)) / 1.7755f;
            const float lit = edge(side, pixel);
            out.tone = 0.6f * lit - 0.4f * (1.0f - lit);
            out.shine = edge(std::sqrt(sq(x + 0.16f) + sq(y - 0.56f)) - 0.09f, pixel);
            break;
        }

        case SpriteShape::Drop: {
            // A glossy drop flying head first (+x), shine near the front.
            out.cover = edge(dropShape(x, y), pixel);
            const float spot = edge(std::sqrt(sq(x - 0.48f) + sq(y - 0.20f)) - 0.13f, pixel);
            const float shade = 1.0f - edge(dropShape(x + 0.03f, y + 0.12f) + 0.08f, pixel * 2.0f);
            out.tone = 0.3f * spot - 0.45f * shade;
            out.shine = spot;
            break;
        }

        case SpriteShape::Splat: {
            // Jelly hitting a surface: hard edged, with a wet shine.
            out.cover = edge(splatShape(x, y), pixel);
            const float spot = edge(std::sqrt(sq((x + 0.18f) / 1.6f) + sq(y - 0.22f)) - 0.11f, pixel);
            const float rim = 1.0f - edge(r - 0.40f, pixel * 3.0f);
            out.tone = 0.3f * spot - 0.35f * rim;
            out.shine = spot;
            break;
        }

        case SpriteShape::Shockwave: {
            // A thick ring of force, bright on its inner edge, with a faint
            // haze filling the middle.
            const float ring = edge(std::fabs(r - 0.77f) - 0.15f, pixel);
            const float haze = 0.15f * std::pow(clamp01(r / 0.62f), 3.0f) * edge(r - 0.62f, pixel);
            out.cover = clamp01(ring + haze);
            const float inner = edge(std::fabs(r - 0.68f) - 0.05f, pixel);
            out.tone = ring * (inner - 0.35f * edge(0.86f - r, pixel));
            out.shine = 0.6f * ring * inner;
            break;
        }

        case SpriteShape::Twinkle: {
            // A sharp four-pointed twinkle: long thin rays, short diagonal
            // ones, and a white-hot middle.
            const float d = 0.70710678f;
            const float u = (x + y) * d, v = (x - y) * d;
            const float rays = std::max(std::max(ray(std::fabs(x), std::fabs(y), 0.95f, 0.07f),
                                                 ray(std::fabs(y), std::fabs(x), 0.95f, 0.07f)),
                                        std::max(ray(std::fabs(u), std::fabs(v), 0.45f, 0.05f),
                                                 ray(std::fabs(v), std::fabs(u), 0.45f, 0.05f)));
            const float core = sq(clamp01(1.0f - r2 / sq(0.30f)));
            out.cover = clamp01(std::max(rays, core) + 0.25f * sq(clamp01(1.0f - r2 / sq(0.55f))));
            out.shine = core;
            break;
        }

        case SpriteShape::Flare: {
            // A starburst: twelve thin rays of different lengths around a
            // white-hot core and a soft halo.
            float angle = std::atan2(y, x);
            if (angle < 0.0f) {
                angle += 6.2831853f;
            }
            const int k = static_cast<int>(std::floor(angle * 12.0f / 6.2831853f + 0.5f)) % 12;
            static const float lengths[12] = {0.95f, 0.55f, 0.80f, 0.50f, 0.92f, 0.62f,
                                              0.86f, 0.46f, 0.95f, 0.58f, 0.76f, 0.52f};
            const float off = std::fabs(angle - static_cast<float>(k) * 6.2831853f / 12.0f);
            const float across = std::min(off, 6.2831853f - off) * r;
            const float rays = r < lengths[k] ? clamp01(1.0f - across / (0.10f * (1.0f - r / lengths[k]) + 1e-4f)) *
                                                    std::pow(1.0f - r / lengths[k], 0.8f)
                                              : 0.0f;
            const float core = sq(clamp01(1.0f - r2 / sq(0.22f)));
            const float halo = 0.5f * sq(clamp01(1.0f - r2 / sq(0.55f)));
            out.cover = clamp01(std::max(rays, halo + core));
            out.shine = core;
            break;
        }

        case SpriteShape::Rays: {
            // Ten soft beams of light from the middle: the sunburst behind a
            // reward. Spin it slowly.
            float angle = std::atan2(y, x);
            if (angle < 0.0f) {
                angle += 6.2831853f;
            }
            const float a = angle * 10.0f / 6.2831853f;
            const float f = std::fabs(a - std::floor(a) - 0.5f) * 2.0f;  // 0 mid-beam, 1 between
            const float beam = sq(clamp01(1.0f - f * 1.15f));
            out.cover = r < 1.0f ? beam * std::pow(1.0f - r, 0.9f) * clamp01(r / 0.12f) +
                                       0.35f * sq(clamp01(1.0f - r2 / sq(0.3f)))
                                 : 0.0f;
            out.cover = clamp01(out.cover);
            break;
        }

        case SpriteShape::Swirl: {
            // A peppermint or lollipop: a disc with white spiral stripes and
            // a gloss spot.
            out.cover = edge(r - 0.88f, pixel);
            float angle = std::atan2(y, x);
            const float s = angle / 6.2831853f * 3.0f + r * 1.7f;
            const float stripe = edge((std::fabs(s - std::floor(s) - 0.5f) - 0.25f) * (r * 2.0f + 0.1f), pixel);
            const float spot = edge(std::sqrt(sq(x + 0.36f) + sq(y - 0.40f)) - 0.13f, pixel);
            const float shade = 1.0f - edge(std::sqrt(sq(x + 0.12f) + sq(y - 0.12f)) - 0.82f, pixel * 2.0f);
            out.tone = 0.8f * stripe - 0.45f * shade;
            out.shine = std::max(0.85f * stripe * (1.0f - 0.5f * shade), spot);
            break;
        }

        case SpriteShape::Bolt: {
            // A lightning bolt along x: a white-hot jagged core in a glow.
            float thin = 1.0f;
            const float d = boltDistance(x, y, thin);
            const float core = edge(d - 0.03f * thin, pixel);
            const float glow = 0.75f * sq(clamp01(1.0f - d / 0.2f)) * thin;
            out.cover = clamp01(std::max(core, glow));
            out.shine = core;
            break;
        }

        case SpriteShape::Starflash: {
            // A burst of light: white spikes and core, the particle's colour
            // in a soft halo around them.
            const float hard = edge(starflashDistance(x, y), pixel);
            const float halo = 0.5f * sq(clamp01(1.0f - r2 / sq(0.85f)));
            out.cover = std::max(hard, halo);
            out.shine = hard;
            break;
        }

        case SpriteShape::Sliver: {
            // A thin bright needle along x, for flying slivers of light.
            out.cover = edge((std::fabs(x) / 0.88f + std::fabs(y) / 0.09f - 1.0f) * 0.09f, pixel);
            out.shine = 0.8f * edge(std::fabs(y) - 0.02f, pixel) * clamp01(1.0f - std::fabs(x));
            break;
        }

        case SpriteShape::Shardring: {
            // A ring breaking into six jagged pieces.
            float angle = std::atan2(y, x);
            if (angle < 0.0f) {
                angle += 6.2831853f;
            }
            const float a = angle * 6.0f / 6.2831853f;
            const float seg = a - std::floor(a);
            const float inPiece = clamp01((seg - 0.08f) / 0.84f);
            const float taper = seg > 0.08f && seg < 0.92f ? std::sin(3.14159265f * inPiece) : 0.0f;
            const float jag = 0.03f * (a * 3.0f - std::floor(a * 3.0f));
            const float thick = (0.08f + jag) * taper;
            out.cover = edge(std::max(r - 0.88f, (0.88f - thick) - r), pixel);
            out.shine = 0.5f * out.cover * edge(r - 0.86f + thick * 0.5f, pixel);
            out.tone = 0.3f * out.cover;
            break;
        }

        case SpriteShape::Bean: {
            // A jelly bean: a gently curved capsule with a long shine.
            const float by = y + 0.28f * x * x - 0.06f;
            const float body = std::sqrt(sq(std::max(std::fabs(x) - 0.48f, 0.0f)) + sq(by)) - 0.38f;
            out.cover = edge(body, pixel);
            const float shineD = std::sqrt(sq(std::max(std::fabs(x + 0.06f) - 0.28f, 0.0f)) +
                                           sq((by - 0.17f) / 0.45f)) - 0.05f;
            const float spot = edge(shineD, pixel);
            const float shade = 1.0f - edge(std::sqrt(sq(std::max(std::fabs(x + 0.04f) - 0.46f, 0.0f)) +
                                                      sq(by - 0.08f)) - 0.32f, pixel * 2.0f);
            out.tone = 0.3f * spot - 0.5f * shade;
            out.shine = spot;
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
        "heart", "streak", "flame", "puff", "burst", "crescent", "orb", "glint", "blaze",
        "candy", "shard", "drop", "splat", "shockwave", "twinkle", "flare", "rays", "swirl", "bean", "bolt",
        "starflash", "sliver", "shardring"};
    const auto i = static_cast<int>(shape);
    return i >= 0 && i < kSpriteShapeCount ? names[i] : "soft";
}

}  // namespace vfx::editor
