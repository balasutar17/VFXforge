#include "vfx/editor/ShapeLibrary.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <cstdint>
#include <mutex>
#include <tuple>
#include <utility>

#include "vfx/Metadata.h"

// The library shapes. Each is one small function from a point on the
// particle to what is there: how much of the particle's colour (cover), a
// lighter or darker tone of it, and how much white shine. They are built
// from a kit of distances to simple figures, which is the first half of
// this file. (x, y) runs from -1 to 1 with y up; px is the size of a pixel
// in those units, used to soften a hard edge by exactly one pixel.
//
// The house style is flat colour lit from the upper left: a lighter band or
// spot toward the upper left, a darker rim along the lower right.

namespace vfx::editor {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kTau = 6.2831853f;

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
float sq(float v) { return v * v; }
float mix(float a, float b, float t) { return a + (b - a) * t; }
float len(float x, float y) { return std::sqrt(x * x + y * y); }
float edge(float distance, float px) { return clamp01(0.5f - distance / px); }
// From 0 at `from` to 1 at `to`, eased at both ends.
float smooth(float from, float to, float v) {
    const float t = clamp01((v - from) / (to - from));
    return t * t * (3.0f - 2.0f * t);
}

struct P {
    float x, y;
};

// The point turned clockwise by an angle, which is the same as the figure
// drawn at it being turned counter-clockwise.
P turned(float x, float y, float degrees) {
    const float a = degrees * kPi / 180.0f;
    const float c = std::cos(a), s = std::sin(a);
    return P{x * c + y * s, -x * s + y * c};
}

// The angle of a point in degrees, 0 to 360, counter-clockwise from +x.
float angleOf(float x, float y) {
    float a = std::atan2(y, x) * 180.0f / kPi;
    return a < 0.0f ? a + 360.0f : a;
}

// The plane folded into `count` equal sectors: the point comes back as it
// would be in the sector centred on `toward` degrees, measured from that
// sector's own middle line (which becomes +x). With `mirror` the two halves
// of the sector are folded together too (y comes back 0 or above).
P fold(float x, float y, int count, float toward, bool mirror, int* which = nullptr) {
    const float sector = 360.0f / static_cast<float>(count);
    float a = angleOf(x, y) - toward + sector * 0.5f;
    a -= 360.0f * std::floor(a / 360.0f);
    const float k = std::floor(a / sector);
    if (which) {
        *which = static_cast<int>(k) % count;
    }
    float within = (a - k * sector - sector * 0.5f) * kPi / 180.0f;
    if (mirror) {
        within = std::fabs(within);
    }
    const float r = len(x, y);
    return P{r * std::cos(within), r * std::sin(within)};
}

// ------------------------------------------------- distances to figures
// Negative inside, positive outside, in the particle's own units.

float sdBox(float x, float y, float hx, float hy) {
    const float qx = std::fabs(x) - hx, qy = std::fabs(y) - hy;
    return len(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f);
}

// A box with rounded corners; hx and hy are its whole half-size.
float sdRound(float x, float y, float hx, float hy, float r) { return sdBox(x, y, hx - r, hy - r) - r; }

// How far from a line between two points (never negative).
float sdSeg(float x, float y, float ax, float ay, float bx, float by) {
    const float ex = bx - ax, ey = by - ay;
    const float t = clamp01(((x - ax) * ex + (y - ay) * ey) / (ex * ex + ey * ey));
    return len(x - ax - ex * t, y - ay - ey * t);
}

// A line between two points that is `ra` thick at one end and `rb` at the other.
float sdTaper(float x, float y, float ax, float ay, float bx, float by, float ra, float rb) {
    const float ex = bx - ax, ey = by - ay;
    const float t = clamp01(((x - ax) * ex + (y - ay) * ey) / (ex * ex + ey * ey));
    return len(x - ax - ex * t, y - ay - ey * t) - mix(ra, rb, t);
}

// Any outline that does not cross itself. After Inigo Quilez's sdPolygon.
float sdPoly(float x, float y, const P* v, int n) {
    float d = sq(x - v[0].x) + sq(y - v[0].y);
    float sign = 1.0f;
    for (int i = 0, j = n - 1; i < n; j = i, ++i) {
        const float ex = v[j].x - v[i].x, ey = v[j].y - v[i].y;
        const float wx = x - v[i].x, wy = y - v[i].y;
        const float t = clamp01((wx * ex + wy * ey) / (ex * ex + ey * ey));
        d = std::min(d, sq(wx - ex * t) + sq(wy - ey * t));
        const bool c1 = y >= v[i].y, c2 = y < v[j].y, c3 = ex * wy > ey * wx;
        if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) {
            sign = -sign;
        }
    }
    return sign * std::sqrt(d);
}
template <std::size_t N>
float sdPoly(float x, float y, const P (&v)[N]) {
    return sdPoly(x, y, v, static_cast<int>(N));
}

// How far from a line through several points (never negative).
float sdLine(float x, float y, const P* v, int n) {
    float d = 1e9f;
    for (int i = 0; i + 1 < n; ++i) {
        d = std::min(d, sdSeg(x, y, v[i].x, v[i].y, v[i + 1].x, v[i + 1].y));
    }
    return d;
}
template <std::size_t N>
float sdLine(float x, float y, const P (&v)[N]) {
    return sdLine(x, y, v, static_cast<int>(N));
}

// A regular figure of `n` sides whose corners are `r` from the middle, one
// corner pointing toward `toward` degrees.
float sdNgon(float x, float y, float r, int n, float toward) {
    const float half = kPi / static_cast<float>(n);
    const P q = fold(x, y, n, toward, true);
    return q.x * std::cos(half) + q.y * std::sin(half) - r * std::cos(half);
}

// A star of `n` points `r` from the middle, with its notches at `inner`
// times that, one point toward `toward` degrees.
float sdStar(float x, float y, float r, int n, float inner, float toward) {
    const float half = kPi / static_cast<float>(n);
    const P q = fold(x, y, n, toward, true);
    const float vx = inner * r * std::cos(half), vy = inner * r * std::sin(half);
    const float d = sdSeg(q.x, q.y, r, 0.0f, vx, vy);
    const float cross = (vx - r) * q.y - vy * (q.x - r);
    return cross > 0.0f ? -d : d;
}

// An oval `a` wide and `b` tall (half-sizes). Close to a true distance near its edge.
float sdEllipse(float x, float y, float a, float b) {
    const float k0 = len(x / a, y / b);
    const float k1 = len(x / (a * a), y / (b * b));
    return k1 > 1e-6f ? k0 * (k0 - 1.0f) / k1 : -std::min(a, b);
}

// The part of a ring between two angles (degrees, counter-clockwise from
// `from` to `to`), `w0` thick at the first end and `w1` at the second, with
// round ends.
float sdArc(float x, float y, float r, float from, float to, float w0, float w1) {
    const float span = to - from;
    float a = angleOf(x, y) - from;
    a -= 360.0f * std::floor(a / 360.0f);
    if (a <= span) {
        return std::fabs(len(x, y) - r) - mix(w0, w1, a / span);
    }
    const float fa = from * kPi / 180.0f, ta = to * kPi / 180.0f;
    return std::min(len(x - r * std::cos(fa), y - r * std::sin(fa)) - w0,
                    len(x - r * std::cos(ta), y - r * std::sin(ta)) - w1);
}

// A pointed oval standing upright: `length` from middle to tip, `width`
// from middle to side.
float sdVesica(float x, float y, float length, float width) {
    const float c = (length * length - width * width) / (2.0f * width);
    return len(std::fabs(x) + c, y) - (c + width);
}

// A slice of pie with its point at the origin, opening upward, `half`
// degrees to each side, `r` long.
float sdPie(float x, float y, float half, float r) {
    const float a = half * kPi / 180.0f;
    const float sn = std::sin(a), cs = std::cos(a);
    x = std::fabs(x);
    const float l = len(x, y) - r;
    const float t = std::clamp(x * sn + y * cs, 0.0f, r);
    const float m = len(x - sn * t, y - cs * t);
    return std::max(l, m * ((cs * x - sn * y) > 0.0f ? 1.0f : -1.0f));
}

// A drop: a round head at +x and a tail tapering to a point at -x.
float sdDrop(float x, float y) {
    const float head = len(x - 0.35f, y) - 0.55f;
    if (x >= 0.35f || x <= -0.95f) {
        return head;
    }
    const float width = 0.55f * std::pow((x + 0.95f) / 1.3f, 0.9f);
    return std::min(head, (std::fabs(y) - width) * 0.85f);
}

// Two figures run together with a soft join `k` wide.
float smin(float a, float b, float k) {
    const float h = clamp01(0.5f + 0.5f * (b - a) / k);
    return mix(b, a, h) - k * h * (1.0f - h);
}

// --------------------------------------------------------------- shading

// A hard-edged figure in the house style. `light` brightens a band along
// its upper-left edge and `shade` darkens a rim along its lower right, each
// `reach` wide: the parts of the figure that a copy of it, moved that far
// toward or away from the light, does not cover.
template <class D>
ShapeSample flat(const D& d, float x, float y, float px, float light = 0.45f, float shade = 0.45f,
                 float reach = 0.1f) {
    ShapeSample s;
    s.cover = edge(d(x, y), px);
    if (s.cover > 0.0f) {
        const float o = reach * 0.70710678f;
        const float lit = light > 0.0f ? 1.0f - edge(d(x - o, y + o), px * 1.5f) : 0.0f;
        const float dark = shade > 0.0f ? 1.0f - edge(d(x + o, y - o), px * 1.5f) : 0.0f;
        s.tone = light * lit - shade * dark;
    }
    return s;
}

// A shine spot: a small oval, leaning the way highlights do.
float gloss(float x, float y, float cx, float cy, float a, float b, float px, float lean = 35.0f) {
    const P q = turned(x - cx, y - cy, lean);
    return edge(sdEllipse(q.x, q.y, a, b), px);
}

// Line work that glows: a crisp line of the particle's colour turning
// white along its middle, in a soft halo.
ShapeSample glowing(float distance, float half, float px, float halo = 0.28f, float strength = 0.55f) {
    ShapeSample s;
    const float core = edge(distance - half, px);
    s.cover = clamp01(std::max(core, strength * sq(clamp01(1.0f - distance / halo))));
    s.shine = 0.7f * edge(distance - half * 0.45f, px);
    return s;
}

// Line work drawn flat: for circles of runes and the like.
ShapeSample inked(float lines, float wash) {
    ShapeSample s;
    s.cover = std::max(lines, wash);
    s.shine = 0.45f * lines;
    return s;
}

// ================================================================ basic
// Plain figures in plain colour, like the disc and the square.

ShapeSample triangle(float x, float y, float px) {
    return flat([](float u, float v) { return sdNgon(u, v + 0.14f, 0.84f, 3, 90.0f) - 0.07f; }, x, y, px, 0.0f, 0.0f);
}

ShapeSample pentagon(float x, float y, float px) {
    return flat([](float u, float v) { return sdNgon(u, v + 0.04f, 0.84f, 5, 90.0f) - 0.05f; }, x, y, px, 0.0f, 0.0f);
}

ShapeSample hexagon(float x, float y, float px) {
    return flat([](float u, float v) { return sdNgon(u, v, 0.86f, 6, 90.0f) - 0.04f; }, x, y, px, 0.0f, 0.0f);
}

ShapeSample octagon(float x, float y, float px) {
    return flat([](float u, float v) { return sdNgon(u, v, 0.88f, 8, 22.5f) - 0.03f; }, x, y, px, 0.0f, 0.0f);
}

ShapeSample oval(float x, float y, float px) {
    return flat([](float u, float v) { return sdEllipse(u, v, 0.92f, 0.6f); }, x, y, px, 0.0f, 0.0f);
}

ShapeSample capsule(float x, float y, float px) {
    return flat([](float u, float v) { return sdSeg(u, v, -0.52f, 0.0f, 0.52f, 0.0f) - 0.38f; }, x, y, px, 0.0f, 0.0f);
}

ShapeSample halfdisc(float x, float y, float px) {
    return flat([](float u, float v) { return std::max(len(u, v + 0.42f) - 0.9f, -(v + 0.42f)); }, x, y, px, 0.0f, 0.0f);
}

ShapeSample wedge(float x, float y, float px) {
    return flat([](float u, float v) { return sdPie(u, v + 0.8f, 27.0f, 1.56f) - 0.04f; }, x, y, px, 0.0f, 0.0f);
}

float plusD(float u, float v) {
    return std::min(sdRound(u, v, 0.86f, 0.27f, 0.08f), sdRound(u, v, 0.27f, 0.86f, 0.08f));
}

ShapeSample plus(float x, float y, float px) { return flat(plusD, x, y, px, 0.0f, 0.0f); }

ShapeSample cross(float x, float y, float px) {
    return flat(
        [](float u, float v) {
            const P q = turned(u, v, 45.0f);
            return std::min(sdRound(q.x, q.y, 0.92f, 0.2f, 0.06f), sdRound(q.x, q.y, 0.2f, 0.92f, 0.06f));
        },
        x, y, px);
}

ShapeSample frame(float x, float y, float px) {
    return flat([](float u, float v) { return std::fabs(sdRound(u, v, 0.8f, 0.8f, 0.14f)) - 0.09f; }, x, y, px, 0.0f,
                0.0f);
}

ShapeSample hexframe(float x, float y, float px) {
    return flat([](float u, float v) { return std::fabs(sdNgon(u, v, 0.84f, 6, 90.0f)) - 0.08f; }, x, y, px, 0.0f,
                0.0f);
}

ShapeSample arc(float x, float y, float px) {
    return flat([](float u, float v) { return sdArc(u, v + 0.45f, 0.9f, 30.0f, 150.0f, 0.1f, 0.1f); }, x, y, px,
                0.0f, 0.0f);
}

ShapeSample dashring(float x, float y, float px) {
    ShapeSample s;
    const float r = len(x, y);
    // Ten dashes: how far, along the ring, from the middle of the nearest gap.
    const float a = angleOf(x, y) / 36.0f;
    const float off = std::fabs(a - std::floor(a) - 0.5f);  // 0 mid-dash, 0.5 mid-gap
    const float along = (0.34f - off) * 36.0f * kPi / 180.0f * r;
    s.cover = edge(std::fabs(r - 0.8f) - 0.07f, px) * edge(-along, px);
    return s;
}

ShapeSample doublering(float x, float y, float px) {
    ShapeSample s;
    const float r = len(x, y);
    s.cover = std::max(edge(std::fabs(r - 0.86f) - 0.05f, px), edge(std::fabs(r - 0.62f) - 0.028f, px));
    return s;
}

ShapeSample softring(float x, float y, float) {
    ShapeSample s;
    s.cover = sq(clamp01(1.0f - std::fabs(len(x, y) - 0.68f) / 0.27f));
    return s;
}

// ================================================== stars and sparkles

ShapeSample starfour(float x, float y, float px) {
    ShapeSample s;
    s.cover = edge(sdStar(x, y, 0.95f, 4, 0.3f, 90.0f), px);
    s.tone = 0.8f * edge(sdStar(x, y, 0.52f, 4, 0.3f, 90.0f), px);
    return s;
}

ShapeSample starsix(float x, float y, float px) {
    ShapeSample s;
    s.cover = edge(sdStar(x, y, 0.92f, 6, 0.5f, 90.0f) - 0.03f, px);
    s.tone = 0.8f * edge(sdStar(x, y, 0.5f, 6, 0.5f, 90.0f) - 0.02f, px);
    return s;
}

ShapeSample stareight(float x, float y, float px) {
    ShapeSample s;
    s.cover = edge(sdStar(x, y, 0.93f, 8, 0.58f, 90.0f) - 0.02f, px);
    s.tone = 0.8f * edge(sdStar(x, y, 0.52f, 8, 0.58f, 90.0f) - 0.02f, px);
    return s;
}

ShapeSample starfat(float x, float y, float px) {
    ShapeSample s = flat([](float u, float v) { return sdStar(u, v + 0.03f, 0.74f, 5, 0.6f, 90.0f) - 0.14f; }, x, y,
                         px, 0.0f, 0.5f, 0.12f);
    const float spot = gloss(x, y, -0.22f, 0.3f, 0.2f, 0.09f, px) * s.cover;
    s.tone += 0.3f * spot;
    s.shine = 0.9f * spot;
    return s;
}

ShapeSample starline(float x, float y, float px) {
    ShapeSample s;
    s.cover = edge(std::fabs(sdStar(x, y + 0.03f, 0.84f, 5, 0.45f, 90.0f)) - 0.055f, px);
    return s;
}

ShapeSample pinwheel(float x, float y, float px) {
    static const P blade[3] = {{0.0f, 0.0f}, {0.92f, 0.0f}, {0.46f, 0.46f}};
    static const P lit[3] = {{0.0f, 0.0f}, {0.92f, 0.0f}, {0.6f, 0.2f}};
    // Four blades, each the last turned a quarter turn (not mirrored).
    const float sector = 90.0f;
    const float a = angleOf(x, y);
    const float within = (a - std::floor(a / sector) * sector) * kPi / 180.0f;
    const float r = len(x, y);
    const float qx = r * std::cos(within), qy = r * std::sin(within);
    ShapeSample s;
    s.cover = edge(sdPoly(qx, qy, blade), px);
    s.tone = 0.6f * edge(sdPoly(qx, qy, lit), px) - 0.3f * (1.0f - edge(sdPoly(qx, qy, lit), px));
    const float pin = edge(r - 0.1f, px);
    s.cover = std::max(s.cover, pin);
    s.shine = 0.8f * pin;
    return s;
}

ShapeSample sunburst(float x, float y, float px) {
    static const P ray[3] = {{0.52f, -0.105f}, {0.95f, 0.0f}, {0.52f, 0.105f}};
    const P q = fold(x, y, 12, 90.0f, false);
    const float r = len(x, y);
    ShapeSample s;
    s.cover = std::max(edge(r - 0.42f, px), edge(sdPoly(q.x, q.y, ray), px));
    s.tone = 0.7f * edge(r - 0.3f, px);
    return s;
}

// A comic-book burst: twelve points of uneven length.
float powD(float x, float y) {
    static const float reach[12] = {0.95f, 0.74f, 0.9f, 0.7f, 0.93f, 0.78f, 0.86f, 0.72f, 0.95f, 0.76f, 0.88f, 0.7f};
    static const std::vector<P> outline = [] {
        std::vector<P> points;
        for (int i = 0; i < 12; ++i) {
            const float tip = (static_cast<float>(i) * 30.0f + 8.0f) * kPi / 180.0f;
            const float dip = tip + 15.0f * kPi / 180.0f;
            points.push_back(P{reach[i] * std::cos(tip), reach[i] * std::sin(tip)});
            points.push_back(P{0.48f * std::cos(dip), 0.48f * std::sin(dip)});
        }
        return points;
    }();
    return sdPoly(x, y, outline.data(), static_cast<int>(outline.size()));
}

ShapeSample comicPow(float x, float y, float px) {
    ShapeSample s;
    s.cover = edge(powD(x, y), px);
    s.tone = 0.8f * edge(powD(x / 0.62f, y / 0.62f) * 0.62f, px);
    return s;
}

ShapeSample bang(float x, float y, float px) {
    ShapeSample s;
    s.cover = edge(std::fabs(sdStar(x, y, 0.9f, 14, 0.8f, 90.0f)) - 0.05f, px);
    return s;
}

ShapeSample speedlines(float x, float y, float px) {
    // Twenty lines flying outward, each its own length.
    static const float starts[20] = {0.42f, 0.6f, 0.5f, 0.66f, 0.46f, 0.58f, 0.4f, 0.68f, 0.52f, 0.62f,
                                     0.44f, 0.64f, 0.5f, 0.7f,  0.42f, 0.56f, 0.48f, 0.66f, 0.54f, 0.6f};
    static const float ends[20] = {0.95f, 0.86f, 0.92f, 0.95f, 0.8f,  0.94f, 0.88f, 0.95f, 0.84f, 0.93f,
                                   0.9f,  0.95f, 0.82f, 0.94f, 0.95f, 0.86f, 0.92f, 0.9f,  0.95f, 0.84f};
    int k = 0;
    const P q = fold(x, y, 20, 9.0f, false, &k);
    ShapeSample s;
    s.cover = edge(sdTaper(q.x, q.y, starts[k], 0.0f, ends[k], 0.0f, 0.034f, 0.006f), px);
    return s;
}

// One thin ray along an axis, narrowing and fading to its tip.
float ray(float along, float across, float length, float width) {
    if (along >= length) {
        return 0.0f;
    }
    const float left = 1.0f - along / length;
    return clamp01(1.0f - across / (width * left + 1e-4f)) * std::sqrt(left);
}

ShapeSample crossflare(float x, float y, float) {
    // The flare of a bright light through a lens: a long streak across, a
    // short one up and down, a white-hot middle.
    const float r2 = x * x + y * y;
    const float rays = std::max(ray(std::fabs(x), std::fabs(y), 0.98f, 0.055f), ray(std::fabs(y), std::fabs(x), 0.46f, 0.045f));
    const float core = sq(clamp01(1.0f - r2 / sq(0.22f)));
    ShapeSample s;
    s.cover = clamp01(std::max(rays, core) + 0.3f * sq(clamp01(1.0f - r2 / sq(0.5f))));
    s.shine = core;
    return s;
}

ShapeSample bokeh(float x, float y, float px) {
    // An out-of-focus light: a faint six-sided disc, brighter at its rim.
    const float d = sdNgon(x, y, 0.86f, 6, 90.0f) - 0.04f;
    ShapeSample s;
    const float inside = edge(d, std::max(px, 0.05f));
    s.cover = inside * (0.45f + 0.55f * clamp01(1.0f + (d + 0.06f) / 0.1f));
    return s;
}

ShapeSample glowdiamond(float x, float y, float) {
    const float d = std::fabs(x) / 0.95f + std::fabs(y) / 0.95f;
    ShapeSample s;
    s.cover = clamp01(1.7f * sq(clamp01(1.0f - d)));
    s.shine = sq(clamp01(1.0f - d / 0.32f));
    return s;
}

// ==================================================== smoke and clouds

float lobes(float x, float y, const float (*list)[3], int n) {
    float d = 1e9f;
    for (int i = 0; i < n; ++i) {
        d = std::min(d, len(x - list[i][0], y - list[i][1]) - list[i][2]);
    }
    return d;
}

ShapeSample cloud(float x, float y, float px) {
    // A storybook cloud: round on top, flat underneath.
    return flat(
        [](float u, float v) {
            static const float list[5][3] = {{-0.5f, -0.12f, 0.34f},
                                             {-0.16f, 0.14f, 0.42f},
                                             {0.3f, 0.06f, 0.38f},
                                             {0.6f, -0.16f, 0.3f},
                                             {0.05f, -0.2f, 0.36f}};
            const float top = lobes(u, v, list, 5);
            return std::max(std::min(top, sdRound(u, v + 0.3f, 0.78f, 0.16f, 0.1f)), -(v + 0.46f));
        },
        x, y, px, 0.5f, 0.45f, 0.12f);
}

ShapeSample puffball(float x, float y, float px) {
    return flat(
        [](float u, float v) {
            static const float list[3][3] = {{-0.34f, -0.24f, 0.5f}, {0.36f, -0.22f, 0.48f}, {0.0f, 0.3f, 0.54f}};
            return lobes(u, v, list, 3);
        },
        x, y, px, 0.5f, 0.5f, 0.13f);
}

ShapeSample dustcloud(float x, float y, float px) {
    // Low and wide, for dust kicked up along the ground.
    return flat(
        [](float u, float v) {
            static const float list[6][3] = {{-0.66f, -0.2f, 0.28f}, {-0.34f, 0.02f, 0.36f}, {0.06f, 0.1f, 0.4f},
                                             {0.44f, 0.0f, 0.34f},   {0.7f, -0.2f, 0.25f},  {0.0f, -0.25f, 0.32f}};
            return std::max(std::min(lobes(u, v, list, 6), sdRound(u, v + 0.34f, 0.84f, 0.16f, 0.12f)), -(v + 0.5f));
        },
        x, y, px, 0.45f, 0.45f, 0.11f);
}

ShapeSample wisp(float x, float y, float) {
    // A curl of smoke: a fat head winding round and thinning away.
    const float d = sdArc(x - 0.02f, y + 0.02f, 0.52f, 70.0f, 340.0f, 0.3f, 0.03f);
    ShapeSample s;
    s.cover = smooth(0.08f, -0.1f, d);
    return s;
}

ShapeSample smokering(float x, float y, float px) {
    return flat(
        [](float u, float v) {
            const float bumps = 0.035f * std::cos(7.0f * std::atan2(v, u));
            return std::fabs(len(u, v) - 0.66f) - (0.2f + bumps);
        },
        x, y, px, 0.45f, 0.5f, 0.09f);
}

ShapeSample steam(float x, float y, float) {
    // Rising steam: an S that thins toward the top.
    const float lower = sdArc(x + 0.1f, y + 0.34f, 0.36f, -90.0f, 90.0f, 0.18f, 0.15f);
    const float upper = sdArc(x + 0.1f, y - 0.38f, 0.36f, 90.0f, 270.0f, 0.06f, 0.15f);
    ShapeSample s;
    s.cover = smooth(0.07f, -0.09f, std::min(lower, upper));
    return s;
}

// ====================================================== fire and light

float fireballD(float x, float y) {
    float d = len(x - 0.4f, y) - 0.46f;
    d = std::min(d, sdTaper(x, y, 0.3f, 0.0f, -0.96f, 0.0f, 0.43f, 0.0f));
    d = std::min(d, sdTaper(x, y, 0.3f, 0.14f, -0.62f, 0.52f, 0.3f, 0.0f));
    d = std::min(d, sdTaper(x, y, 0.3f, -0.14f, -0.56f, -0.5f, 0.3f, 0.0f));
    return d;
}

ShapeSample fireball(float x, float y, float px) {
    // A ball of fire flying head first (+x), flames streaming behind.
    const auto shrunk = [&](float k) { return fireballD((x - 0.4f) / k + 0.4f, y / k) * k; };
    ShapeSample s;
    s.cover = edge(fireballD(x, y), px);
    s.tone = 0.8f * edge(shrunk(0.66f), px);
    s.shine = 0.85f * edge(shrunk(0.36f), px);
    return s;
}

ShapeSample ember(float x, float y, float px) {
    static const P chip[6] = {{-0.5f, -0.1f}, {-0.18f, -0.46f}, {0.36f, -0.34f},
                              {0.52f, 0.12f}, {0.1f, 0.48f},    {-0.36f, 0.34f}};
    const float d = sdPoly(x, y, chip);
    ShapeSample s;
    s.cover = std::max(edge(d, px), 0.5f * sq(clamp01(1.0f - (x * x + y * y) / sq(0.95f))));
    s.shine = 0.9f * edge(d + 0.16f, px);
    return s;
}

// A tongue of flame standing up, swaying `bend` to the side as it rises.
float tongueD(float x, float y, float bottom, float top, float girth, float bend) {
    const float t = (y - bottom) / (top - bottom);
    if (t <= 0.0f || t >= 1.0f) {
        return 1.0f;
    }
    const float base = (0.3f - std::min(t, 0.3f)) / 0.3f;
    const float taper = 1.0f - std::max(t - 0.3f, 0.0f) / 0.7f;
    const float width = girth * std::sqrt(clamp01(1.0f - base * base)) * std::pow(taper, 0.85f);
    return (std::fabs(x - bend * std::sin(t * 4.4f) * t) - width) * 0.75f;
}

ShapeSample flametongue(float x, float y, float px) {
    ShapeSample s;
    s.cover = edge(tongueD(x, y, -0.92f, 0.95f, 0.46f, 0.26f), px);
    s.tone = 0.8f * edge(tongueD(x, y, -0.78f, 0.5f, 0.26f, 0.2f), px);
    return s;
}

ShapeSample spark(float x, float y, float px) {
    ShapeSample s;
    s.cover = edge((std::fabs(x) / 0.92f + std::fabs(y) / 0.17f - 1.0f) * 0.17f, px);
    s.shine = 0.9f * edge((std::fabs(x) / 0.6f + std::fabs(y) / 0.07f - 1.0f) * 0.07f, px);
    return s;
}

ShapeSample zigzag(float x, float y, float px) {
    static const P line[5] = {{-0.92f, 0.16f}, {-0.36f, -0.26f}, {0.04f, 0.3f}, {0.42f, -0.22f}, {0.92f, 0.1f}};
    const float thin = 1.0f - std::pow(std::fabs(x), 5.0f);
    return glowing(sdLine(x, y, line), 0.06f * thin, px, 0.3f, 0.6f * thin);
}

ShapeSample lightcone(float x, float y, float) {
    // A shaft of light coming down from a point: bright at the top, wider
    // and fainter as it falls.
    const float t = clamp01((0.95f - y) / 1.9f);
    const float width = 0.06f + 0.62f * t;
    const float across = sq(clamp01(1.0f - std::fabs(x) / width));
    const float ends = clamp01(t / 0.05f) * clamp01((1.0f - t) / 0.25f);
    ShapeSample s;
    s.cover = across * ends * (1.0f - 0.6f * t);
    s.shine = 0.6f * sq(clamp01(1.0f - std::fabs(x) / (0.3f * width))) * sq(1.0f - t) * ends;
    return s;
}

ShapeSample halo(float x, float y, float) {
    const float r = len(x, y);
    ShapeSample s;
    s.cover = clamp01(sq(clamp01(1.0f - std::fabs(r - 0.76f) / 0.16f)) + 0.14f * clamp01((0.76f - r) / 0.1f));
    s.shine = 0.5f * sq(clamp01(1.0f - std::fabs(r - 0.76f) / 0.05f));
    return s;
}

ShapeSample glowbar(float x, float y, float) {
    const float ends = smooth(0.96f, 0.5f, std::fabs(x));
    ShapeSample s;
    s.cover = sq(clamp01(1.0f - std::fabs(y) / 0.24f)) * ends;
    s.shine = 0.85f * clamp01(1.0f - std::fabs(y) / 0.06f) * ends;
    return s;
}

// ==================================================== water and nature

ShapeSample droplet(float x, float y, float px) {
    // A falling drop, round at the bottom and pointed at the top.
    ShapeSample s = flat([](float u, float v) { return sdDrop(-v - 0.02f, u); }, x, y, px, 0.0f, 0.45f, 0.12f);
    const float spot = gloss(x, y, -0.2f, -0.2f, 0.17f, 0.08f, px, -55.0f) * s.cover;
    s.tone += 0.3f * spot;
    s.shine = spot;
    return s;
}

float splashD(float x, float y) {
    // A crown of water: a pool with five fingers thrown up from it.
    float d = sdEllipse(x, y + 0.62f, 0.74f, 0.2f);
    static const float fingers[5][2] = {{-0.78f, 0.1f}, {-0.42f, 0.5f}, {0.0f, 0.66f}, {0.42f, 0.5f}, {0.78f, 0.1f}};
    for (const auto& f : fingers) {
        d = smin(d, sdTaper(x, y, f[0] * 0.55f, -0.58f, f[0], f[1], 0.16f, 0.085f), 0.1f);
    }
    return d;
}

ShapeSample splash(float x, float y, float px) {
    ShapeSample s = flat(splashD, x, y, px, 0.35f, 0.4f, 0.08f);
    // Drops that have come free of the fingers.
    static const float drops[3][3] = {{-0.6f, 0.78f, 0.075f}, {0.0f, 0.9f, 0.07f}, {0.62f, 0.76f, 0.075f}};
    for (const auto& d : drops) {
        s.cover = std::max(s.cover, edge(len(x - d[0], y - d[1]) - d[2], px));
    }
    return s;
}

ShapeSample ripple(float x, float y, float px) {
    const float r = len(x, y);
    const float soft = std::max(px, 0.03f);
    ShapeSample s;
    s.cover = std::max({edge(std::fabs(r - 0.88f) - 0.03f, soft), 0.8f * edge(std::fabs(r - 0.58f) - 0.04f, soft),
                        0.6f * edge(std::fabs(r - 0.28f) - 0.04f, soft)});
    return s;
}

float waveD(float x, float y) {
    // A breaking wave rolling to the right: a hump with a hollow under its
    // curl, standing on a strip of water.
    const float hump = len(x + 0.1f, y + 0.12f) - 0.74f;
    const float hollow = len(x - 0.36f, y + 0.2f) - 0.46f;
    const float curl = std::max(hump, -hollow);
    const float water = sdRound(x, y + 0.72f, 0.94f, 0.16f, 0.08f);
    return std::max(std::min(curl, water), -(y + 0.88f));
}

ShapeSample wave(float x, float y, float px) {
    ShapeSample s = flat(waveD, x, y, px, 0.0f, 0.35f, 0.09f);
    // Foam along the crest.
    const float crest = len(x + 0.1f, y + 0.12f) - 0.74f;
    const float foam = edge(-(crest + 0.11f), px) * edge(-(y + 0.3f), px) * s.cover;
    s.tone += 0.75f * foam;
    s.shine = 0.5f * foam;
    static const float spray[3][3] = {{0.72f, 0.5f, 0.07f}, {0.86f, 0.24f, 0.055f}, {0.5f, 0.78f, 0.05f}};
    for (const auto& d : spray) {
        const float drop = edge(len(x - d[0], y - d[1]) - d[2], px);
        s.cover = std::max(s.cover, drop);
        s.tone = std::max(s.tone, 0.6f * drop);
    }
    return s;
}

ShapeSample leaf(float x, float y, float px) {
    const P q = turned(x, y, -40.0f);
    ShapeSample s;
    const float body = sdVesica(q.x, q.y - 0.06f, 0.86f, 0.4f);
    const float stem = sdTaper(q.x, q.y, 0.0f, -0.72f, 0.0f, -0.96f, 0.035f, 0.03f);
    s.cover = edge(std::min(body, stem), px);
    const float inside = edge(body, px);
    // One half catches the light; the midrib and veins are darker.
    float veins = std::fabs(q.x) - 0.022f;
    for (const float at : {-0.4f, -0.1f, 0.2f, 0.5f}) {
        veins = std::min(veins, sdSeg(std::fabs(q.x), q.y, 0.0f, at, 0.28f, at + 0.2f) - 0.015f);
    }
    s.tone = inside * (q.x < 0.0f ? 0.35f : -0.1f) - 0.55f * edge(veins, px) * inside;
    return s;
}

ShapeSample petal(float x, float y, float px) {
    ShapeSample s = flat([](float u, float v) { return sdDrop(v - 0.02f, u / 1.25f); }, x, y, px, 0.4f, 0.35f, 0.1f);
    // A crease up the middle, fading toward the tip.
    s.tone -= 0.4f * edge(std::fabs(x) - 0.02f, px) * clamp01((0.3f - y) / 0.6f) * s.cover;
    return s;
}

ShapeSample flower(float x, float y, float px) {
    const P q = fold(x, y, 5, 90.0f, true);
    const float r = len(x, y);
    ShapeSample s;
    const float petals = sdEllipse(q.y, q.x - 0.54f, 0.3f, 0.4f);
    s.cover = std::max(edge(petals, px), edge(r - 0.24f, px));
    const float rim = 1.0f - edge(sdEllipse(q.y, q.x - 0.5f, 0.3f, 0.4f) + 0.05f, px * 1.5f);
    s.tone = -0.3f * rim * edge(petals, px);
    const float heart = edge(r - 0.24f, px);
    s.tone = mix(s.tone, 0.85f, heart);
    s.shine = 0.45f * heart;
    return s;
}

ShapeSample clover(float x, float y, float px) {
    const P q = fold(x, y - 0.08f, 4, 45.0f, true);
    ShapeSample s;
    const float leaves = std::min(len(q.x - 0.5f, q.y - 0.2f) - 0.3f, len(q.x - 0.3f, q.y) - 0.26f);
    const float stem = sdTaper(x, y, 0.0f, -0.1f, 0.12f, -0.95f, 0.06f, 0.04f);
    s.cover = edge(std::min(leaves, stem), px);
    const float inside = edge(leaves, px);
    s.tone = inside * (0.3f * edge(len(q.x - 0.5f, q.y - 0.2f) - 0.2f, px) -
                       0.5f * edge(sdSeg(q.x, q.y, 0.12f, 0.0f, 0.62f, 0.0f) - 0.018f, px));
    return s;
}

ShapeSample snowflake(float x, float y, float px) {
    const P q = fold(x, y, 6, 90.0f, true);
    float d = sdSeg(q.x, q.y, 0.0f, 0.0f, 0.9f, 0.0f) - 0.045f;
    d = std::min(d, sdSeg(q.x, q.y, 0.56f, 0.0f, 0.76f, 0.2f) - 0.04f);
    d = std::min(d, sdSeg(q.x, q.y, 0.32f, 0.0f, 0.48f, 0.16f) - 0.04f);
    d = std::min(d, sdNgon(x, y, 0.17f, 6, 90.0f));
    ShapeSample s;
    s.cover = std::max(edge(d, px), 0.22f * sq(clamp01(1.0f - (x * x + y * y) / sq(0.95f))));
    s.shine = 0.6f * edge(d, px);
    return s;
}

ShapeSample feather(float x, float y, float px) {
    const P q = turned(x, y, -35.0f);
    ShapeSample s;
    float body = sdVesica(q.x, q.y - 0.12f, 0.8f, 0.3f);
    // Two splits in the vane, as real feathers have.
    body = std::max(body, -(sdTaper(q.x, q.y, 0.34f, 0.3f, 0.06f, 0.14f, 0.05f, 0.0f)));
    body = std::max(body, -(sdTaper(q.x, q.y, -0.34f, -0.02f, -0.06f, -0.2f, 0.05f, 0.0f)));
    const float quill = sdTaper(q.x, q.y, 0.0f, -0.96f, 0.0f, 0.6f, 0.03f, 0.012f);
    s.cover = edge(std::min(body, quill), px);
    const float vane = edge(body, px);
    // Barbs slanting up from the quill, lighter on the side toward the light.
    const float slant = (q.y - std::fabs(q.x) * 1.3f) * 7.0f;
    const float barb = std::fabs(slant - std::floor(slant) - 0.5f) < 0.2f ? 1.0f : 0.0f;
    s.tone = vane * ((q.x < 0.0f ? 0.3f : -0.05f) - 0.18f * barb) + 0.5f * edge(quill, px);
    return s;
}

ShapeSample pebble(float x, float y, float px) {
    return flat(
        [](float u, float v) {
            static const P stone[7] = {{-0.58f, -0.3f}, {-0.1f, -0.5f}, {0.5f, -0.36f}, {0.68f, 0.06f},
                                       {0.3f, 0.44f},   {-0.3f, 0.48f}, {-0.66f, 0.12f}};
            return sdPoly(u, v, stone) - 0.16f;
        },
        x, y, px, 0.4f, 0.5f, 0.16f);
}

ShapeSample raindash(float x, float y, float) {
    // A streak of rain flying head first (+x).
    const float along = std::pow(clamp01((x + 0.95f) / 1.6f), 1.4f) * clamp01((0.95f - x) / 0.1f);
    ShapeSample s;
    s.cover = sq(clamp01(1.0f - std::fabs(y) / 0.075f)) * along;
    s.shine = 0.6f * clamp01(1.0f - std::fabs(y) / 0.03f) * along * clamp01((x + 0.2f) / 0.8f);
    return s;
}

// ==================================================== candy and rewards

ShapeSample coin(float x, float y, float px) {
    const float r = len(x, y);
    ShapeSample s = flat([](float u, float v) { return len(u, v) - 0.9f; }, x, y, px, 0.3f, 0.4f, 0.07f);
    const float groove = edge(std::fabs(r - 0.7f) - 0.028f, px);
    const float star = edge(sdStar(x, y + 0.02f, 0.46f, 5, 0.46f, 90.0f) - 0.02f, px);
    s.tone = (s.tone - 0.5f * groove + 0.55f * star) * s.cover;
    // A glint across the face.
    const float band = clamp01(1.0f - std::fabs((x + y) * 0.7071f + 0.2f) / 0.09f);
    s.shine = 0.5f * band * s.cover * edge(r - 0.66f, px);
    return s;
}

ShapeSample gem(float x, float y, float px) {
    // A round-cut gem seen from above: a flat table and eight facets, each
    // as bright as it faces the light.
    ShapeSample s;
    s.cover = edge(sdNgon(x, y, 0.92f, 8, 22.5f), px);
    const float table = edge(sdNgon(x, y, 0.46f, 8, 22.5f), px);
    const float facing = std::floor(angleOf(x, y) / 45.0f + 0.5f) * 45.0f * kPi / 180.0f;
    const float lit = (-std::cos(facing) + std::sin(facing)) * 0.7071f;
    s.tone = mix(0.6f * lit, 0.3f, table) * s.cover;
    const float glint = edge(sdStar(x + 0.3f, y - 0.34f, 0.2f, 4, 0.25f, 90.0f), px);
    s.shine = glint;
    return s;
}

ShapeSample emerald(float x, float y, float px) {
    static const P cut[8] = {{-0.4f, -0.88f}, {0.4f, -0.88f}, {0.62f, -0.66f}, {0.62f, 0.66f},
                             {0.4f, 0.88f},   {-0.4f, 0.88f}, {-0.62f, 0.66f}, {-0.62f, -0.66f}};
    static const P table[8] = {{-0.22f, -0.56f}, {0.22f, -0.56f}, {0.32f, -0.46f}, {0.32f, 0.46f},
                               {0.22f, 0.56f},   {-0.22f, 0.56f}, {-0.32f, 0.46f}, {-0.32f, -0.46f}};
    ShapeSample s;
    s.cover = edge(sdPoly(x, y, cut), px);
    const float top = edge(sdPoly(x, y, table), px);
    // Four long facets round the table: which one this point is on.
    const bool upright = (0.88f - std::fabs(y)) < (0.62f - std::fabs(x)) * 1.1f;
    const float side = upright ? (y > 0.0f ? 0.55f : -0.5f) : (x < 0.0f ? 0.25f : -0.25f);
    s.tone = mix(side, 0.1f, top) * s.cover;
    s.shine = 0.6f * top * clamp01(1.0f - std::fabs((x + y) * 0.7071f + 0.12f) / 0.07f);
    return s;
}

ShapeSample crystal(float x, float y, float px) {
    static const P prism[6] = {{0.0f, 0.95f},  {0.4f, 0.52f},   {0.4f, -0.52f},
                               {0.0f, -0.95f}, {-0.4f, -0.52f}, {-0.4f, 0.52f}};
    ShapeSample s;
    s.cover = edge(sdPoly(x, y, prism), px);
    // Three long faces, lit from the left, and darker caps at the ends.
    const float face = x < -0.13f ? 0.5f : (x > 0.13f ? -0.4f : 0.05f);
    const float cap = clamp01((std::fabs(y) - 0.52f) / 0.02f);
    s.tone = (face - 0.15f * cap * (y < 0.0f ? 1.0f : -1.0f)) * s.cover;
    s.shine = 0.7f * edge(std::fabs(x + 0.13f) - 0.018f, px) * edge(std::fabs(y) - 0.5f, px) * s.cover;
    return s;
}

float crownD(float x, float y) {
    static const P band[7] = {{-0.78f, -0.6f}, {0.78f, -0.6f}, {0.88f, 0.3f}, {0.44f, -0.08f},
                              {0.0f, 0.56f},   {-0.44f, -0.08f}, {-0.88f, 0.3f}};
    float d = sdPoly(x, y, band) - 0.03f;
    d = std::min(d, len(std::fabs(x) - 0.88f, y - 0.38f) - 0.1f);
    return std::min(d, len(x, y - 0.66f) - 0.11f);
}

ShapeSample crown(float x, float y, float px) {
    ShapeSample s = flat(crownD, x, y, px, 0.4f, 0.4f, 0.08f);
    // A darker band round the brow, set with three jewels.
    const float band = edge(std::fabs(y + 0.42f) - 0.1f, px) * s.cover;
    float jewels = len(x, y + 0.42f) - 0.065f;
    jewels = std::min(jewels, len(std::fabs(x) - 0.42f, y + 0.42f) - 0.055f);
    const float jewel = edge(jewels, px);
    s.tone = mix(s.tone, -0.4f, band);
    s.tone = mix(s.tone, 0.8f, jewel);
    s.shine = 0.8f * jewel;
    return s;
}

float trophyD(float x, float y) {
    // The cup: an oval cut flat across the top.
    float d = std::max(sdEllipse(x, y - 0.42f, 0.5f, 0.62f), y - 0.72f);
    d = std::min(d, sdRound(x, y + 0.36f, 0.08f, 0.2f, 0.03f));           // the stem
    d = std::min(d, sdRound(x, y + 0.72f, 0.42f, 0.14f, 0.05f));          // the foot
    d = std::min(d, std::fabs(len(std::fabs(x) - 0.52f, y - 0.36f) - 0.2f) - 0.055f);  // the handles
    return d;
}

ShapeSample trophy(float x, float y, float px) {
    ShapeSample s = flat(trophyD, x, y, px, 0.0f, 0.4f, 0.08f);
    const float streak = edge(sdRound(x + 0.24f, y - 0.36f, 0.05f, 0.24f, 0.05f), px) * s.cover;
    s.tone += 0.5f * streak;
    s.shine = 0.7f * streak;
    return s;
}

float keyD(float x, float y) {
    float d = std::fabs(len(x + 0.52f, y) - 0.3f) - 0.11f;
    d = std::min(d, sdRound(x - 0.28f, y, 0.62f, 0.085f, 0.04f));
    d = std::min(d, sdRound(x - 0.58f, y + 0.2f, 0.07f, 0.16f, 0.02f));
    return std::min(d, sdRound(x - 0.8f, y + 0.16f, 0.07f, 0.12f, 0.02f));
}

ShapeSample key(float x, float y, float px) { return flat(keyD, x, y, px, 0.4f, 0.4f, 0.06f); }

ShapeSample gift(float x, float y, float px) {
    const float box = sdRound(x, y + 0.36f, 0.68f, 0.5f, 0.06f);
    const float lid = sdRound(x, y - 0.24f, 0.78f, 0.15f, 0.05f);
    const P left = turned(x + 0.26f, y - 0.62f, 25.0f), right = turned(x - 0.26f, y - 0.62f, -25.0f);
    const float bow = std::min(sdEllipse(left.x, left.y, 0.27f, 0.17f), sdEllipse(right.x, right.y, 0.27f, 0.17f));
    ShapeSample s;
    s.cover = edge(std::min({box, lid, bow, len(x, y - 0.56f) - 0.1f}), px);
    // The ribbon and the bow are a lighter tone; the box is shaded under its lid.
    const float ribbon = edge(std::fabs(x) - 0.12f, px) * edge(std::min(box, lid), px);
    const float knot = edge(std::min(bow, len(x, y - 0.56f) - 0.1f), px);
    const float under = edge(std::fabs(y + 0.02f) - 0.07f, px) * edge(box, px);
    s.tone = (0.65f * std::max(ribbon, knot) - 0.4f * under * (1.0f - ribbon) - 0.25f * edge(x - 0.4f, px) * (1.0f - knot)) * s.cover;
    return s;
}

ShapeSample ribbon(float x, float y, float px) {
    // A curl of paper ribbon, lighter where it turns toward the light.
    const float wave = 0.36f * std::sin(x * 3.5f);
    const float slope = 0.36f * 3.5f * std::cos(x * 3.5f);
    const float d = std::max((std::fabs(y - wave) / std::sqrt(1.0f + slope * slope)) - 0.13f, std::fabs(x) - 0.92f);
    ShapeSample s;
    s.cover = edge(d, px);
    s.tone = (std::cos(x * 3.5f) > 0.0f ? 0.4f : -0.35f) * s.cover;
    return s;
}

ShapeSample confetti(float x, float y, float px) {
    static const P front[4] = {{-0.82f, -0.34f}, {0.0f, -0.52f}, {0.0f, 0.34f}, {-0.82f, 0.56f}};
    static const P back[4] = {{0.0f, -0.52f}, {0.82f, -0.22f}, {0.82f, 0.6f}, {0.0f, 0.34f}};
    const float a = edge(sdPoly(x, y, front), px), b = edge(sdPoly(x, y, back), px);
    ShapeSample s;
    s.cover = std::max(a, b);
    s.tone = 0.4f * a - 0.4f * b * (1.0f - a);
    return s;
}

ShapeSample balloon(float x, float y, float px) {
    static const P knot[3] = {{0.0f, -0.56f}, {0.13f, -0.76f}, {-0.13f, -0.76f}};
    ShapeSample s = flat(
        [](float u, float v) { return std::min(sdEllipse(u, v - 0.16f, 0.62f, 0.74f), sdPoly(u, v, knot) - 0.02f); }, x,
        y, px, 0.0f, 0.45f, 0.14f);
    const float spot = gloss(x, y, -0.26f, 0.44f, 0.2f, 0.09f, px) * s.cover;
    s.tone += 0.3f * spot;
    s.shine = 0.9f * spot;
    return s;
}

ShapeSample donut(float x, float y, float px) {
    const float r = len(x, y);
    ShapeSample s = flat([](float u, float v) { return std::fabs(len(u, v) - 0.56f) - 0.34f; }, x, y, px, 0.0f, 0.4f,
                         0.1f);
    // Icing with a wavy edge, and sprinkles.
    const float drip = 0.05f * std::cos(9.0f * std::atan2(y, x));
    const float icing = edge(std::fabs(r - 0.55f) - (0.22f + drip), px);
    static const float sprinkles[7][3] = {{0.5f, 0.16f, 20.0f},  {0.2f, 0.52f, 110.0f},  {-0.34f, 0.44f, 60.0f},
                                          {-0.56f, -0.04f, 150.0f}, {-0.3f, -0.5f, 30.0f}, {0.2f, -0.54f, 80.0f},
                                          {0.56f, -0.24f, 130.0f}};
    float bits = 1e9f;
    for (const auto& b : sprinkles) {
        const float a = b[2] * kPi / 180.0f;
        bits = std::min(bits, sdSeg(x, y, b[0] - 0.05f * std::cos(a), b[1] - 0.05f * std::sin(a),
                                    b[0] + 0.05f * std::cos(a), b[1] + 0.05f * std::sin(a)) - 0.022f);
    }
    const float sprinkle = edge(bits, px);
    s.tone = mix(s.tone, 0.55f, icing * s.cover);
    s.shine = 0.85f * sprinkle * s.cover;
    return s;
}

ShapeSample bonbon(float x, float y, float px) {
    // A wrapped sweet: a round middle and a twist of paper at each end.
    static const P twist[3] = {{0.42f, 0.0f}, {0.95f, 0.42f}, {0.95f, -0.42f}};
    const float body = sdEllipse(x, y, 0.52f, 0.4f);
    const float ends = sdPoly(std::fabs(x), y, twist) - 0.03f;
    ShapeSample s;
    s.cover = edge(std::min(body, ends), px);
    const float middle = edge(body, px);
    const float stripe = std::fabs((x + y * 0.6f) * 4.0f - std::floor((x + y * 0.6f) * 4.0f) - 0.5f) < 0.22f ? 1.0f : 0.0f;
    const float shade = 1.0f - edge(sdEllipse(x + 0.07f, y - 0.07f, 0.5f, 0.38f), px * 2.0f);
    const float crease = edge(std::fabs(y) - 0.03f, px) * (1.0f - middle);
    s.tone = (middle * (0.5f * stripe - 0.45f * shade) + (1.0f - middle) * (0.25f - 0.5f * crease)) * s.cover;
    s.shine = 0.85f * gloss(x, y, -0.18f, 0.2f, 0.16f, 0.06f, px) * middle;
    return s;
}

ShapeSample medal(float x, float y, float px) {
    static const P left[4] = {{-0.62f, 0.95f}, {-0.26f, 0.95f}, {0.1f, 0.2f}, {-0.2f, 0.1f}};
    static const P right[4] = {{0.62f, 0.95f}, {0.26f, 0.95f}, {-0.1f, 0.2f}, {0.2f, 0.1f}};
    const float disc = len(x, y + 0.34f) - 0.58f;
    const float a = sdPoly(x, y, left), b = sdPoly(x, y, right);
    ShapeSample s;
    s.cover = edge(std::min({disc, a, b}), px);
    const float face = edge(disc, px);
    const float star = edge(sdStar(x, y + 0.36f, 0.34f, 5, 0.46f, 90.0f) - 0.015f, px);
    const float rim = edge(std::fabs(len(x, y + 0.34f) - 0.48f) - 0.022f, px);
    const float shade = 1.0f - edge(len(x + 0.06f, y + 0.28f) - 0.56f, px * 2.0f);
    s.tone = mix((edge(a, px) > 0.5f ? 0.35f : -0.3f), 0.55f * star - 0.45f * rim - 0.4f * shade, face) * s.cover;
    return s;
}

float noteD(float x, float y) {
    static const P flag[5] = {{0.1f, 0.86f}, {0.66f, 0.36f}, {0.56f, -0.02f}, {0.48f, 0.3f}, {0.1f, 0.5f}};
    const P head = turned(x + 0.24f, y + 0.6f, 20.0f);
    float d = sdEllipse(head.x, head.y, 0.34f, 0.25f);
    d = std::min(d, sdRound(x - 0.045f, y - 0.12f, 0.055f, 0.74f, 0.02f));
    return std::min(d, sdPoly(x, y, flag) - 0.02f);
}

ShapeSample note(float x, float y, float px) { return flat(noteD, x, y, px, 0.35f, 0.4f, 0.07f); }

ShapeSample cherry(float x, float y, float px) {
    const float fruit = std::min(len(x + 0.4f, y + 0.46f) - 0.37f, len(x - 0.42f, y + 0.5f) - 0.37f);
    const float stems = std::min(sdSeg(x, y, -0.36f, -0.12f, 0.16f, 0.82f), sdSeg(x, y, 0.4f, -0.16f, 0.16f, 0.82f)) - 0.035f;
    const P q = turned(x - 0.44f, y - 0.72f, 60.0f);
    const float leaf = sdVesica(q.x, q.y, 0.34f, 0.14f);
    ShapeSample s;
    s.cover = edge(std::min({fruit, stems, leaf}), px);
    const float round = edge(fruit, px);
    const float shade = 1.0f - edge(std::min(len(x + 0.34f, y + 0.4f) - 0.33f, len(x - 0.36f, y + 0.44f) - 0.33f), px * 2.0f);
    const float spot = std::max(gloss(x, y, -0.52f, -0.3f, 0.11f, 0.05f, px), gloss(x, y, 0.3f, -0.34f, 0.11f, 0.05f, px));
    s.tone = (round * (0.3f * spot - 0.45f * shade) + (1.0f - round) * -0.35f) * s.cover;
    s.shine = 0.9f * spot * round;
    return s;
}

// ================================================================ magic

ShapeSample pentacle(float x, float y, float px) {
    const float r = len(x, y);
    const auto line = [&](float distance, float half) { return edge(std::fabs(distance) - half, px); };
    float lines = std::max(line(r - 0.9f, 0.032f), line(r - 0.78f, 0.012f));
    // The five-pointed star drawn in one stroke: each corner joined to the
    // next but one.
    float chords = 1e9f;
    for (int i = 0; i < 5; ++i) {
        const float a = (90.0f + 72.0f * static_cast<float>(i)) * kPi / 180.0f;
        const float b = (90.0f + 72.0f * static_cast<float>(i + 2)) * kPi / 180.0f;
        chords = std::min(chords, sdSeg(x, y, 0.77f * std::cos(a), 0.77f * std::sin(a), 0.77f * std::cos(b),
                                        0.77f * std::sin(b)));
    }
    lines = std::max(lines, edge(chords - 0.016f, px));
    return inked(lines, 0.14f * sq(clamp01(1.0f - r * r / sq(0.93f))));
}

ShapeSample triad(float x, float y, float px) {
    const float r = len(x, y);
    const auto line = [&](float distance, float half) { return edge(std::fabs(distance) - half, px); };
    float lines = line(r - 0.9f, 0.03f);
    lines = std::max(lines, line(sdNgon(x, y, 0.86f, 3, 90.0f), 0.014f));
    lines = std::max(lines, line(sdNgon(x, y, 0.86f, 3, 270.0f), 0.01f));
    lines = std::max(lines, line(r - 0.43f, 0.014f));
    lines = std::max(lines, edge(r - 0.07f, px));
    // A small ring at each corner of the upright triangle.
    const P q = fold(x, y, 3, 90.0f, true);
    lines = std::max(lines, line(len(q.x - 0.86f, q.y) - 0.09f, 0.012f));
    return inked(lines, 0.14f * sq(clamp01(1.0f - r * r / sq(0.93f))));
}

ShapeSample spiral(float x, float y, float px) {
    // One line winding outward from the middle, two and a half times round.
    const float r = len(x, y);
    const float pitch = 0.34f;
    const float turns = std::atan2(y, x) / kTau;
    const float t = r / pitch - turns;
    const float nearest = std::floor(t + 0.5f);
    const float d = std::fabs(t - nearest) * pitch;
    const float ends = edge(r - 0.92f, px) * (nearest >= 0.0f && nearest <= 2.0f ? 1.0f : 0.0f);
    ShapeSample s = glowing(d, 0.05f * (1.0f - 0.45f * r), px, 0.16f, 0.35f);
    s.cover *= ends;
    s.shine *= ends;
    return s;
}

// How far a point is from the nearest wall of a honeycomb whose cells are
// `size` across.
float honeycomb(float x, float y, float size) {
    x /= size;
    y /= size;
    const float sx = 1.0f, sy = 1.7320508f;
    const auto wrap = [](float v, float period) { return v - period * std::floor(v / period) - period * 0.5f; };
    const float ax = wrap(x, sx), ay = wrap(y, sy);
    const float bx = wrap(x - sx * 0.5f, sx), by = wrap(y - sy * 0.5f, sy);
    float gx = ax, gy = ay;
    if (bx * bx + by * by < ax * ax + ay * ay) {
        gx = bx;
        gy = by;
    }
    gx = std::fabs(gx);
    gy = std::fabs(gy);
    return (0.5f - std::max(gx * 0.5f + gy * 0.8660254f, gx)) * size;
}

ShapeSample hexshield(float x, float y, float px) {
    // A wall of force: a six-sided pane of honeycomb.
    const float pane = sdNgon(x, y, 0.92f, 6, 90.0f);
    const float inside = edge(pane, px);
    const float walls = edge(honeycomb(x, y, 0.36f) - 0.018f, px);
    const float border = edge(std::fabs(pane + 0.035f) - 0.035f, px);
    ShapeSample s;
    s.cover = inside * std::max({walls, border, 0.2f + 0.2f * clamp01(1.0f + pane / 0.5f)});
    s.shine = 0.4f * inside * std::max(walls, border);
    return s;
}

ShapeSample eye(float x, float y, float px) {
    const P q{y, x};  // the pointed oval lies on its side
    const float lids = sdVesica(q.x, q.y, 0.94f, 0.52f);
    const float r = len(x, y);
    const auto line = [&](float distance, float half) { return edge(std::fabs(distance) - half, px); };
    float lines = line(lids + 0.04f, 0.04f);
    lines = std::max(lines, line(r - 0.33f, 0.03f) * edge(lids, px));
    lines = std::max(lines, edge(r - 0.15f, px));
    ShapeSample s = inked(lines, 0.16f * edge(lids, px));
    const float glint = edge(len(x + 0.07f, y - 0.07f) - 0.045f, px);
    s.shine = std::max(s.shine, glint);
    return s;
}

ShapeSample moon(float x, float y, float px) {
    return flat([](float u, float v) { return std::max(len(u + 0.08f, v) - 0.86f, -(len(u - 0.3f, v - 0.14f) - 0.72f)); },
                x, y, px, 0.45f, 0.4f, 0.08f);
}

float wingD(float x, float y) {
    // An arm rising from the shoulder to the wing tip, and the flight
    // feathers hanging from it, longest in the middle.
    static const float feathers[6][5] = {{-0.6f, -0.16f, -0.44f, -0.7f, 0.13f}, {-0.36f, 0.06f, -0.08f, -0.76f, 0.13f},
                                         {-0.1f, 0.28f, 0.3f, -0.62f, 0.13f},   {0.16f, 0.46f, 0.64f, -0.36f, 0.12f},
                                         {0.42f, 0.56f, 0.88f, -0.04f, 0.11f},  {0.64f, 0.6f, 0.96f, 0.3f, 0.09f}};
    float d = sdTaper(x, y, -0.78f, -0.34f, 0.3f, 0.56f, 0.15f, 0.1f);
    d = std::min(d, sdTaper(x, y, 0.3f, 0.56f, 0.9f, 0.62f, 0.1f, 0.04f));
    for (const auto& f : feathers) {
        d = std::min(d, sdTaper(x, y, f[0], f[1], f[2], f[3], f[4], 0.05f));
    }
    return d;
}

ShapeSample wing(float x, float y, float px) {
    const float k = 0.92f;  // drawn a little smaller, to keep the tip inside the square
    return flat([k](float u, float v) { return wingD(u / k, v / k) * k; }, x, y, px, 0.45f, 0.4f, 0.07f);
}

ShapeSample skull(float x, float y, float px) {
    const float bone = std::min(len(x, y - 0.16f) - 0.66f, sdRound(x, y + 0.5f, 0.36f, 0.3f, 0.1f));
    ShapeSample s = flat([](float u, float v) { return std::min(len(u, v - 0.16f) - 0.66f, sdRound(u, v + 0.5f, 0.36f, 0.3f, 0.1f)); },
                         x, y, px, 0.0f, 0.4f, 0.1f);
    // Eye sockets, a nose and the gaps between the teeth are see-through.
    static const P nose[3] = {{0.0f, -0.08f}, {0.09f, -0.26f}, {-0.09f, -0.26f}};
    float holes = len(std::fabs(x) - 0.27f, y - 0.08f) - 0.19f;
    holes = std::min(holes, sdPoly(x, y, nose) - 0.01f);
    for (const float at : {-0.17f, 0.0f, 0.17f}) {
        holes = std::min(holes, sdRound(x - at, y + 0.68f, 0.022f, 0.12f, 0.01f));
    }
    s.cover = std::min(s.cover, edge(bone, px)) * (1.0f - edge(holes, px));
    return s;
}

ShapeSample octagram(float x, float y, float px) {
    const P q = turned(x, y, 45.0f);
    const float r = len(x, y);
    const auto line = [&](float distance, float half) { return edge(std::fabs(distance) - half, px); };
    float lines = std::max(line(sdBox(x, y, 0.6f, 0.6f), 0.016f), line(sdBox(q.x, q.y, 0.6f, 0.6f), 0.016f));
    lines = std::max(lines, line(r - 0.9f, 0.03f));
    lines = std::max(lines, line(r - 0.3f, 0.012f));
    return inked(lines, 0.14f * sq(clamp01(1.0f - r * r / sq(0.93f))));
}

ShapeSample portalring(float x, float y, float px) {
    const float r = len(x, y);
    // Three long pieces of a heavy ring with gaps between, and a fine ring inside.
    const float a = angleOf(x, y) / 120.0f;
    const float off = std::fabs(a - std::floor(a) - 0.5f);
    const float along = (0.42f - off) * 120.0f * kPi / 180.0f * r;
    const float heavy = edge(std::fabs(r - 0.8f) - 0.09f, px) * edge(-along, px);
    const float fine = edge(std::fabs(r - 0.6f) - 0.014f, px);
    ShapeSample s;
    s.cover = std::max({heavy, fine, 0.12f * sq(clamp01(1.0f - r * r / sq(0.9f)))});
    s.shine = 0.6f * heavy * edge(std::fabs(r - 0.74f) - 0.025f, px);
    s.tone = 0.3f * heavy;
    return s;
}

ShapeSample crosshair(float x, float y, float px) {
    const float r = len(x, y);
    const P q = fold(x, y, 4, 0.0f, true);
    float d = std::fabs(r - 0.66f) - 0.04f;
    d = std::min(d, sdSeg(q.x, q.y, 0.46f, 0.0f, 0.94f, 0.0f) - 0.04f);
    d = std::min(d, r - 0.07f);
    ShapeSample s;
    s.cover = edge(d, px);
    return s;
}

ShapeSample yinyang(float x, float y, float px) {
    const float r = len(x, y);
    ShapeSample s;
    s.cover = edge(r - 0.9f, px);
    const float upper = len(x, y - 0.45f), lower = len(x, y + 0.45f);
    // Light on the left, with each half's head swelling into the other.
    float light = edge(x, px);
    light = std::max(light, edge(upper - 0.45f, px));
    light = std::min(light, 1.0f - edge(lower - 0.45f, px));
    light = std::max(light, edge(lower - 0.13f, px));
    light = std::min(light, 1.0f - edge(upper - 0.13f, px));
    s.tone = mix(-0.7f, 0.75f, light) * s.cover;
    return s;
}

// The runes are drawn this much smaller than their numbers say, so their
// halo ends inside the square.
constexpr float kRune = 0.8f;

ShapeSample runea(float x, float y, float px) {
    // An upright with two arms raised to the right.
    x /= kRune;
    y /= kRune;
    float d = sdSeg(x, y, -0.2f, -0.86f, -0.2f, 0.86f);
    d = std::min(d, sdSeg(x, y, -0.2f, 0.3f, 0.46f, 0.84f));
    d = std::min(d, sdSeg(x, y, -0.2f, -0.14f, 0.46f, 0.4f));
    return glowing(d * kRune, 0.07f, px);
}

ShapeSample runeb(float x, float y, float px) {
    // A lozenge standing on two legs.
    static const P loop[5] = {{0.0f, 0.86f}, {0.46f, 0.3f}, {0.0f, -0.26f}, {-0.46f, 0.3f}, {0.0f, 0.86f}};
    x /= kRune;
    y /= kRune;
    float d = sdLine(x, y, loop);
    d = std::min(d, sdSeg(std::fabs(x), y, 0.0f, -0.26f, 0.46f, -0.86f));
    return glowing(d * kRune, 0.07f, px);
}

ShapeSample runec(float x, float y, float px) {
    // A stroke of lightning: down, back and down again.
    static const P line[4] = {{0.34f, 0.86f}, {-0.34f, 0.2f}, {0.34f, -0.2f}, {-0.34f, -0.86f}};
    return glowing(sdLine(x / kRune, y / kRune, line) * kRune, 0.07f, px);
}

// ====================================================== hits and debris

ShapeSample slash(float x, float y, float px) {
    // The arc a blade leaves in the air: thin, bowed upward, bright along
    // its outer edge.
    const float outer = len(x, y + 0.77f) - 1.25f;
    const float inner = len(x, y + 1.12f) - 1.5f;
    const float d = std::max(outer, -inner);
    ShapeSample s;
    s.cover = edge(d, px);
    const float lead = edge(outer + 0.05f, px);
    s.tone = 0.6f * (1.0f - lead) * s.cover;
    s.shine = 0.7f * (1.0f - edge(outer + 0.025f, px)) * s.cover;
    return s;
}

ShapeSample claw(float x, float y, float px) {
    // Three rakes of a claw, the middle one longest.
    float d = 1e9f;
    static const float marks[3][3] = {{-0.46f, 0.05f, 0.74f}, {0.0f, 0.0f, 0.92f}, {0.46f, -0.05f, 0.74f}};
    for (const auto& m : marks) {
        const P q = turned(x - m[0], y - m[1], 18.0f);
        d = std::min(d, sdVesica(q.x, q.y, m[2], 0.105f));
    }
    ShapeSample s;
    s.cover = edge(d, px);
    s.tone = 0.5f * edge(d + 0.045f, px);
    return s;
}

ShapeSample crack(float x, float y, float px) {
    // Cracks running out from a blow, each jagged and thinning to nothing.
    static const P a[4] = {{0.0f, 0.0f}, {0.3f, 0.24f}, {0.5f, 0.2f}, {0.9f, 0.5f}};
    static const P b[4] = {{0.0f, 0.0f}, {-0.2f, 0.36f}, {-0.1f, 0.6f}, {-0.3f, 0.92f}};
    static const P c[4] = {{0.0f, 0.0f}, {-0.4f, -0.06f}, {-0.56f, -0.3f}, {-0.92f, -0.36f}};
    static const P e[4] = {{0.0f, 0.0f}, {0.16f, -0.36f}, {0.4f, -0.5f}, {0.46f, -0.9f}};
    static const P f[3] = {{0.5f, 0.2f}, {0.74f, 0.02f}, {0.94f, 0.04f}};
    static const P g[3] = {{-0.56f, -0.3f}, {-0.6f, -0.62f}, {-0.76f, -0.8f}};
    const float d = std::min({sdLine(x, y, a), sdLine(x, y, b), sdLine(x, y, c), sdLine(x, y, e), sdLine(x, y, f),
                              sdLine(x, y, g)});
    ShapeSample s;
    s.cover = edge(d - 0.07f * clamp01(1.0f - len(x, y) / 1.02f), px);
    return s;
}

ShapeSample impact(float x, float y, float px) {
    // The lines drawn round a blow: twelve dashes, long and short by turns.
    int k = 0;
    const P q = fold(x, y, 12, 90.0f, false, &k);
    const bool isLong = k % 2 == 0;
    ShapeSample s;
    s.cover = edge(sdTaper(q.x, q.y, isLong ? 0.4f : 0.56f, 0.0f, isLong ? 0.95f : 0.82f, 0.0f, 0.06f, 0.012f), px);
    return s;
}

ShapeSample rock(float x, float y, float px) {
    static const P stone[7] = {{-0.7f, -0.36f}, {-0.16f, -0.62f}, {0.56f, -0.46f}, {0.8f, 0.1f},
                               {0.36f, 0.6f},   {-0.3f, 0.56f},   {-0.78f, 0.14f}};
    // Three faces meeting at a ridge inside the stone.
    static const P top[4] = {{-0.78f, 0.14f}, {-0.3f, 0.56f}, {0.36f, 0.6f}, {-0.02f, 0.08f}};
    static const P right[5] = {{0.36f, 0.6f}, {0.8f, 0.1f}, {0.56f, -0.46f}, {0.12f, -0.2f}, {-0.02f, 0.08f}};
    ShapeSample s;
    s.cover = edge(sdPoly(x, y, stone), px);
    s.tone = (0.45f * edge(sdPoly(x, y, top), px) - 0.45f * edge(sdPoly(x, y, right), px)) * s.cover;
    return s;
}

ShapeSample splinter(float x, float y, float px) {
    static const P wood[7] = {{-0.95f, -0.04f}, {-0.3f, -0.2f}, {0.5f, -0.15f}, {0.95f, 0.02f},
                              {0.42f, 0.13f},   {-0.2f, 0.21f}, {-0.7f, 0.1f}};
    static const P grain[3] = {{-0.7f, 0.0f}, {0.0f, 0.03f}, {0.7f, 0.0f}};
    ShapeSample s;
    s.cover = edge(sdPoly(x, y, wood), px);
    s.tone = (0.3f * (y > 0.02f ? 1.0f : 0.0f) - 0.45f * edge(sdLine(x, y, grain) - 0.014f, px)) * s.cover;
    return s;
}

ShapeSample glass(float x, float y, float px) {
    static const P pane[3] = {{-0.56f, -0.82f}, {0.78f, -0.28f}, {-0.16f, 0.92f}};
    ShapeSample s;
    const float d = sdPoly(x, y, pane);
    s.cover = edge(d, px) * 0.85f;
    // A bright edge, and the streak of a reflection across it.
    const float rim = 1.0f - edge(d + 0.05f, px);
    const float streak = clamp01(1.0f - std::fabs((x - y) * 0.7071f - 0.02f) / 0.07f);
    s.tone = 0.5f * rim;
    s.shine = 0.75f * std::max(streak, 0.5f * rim) * edge(d, px);
    return s;
}

float gearD(float x, float y) {
    const P q = fold(x, y, 8, 90.0f, true);
    const float teeth = sdRound(q.x - 0.74f, q.y, 0.18f, 0.13f, 0.03f);
    return std::max(std::min(len(x, y) - 0.66f, teeth), -(len(x, y) - 0.26f));
}

ShapeSample gear(float x, float y, float px) { return flat(gearD, x, y, px, 0.4f, 0.4f, 0.07f); }

float boneD(float x, float y) {
    float d = sdSeg(x, y, -0.56f, 0.0f, 0.56f, 0.0f) - 0.13f;
    return std::min(d, len(std::fabs(x) - 0.66f, std::fabs(y) - 0.15f) - 0.19f);
}

ShapeSample bone(float x, float y, float px) { return flat(boneD, x, y, px, 0.0f, 0.4f, 0.08f); }

ShapeSample brick(float x, float y, float px) {
    static const P chip[3] = {{0.5f, 0.62f}, {0.98f, 0.62f}, {0.98f, 0.14f}};
    ShapeSample s = flat(
        [](float u, float v) {
            const P q = turned(u, v, 8.0f);
            return std::max(sdRound(q.x, q.y, 0.84f, 0.5f, 0.05f), -sdPoly(q.x, q.y, chip));
        },
        x, y, px, 0.4f, 0.45f, 0.1f);
    return s;
}

// ============================================================== symbols

ShapeSample arrow(float x, float y, float px) {
    return flat(
        [](float u, float v) {
            static const P outline[7] = {{-0.9f, -0.2f}, {0.14f, -0.2f}, {0.14f, -0.56f}, {0.92f, 0.0f},
                                         {0.14f, 0.56f}, {0.14f, 0.2f},  {-0.9f, 0.2f}};
            return sdPoly(u, v, outline) - 0.03f;
        },
        x, y, px, 0.4f, 0.4f, 0.08f);
}

ShapeSample chevron(float x, float y, float px) {
    return flat(
        [](float u, float v) {
            static const P outline[6] = {{-0.5f, -0.86f}, {-0.02f, -0.86f}, {0.74f, 0.0f},
                                         {-0.02f, 0.86f}, {-0.5f, 0.86f},  {0.26f, 0.0f}};
            return sdPoly(u, v, outline) - 0.03f;
        },
        x, y, px, 0.4f, 0.4f, 0.08f);
}

ShapeSample check(float x, float y, float px) {
    return flat(
        [](float u, float v) {
            static const P line[3] = {{-0.7f, -0.02f}, {-0.26f, -0.5f}, {0.72f, 0.56f}};
            return sdLine(u, v, line) - 0.16f;
        },
        x, y, px, 0.4f, 0.4f, 0.08f);
}

ShapeSample exclaim(float x, float y, float px) {
    return flat(
        [](float u, float v) {
            return std::min(sdTaper(u, v, 0.0f, 0.72f, 0.0f, -0.22f, 0.2f, 0.1f), len(u, v + 0.7f) - 0.18f);
        },
        x, y, px, 0.4f, 0.4f, 0.08f);
}

ShapeSample question(float x, float y, float px) {
    return flat(
        [](float u, float v) {
            float d = sdArc(u, v - 0.42f, 0.36f, -65.0f, 180.0f, 0.14f, 0.14f);
            d = std::min(d, sdSeg(u, v, 0.152f, 0.094f, 0.0f, -0.1f) - 0.14f);
            d = std::min(d, sdSeg(u, v, 0.0f, -0.1f, 0.0f, -0.26f) - 0.14f);
            return std::min(d, len(u, v + 0.72f) - 0.17f);
        },
        x, y, px, 0.4f, 0.4f, 0.08f);
}

ShapeSample lightning(float x, float y, float px) {
    return flat(
        [](float u, float v) {
            static const P outline[7] = {{0.06f, 0.95f}, {-0.56f, -0.06f}, {-0.1f, -0.06f}, {-0.3f, -0.95f},
                                         {0.56f, 0.16f}, {0.1f, 0.16f},    {0.46f, 0.95f}};
            return sdPoly(u, v, outline);
        },
        x, y, px, 0.5f, 0.4f, 0.08f);
}

float shieldD(float x, float y) {
    static const P outline[10] = {{-0.72f, 0.76f}, {0.0f, 0.9f},    {0.72f, 0.76f},  {0.72f, 0.1f},  {0.6f, -0.32f},
                                  {0.36f, -0.66f}, {0.0f, -0.92f}, {-0.36f, -0.66f}, {-0.6f, -0.32f}, {-0.72f, 0.1f}};
    return sdPoly(x, y, outline) - 0.03f;
}

ShapeSample shield(float x, float y, float px) {
    ShapeSample s;
    const float d = shieldD(x, y);
    s.cover = edge(d, px);
    // A raised border, and a face split down the middle, light and dark.
    const float face = edge(d + 0.14f, px);
    s.tone = mix(-0.35f, x < 0.0f ? 0.4f : 0.05f, face) * s.cover;
    return s;
}

ShapeSample sword(float x, float y, float px) {
    static const P blade[5] = {{-0.11f, -0.36f}, {0.11f, -0.36f}, {0.11f, 0.7f}, {0.0f, 0.95f}, {-0.11f, 0.7f}};
    const float steel = sdPoly(x, y, blade);
    const float hilt = std::min({sdRound(x, y + 0.42f, 0.4f, 0.075f, 0.04f), sdRound(x, y + 0.66f, 0.065f, 0.2f, 0.02f),
                                 len(x, y + 0.88f) - 0.1f});
    ShapeSample s;
    s.cover = edge(std::min(steel, hilt), px);
    const float onBlade = edge(steel, px);
    s.tone = (onBlade * (x < 0.0f ? 0.55f : 0.05f) - (1.0f - onBlade) * 0.35f) * s.cover;
    s.shine = 0.5f * onBlade * (x < 0.0f ? 1.0f : 0.0f);
    return s;
}

float pawD(float x, float y) {
    float d = smin(len(x, y + 0.36f) - 0.34f, len(std::fabs(x) - 0.2f, y + 0.5f) - 0.26f, 0.08f);
    d = std::min(d, sdEllipse(std::fabs(x) - 0.6f, y - 0.06f, 0.17f, 0.21f));
    return std::min(d, sdEllipse(std::fabs(x) - 0.23f, y - 0.5f, 0.17f, 0.22f));
}

ShapeSample paw(float x, float y, float px) { return flat(pawD, x, y, px, 0.0f, 0.4f, 0.08f); }

ShapeSample zed(float x, float y, float px) {
    // Sleep: a big Z and a small one drifting off above it.
    static const P big[4] = {{-0.76f, 0.2f}, {-0.1f, 0.2f}, {-0.76f, -0.72f}, {-0.1f, -0.72f}};
    static const P little[4] = {{0.24f, 0.84f}, {0.72f, 0.84f}, {0.24f, 0.32f}, {0.72f, 0.32f}};
    return flat([](float u, float v) { return std::min(sdLine(u, v, big) - 0.1f, sdLine(u, v, little) - 0.07f); }, x, y,
                px, 0.4f, 0.4f, 0.06f);
}

float infinityD(float x, float y) {
    // A figure of eight on its side (Bernoulli's lemniscate), as a band.
    static const std::vector<P> loop = [] {
        std::vector<P> points;
        for (int i = 0; i <= 64; ++i) {
            const float t = kTau * static_cast<float>(i) / 64.0f;
            const float k = 0.88f / (1.0f + sq(std::sin(t)));
            points.push_back(P{k * std::cos(t), k * std::sin(t) * std::cos(t) * 1.25f});
        }
        return points;
    }();
    return sdLine(x, y, loop.data(), static_cast<int>(loop.size())) - 0.1f;
}

ShapeSample infinity(float x, float y, float px) { return flat(infinityD, x, y, px, 0.4f, 0.4f, 0.06f); }

ShapeSample lock(float x, float y, float px) {
    const auto whole = [](float u, float v) {
        const float body = sdRound(u, v + 0.3f, 0.6f, 0.46f, 0.1f);
        const float hoop = std::max(std::fabs(len(u, v - 0.14f) - 0.36f) - 0.09f, -(v - 0.14f));
        const float legs = sdRound(std::fabs(u) - 0.36f, v - 0.08f, 0.09f, 0.1f, 0.0f);
        return std::min({body, hoop, legs});
    };
    ShapeSample s = flat(whole, x, y, px, 0.4f, 0.4f, 0.08f);
    // The keyhole, darker.
    static const P slot[3] = {{0.0f, -0.24f}, {0.09f, -0.52f}, {-0.09f, -0.52f}};
    const float hole = edge(std::min(len(x, y + 0.22f) - 0.11f, sdPoly(x, y, slot)), px);
    s.tone = mix(s.tone, -0.75f, hole * s.cover);
    return s;
}

ShapeSample fish(float xIn, float y, float px) {
    const float x = -xIn;  // head to the right, so a fish aligned to its movement swims head first
    // A side-on fish, head to the right: a plump body, a forked tail, a back fin and a belly fin.
    static const P tail[3] = {{0.38f, 0.0f}, {0.94f, 0.42f}, {0.94f, -0.42f}};
    static const P back[3] = {{-0.18f, 0.26f}, {0.2f, 0.26f}, {-0.02f, 0.56f}};
    static const P belly[3] = {{-0.05f, -0.26f}, {0.17f, -0.26f}, {0.03f, -0.46f}};
    const auto whole = [](float u, float v) {
        const float body = sdEllipse(u + 0.1f, v, 0.62f, 0.33f);
        const float fins = std::min(sdPoly(u, v, back), sdPoly(u, v, belly));
        const float notch = len(u - 0.97f, v) - 0.12f;
        return std::max(std::min({body, sdPoly(u, v, tail), fins}), -notch);
    };
    ShapeSample s = flat(whole, x, y, px, 0.4f, 0.4f, 0.07f);
    // The eye, and a lighter belly.
    const float eye = edge(len(x + 0.42f, y - 0.08f) - 0.06f, px);
    const float belly2 = edge(sdEllipse(x + 0.1f, y + 0.2f, 0.48f, 0.12f), px) * s.cover;
    s.tone = mix(s.tone, -0.8f, eye * s.cover);
    s.tone += 0.35f * belly2 * (1.0f - eye);
    return s;
}

// ============================================================ sci-fi (0.41)
// Line work for holograms, scanners, portals, arcs and targeting: crisp bright
// lines in the particle's colour turning white along their middle, over a faint
// wash. Each is one layer, so a machine is built from several that turn at
// different speeds.

// How far a point is from a ring of `count` marks between two radii.
float tickMarks(float x, float y, int count, float r0, float r1, float half) {
    const float r = len(x, y);
    const float sector = kTau / static_cast<float>(count);
    float a = std::atan2(y, x);
    a -= sector * std::floor(a / sector + 0.5f);
    return std::max(std::fabs(r * std::sin(a)) - half, std::max(r0 - r, r - r1));
}
float ringLine(float r, float radius, float half) { return std::fabs(r - radius) - half; }
float fract01(float v) { return v - std::floor(v); }

ShapeSample holoTicks(float x, float y, float px) {
    const float r = len(x, y);
    float strong = ringLine(r, 0.94f, 0.012f);
    strong = std::min(strong, tickMarks(x, y, 12, 0.74f, 0.9f, 0.016f));
    float fine = std::min(ringLine(r, 0.7f, 0.008f), tickMarks(x, y, 72, 0.79f, 0.87f, 0.006f));
    fine = std::min(fine, ringLine(r, 0.62f, 0.005f));
    const float lines = std::max(edge(strong, px), 0.7f * edge(fine, px));
    return inked(lines, 0.07f * smooth(0.7f, 0.5f, r));
}

ShapeSample holoSegs(float x, float y, float px) {
    const float r = len(x, y);
    const float thick = std::min({sdArc(x, y, 0.78f, 8.0f, 100.0f, 0.058f, 0.058f), sdArc(x, y, 0.78f, 126.0f, 206.0f, 0.058f, 0.058f),
                                  sdArc(x, y, 0.78f, 228.0f, 348.0f, 0.058f, 0.058f)});
    const float thin = std::min({sdArc(x, y, 0.64f, 30.0f, 160.0f, 0.011f, 0.011f), sdArc(x, y, 0.64f, 200.0f, 330.0f, 0.011f, 0.011f),
                                 sdArc(x, y, 0.92f, 50.0f, 130.0f, 0.01f, 0.01f), sdArc(x, y, 0.92f, 230.0f, 310.0f, 0.01f, 0.01f)});
    const float body = edge(thick, px);
    ShapeSample s = inked(std::max(0.85f * edge(thin, px), body), 0.0f);
    // A bright thread down the middle of each heavy piece, like a lit edge.
    s.shine = std::max(s.shine, 0.9f * body * edge(ringLine(r, 0.78f, 0.016f), px));
    s.tone = -0.25f * body * (1.0f - edge(ringLine(r, 0.78f, 0.03f), px));
    return s;
}

ShapeSample holoNodes(float x, float y, float px) {
    const float r = len(x, y);
    const P q = fold(x, y, 8, 0.0f, false);
    const P w = fold(x, y, 8, 22.5f, false);
    const float ring = ringLine(r, 0.82f, 0.007f);
    const float dots = len(q.x - 0.82f, q.y) - 0.05f;
    const float diamonds = (std::fabs(w.x - 0.82f) + std::fabs(w.y)) * 0.7071f - 0.03f;
    float inner = std::min(ringLine(r, 0.5f, 0.006f), tickMarks(x, y, 4, 0.42f, 0.58f, 0.008f));
    inner = std::min(inner, ringLine(r, 0.2f, 0.01f));
    const float lines = std::max({0.8f * edge(ring, px), edge(dots, px), edge(diamonds, px), 0.7f * edge(inner, px),
                                  edge(r - 0.035f, px)});
    return inked(lines, 0.05f * smooth(0.5f, 0.2f, r));
}

ShapeSample holoDisc(float x, float y, float px) {
    const float r = len(x, y);
    const float rings = std::min({ringLine(r, 0.7f, 0.004f), ringLine(r, 0.45f, 0.004f), ringLine(r, 0.22f, 0.004f)});
    const float spokes = tickMarks(x, y, 24, 0.22f, 0.94f, 0.003f);
    const float lines = std::max({edge(ringLine(r, 0.94f, 0.016f), px), 0.55f * edge(rings, px), 0.3f * edge(spokes, px)});
    return inked(lines, (0.05f + 0.2f * (1.0f - smooth(0.0f, 0.95f, r))) * edge(r - 0.95f, px));
}

ShapeSample scanDisc(float x, float y, float px) {
    const float r = len(x, y);
    const float rim = edge(ringLine(r, 0.9f, 0.028f), px);
    const float inner = 0.5f * edge(ringLine(r, 0.78f, 0.006f), px);
    ShapeSample s = inked(std::max(rim, inner), 0.3f * sq(smooth(0.2f, 0.9f, r)) * edge(r - 0.9f, px));
    s.shine = std::max(s.shine, 0.9f * rim);
    return s;
}

// A hologram's upright cylinder: lit rims, bright edges, a grid and scan lines.
ShapeSample holoCylinder(float x, float y, float px) {
    const float w = 0.34f, ry = 0.085f, top = 0.8f, bot = -0.8f;
    const float ax = std::fabs(x);
    const float rect = std::max({ax - w, y - top, bot - y});
    const float body = std::max({edge(rect, px), edge(sdEllipse(x, y - top, w, ry), px), edge(sdEllipse(x, y - bot, w, ry), px)});
    const auto rim = [&](float cy) {
        const float d = std::fabs(sdEllipse(x, y - cy, w, ry)) - 0.009f;
        return edge(d, px) * (y < cy ? 1.0f : 0.35f);
    };
    const float rims = std::max(rim(top), rim(bot));
    const float within = edge(std::max(y - top, bot - y), px);
    const float edges = edge(std::fabs(ax - w) - 0.008f, px) * within;
    const float grid = 0.22f * edge(std::fabs(ax - w * 0.5f) - 0.003f, px) * within;
    const float scan = 0.16f * edge(std::fabs(fract01(y * 9.0f) - 0.5f) - 0.44f, px * 9.0f) * body;
    const float fill = (0.07f + 0.16f * sq(ax / w)) * body;
    return inked(std::max({rims, edges, grid, scan}), fill);
}

// Two strands winding round an upright axis, with rungs between them.
ShapeSample helix(float x, float y, float px) {
    const float amp = 0.34f, k = 14.0f;
    const auto strand = [&](float phase) {
        const float s = amp * std::sin(k * y + phase);
        const float slope = amp * k * std::cos(k * y + phase);
        return std::fabs(x - s) / std::sqrt(1.0f + slope * slope);
    };
    const float da = std::cos(k * y), db = -std::cos(k * y);  // toward the viewer is brighter
    const float ea = edge(strand(0.0f) - 0.026f, px) * (0.55f + 0.45f * da);
    const float eb = edge(strand(kPi) - 0.026f, px) * (0.55f + 0.45f * db);
    const float rung = 0.4f * edge(std::fabs(fract01(y * 4.4f) - 0.5f) - 0.47f, px * 4.4f) *
                       edge(std::fabs(x) - std::fabs(amp * std::sin(k * y)), px);
    return inked(std::max({ea, eb, rung}) * smooth(1.0f, 0.82f, std::fabs(y)), 0.0f);
}

// A spreading pulse: a hard bright rim over a soft glow that thickens inward.
ShapeSample energyRing(float x, float y, float px) {
    const float r = len(x, y);
    const float rim = edge(ringLine(r, 0.84f, 0.03f), px);
    const float inner = 0.55f * edge(ringLine(r, 0.7f, 0.012f), px);
    const float glow = sq(clamp01(1.0f - (0.86f - r) / 0.34f)) * edge(r - 0.86f, px) * 0.6f;
    ShapeSample s;
    s.cover = std::max({rim, inner, glow});
    s.shine = 0.85f * rim;
    return s;
}

// A crescent slice of energy, its points trailing behind: +x is the way it travels.
ShapeSample sliceArc(float x, float y, float px) {
    const float r = len(x + 0.3f, y);
    const float a = std::atan2(y, x + 0.3f) * 180.0f / kPi;
    const float t = clamp01((a + 62.0f) / 124.0f);
    const float width = 0.15f * std::sin(kPi * t) + 0.004f;
    const float inside = edge(std::fabs(a) - 62.0f, px * 60.0f);
    ShapeSample s;
    s.cover = std::max(edge(std::fabs(r - 0.9f) - width, px) * inside,
                       0.35f * sq(clamp01(1.0f - std::fabs(r - 0.9f) / (width + 0.2f))) * inside);
    s.shine = 0.9f * edge(std::fabs(r - 0.9f) - width * 0.4f, px) * inside;
    return s;
}

// Swirling arms round a dark centre, for a portal's inside. Turn it to make it flow.
ShapeSample swirlArms(float x, float y, float px) {
    const float r = len(x, y);
    const float theta = std::atan2(y, x);
    const int arms = 5;
    const float u = theta * static_cast<float>(arms) / kTau - 3.0f * std::log(r + 0.12f);
    const float lateral = std::fabs(fract01(u) - 0.5f) * kTau * r / static_cast<float>(arms);
    const float half = 0.03f + 0.09f * (1.0f - r);
    const float fadeIn = smooth(0.07f, 0.28f, r) * smooth(1.0f, 0.82f, r);
    const float arm = edge(lateral - half, px) * fadeIn;
    ShapeSample s;
    s.cover = std::max(arm, 0.45f * sq(clamp01(1.0f - lateral / (half + 0.1f))) * fadeIn);
    s.shine = 0.8f * edge(lateral - half * 0.4f, px) * smooth(0.5f, 0.12f, r) * arm;
    return s;
}

float boltDistance(float x, float y, const P* pts, int n) {
    float d = 1e9f;
    for (int i = 0; i + 1 < n; ++i) {
        d = std::min(d, sdSeg(x, y, pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y));
    }
    return d;
}

// Jagged bolts from the left of the shape to the right, each with its own side branches.
ShapeSample arcBolt(float x, float y, float px) {
    static const P v[] = {{-0.82f, 0.00f}, {-0.75f, 0.00f}, {-0.67f, -0.04f}, {-0.58f, -0.04f}, {-0.54f, -0.03f}, {-0.46f, -0.07f}, {-0.38f, -0.01f}, {-0.33f, -0.01f}, {-0.25f, 0.06f}, {-0.17f, 0.00f}, {-0.09f, 0.02f}, {-0.06f, -0.00f}, {0.03f, 0.03f}, {0.09f, -0.01f}, {0.20f, -0.04f}, {0.26f, 0.08f}, {0.30f, 0.08f}, {0.42f, 0.05f}, {0.48f, 0.03f}, {0.54f, -0.02f}, {0.61f, -0.03f}, {0.65f, -0.02f}, {0.72f, -0.04f}, {0.82f, 0.00f}};
    static const P b1[] = {{-0.38f, -0.01f}, {-0.31f, 0.12f}, {-0.26f, 0.15f}, {-0.17f, 0.24f}};
    static const P b2[] = {{0.26f, 0.08f}, {0.30f, 0.01f}, {0.35f, -0.04f}, {0.43f, -0.10f}};
    const float branches = std::min(boltDistance(x, y, b1, 4), boltDistance(x, y, b2, 4)) * 1.5f;
    return glowing(std::min(boltDistance(x, y, v, 24), branches), 0.013f, px, 0.14f, 0.5f);
}

ShapeSample arcJag(float x, float y, float px) {
    static const P v[] = {{-0.82f, 0.00f}, {-0.71f, 0.06f}, {-0.63f, 0.02f}, {-0.56f, 0.04f}, {-0.53f, 0.04f}, {-0.42f, 0.15f}, {-0.36f, -0.05f}, {-0.27f, 0.10f}, {-0.22f, -0.05f}, {-0.14f, 0.03f}, {-0.04f, 0.06f}, {0.03f, -0.07f}, {0.11f, -0.16f}, {0.16f, -0.01f}, {0.25f, 0.08f}, {0.37f, 0.03f}, {0.42f, -0.02f}, {0.51f, 0.04f}, {0.59f, -0.08f}, {0.65f, -0.07f}, {0.71f, -0.03f}, {0.82f, 0.00f}};
    static const P b1[] = {{-0.42f, 0.15f}, {-0.33f, 0.05f}, {-0.22f, -0.03f}, {-0.13f, -0.20f}};
    static const P b2[] = {{0.03f, -0.07f}, {0.10f, 0.14f}, {0.20f, 0.19f}, {0.26f, 0.38f}};
    static const P b3[] = {{0.42f, -0.02f}, {0.46f, -0.14f}, {0.52f, -0.24f}, {0.58f, -0.39f}, {0.64f, -0.48f}, {0.68f, -0.56f}};
    const float branches = std::min({boltDistance(x, y, b1, 4), boltDistance(x, y, b2, 4), boltDistance(x, y, b3, 6)}) * 1.4f;
    return glowing(std::min(boltDistance(x, y, v, 22), branches), 0.018f, px, 0.16f, 0.5f);
}

ShapeSample arcFork(float x, float y, float px) {
    static const P v[] = {{-0.82f, 0.00f}, {-0.73f, 0.02f}, {-0.65f, -0.03f}, {-0.57f, 0.03f}, {-0.48f, -0.04f}, {-0.42f, 0.02f}, {-0.32f, -0.05f}, {-0.30f, -0.07f}, {-0.20f, 0.06f}, {-0.09f, -0.05f}, {-0.03f, -0.05f}, {0.06f, 0.08f}, {0.09f, 0.06f}, {0.20f, -0.03f}, {0.26f, 0.07f}, {0.35f, 0.01f}, {0.43f, 0.02f}, {0.48f, -0.03f}, {0.57f, 0.05f}, {0.65f, 0.02f}, {0.77f, 0.04f}, {0.82f, 0.00f}};
    static const P b1[] = {{-0.42f, 0.02f}, {-0.31f, 0.22f}, {-0.26f, 0.27f}, {-0.16f, 0.47f}, {-0.07f, 0.56f}, {0.03f, 0.73f}};
    static const P b2[] = {{-0.09f, -0.05f}, {0.03f, -0.26f}, {0.12f, -0.32f}, {0.21f, -0.64f}};
    static const P b3[] = {{0.26f, 0.07f}, {0.35f, 0.24f}, {0.42f, 0.37f}, {0.48f, 0.59f}, {0.58f, 0.68f}};
    static const P b4[] = {{0.48f, -0.03f}, {0.52f, -0.22f}, {0.56f, -0.32f}, {0.62f, -0.47f}, {0.70f, -0.52f}, {0.77f, -0.63f}};
    const float branches = std::min({boltDistance(x, y, b1, 6), boltDistance(x, y, b2, 4), boltDistance(x, y, b3, 5), boltDistance(x, y, b4, 6)}) * 1.3f;
    return glowing(std::min(boltDistance(x, y, v, 22), branches), 0.012f, px, 0.13f, 0.5f);
}

// Targeting: brackets at the corners, ticks round the middle and a bright centre.
ShapeSample reticle(float x, float y, float px) {
    const float ax = std::fabs(x), ay = std::fabs(y);
    const float c = 0.78f, arm = 0.34f, t = 0.022f;
    const float brackets = std::min(sdSeg(ax, ay, c, c, c, c - arm), sdSeg(ax, ay, c, c, c - arm, c)) - t;
    const float r = len(x, y);
    float fine = std::min(ringLine(r, 0.5f, 0.005f), tickMarks(x, y, 36, 0.43f, 0.5f, 0.006f));
    fine = std::min(fine, tickMarks(x, y, 4, 0.5f, 0.62f, 0.012f));
    return inked(std::max({edge(brackets, px), 0.75f * edge(fine, px), edge(r - 0.04f, px)}), 0.0f);
}

// A lock-on ring with four triangles closing in on it from the compass points.
ShapeSample lockRing(float x, float y, float px) {
    const float r = len(x, y);
    const P q = fold(x, y, 4, 0.0f, false);
    const float tri = std::max({0.72f - q.x, q.x - 0.95f, (std::fabs(q.y) - (q.x - 0.72f) * 0.45f) * 0.9f});
    const float bands = std::max(edge(ringLine(r, 0.6f, 0.016f), px), 0.6f * edge(ringLine(r, 0.48f, 0.006f), px));
    const float ticks = 0.7f * edge(tickMarks(x, y, 24, 0.52f, 0.58f, 0.01f), px);
    return inked(std::max({edge(tri, px), bands, ticks}), 0.04f * smooth(0.6f, 0.0f, r));
}


// ------------------------------------------------- shapes that can be changed
//
// Some shapes have settings of their own (see ShapeEdit in Program.h): how
// many points, how thick, how pointed, how round. Each of those is drawn
// here a second time, from its own measurements and the settings, so that
// with every setting at zero it is the shape above, and the settings move it
// away from that. A shape that is not being changed is still drawn by its
// own formula above (or, for the first thirty-five, by the graphics card).

// A setting from -1 to 1 as a measurement: `own` at 0, `low` at -1, `high`
// at 1, and evenly between.
float bent(float amount, float low, float own, float high) {
    return amount < 0.0f ? own + (own - low) * amount : own + (high - own) * amount;
}

int countOr(const ShapeEdit& e, int own, int lowest, int highest) {
    return std::clamp(e.count > 0 ? e.count : own, lowest, highest);
}

// ---- figures with straight sides

struct Polygon {
    int sides;
    float reach;   // from the middle to a corner
    float drop;    // how far below the middle of the square it is drawn
    float toward;  // where one corner points, in degrees
};

// How far from the edge of a regular figure, measured truly (sdNgon above
// measures to the nearest side's line, which is the same thing except
// outside a corner): the difference is what makes a corner come out round
// when the figure is grown.
float sdNgonTrue(float x, float y, float r, int n, float toward) {
    const float half = kPi / static_cast<float>(n);
    const P q = fold(x, y, n, toward, true);
    const float mid = r * std::cos(half);  // from the middle to the middle of a side
    const float d = sdSeg(q.x, q.y, r, 0.0f, mid * std::cos(half), mid * std::sin(half));
    return q.x * std::cos(half) + q.y * std::sin(half) - mid > 0.0f ? d : -d;
}

// Where a figure of `n` sides sits: how far below the middle of the square,
// and where one corner points.
struct Placed {
    float drop, toward;
};

Placed placePolygon(const Polygon& own, int n, float reach) {
    if (n == own.sides) {
        return Placed{own.drop * reach / own.reach, own.toward};
    }
    // A corner points up, unless that would leave the figure standing on a
    // corner when it could sit flat (a square, an octagon).
    const float half = kPi / static_cast<float>(n);
    return Placed{n % 2 == 1 ? 0.3f * reach * (1.0f - std::cos(half)) : 0.0f,
                  n % 4 == 0 ? 90.0f + 180.0f / static_cast<float>(n) : 90.0f};
}

// A figure of `n` sides whose corners have been rounded by `round` (0 not
// at all, 1 all the way to a circle). Rounding takes the corners off and
// leaves the sides where they are.
float polygonD(float u, float v, int n, float round, float reach, const Placed& at) {
    if (!(round > 0.0f)) {
        return sdNgon(u, v + at.drop, reach, n, at.toward);
    }
    const float cosHalf = std::cos(kPi / static_cast<float>(n));
    const float corner = round * 0.999f * reach * cosHalf;
    return sdNgonTrue(u, v + at.drop, reach - corner / cosHalf, n, at.toward) - corner;
}

template <const Polygon& own>
ShapeSample polygonEdited(float x, float y, float px, const ShapeEdit& e) {
    const int n = countOr(e, own.sides, 3, 12);
    const Placed at = placePolygon(own, n, own.reach);
    return flat([&](float u, float v) { return polygonD(u, v, n, e.round, own.reach, at); }, x, y, px, 0.0f, 0.0f);
}

// As drawn above, each is a figure with sharp corners this far across.
constexpr Polygon kTriangle{3, 0.98f, 0.14f, 90.0f};
constexpr Polygon kPentagon{5, 0.9018f, 0.04f, 90.0f};
constexpr Polygon kHexagon{6, 0.9062f, 0.0f, 90.0f};
constexpr Polygon kOctagon{8, 0.9125f, 0.0f, 22.5f};
constexpr Polygon kHexFrame{6, 0.9324f, 0.0f, 90.0f};

ShapeSample hexframeEdited(float x, float y, float px, const ShapeEdit& e) {
    // The outside of the line stays where it is; a thicker line grows inward.
    const int n = countOr(e, 6, 3, 12);
    const float cosHalf = std::cos(kPi / static_cast<float>(n));
    const float half = bent(e.thickness, 0.02f, 0.08f, 0.41f);  // of the line, across it
    const float outer = kHexFrame.reach;
    const float inner = std::max(outer - 2.0f * half / cosHalf, 0.0f);
    const Placed at = placePolygon(kHexFrame, n, outer);
    const float round = std::max(e.round, 0.0f);
    return flat(
        [&](float u, float v) {
            const float outside = polygonD(u, v, n, round, outer, at);
            return inner > 0.01f ? std::max(outside, -polygonD(u, v, n, round, inner, at)) : outside;
        },
        x, y, px, 0.0f, 0.0f);
}

ShapeSample squareEdited(float x, float y, float px, const ShapeEdit& e) {
    const float corner = bent(e.round, 0.0f, 0.12f, 0.78f);
    ShapeSample s;
    s.cover = edge(sdRound(x, y, 0.78f, 0.78f, corner), px);
    return s;
}

ShapeSample frameEdited(float x, float y, float px, const ShapeEdit& e) {
    const float half = bent(e.thickness, 0.0225f, 0.09f, 0.455f);
    const float side = 0.89f - half;
    const float corner = bent(e.round, 0.0f, 0.14f, side);
    return flat([&](float u, float v) { return std::fabs(sdRound(u, v, side, side, corner)) - half; }, x, y, px,
                0.0f, 0.0f);
}

ShapeSample plusEdited(float x, float y, float px, const ShapeEdit& e) {
    const float arm = bent(e.thickness, 0.07f, 0.27f, 0.6f);
    const float corner = bent(e.round, 0.0f, 0.08f, arm);
    return flat(
        [&](float u, float v) { return std::min(sdRound(u, v, 0.86f, arm, corner), sdRound(u, v, arm, 0.86f, corner)); },
        x, y, px, 0.0f, 0.0f);
}

ShapeSample crossEdited(float x, float y, float px, const ShapeEdit& e) {
    const float arm = bent(e.thickness, 0.05f, 0.2f, 0.36f);
    const float corner = bent(e.round, 0.0f, 0.06f, arm);
    return flat(
        [&](float u, float v) {
            const P q = turned(u, v, 45.0f);
            return std::min(sdRound(q.x, q.y, 0.92f, arm, corner), sdRound(q.x, q.y, arm, 0.92f, corner));
        },
        x, y, px);
}

// ---- rings

ShapeSample ringEdited(float x, float y, float px, const ShapeEdit& e) {
    // The outside stays where it is; a thicker ring grows inward, to a disc.
    const float half = bent(e.thickness, 0.012f, 0.055f, 0.46f);
    ShapeSample s;
    s.cover = edge(std::fabs(len(x, y) - (0.905f - half)) - half, px);
    return s;
}

ShapeSample doubleringEdited(float x, float y, float px, const ShapeEdit& e) {
    const float outer = bent(e.thickness, 0.0125f, 0.05f, 0.1f);
    const float inner = bent(e.thickness, 0.007f, 0.028f, 0.07f);
    const float r = len(x, y);
    ShapeSample s;
    s.cover = std::max(edge(std::fabs(r - (0.91f - outer)) - outer, px), edge(std::fabs(r - 0.62f) - inner, px));
    return s;
}

ShapeSample dashringEdited(float x, float y, float px, const ShapeEdit& e) {
    const int n = countOr(e, 10, 3, 24);
    const float half = bent(e.thickness, 0.0175f, 0.07f, 0.3f);
    const float sector = 360.0f / static_cast<float>(n);
    const float r = len(x, y);
    const float a = angleOf(x, y) / sector;
    const float off = std::fabs(a - std::floor(a) - 0.5f);  // 0 mid-dash, 0.5 mid-gap
    const float along = (0.34f - off) * sector * kPi / 180.0f * r;
    ShapeSample s;
    s.cover = edge(std::fabs(r - (0.87f - half)) - half, px) * edge(-along, px);
    return s;
}

ShapeSample arcEdited(float x, float y, float px, const ShapeEdit& e) {
    const float half = bent(e.thickness, 0.025f, 0.1f, 0.16f);
    return flat([&](float u, float v) { return sdArc(u, v + 0.45f, 0.9f, 30.0f, 150.0f, half, half); }, x, y, px,
                0.0f, 0.0f);
}

// ---- stars

struct Star {
    int points;
    float reach;   // from the middle to the tip of a point
    float inner;   // how far out the notches are, as a share of the points
    float corner;  // how round the points and notches are
    float drop;    // how far below the middle of the square it is drawn
};

struct StarNow {
    int n;
    float r, inner, corner, drop;
};

StarNow starNow(const Star& own, const ShapeEdit& e, float roundest = 0.3f) {
    StarNow now;
    now.n = countOr(e, own.points, 3, 16);
    // With its notches this far out a star is a plain figure with straight
    // sides. A star given more or fewer points keeps its character: its
    // notches the same share of the way there.
    const float flatAt = std::cos(kPi / static_cast<float>(now.n));
    const float ownInner = now.n == own.points
                               ? own.inner
                               : flatAt * own.inner / std::cos(kPi / static_cast<float>(own.points));
    now.inner = bent(e.sharpness, ownInner + (flatAt - ownInner) * 0.92f, ownInner, std::max(0.08f, ownInner * 0.3f));
    now.corner = bent(e.round, 0.0f, own.corner, std::max(own.corner, roundest));
    now.r = own.reach - now.corner;
    const float ownR = std::max(own.reach - own.corner, 1e-3f);
    if (now.n == own.points) {
        now.drop = own.drop * now.r / ownR;
    } else {
        now.drop = now.n % 2 == 1 ? 0.25f * now.r * (1.0f - flatAt) : 0.0f;
    }
    return now;
}

float starD(float u, float v, const StarNow& now, float scale = 1.0f, float cornerScale = 1.0f) {
    return sdStar(u, v + now.drop, now.r * scale, now.n, now.inner, 90.0f) - now.corner * cornerScale;
}

// A star with a smaller, lighter star inside it.
ShapeSample starInStar(float x, float y, float px, const StarNow& now, float innerScale, float innerCorner,
                       float innerUp, float light) {
    ShapeSample s;
    s.cover = edge(starD(x, y, now), px);
    s.tone = light * edge(sdStar(x, y + now.drop + innerUp, now.r * innerScale, now.n, now.inner, 90.0f) - innerCorner, px);
    return s;
}

ShapeSample starEdited(float x, float y, float px, const ShapeEdit& e) {
    const StarNow now = starNow(Star{5, 0.92f, 0.45f, 0.10f, 0.0f}, e);
    return starInStar(x, y, px, now, 0.46f / 0.82f, 0.6f * now.corner, 0.02f, 1.0f);
}

ShapeSample starfourEdited(float x, float y, float px, const ShapeEdit& e) {
    const StarNow now = starNow(Star{4, 0.95f, 0.3f, 0.0f, 0.0f}, e);
    return starInStar(x, y, px, now, 0.52f / 0.95f, 0.55f * now.corner, 0.0f, 0.8f);
}

ShapeSample starsixEdited(float x, float y, float px, const ShapeEdit& e) {
    const StarNow now = starNow(Star{6, 0.95f, 0.5f, 0.03f, 0.0f}, e);
    return starInStar(x, y, px, now, 0.5f / 0.92f, now.corner * 2.0f / 3.0f, 0.0f, 0.8f);
}

ShapeSample stareightEdited(float x, float y, float px, const ShapeEdit& e) {
    const StarNow now = starNow(Star{8, 0.95f, 0.58f, 0.02f, 0.0f}, e);
    return starInStar(x, y, px, now, 0.52f / 0.93f, now.corner, 0.0f, 0.8f);
}

ShapeSample starfatEdited(float x, float y, float px, const ShapeEdit& e) {
    const StarNow now = starNow(Star{5, 0.88f, 0.6f, 0.14f, 0.03f}, e, 0.36f);
    ShapeSample s = flat([&](float u, float v) { return starD(u, v, now); }, x, y, px, 0.0f, 0.5f, 0.12f);
    const float spot = gloss(x, y, -0.22f, 0.3f, 0.2f, 0.09f, px) * s.cover;
    s.tone += 0.3f * spot;
    s.shine = 0.9f * spot;
    return s;
}

ShapeSample starlineEdited(float x, float y, float px, const ShapeEdit& e) {
    // The outside stays where it is; a thicker line grows inward.
    const float half = bent(e.thickness, 0.014f, 0.055f, 0.2f);
    const StarNow now = starNow(Star{5, 0.895f - half, 0.45f, 0.0f, 0.03f}, e);
    ShapeSample s;
    s.cover = edge(std::fabs(starD(x, y, now)) - half, px);
    return s;
}

// ---- things counted

ShapeSample sunburstEdited(float x, float y, float px, const ShapeEdit& e) {
    const int n = countOr(e, 12, 5, 24);
    const float wide = std::min(0.105f * 12.0f / static_cast<float>(n), 0.2f);
    const P ray[3] = {{0.52f, -wide}, {0.95f, 0.0f}, {0.52f, wide}};
    const P q = fold(x, y, n, 90.0f, false);
    const float r = len(x, y);
    ShapeSample s;
    s.cover = std::max(edge(r - 0.42f, px), edge(sdPoly(q.x, q.y, ray, 3), px));
    s.tone = 0.7f * edge(r - 0.3f, px);
    return s;
}

ShapeSample flowerEdited(float x, float y, float px, const ShapeEdit& e) {
    const int n = countOr(e, 5, 3, 12);
    const float wide = std::min(0.3f * 5.0f / static_cast<float>(n), 0.36f);
    const P q = fold(x, y, n, 90.0f, true);
    const float r = len(x, y);
    ShapeSample s;
    const float petals = sdEllipse(q.y, q.x - 0.54f, wide, 0.4f);
    s.cover = std::max(edge(petals, px), edge(r - 0.24f, px));
    const float rim = 1.0f - edge(sdEllipse(q.y, q.x - 0.5f, wide, 0.4f) + 0.05f, px * 1.5f);
    s.tone = -0.3f * rim * edge(petals, px);
    const float heart = edge(r - 0.24f, px);
    s.tone = mix(s.tone, 0.85f, heart);
    s.shine = 0.45f * heart;
    return s;
}

ShapeSample gearEdited(float x, float y, float px, const ShapeEdit& e) {
    const int n = countOr(e, 8, 5, 16);
    const float tall = std::clamp(0.13f * 8.0f / static_cast<float>(n), 0.05f, 0.2f);
    const float corner = std::min(0.03f, tall * 0.4f);
    return flat(
        [&](float u, float v) {
            const P q = fold(u, v, n, 90.0f, true);
            const float teeth = sdRound(q.x - 0.74f, q.y, 0.18f, tall, corner);
            return std::max(std::min(len(u, v) - 0.66f, teeth), -(len(u, v) - 0.26f));
        },
        x, y, px, 0.4f, 0.4f, 0.07f);
}

// ---- the second set (0.15)

ShapeSample cloverEdited(float x, float y, float px, const ShapeEdit& e) {
    const int n = countOr(e, 4, 3, 8);
    // An odd number of leaves has one straight up; an even number sits
    // either side of the stem, none down over it. More leaves are narrower.
    const float toward = n % 2 == 1 ? 90.0f : 90.0f - 180.0f / static_cast<float>(n);
    P q = fold(x, y - 0.08f, n, toward, true);
    q.y *= static_cast<float>(n) / 4.0f;
    ShapeSample s;
    const float leaves = std::min(len(q.x - 0.5f, q.y - 0.2f) - 0.3f, len(q.x - 0.3f, q.y) - 0.26f);
    const float stem = sdTaper(x, y, 0.0f, -0.1f, 0.12f, -0.95f, 0.06f, 0.04f);
    s.cover = edge(std::min(leaves, stem), px);
    const float inside = edge(leaves, px);
    s.tone = inside * (0.3f * edge(len(q.x - 0.5f, q.y - 0.2f) - 0.2f, px) -
                       0.5f * edge(sdSeg(q.x, q.y, 0.12f, 0.0f, 0.62f, 0.0f) - 0.018f, px));
    return s;
}

ShapeSample snowflakeEdited(float x, float y, float px, const ShapeEdit& e) {
    const int n = countOr(e, 6, 3, 12);
    const float w = bent(e.thickness, 0.4f, 1.0f, 2.2f);
    const P q = fold(x, y, n, 90.0f, true);
    float d = sdSeg(q.x, q.y, 0.0f, 0.0f, 0.9f - std::max(0.0f, 0.045f * (w - 1.0f)), 0.0f) - 0.045f * w;
    d = std::min(d, sdSeg(q.x, q.y, 0.56f, 0.0f, 0.76f, 0.2f) - 0.04f * w);
    d = std::min(d, sdSeg(q.x, q.y, 0.32f, 0.0f, 0.48f, 0.16f) - 0.04f * w);
    d = std::min(d, sdNgon(x, y, 0.17f, n, 90.0f));
    ShapeSample s;
    s.cover = std::max(edge(d, px), 0.22f * sq(clamp01(1.0f - (x * x + y * y) / sq(0.95f))));
    s.shine = 0.6f * edge(d, px);
    return s;
}

ShapeSample pinwheelEdited(float x, float y, float px, const ShapeEdit& e) {
    const int n = countOr(e, 4, 2, 8);
    const float sector = 360.0f / static_cast<float>(n);
    const float tipAt = 0.5f * sector * kPi / 180.0f, litAt = 18.43f * sector / 90.0f * kPi / 180.0f;
    const P blade[3] = {{0.0f, 0.0f}, {0.92f, 0.0f}, {0.6505f * std::cos(tipAt), 0.6505f * std::sin(tipAt)}};
    const P lit[3] = {{0.0f, 0.0f}, {0.92f, 0.0f}, {0.6325f * std::cos(litAt), 0.6325f * std::sin(litAt)}};
    const float a = angleOf(x, y);
    const float within = (a - std::floor(a / sector) * sector) * kPi / 180.0f;
    const float r = len(x, y);
    const float qx = r * std::cos(within), qy = r * std::sin(within);
    ShapeSample s;
    s.cover = edge(sdPoly(qx, qy, blade, 3), px);
    s.tone = 0.6f * edge(sdPoly(qx, qy, lit, 3), px) - 0.3f * (1.0f - edge(sdPoly(qx, qy, lit, 3), px));
    const float pin = edge(r - 0.1f, px);
    s.cover = std::max(s.cover, pin);
    s.shine = 0.8f * pin;
    return s;
}

ShapeSample crownEdited(float x, float y, float px, const ShapeEdit& e) {
    const int n = countOr(e, 3, 3, 9);
    // The points spread evenly across the brow, tallest in the middle, each
    // with a ball on top; the dips between them all at one height.
    P band[2 + 9 + 8];
    int k = 0;
    band[k++] = P{-0.78f, -0.6f};
    band[k++] = P{0.78f, -0.6f};
    const auto tipX = [&](int i) { return 0.88f - 1.76f * static_cast<float>(i) / static_cast<float>(n - 1); };
    const auto tipY = [](float tx) { return 0.3f + 0.26f * (1.0f - sq(tx / 0.88f)); };
    for (int i = 0; i < n; ++i) {
        band[k++] = P{tipX(i), tipY(tipX(i))};
        if (i + 1 < n) {
            band[k++] = P{0.5f * (tipX(i) + tipX(i + 1)), -0.08f};
        }
    }
    const float small = std::min(1.0f, 4.0f / static_cast<float>(n + 1));
    const auto shapeD = [&](float u, float v) {
        float d = sdPoly(u, v, band, k) - 0.03f;
        for (int i = 0; i < n; ++i) {
            const float tx = tipX(i), middle = 1.0f - sq(tx / 0.88f);
            d = std::min(d, len(u - tx, v - tipY(tx) - 0.08f - 0.02f * middle) - (0.1f + 0.01f * middle) * small);
        }
        return d;
    };
    ShapeSample s = flat(shapeD, x, y, px, 0.4f, 0.4f, 0.08f);
    const float band2 = edge(std::fabs(y + 0.42f) - 0.1f, px) * s.cover;
    float jewels = len(x, y + 0.42f) - 0.065f;
    jewels = std::min(jewels, len(std::fabs(x) - 0.42f, y + 0.42f) - 0.055f);
    const float jewel = edge(jewels, px);
    s.tone = mix(s.tone, -0.4f, band2);
    s.tone = mix(s.tone, 0.8f, jewel);
    s.shine = 0.8f * jewel;
    return s;
}

float powEditedD(float x, float y, int n, float dipAt) {
    static const float reach[12] = {0.95f, 0.74f, 0.9f, 0.7f, 0.93f, 0.78f, 0.86f, 0.72f, 0.95f, 0.76f, 0.88f, 0.7f};
    P outline[40];
    const float sector = 360.0f / static_cast<float>(n);
    for (int i = 0; i < n; ++i) {
        const float tip = (static_cast<float>(i) * sector + 8.0f) * kPi / 180.0f;
        const float dip = tip + 0.5f * sector * kPi / 180.0f;
        outline[2 * i] = P{reach[i % 12] * std::cos(tip), reach[i % 12] * std::sin(tip)};
        outline[2 * i + 1] = P{dipAt * std::cos(dip), dipAt * std::sin(dip)};
    }
    return sdPoly(x, y, outline, 2 * n);
}

ShapeSample powEdited(float x, float y, float px, const ShapeEdit& e) {
    const int n = countOr(e, 12, 6, 20);
    const float dip = bent(e.sharpness, 0.7f, 0.48f, 0.28f);
    ShapeSample s;
    s.cover = edge(powEditedD(x, y, n, dip), px);
    s.tone = 0.8f * edge(powEditedD(x / 0.62f, y / 0.62f, n, dip) * 0.62f, px);
    return s;
}

ShapeSample bangEdited(float x, float y, float px, const ShapeEdit& e) {
    const int n = countOr(e, 14, 6, 24);
    const float half = bent(e.thickness, 0.015f, 0.05f, 0.12f);
    const float inner = bent(e.sharpness, 0.9f, 0.8f, 0.55f);
    ShapeSample s;
    s.cover = edge(std::fabs(sdStar(x, y, 0.95f - half, n, inner, 90.0f)) - half, px);
    return s;
}

ShapeSample pentacleEdited(float x, float y, float px, const ShapeEdit& e) {
    const int n = countOr(e, 5, 5, 12);
    const float r = len(x, y);
    const auto line = [&](float distance, float half) { return edge(std::fabs(distance) - half, px); };
    float lines = std::max(line(r - 0.9f, 0.032f), line(r - 0.78f, 0.012f));
    // Each corner joined to the next but one.
    float chords = 1e9f;
    const float step = 360.0f / static_cast<float>(n);
    for (int i = 0; i < n; ++i) {
        const float a = (90.0f + step * static_cast<float>(i)) * kPi / 180.0f;
        const float b = (90.0f + step * static_cast<float>(i + 2)) * kPi / 180.0f;
        chords = std::min(chords, sdSeg(x, y, 0.77f * std::cos(a), 0.77f * std::sin(a), 0.77f * std::cos(b),
                                        0.77f * std::sin(b)));
    }
    lines = std::max(lines, edge(chords - 0.016f, px));
    return inked(lines, 0.14f * sq(clamp01(1.0f - r * r / sq(0.93f))));
}

ShapeSample speedlinesEdited(float x, float y, float px, const ShapeEdit& e) {
    static const float starts[20] = {0.42f, 0.6f, 0.5f, 0.66f, 0.46f, 0.58f, 0.4f, 0.68f, 0.52f, 0.62f,
                                     0.44f, 0.64f, 0.5f, 0.7f,  0.42f, 0.56f, 0.48f, 0.66f, 0.54f, 0.6f};
    static const float ends[20] = {0.95f, 0.86f, 0.92f, 0.95f, 0.8f,  0.94f, 0.88f, 0.95f, 0.84f, 0.93f,
                                   0.9f,  0.95f, 0.82f, 0.94f, 0.95f, 0.86f, 0.92f, 0.9f,  0.95f, 0.84f};
    const int n = countOr(e, 20, 6, 31);
    const float half = bent(e.thickness, 0.012f, 0.034f, 0.07f);
    int k = 0;
    const P q = fold(x, y, n, 180.0f / static_cast<float>(n), false, &k);
    k = ((k % 20) + 20) % 20;
    ShapeSample s;
    s.cover = edge(sdTaper(q.x, q.y, starts[k], 0.0f, ends[k] - std::max(0.0f, half - 0.034f), 0.0f, half, half * 0.18f), px);
    return s;
}

ShapeSample impactEdited(float x, float y, float px, const ShapeEdit& e) {
    const int n = countOr(e, 12, 6, 24);
    const float half = bent(e.thickness, 0.02f, 0.06f, 0.12f);
    int k = 0;
    const P q = fold(x, y, n, 90.0f, false, &k);
    const bool isLong = k % 2 == 0;
    ShapeSample s;
    s.cover = edge(sdTaper(q.x, q.y, isLong ? 0.4f : 0.56f, 0.0f, (isLong ? 0.95f : 0.82f) - std::max(0.0f, half - 0.06f), 0.0f,
                           half, half * 0.2f),
                   px);
    return s;
}

ShapeSample clawEdited(float x, float y, float px, const ShapeEdit& e) {
    const int n = countOr(e, 3, 1, 5);
    // Marks close together are drawn thinner, so they stay apart.
    const float spacing = n > 1 ? 0.92f / static_cast<float>(n - 1) : 1.0f;
    const float wide = std::min(bent(e.thickness, 0.04f, 0.105f, 0.17f), 0.35f * spacing);
    float d = 1e9f;
    for (int i = 0; i < n; ++i) {
        const float mx = n == 1 ? 0.0f : -0.46f + 0.92f * static_cast<float>(i) / static_cast<float>(n - 1);
        const float my = -0.1087f * mx;
        const float length = 0.92f - 0.18f * sq(mx / 0.46f);
        const P q = turned(x - mx, y - my, 18.0f);
        d = std::min(d, sdVesica(q.x, q.y, length, wide));
    }
    ShapeSample s;
    s.cover = edge(d, px);
    s.tone = 0.5f * edge(d + 0.045f * wide / 0.105f, px);
    return s;
}

ShapeSample rippleEdited(float x, float y, float px, const ShapeEdit& e) {
    const int n = countOr(e, 3, 1, 6);
    const float w = bent(e.thickness, 0.35f, 1.0f, 2.2f);
    const float r = len(x, y);
    const float soft = std::max(px, 0.03f);
    const float step = n > 1 ? 0.6f / static_cast<float>(n - 1) : 0.0f;
    float cover = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float at = 0.88f - step * static_cast<float>(i);
        const float half = (i == 0 ? 0.03f : 0.04f) * w;
        const float strength = std::max(0.25f, 1.0f - 0.2f * static_cast<float>(i));
        cover = std::max(cover, strength * edge(std::fabs(r - at) - half, soft));
    }
    ShapeSample s;
    s.cover = cover;
    return s;
}

ShapeSample zigzagEdited(float x, float y, float px, const ShapeEdit& e) {
    const int n = countOr(e, 3, 1, 9);
    const float w = bent(e.thickness, 0.35f, 1.0f, 2.2f);
    static const P own[5] = {{-0.92f, 0.16f}, {-0.36f, -0.26f}, {0.04f, 0.3f}, {0.42f, -0.22f}, {0.92f, 0.1f}};
    P line[11];
    int count = 0;
    if (n == 3) {
        for (const P& p : own) {
            line[count++] = p;
        }
    } else {
        for (int i = 0; i < n + 2; ++i) {
            const float lx = -0.92f + 1.84f * static_cast<float>(i) / static_cast<float>(n + 1);
            const float ly = i == 0 ? 0.16f : i == n + 1 ? 0.1f : (i % 2 == 1 ? -0.26f : 0.3f);
            line[count++] = P{lx, ly};
        }
    }
    const float thin = 1.0f - std::pow(std::fabs(x), 5.0f);
    return glowing(sdLine(x, y, line, count), 0.06f * thin * w, px, 0.3f, 0.6f * thin);
}

ShapeSample spiralEdited(float x, float y, float px, const ShapeEdit& e) {
    const int n = countOr(e, 3, 1, 6);
    const float w = bent(e.thickness, 0.35f, 1.0f, 2.0f);
    const float r = len(x, y);
    const float pitch = 0.34f * 3.0f / static_cast<float>(n);
    const float turns = std::atan2(y, x) / kTau;
    const float t = r / pitch - turns;
    const float nearest = std::floor(t + 0.5f);
    const float d = std::fabs(t - nearest) * pitch;
    const float ends = edge(r - 0.92f, px) * (nearest >= 0.0f && nearest <= static_cast<float>(n - 1) ? 1.0f : 0.0f);
    const float half = std::min(0.05f * w, 0.42f * pitch) * (1.0f - 0.45f * r);
    ShapeSample s = glowing(d, half, px, std::min(0.16f, 0.45f * pitch), 0.35f);
    s.cover *= ends;
    s.shine *= ends;
    return s;
}

ShapeSample portalringEdited(float x, float y, float px, const ShapeEdit& e) {
    const int n = countOr(e, 3, 2, 8);
    const float half = bent(e.thickness, 0.03f, 0.09f, 0.16f);
    const float at = 0.89f - half;
    const float r = len(x, y);
    const float sector = 360.0f / static_cast<float>(n);
    const float a = angleOf(x, y) / sector;
    const float off = std::fabs(a - std::floor(a) - 0.5f);
    const float along = (0.42f - off) * sector * kPi / 180.0f * r;
    const float heavy = edge(std::fabs(r - at) - half, px) * edge(-along, px);
    const float fine = edge(std::fabs(r - std::min(0.6f, at - half - 0.06f)) - 0.014f, px);
    ShapeSample s;
    s.cover = std::max({heavy, fine, 0.12f * sq(clamp01(1.0f - r * r / sq(0.9f)))});
    s.shine = 0.6f * heavy * edge(std::fabs(r - (at - 0.06f * half / 0.09f)) - 0.025f * half / 0.09f, px);
    s.tone = 0.3f * heavy;
    return s;
}

ShapeSample softringEdited(float x, float y, float, const ShapeEdit& e) {
    const float half = bent(e.thickness, 0.08f, 0.27f, 0.47f);
    ShapeSample s;
    s.cover = sq(clamp01(1.0f - std::fabs(len(x, y) - (0.95f - half)) / half));
    return s;
}

ShapeSample haloEdited(float x, float y, float, const ShapeEdit& e) {
    const float half = bent(e.thickness, 0.05f, 0.16f, 0.3f);
    const float at = 0.92f - half;
    const float r = len(x, y);
    ShapeSample s;
    s.cover = clamp01(sq(clamp01(1.0f - std::fabs(r - at) / half)) + 0.14f * clamp01((at - r) / 0.1f));
    s.shine = 0.5f * sq(clamp01(1.0f - std::fabs(r - at) / (0.05f * half / 0.16f)));
    return s;
}

ShapeSample donutEdited(float x, float y, float px, const ShapeEdit& e) {
    const float half = bent(e.thickness, 0.12f, 0.34f, 0.45f);
    const float at = 0.9f - half;
    const float share = half / 0.34f;
    const float r = len(x, y);
    ShapeSample s = flat([&](float u, float v) { return std::fabs(len(u, v) - at) - half; }, x, y, px, 0.0f, 0.4f,
                         0.1f);
    const float drip = 0.05f * share * std::cos(9.0f * std::atan2(y, x));
    const float icing = edge(std::fabs(r - (at - 0.01f)) - (0.22f * share + drip), px);
    static const float sprinkles[7][3] = {{0.5f, 0.16f, 20.0f},  {0.2f, 0.52f, 110.0f},  {-0.34f, 0.44f, 60.0f},
                                          {-0.56f, -0.04f, 150.0f}, {-0.3f, -0.5f, 30.0f}, {0.2f, -0.54f, 80.0f},
                                          {0.56f, -0.24f, 130.0f}};
    float bits = 1e9f;
    for (const auto& b : sprinkles) {
        const float a = b[2] * kPi / 180.0f;
        bits = std::min(bits, sdSeg(x, y, b[0] - 0.05f * std::cos(a), b[1] - 0.05f * std::sin(a),
                                    b[0] + 0.05f * std::cos(a), b[1] + 0.05f * std::sin(a)) - 0.022f);
    }
    const float sprinkle = edge(bits, px);
    s.tone = mix(s.tone, 0.55f, icing * s.cover);
    s.shine = 0.85f * sprinkle * s.cover * icing;
    return s;
}

ShapeSample crosshairEdited(float x, float y, float px, const ShapeEdit& e) {
    const float half = bent(e.thickness, 0.015f, 0.04f, 0.1f);
    const float r = len(x, y);
    const P q = fold(x, y, 4, 0.0f, true);
    float d = std::fabs(r - 0.66f) - half;
    d = std::min(d, sdSeg(q.x, q.y, 0.46f, 0.0f, 0.98f - half, 0.0f) - half);
    d = std::min(d, r - 0.07f);
    ShapeSample s;
    s.cover = edge(d, px);
    return s;
}

ShapeSample smokeringEdited(float x, float y, float px, const ShapeEdit& e) {
    const int n = countOr(e, 7, 3, 16);
    const float half = bent(e.thickness, 0.06f, 0.2f, 0.38f);
    const float at = 0.86f - half;
    return flat(
        [&](float u, float v) {
            const float bumps = 0.035f * std::cos(static_cast<float>(n) * std::atan2(v, u));
            return std::fabs(len(u, v) - at) - (half + bumps);
        },
        x, y, px, 0.45f, 0.5f, 0.09f);
}

ShapeSample glowbarEdited(float x, float y, float, const ShapeEdit& e) {
    const float half = bent(e.thickness, 0.08f, 0.24f, 0.6f);
    const float ends = smooth(0.96f, 0.5f, std::fabs(x));
    ShapeSample s;
    s.cover = sq(clamp01(1.0f - std::fabs(y) / half)) * ends;
    s.shine = 0.85f * clamp01(1.0f - std::fabs(y) / (0.06f * half / 0.24f)) * ends;
    return s;
}

ShapeSample wedgeEdited(float x, float y, float px, const ShapeEdit& e) {
    const float half = bent(e.sharpness, 36.0f, 27.0f, 8.0f);
    return flat([&](float u, float v) { return sdPie(u, v + 0.8f, half, 1.56f) - 0.04f; }, x, y, px, 0.0f, 0.0f);
}

ShapeSample diamondEdited(float x, float y, float px, const ShapeEdit& e) {
    // The same sums as the diamond in Shapes.cpp, with its width and the
    // roundness of its corners as settings. It keeps its height.
    const float w = bent(e.sharpness, 0.86f, 0.66f, 0.3f), h = 0.92f;
    const float k = 1.0f / std::sqrt(1.0f / (w * w) + 1.0f / (h * h));  // from the middle to a side
    float d = (std::fabs(x) / w + std::fabs(y) / h - 1.0f) * k;
    if (e.round > 0.0f) {
        // Corners taken off, sides where they were: a smaller diamond, grown.
        const float corner = e.round * 0.999f * k;
        const float scale = (k - corner) / k;
        const P outline[4] = {{0.0f, h * scale}, {w * scale, 0.0f}, {0.0f, -h * scale}, {-w * scale, 0.0f}};
        d = sdPoly(x, y, outline, 4) - corner;
    }
    ShapeSample s;
    s.cover = edge(d, px);
    s.tone = clamp01((y - x) * 4.0f) * 0.8f * edge((std::fabs(x) / w + std::fabs(y) / h - 0.72f) * k, px);
    return s;
}

using DrawEdited = ShapeSample (*)(float x, float y, float px, const ShapeEdit& e);

struct Editable {
    const char* name;
    ShapeEditInfo info;  // countLowest, countHighest, countOwn, countLabel, thickness, sharpness, round, sharperToo
    DrawEdited draw;
};

const std::vector<Editable>& editables() {
    static const std::vector<Editable> list = {
        {"star", {3, 16, 5, "Points", false, true, true, true}, starEdited},
        {"ring", {0, 0, 0, "", true, false, false, false}, ringEdited},
        {"square", {0, 0, 0, "", false, false, true, true}, squareEdited},
        {"triangle", {3, 12, 3, "Sides", false, false, true, false}, polygonEdited<kTriangle>},
        {"pentagon", {3, 12, 5, "Sides", false, false, true, false}, polygonEdited<kPentagon>},
        {"hexagon", {3, 12, 6, "Sides", false, false, true, false}, polygonEdited<kHexagon>},
        {"octagon", {3, 12, 8, "Sides", false, false, true, false}, polygonEdited<kOctagon>},
        {"plus", {0, 0, 0, "", true, false, true, true}, plusEdited},
        {"cross", {0, 0, 0, "", true, false, true, true}, crossEdited},
        {"frame", {0, 0, 0, "", true, false, true, true}, frameEdited},
        {"hexframe", {3, 12, 6, "Sides", true, false, true, false}, hexframeEdited},
        {"arc", {0, 0, 0, "", true, false, false, false}, arcEdited},
        {"dashring", {3, 24, 10, "Dashes", true, false, false, false}, dashringEdited},
        {"doublering", {0, 0, 0, "", true, false, false, false}, doubleringEdited},
        {"starfour", {3, 16, 4, "Points", false, true, true, true}, starfourEdited},
        {"starsix", {3, 16, 6, "Points", false, true, true, true}, starsixEdited},
        {"stareight", {3, 16, 8, "Points", false, true, true, true}, stareightEdited},
        {"starfat", {3, 16, 5, "Points", false, true, true, true}, starfatEdited},
        {"starline", {3, 16, 5, "Points", true, true, true, true}, starlineEdited},
        {"sunburst", {5, 24, 12, "Rays", false, false, false, false}, sunburstEdited},
        {"flower", {3, 12, 5, "Petals", false, false, false, false}, flowerEdited},
        {"gear", {5, 16, 8, "Teeth", false, false, false, false}, gearEdited},
        {"diamond", {0, 0, 0, "", false, true, true, false}, diamondEdited},
        {"clover", {3, 8, 4, "Leaves", false, false, false, false}, cloverEdited},
        {"snowflake", {3, 12, 6, "Arms", true, false, false, false}, snowflakeEdited},
        {"pinwheel", {2, 8, 4, "Blades", false, false, false, false}, pinwheelEdited},
        {"crown", {3, 9, 3, "Points", false, false, false, false}, crownEdited},
        {"pow", {6, 20, 12, "Spikes", false, true, false, false}, powEdited},
        {"bang", {6, 24, 14, "Spikes", true, true, false, false}, bangEdited},
        {"pentacle", {5, 12, 5, "Points", false, false, false, false}, pentacleEdited},
        {"speedlines", {6, 31, 20, "Lines", true, false, false, false}, speedlinesEdited},
        {"impact", {6, 24, 12, "Lines", true, false, false, false}, impactEdited},
        {"claw", {1, 5, 3, "Marks", true, false, false, false}, clawEdited},
        {"ripple", {1, 6, 3, "Rings", true, false, false, false}, rippleEdited},
        {"zigzag", {1, 9, 3, "Bends", true, false, false, false}, zigzagEdited},
        {"spiral", {2, 6, 3, "Turns", true, false, false, false}, spiralEdited},
        {"portalring", {2, 8, 3, "Pieces", true, false, false, false}, portalringEdited},
        {"smokering", {3, 16, 7, "Puffs", true, false, false, false}, smokeringEdited},
        {"softring", {0, 0, 0, "", true, false, false, false}, softringEdited},
        {"halo", {0, 0, 0, "", true, false, false, false}, haloEdited},
        {"donut", {0, 0, 0, "", true, false, false, false}, donutEdited},
        {"crosshair", {0, 0, 0, "", true, false, false, false}, crosshairEdited},
        {"glowbar", {0, 0, 0, "", true, false, false, false}, glowbarEdited},
        {"wedge", {0, 0, 0, "", false, true, false, false}, wedgeEdited},
    };
    return list;
}

// ------------------------------------------------------------ the list

using Draw = ShapeSample (*)(float x, float y, float px);

struct Entry {
    ShapeInfo info;
    Draw draw;  // null for the formula shapes, which sampleShape draws
};

const std::vector<Entry>& entries() {
    static const std::vector<Entry> list = {
        // ---- the formula shapes, in the order of the SpriteShape enum
        {{"soft", "Soft dot", "Basic"}, nullptr},
        {{"disc", "Disc", "Basic"}, nullptr},
        {{"ring", "Ring", "Basic"}, nullptr},
        {{"bubble", "Bubble", "Water and nature"}, nullptr},
        {{"sparkle", "Sparkle", "Stars and sparkles"}, nullptr},
        {{"star", "Star", "Stars and sparkles"}, nullptr},
        {{"smoke", "Smoke", "Smoke and clouds"}, nullptr},
        {{"square", "Square", "Basic"}, nullptr},
        {{"diamond", "Diamond", "Basic"}, nullptr},
        {{"heart", "Heart", "Candy and rewards"}, nullptr},
        {{"streak", "Streak", "Fire and light"}, nullptr},
        {{"flame", "Flame", "Fire and light"}, nullptr},
        {{"puff", "Puff", "Smoke and clouds"}, nullptr},
        {{"burst", "Burst", "Stars and sparkles"}, nullptr},
        {{"crescent", "Crescent", "Hits and debris"}, nullptr},
        {{"orb", "Orb", "Candy and rewards"}, nullptr},
        {{"glint", "Glint", "Stars and sparkles"}, nullptr},
        {{"blaze", "Blaze", "Fire and light"}, nullptr},
        {{"candy", "Candy", "Candy and rewards"}, nullptr},
        {{"shard", "Shard", "Hits and debris"}, nullptr},
        {{"drop", "Drop", "Water and nature"}, nullptr},
        {{"splat", "Splat", "Water and nature"}, nullptr},
        {{"shockwave", "Shockwave", "Magic"}, nullptr},
        {{"twinkle", "Twinkle", "Stars and sparkles"}, nullptr},
        {{"flare", "Flare", "Stars and sparkles"}, nullptr},
        {{"rays", "Rays", "Stars and sparkles"}, nullptr},
        {{"swirl", "Swirl", "Candy and rewards"}, nullptr},
        {{"bean", "Bean", "Candy and rewards"}, nullptr},
        {{"bolt", "Bolt", "Fire and light"}, nullptr},
        {{"starflash", "Star flash", "Stars and sparkles"}, nullptr},
        {{"sliver", "Sliver", "Fire and light"}, nullptr},
        {{"shardring", "Shard ring", "Magic"}, nullptr},
        {{"vortex", "Vortex", "Magic"}, nullptr},
        {{"sigil", "Sigil", "Magic"}, nullptr},
        {{"beam", "Beam", "Fire and light"}, nullptr},

        // ---- the library
        {{"triangle", "Triangle", "Basic"}, triangle},
        {{"pentagon", "Pentagon", "Basic"}, pentagon},
        {{"hexagon", "Hexagon", "Basic"}, hexagon},
        {{"octagon", "Octagon", "Basic"}, octagon},
        {{"oval", "Oval", "Basic"}, oval},
        {{"capsule", "Capsule", "Basic"}, capsule},
        {{"halfdisc", "Half disc", "Basic"}, halfdisc},
        {{"wedge", "Wedge", "Basic"}, wedge},
        {{"plus", "Plus", "Basic"}, plus},
        {{"cross", "Cross", "Basic"}, cross},
        {{"frame", "Frame", "Basic"}, frame},
        {{"hexframe", "Hex frame", "Basic"}, hexframe},
        {{"arc", "Arc", "Basic"}, arc},
        {{"dashring", "Dashed ring", "Basic"}, dashring},
        {{"doublering", "Double ring", "Basic"}, doublering},
        {{"softring", "Soft ring", "Basic"}, softring},

        {{"starfour", "Four-point star", "Stars and sparkles"}, starfour},
        {{"starsix", "Six-point star", "Stars and sparkles"}, starsix},
        {{"stareight", "Eight-point star", "Stars and sparkles"}, stareight},
        {{"starfat", "Plump star", "Stars and sparkles"}, starfat},
        {{"starline", "Star outline", "Stars and sparkles"}, starline},
        {{"pinwheel", "Pinwheel", "Stars and sparkles"}, pinwheel},
        {{"sunburst", "Sunburst", "Stars and sparkles"}, sunburst},
        {{"pow", "Pow", "Stars and sparkles"}, comicPow},
        {{"bang", "Bang", "Stars and sparkles"}, bang},
        {{"speedlines", "Speed lines", "Stars and sparkles"}, speedlines},
        {{"crossflare", "Lens flare", "Stars and sparkles"}, crossflare},
        {{"bokeh", "Bokeh", "Stars and sparkles"}, bokeh},
        {{"glowdiamond", "Glow diamond", "Stars and sparkles"}, glowdiamond},

        {{"cloud", "Cloud", "Smoke and clouds"}, cloud},
        {{"puffball", "Puffball", "Smoke and clouds"}, puffball},
        {{"dustcloud", "Dust cloud", "Smoke and clouds"}, dustcloud},
        {{"wisp", "Wisp", "Smoke and clouds"}, wisp},
        {{"smokering", "Smoke ring", "Smoke and clouds"}, smokering},
        {{"steam", "Steam", "Smoke and clouds"}, steam},

        {{"fireball", "Fireball", "Fire and light"}, fireball},
        {{"ember", "Ember", "Fire and light"}, ember},
        {{"flametongue", "Flame tongue", "Fire and light"}, flametongue},
        {{"spark", "Spark", "Fire and light"}, spark},
        {{"zigzag", "Zigzag", "Fire and light"}, zigzag},
        {{"lightcone", "Light cone", "Fire and light"}, lightcone},
        {{"halo", "Halo", "Fire and light"}, halo},
        {{"glowbar", "Glow bar", "Fire and light"}, glowbar},

        {{"droplet", "Droplet", "Water and nature"}, droplet},
        {{"splash", "Splash", "Water and nature"}, splash},
        {{"ripple", "Ripple", "Water and nature"}, ripple},
        {{"wave", "Wave", "Water and nature"}, wave},
        {{"leaf", "Leaf", "Water and nature"}, leaf},
        {{"petal", "Petal", "Water and nature"}, petal},
        {{"flower", "Flower", "Water and nature"}, flower},
        {{"clover", "Clover", "Water and nature"}, clover},
        {{"snowflake", "Snowflake", "Water and nature"}, snowflake},
        {{"feather", "Feather", "Water and nature"}, feather},
        {{"pebble", "Pebble", "Water and nature"}, pebble},
        {{"raindash", "Rain streak", "Water and nature"}, raindash},

        {{"coin", "Coin", "Candy and rewards"}, coin},
        {{"gem", "Gem", "Candy and rewards"}, gem},
        {{"emerald", "Emerald", "Candy and rewards"}, emerald},
        {{"crystal", "Crystal", "Candy and rewards"}, crystal},
        {{"crown", "Crown", "Candy and rewards"}, crown},
        {{"trophy", "Trophy", "Candy and rewards"}, trophy},
        {{"key", "Key", "Candy and rewards"}, key},
        {{"gift", "Gift", "Candy and rewards"}, gift},
        {{"ribbon", "Ribbon", "Candy and rewards"}, ribbon},
        {{"confetti", "Confetti", "Candy and rewards"}, confetti},
        {{"balloon", "Balloon", "Candy and rewards"}, balloon},
        {{"donut", "Donut", "Candy and rewards"}, donut},
        {{"bonbon", "Bonbon", "Candy and rewards"}, bonbon},
        {{"medal", "Medal", "Candy and rewards"}, medal},
        {{"note", "Music note", "Candy and rewards"}, note},
        {{"cherry", "Cherry", "Candy and rewards"}, cherry},

        {{"pentacle", "Pentacle", "Magic"}, pentacle},
        {{"triad", "Triad", "Magic"}, triad},
        {{"spiral", "Spiral", "Magic"}, spiral},
        {{"hexshield", "Hex shield", "Magic"}, hexshield},
        {{"eye", "Eye", "Magic"}, eye},
        {{"moon", "Moon", "Magic"}, moon},
        {{"wing", "Wing", "Magic"}, wing},
        {{"skull", "Skull", "Magic"}, skull},
        {{"octagram", "Octagram", "Magic"}, octagram},
        {{"portalring", "Portal ring", "Magic"}, portalring},
        {{"crosshair", "Crosshair", "Magic"}, crosshair},
        {{"yinyang", "Yin and yang", "Magic"}, yinyang},
        {{"runea", "Rune: arms", "Magic"}, runea},
        {{"runeb", "Rune: lozenge", "Magic"}, runeb},
        {{"runec", "Rune: stroke", "Magic"}, runec},

        {{"slash", "Slash", "Hits and debris"}, slash},
        {{"claw", "Claw marks", "Hits and debris"}, claw},
        {{"crack", "Crack", "Hits and debris"}, crack},
        {{"impact", "Impact lines", "Hits and debris"}, impact},
        {{"rock", "Rock", "Hits and debris"}, rock},
        {{"splinter", "Splinter", "Hits and debris"}, splinter},
        {{"glass", "Glass shard", "Hits and debris"}, glass},
        {{"gear", "Gear", "Hits and debris"}, gear},
        {{"bone", "Bone", "Hits and debris"}, bone},
        {{"brick", "Brick", "Hits and debris"}, brick},

        {{"arrow", "Arrow", "Symbols"}, arrow},
        {{"chevron", "Chevron", "Symbols"}, chevron},
        {{"check", "Tick", "Symbols"}, check},
        {{"exclaim", "Exclamation", "Symbols"}, exclaim},
        {{"question", "Question", "Symbols"}, question},
        {{"lightning", "Lightning", "Symbols"}, lightning},
        {{"shield", "Shield", "Symbols"}, shield},
        {{"sword", "Sword", "Symbols"}, sword},
        {{"paw", "Paw", "Symbols"}, paw},
        {{"zed", "Sleep", "Symbols"}, zed},
        {{"infinity", "Infinity", "Symbols"}, infinity},
        {{"lock", "Lock", "Symbols"}, lock},
        {{"fish", "Fish", "Water and nature"}, fish},
        {{"holoticks", "Holo ticks", "Sci-fi"}, holoTicks},
        {{"holosegs", "Holo segments", "Sci-fi"}, holoSegs},
        {{"holonodes", "Holo nodes", "Sci-fi"}, holoNodes},
        {{"holodisc", "Holo platform", "Sci-fi"}, holoDisc},
        {{"scandisc", "Scan disc", "Sci-fi"}, scanDisc},
        {{"holocylinder", "Holo cylinder", "Sci-fi"}, holoCylinder},
        {{"helix", "Helix", "Sci-fi"}, helix},
        {{"energyring", "Energy ring", "Sci-fi"}, energyRing},
        {{"slicearc", "Slice arc", "Sci-fi"}, sliceArc},
        {{"swirlarms", "Swirl arms", "Sci-fi"}, swirlArms},
        {{"arcbolt", "Arc bolt", "Sci-fi"}, arcBolt},
        {{"arcjag", "Arc jagged", "Sci-fi"}, arcJag},
        {{"arcfork", "Arc fork", "Sci-fi"}, arcFork},
        {{"reticle", "Reticle", "Sci-fi"}, reticle},
        {{"lockring", "Lock ring", "Sci-fi"}, lockRing},
    };
    return list;
}

constexpr std::uint64_t kShapeTextureBase = 0x5348415000000000ull;  // "SHAP", above the 34 bits that say which

}  // namespace

int shapeCount() { return static_cast<int>(entries().size()); }

const ShapeInfo& shapeInfo(int number) {
    const auto& list = entries();
    const int last = static_cast<int>(list.size()) - 1;
    return list[static_cast<std::size_t>(number < 0 ? 0 : (number > last ? last : number))].info;
}

int shapeNumber(std::string_view name) {
    const auto& list = entries();
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (name == list[i].info.name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const std::vector<std::string>& shapeCategories() {
    static const std::vector<std::string> categories = {
        "Basic",          "Stars and sparkles", "Smoke and clouds", "Fire and light", "Water and nature",
        "Candy and rewards", "Magic",            "Hits and debris",  "Symbols", "Sci-fi"};
    return categories;
}

namespace {

// The shapes that can be changed, by number (null for the rest).
const std::vector<const Editable*>& editableByNumber() {
    static const std::vector<const Editable*> byNumber = [] {
        std::vector<const Editable*> out(static_cast<std::size_t>(shapeCount()), nullptr);
        for (const Editable& e : editables()) {
            const int number = shapeNumber(e.name);
            if (number >= 0) {
                out[static_cast<std::size_t>(number)] = &e;
            }
        }
        return out;
    }();
    return byNumber;
}

const Editable* editableFor(int number) {
    const auto& all = editableByNumber();
    return number >= 0 && number < static_cast<int>(all.size()) ? all[static_cast<std::size_t>(number)] : nullptr;
}

float wholeSteps(float amount) {
    if (!std::isfinite(amount)) {
        return 0.0f;
    }
    return std::round(std::clamp(amount, -1.0f, 1.0f) * static_cast<float>(kShapeEditSteps)) /
           static_cast<float>(kShapeEditSteps);
}

// A change as 26 bits: the count in five, and each amount in seven, where
// 64 stands for none.
std::uint32_t packed(const ShapeEdit& e) {
    const auto amount = [](float v) {
        return static_cast<std::uint32_t>(std::lround(wholeSteps(v) * static_cast<float>(kShapeEditSteps)) + 64);
    };
    return (static_cast<std::uint32_t>(std::clamp(e.count, 0, kShapeEditMaxCount)) << 21) | (amount(e.thickness) << 14) |
           (amount(e.sharpness) << 7) | amount(e.round);
}

ShapeEdit unpacked(std::uint32_t bits) {
    const auto amount = [](std::uint32_t v) {
        const int steps = std::clamp(static_cast<int>(v & 0x7fu) - 64, -kShapeEditSteps, kShapeEditSteps);
        return static_cast<float>(steps) / static_cast<float>(kShapeEditSteps);
    };
    ShapeEdit e;
    e.count = static_cast<int>((bits >> 21) & 0x1fu);
    e.thickness = amount(bits >> 14);
    e.sharpness = amount(bits >> 7);
    e.round = amount(bits);
    return e;
}

}  // namespace

const ShapeEditInfo& shapeEditInfo(int number) {
    static const ShapeEditInfo none{};
    const Editable* editable = editableFor(number);
    return editable ? editable->info : none;
}

ShapeEdit usableEdit(int number, const ShapeEdit& asked) {
    const ShapeEditInfo& info = shapeEditInfo(number);
    ShapeEdit e;
    if (info.countHighest > 0 && asked.count > 0) {
        e.count = std::clamp(asked.count, info.countLowest, info.countHighest);
        if (e.count == info.countOwn) {
            e.count = 0;  // the shape's own count is the shape as drawn
        }
    }
    e.thickness = info.thickness ? wholeSteps(asked.thickness) : 0.0f;
    e.sharpness = info.sharpness ? wholeSteps(asked.sharpness) : 0.0f;
    e.round = info.round ? wholeSteps(asked.round) : 0.0f;
    if (!info.sharperToo && e.round < 0.0f) {
        e.round = 0.0f;
    }
    return e;
}

ShapeSample sampleShapeEdited(int number, const ShapeEdit& asked, float x, float y, float aaX, float aaY) {
    const ShapeEdit e = usableEdit(number, asked);
    const Editable* editable = editableFor(number);
    if (!editable || !e.any()) {
        return sampleShape(static_cast<SpriteShape>(std::clamp(number, 0, shapeCount() - 1)), x, y, aaX, aaY);
    }
    // Nothing is drawn outside the particle's own square.
    if (std::fabs(x) > 1.0f || std::fabs(y) > 1.0f) {
        return ShapeSample{};
    }
    const float px = std::max(std::max(aaX, aaY), 1e-4f);
    ShapeSample s = editable->draw(x, y, px, e);
    s.cover = clamp01(s.cover);
    s.tone = std::clamp(s.tone, -1.0f, 1.0f);
    s.shine = clamp01(s.shine);
    return s;
}

bool drawnFromPicture(int number, const ShapeEdit& edit) {
    return isLibraryShape(number) || (number >= 0 && number < kSpriteShapeCount && usableEdit(number, edit).any());
}

ShapeSample sampleLibraryShape(int number, float x, float y, float aaX, float aaY) {
    const auto& list = entries();
    if (number < kSpriteShapeCount || number >= static_cast<int>(list.size())) {
        return ShapeSample{};
    }
    // Nothing is drawn outside the particle's own square.
    if (std::fabs(x) > 1.0f || std::fabs(y) > 1.0f) {
        return ShapeSample{};
    }
    const float px = std::max(std::max(aaX, aaY), 1e-4f);
    ShapeSample s = list[static_cast<std::size_t>(number)].draw(x, y, px);
    s.cover = clamp01(s.cover);
    s.tone = std::clamp(s.tone, -1.0f, 1.0f);
    s.shine = clamp01(s.shine);
    return s;
}

Image drawShapePicture(int number, int size, const ShapeEdit& edit) {
    Image image;
    size = std::clamp(size, 1, 4096);
    image.width = image.height = size;
    image.rgba.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4u, 0);
    const float pixel = 2.0f / static_cast<float>(size);
    number = std::clamp(number, 0, shapeCount() - 1);
    const auto byte = [](float v) { return static_cast<std::uint8_t>(std::lround(clamp01(v) * 255.0f)); };
    for (int row = 0; row < size; ++row) {
        for (int col = 0; col < size; ++col) {
            const float x = (static_cast<float>(col) + 0.5f) * pixel - 1.0f;
            const float y = 1.0f - (static_cast<float>(row) + 0.5f) * pixel;
            const ShapeSample s = sampleShapeEdited(number, edit, x, y, pixel, pixel);
            std::uint8_t* out = &image.rgba[(static_cast<std::size_t>(row) * static_cast<std::size_t>(size) +
                                             static_cast<std::size_t>(col)) * 4u];
            out[0] = byte(s.tone * 0.5f + 0.5f);
            out[1] = byte(s.shine);
            out[2] = byte(s.cover);
            out[3] = 255;
        }
    }
    return image;
}

std::shared_ptr<const Image> shapePicture(int number, int size, const ShapeEdit& asked) {
    struct Key {
        int number, size;
        std::uint32_t edit;
        bool operator<(const Key& o) const { return std::tie(number, size, edit) < std::tie(o.number, o.size, o.edit); }
        bool operator==(const Key& o) const { return number == o.number && size == o.size && edit == o.edit; }
    };
    static std::mutex guard;
    // A shape as drawn is kept for good. A changed one is one of endlessly
    // many (every stop of a slider is another), so only the last few are.
    static std::map<Key, std::shared_ptr<const Image>> made;
    static std::vector<std::pair<Key, std::shared_ptr<const Image>>> changed;  // newest last
    constexpr std::size_t kChangedKept = 12;

    number = std::clamp(number, 0, shapeCount() - 1);
    size = std::clamp(size, 8, kShapePictureSize);
    const ShapeEdit edit = usableEdit(number, asked);
    const Key key{number, size, packed(edit)};
    {
        const std::lock_guard<std::mutex> lock(guard);
        if (!edit.any()) {
            const auto found = made.find(key);
            if (found != made.end()) {
                return found->second;
            }
        } else {
            for (std::size_t i = 0; i < changed.size(); ++i) {
                if (changed[i].first == key) {
                    auto kept = changed[i];
                    changed.erase(changed.begin() + static_cast<std::ptrdiff_t>(i));
                    changed.push_back(kept);
                    return kept.second;
                }
            }
        }
    }
    // Drawn outside the lock: two threads may both draw the same picture
    // once, and the first to finish is the one that is kept.
    auto drawn = std::make_shared<const Image>(drawShapePicture(number, size, edit));
    const std::lock_guard<std::mutex> lock(guard);
    if (!edit.any()) {
        return made.emplace(key, std::move(drawn)).first->second;
    }
    for (const auto& kept : changed) {
        if (kept.first == key) {
            return kept.second;
        }
    }
    changed.emplace_back(key, drawn);
    if (changed.size() > kChangedKept) {
        changed.erase(changed.begin());
    }
    return drawn;
}

// A shape's picture goes by an ID made of a fixed top part, the shape's
// number in eight bits and the change to it in twenty-six.
Id shapeTextureId(int number, const ShapeEdit& edit) {
    const std::uint64_t shape = static_cast<std::uint64_t>(std::clamp(number, 0, 255));
    return Id{kShapeTextureBase | (shape << 26) | packed(usableEdit(number, edit))};
}

int shapeFromTextureId(Id id, ShapeEdit* edit) {
    if ((id.value & ~((1ull << 34) - 1ull)) != kShapeTextureBase) {
        return -1;
    }
    const int number = static_cast<int>((id.value >> 26) & 0xffull);
    const std::uint32_t bits = static_cast<std::uint32_t>(id.value & ((1ull << 26) - 1ull));
    if (number >= shapeCount()) {
        return -1;
    }
    const ShapeEdit found = usableEdit(number, unpacked(bits));
    if (!isLibraryShape(number) && !found.any()) {
        return -1;  // one of the first thirty-five as drawn: the graphics card's own formula
    }
    if (edit) {
        *edit = found;
    }
    return number;
}

}  // namespace vfx::editor
