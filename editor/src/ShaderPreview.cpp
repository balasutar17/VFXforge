#include "vfx/editor/ShaderPreview.h"

#include <algorithm>
#include <array>
#include <functional>
#include <vector>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <thread>

#include "ShaderMath.h"

// ------------------------------------------------------------------------------------------
// The shader source, compiled as C++. The parameter index constants (CM_speed, HG_color ...)
// come from ShaderParams.def, counted the same way the Unity export counts them.
namespace vfx::editor::hlsl::generated {

thread_local int g_octaves = 3;
thread_local int g_quality = 1;
#define VFX_OCTAVES g_octaves
#define VFX_QUALITY g_quality

enum : int { kCm0 = __COUNTER__ };
#define VFX_COMMON(key, label, tip, mn, mx, df, adv) enum : int { CM_##key = __COUNTER__ - kCm0 - 1 };
#define VFX_FAMILY(CODE) enum : int { CODE##_base = __COUNTER__ };
#define VFX_F(CODE, key, label, tip, mn, mx, df, adv) enum : int { CODE##_##key = __COUNTER__ - CODE##_base - 1 + 8 };
#define VFX_C(CODE, key, label, tip, r, g, b, a) enum : int { CODE##_##key = __COUNTER__ - CODE##_base - 1 + 8 };
#define VFX_CH(CODE, key, label, tip, df, list) enum : int { CODE##_##key = __COUNTER__ - CODE##_base - 1 + 8 };
#include "../shaders/ShaderParams.def"
#undef VFX_COMMON
#undef VFX_FAMILY
#undef VFX_F
#undef VFX_C
#undef VFX_CH

#include "../shaders/ShaderCore.hlsli"

}  // namespace vfx::editor::hlsl::generated

namespace vfx::editor::shader {

namespace {

namespace g = hlsl::generated;

constexpr float kPi = 3.14159265358979f;

// ------------------------------------------------------------ the preview shapes
struct Mesh {
    std::vector<float> p, n, uv;  // three, three and two numbers a corner
    std::vector<std::uint32_t> t;
    bool flat = false;            // a quad seen head-on, not orbited
    std::size_t corners() const { return p.size() / 3; }
};

std::uint32_t corner(Mesh& m, float x, float y, float z, float nx, float ny, float nz, float u, float v) {
    m.p.insert(m.p.end(), {x, y, z});
    m.n.insert(m.n.end(), {nx, ny, nz});
    m.uv.insert(m.uv.end(), {u, v});
    return static_cast<std::uint32_t>(m.corners() - 1);
}

// A flat triangle: its own corners and normal, so the facets show.
void flatTri(Mesh& m, const float a[3], const float b[3], const float c[3]) {
    const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    const float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    const float l = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (l > 1e-12f) {
        nx /= l;
        ny /= l;
        nz /= l;
    }
    const std::uint32_t i0 = corner(m, a[0], a[1], a[2], nx, ny, nz, a[0] * 0.5f + 0.5f, a[1] * 0.5f + 0.5f);
    const std::uint32_t i1 = corner(m, b[0], b[1], b[2], nx, ny, nz, b[0] * 0.5f + 0.5f, b[1] * 0.5f + 0.5f);
    const std::uint32_t i2 = corner(m, c[0], c[1], c[2], nx, ny, nz, c[0] * 0.5f + 0.5f, c[1] * 0.5f + 0.5f);
    m.t.insert(m.t.end(), {i0, i1, i2});
}

Mesh makeSphere() {
    Mesh m;
    const int seg = 56, rings = 36;
    for (int r = 0; r <= rings; ++r) {
        const float v = static_cast<float>(r) / static_cast<float>(rings);
        const float phi = v * kPi;
        for (int s = 0; s <= seg; ++s) {
            const float u = static_cast<float>(s) / static_cast<float>(seg);
            const float th = u * 2.0f * kPi;
            const float x = std::sin(phi) * std::cos(th), y = std::cos(phi), z = std::sin(phi) * std::sin(th);
            corner(m, x, y, z, x, y, z, u, 1.0f - v);
        }
    }
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < seg; ++s) {
            const auto a = static_cast<std::uint32_t>(r * (seg + 1) + s), b = a + 1, c = a + static_cast<std::uint32_t>(seg + 1), d = c + 1;
            m.t.insert(m.t.end(), {a, c, b, b, c, d});
        }
    }
    return m;
}

Mesh makeCube() {
    Mesh m;
    const float h = 0.78f;
    const float f[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const auto& n : f) {
        // Two axes across the face.
        float ax[3], ay[3];
        const int big = std::fabs(n[0]) > 0.5f ? 0 : std::fabs(n[1]) > 0.5f ? 1 : 2;
        for (int i = 0; i < 3; ++i) ax[i] = ay[i] = 0;
        ax[(big + 1) % 3] = 1;
        ay[(big + 2) % 3] = 1;
        // Make (ax, ay, n) right-handed so the winding faces out.
        const float cz = ax[1] * ay[2] - ax[2] * ay[1], cy = ax[2] * ay[0] - ax[0] * ay[2], cx = ax[0] * ay[1] - ax[1] * ay[0];
        if (cx * n[0] + cy * n[1] + cz * n[2] < 0) {
            for (int i = 0; i < 3; ++i) ay[i] = -ay[i];
        }
        std::uint32_t id[4];
        const float uu[4] = {0, 1, 1, 0}, vv[4] = {0, 0, 1, 1};
        for (int k = 0; k < 4; ++k) {
            const float su = uu[k] * 2 - 1, sv = vv[k] * 2 - 1;
            id[k] = corner(m, (n[0] + ax[0] * su + ay[0] * sv) * h, (n[1] + ax[1] * su + ay[1] * sv) * h,
                           (n[2] + ax[2] * su + ay[2] * sv) * h, n[0], n[1], n[2], uu[k], vv[k]);
        }
        m.t.insert(m.t.end(), {id[0], id[1], id[2], id[0], id[2], id[3]});
    }
    return m;
}

Mesh makePlane() {
    Mesh m;
    const auto a = corner(m, -1, -1, 0, 0, 0, 1, 0, 0), b = corner(m, 1, -1, 0, 0, 0, 1, 1, 0), c = corner(m, 1, 1, 0, 0, 0, 1, 1, 1),
               d = corner(m, -1, 1, 0, 0, 0, 1, 0, 1);
    m.t = {a, b, c, a, c, d};
    return m;
}

Mesh makeQuad() {
    Mesh m = makePlane();
    m.flat = true;
    return m;
}

Mesh makeTube() {
    Mesh m;
    const int seg = 72;
    for (int s = 0; s <= seg; ++s) {
        const float u = static_cast<float>(s) / static_cast<float>(seg);
        const float th = u * 2.0f * kPi;
        const float x = std::cos(th), z = std::sin(th);
        corner(m, x * 0.85f, -0.9f, z * 0.85f, x, 0, z, u, 0);
        corner(m, x * 0.85f, 0.9f, z * 0.85f, x, 0, z, u, 1);
    }
    for (int s = 0; s < seg; ++s) {
        const auto a = static_cast<std::uint32_t>(s * 2);
        m.t.insert(m.t.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
    }
    return m;
}

Mesh makeCrystal() {
    Mesh m;
    const int n = 6;
    float lo[n][3], hi[n][3];
    for (int i = 0; i < n; ++i) {
        const float th = static_cast<float>(i) / static_cast<float>(n) * 2.0f * kPi;
        lo[i][0] = std::cos(th) * 0.5f;
        lo[i][1] = -0.45f;
        lo[i][2] = std::sin(th) * 0.5f;
        hi[i][0] = lo[i][0];
        hi[i][1] = 0.45f;
        hi[i][2] = lo[i][2];
    }
    const float top[3] = {0, 1.0f, 0}, bottom[3] = {0, -1.0f, 0};
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        flatTri(m, lo[i], hi[j], hi[i]);
        flatTri(m, lo[i], lo[j], hi[j]);
        flatTri(m, hi[i], hi[j], top);
        flatTri(m, lo[j], lo[i], bottom);
    }
    return m;
}

Mesh makeCone() {
    Mesh m;
    const int seg = 48;
    for (int s = 0; s <= seg; ++s) {
        const float u = static_cast<float>(s) / static_cast<float>(seg);
        const float th = u * 2.0f * kPi;
        const float x = std::cos(th), z = std::sin(th);
        const float nl = std::sqrt(1.0f + 0.2f);
        corner(m, x * 0.45f, -1.0f, z * 0.45f, x / nl, 0.45f / nl, z / nl, u, 0);
        corner(m, 0, 1.0f, 0, x / nl, 0.45f / nl, z / nl, u, 1);
    }
    for (int s = 0; s < seg; ++s) {
        const auto a = static_cast<std::uint32_t>(s * 2);
        m.t.insert(m.t.end(), {a, a + 2, a + 1});
    }
    return m;
}

Mesh makeFromModel(const Model& model) {
    Mesh m;
    const float cx = 0.5f * (model.minX + model.maxX), cy = 0.5f * (model.minY + model.maxY), cz = 0.5f * (model.minZ + model.maxZ);
    const float size = std::max({model.maxX - model.minX, model.maxY - model.minY, model.maxZ - model.minZ, 1e-6f});
    const float s = 1.8f / size;
    for (std::size_t i = 0; i < model.corners(); ++i) {
        const float x = (model.positions[i * 3] - cx) * s, y = (model.positions[i * 3 + 1] - cy) * s, z = (model.positions[i * 3 + 2] - cz) * s;
        const float u = std::atan2(z, x) / (2.0f * kPi) + 0.5f;
        corner(m, x, y, z, model.normals[i * 3], model.normals[i * 3 + 1], model.normals[i * 3 + 2], u, y * 0.5f + 0.5f);
    }
    m.t = model.triangles;
    return m;
}

bool isQuadShape(const std::string& s) {
    return s == "sprite" || s == "particle" || s == "star" || s == "gem" || s == "button" || s == "frame" || s == "badge";
}

Mesh makeShape(const std::string& shape, const Model* model) {
    if (shape == "model" && model && model->triangleCount() > 0) return makeFromModel(*model);
    if (isQuadShape(shape)) return makeQuad();
    if (shape == "cube") return makeCube();
    if (shape == "plane") return makePlane();
    if (shape == "ring") return makeTube();
    if (shape == "crystal") return makeCrystal();
    if (shape == "cone") return makeCone();
    return makeSphere();
}

// ------------------------------------------------------------ sprite stand-ins for the UI family
float sdStar(hlsl::float2 p, float r, float rf) {
    using namespace hlsl;
    using namespace g;
    const float2 k1(0.809016994375f, -0.587785252292f);
    const float2 k2(-k1.x, k1.y);
    p.x = std::fabs(p.x);
    p = p - k1 * (2.0f * std::max(dot(k1, p), 0.0f));
    p = p - k2 * (2.0f * std::max(dot(k2, p), 0.0f));
    p.x = std::fabs(p.x);
    p.y -= r;
    const float2 ba = float2(-k1.y, k1.x) * rf - float2(0, 1);
    const float h = std::clamp(dot(p, ba) / dot(ba, ba), 0.0f, r);
    const float2 d = p - ba * h;
    return length(d) * (p.y * ba.x - p.x * ba.y < 0 ? -1.0f : 1.0f);
}

float sdHexagon(hlsl::float2 p, float r) {
    using namespace hlsl;
    using namespace g;
    const float2 k(-0.866025404f, 0.5f);
    const float kz = 0.577350269f;
    p = abs(p);
    p = p - k * (2.0f * std::min(dot(k, p), 0.0f));
    p = p - float2(std::clamp(p.x, -kz * r, kz * r), r);
    return length(p) * (p.y < 0 ? -1.0f : 1.0f);
}

float sdBox(hlsl::float2 p, hlsl::float2 b, float rad) {
    using namespace hlsl;
    using namespace g;
    const float2 q = abs(p) - b + float2(rad, rad);
    const float ox = std::max(q.x, 0.0f), oy = std::max(q.y, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(q.x, q.y), 0.0f) - rad;
}

// The sprite stand-in's alpha: from the shape, or a rounded border when Shape from is Border.
float spriteAlpha(const std::string& shape, bool border, const g::Params& P, hlsl::float2 uv) {
    using namespace hlsl;
    using namespace g;
    if (border || shape == "frame") {
        if (border) return borderAlpha(uv, P.v[UI_frameWidth].x, P.v[UI_frameRadius].x);
    }
    const float2 p = (uv - float2(0.5f, 0.5f)) * 2.0f;
    float d = 1;
    if (shape == "gem") {
        d = sdHexagon(float2(p.x, p.y), 0.55f) - 0.06f;
    } else if (shape == "button") {
        d = sdBox(p, float2(0.78f, 0.40f), 0.24f);
    } else if (shape == "badge") {
        d = length(p) - 0.62f;
    } else if (shape == "frame") {
        d = sdBox(p, float2(0.78f, 0.78f), 0.2f);
    } else {
        d = sdStar(float2(p.x, p.y + 0.05f), 0.62f, 0.5f) - 0.05f;
    }
    return 1.0f - hlsl::smoothstep(-0.012f, 0.012f, d);
}

// ------------------------------------------------------------ the picture
struct Layer {
    std::vector<float> z;
    std::vector<int> tri;
    std::vector<float> b0, b1;
    void reset(std::size_t n) {
        z.assign(n, std::numeric_limits<float>::infinity());
        tri.assign(n, -1);
        b0.assign(n, 0);
        b1.assign(n, 0);
    }
};

void parallelRows(int rows, const std::function<void(int, int)>& work) {
    const int threads = static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1u, 8u));
    if (threads <= 1 || rows < 16) {
        work(0, rows);
        return;
    }
    std::vector<std::thread> pool;
    const int chunk = (rows + threads - 1) / threads;
    for (int i = 0; i < threads; ++i) {
        const int a = i * chunk, b = std::min(rows, a + chunk);
        if (a >= b) break;
        pool.emplace_back(work, a, b);
    }
    for (auto& t : pool) t.join();
}

float tonemap(float x) { return 1.0f - std::exp(-1.25f * std::max(x, 0.0f)); }

void background(int kind, int x, int y, float out[3]) {
    float v = 0.04f;
    float r = 0.035f, gg = 0.040f, b = 0.050f;
    if (kind == 1) {
        r = gg = b = 0.27f;
    } else if (kind == 2) {
        r = gg = b = 0.82f;
    } else if (kind == 3) {
        v = ((x / 16 + y / 16) % 2 == 0) ? 0.20f : 0.28f;
        r = gg = b = v;
    }
    out[0] = r;
    out[1] = gg;
    out[2] = b;
}

}  // namespace

const std::map<std::string, int>& previewIndices() {
    static const std::map<std::string, int> table = [] {
        std::map<std::string, int> t;
#define VFX_COMMON(key, label, tip, mn, mx, df, adv) t["CM_" #key] = g::CM_##key;
#define VFX_FAMILY(CODE)
#define VFX_F(CODE, key, label, tip, mn, mx, df, adv) t[#CODE "_" #key] = g::CODE##_##key;
#define VFX_C(CODE, key, label, tip, r, gg, b, a) t[#CODE "_" #key] = g::CODE##_##key;
#define VFX_CH(CODE, key, label, tip, df, list) t[#CODE "_" #key] = g::CODE##_##key;
#include "../shaders/ShaderParams.def"
#undef VFX_COMMON
#undef VFX_FAMILY
#undef VFX_F
#undef VFX_C
#undef VFX_CH
        return t;
    }();
    return table;
}

const std::vector<std::string>& previewShapes() {
    static const std::vector<std::string> shapes = {"sphere", "cube", "plane", "ring", "crystal", "cone", "particle", "star", "gem", "button", "frame", "badge"};
    return shapes;
}

Picture renderPreview(const Settings& settings, const PreviewOptions& opt) {
    Picture out;
    const Family* family = findFamily(settings.family);
    if (!family || opt.width <= 0 || opt.height <= 0 || opt.width > 4096 || opt.height > 4096) return out;
    const int W = opt.width, H = opt.height;
    const std::string shapeName = opt.shape.empty() ? settings.shape : opt.shape;
    const Mesh mesh = makeShape(shapeName, opt.model);
    const bool flat = mesh.flat;
    const std::size_t pixels = static_cast<std::size_t>(W) * static_cast<std::size_t>(H);

    // The values at this moment of the timeline, as the keyframes make them.
    const std::vector<std::array<float, 4>> values = valuesAt(settings, animationTime(settings.animation, opt.time));
    g::Params P;
    for (std::size_t i = 0; i < values.size() && i < static_cast<std::size_t>(kMaxParams); ++i) {
        P.v[i] = hlsl::float4(values[i][0], values[i][1], values[i][2], values[i][3]);
    }
    const float speed = P.v[g::CM_speed].x;
    const float shaderTime = static_cast<float>(std::max(opt.time, 0.0)) * speed;

    // The camera.
    const float cp = std::cos(opt.pitch), sp = std::sin(opt.pitch);
    const float dist = 5.6f / std::max(opt.zoom, 0.1f);
    const float ex = std::sin(opt.yaw) * cp * dist, ey = sp * dist, ez = std::cos(opt.yaw) * cp * dist;
    float fx = -ex, fy = -ey, fz = -ez;
    {
        const float l = std::sqrt(fx * fx + fy * fy + fz * fz);
        fx /= l;
        fy /= l;
        fz /= l;
    }
    float rx = fz, ry = 0, rz = -fx;  // forward x up(0,1,0)
    {
        const float l = std::sqrt(rx * rx + rz * rz);
        rx /= l;
        rz /= l;
    }
    const float ux = ry * fz - rz * fy, uy = rz * fx - rx * fz, uz = rx * fy - ry * fx;
    const float focal = 0.5f * static_cast<float>(std::min(W, H)) / std::tan(0.5f * 30.0f * kPi / 180.0f);

    const int ss = 1;
    const int SW = W * ss, SH = H * ss;
    const std::size_t spixels = pixels * static_cast<std::size_t>(ss * ss);
    Layer front, back;
    front.reset(spixels);
    back.reset(spixels);

    // Every corner on the screen.
    const std::size_t nc = mesh.corners();
    std::vector<float> sx(nc), sy(nc), sz(nc);
    std::vector<unsigned char> ok(nc, 1);
    for (std::size_t i = 0; i < nc; ++i) {
        const float px = mesh.p[i * 3], py = mesh.p[i * 3 + 1], pz = mesh.p[i * 3 + 2];
        if (flat) {
            // Head-on and square to the picture, filling it.
            const float half = 0.5f * static_cast<float>(std::min(SW, SH)) * 0.92f / std::max(opt.zoom, 0.1f);
            sx[i] = 0.5f * static_cast<float>(SW) + px * half;
            sy[i] = 0.5f * static_cast<float>(SH) - py * half;
            sz[i] = 1.0f;
        } else {
            const float dx = px - ex, dy = py - ey, dz = pz - ez;
            const float cx = dx * rx + dy * ry + dz * rz, cy = dx * ux + dy * uy + dz * uz, cz = dx * fx + dy * fy + dz * fz;
            if (cz < 0.05f) {
                ok[i] = 0;
                continue;
            }
            sx[i] = 0.5f * static_cast<float>(SW) + cx / cz * focal * static_cast<float>(ss);
            sy[i] = 0.5f * static_cast<float>(SH) - cy / cz * focal * static_cast<float>(ss);
            sz[i] = cz;
        }
    }
    // Rasterise: the nearest front-facing and the nearest back-facing surface at every pixel.
    for (std::size_t t = 0; t + 2 < mesh.t.size(); t += 3) {
        const std::uint32_t a = mesh.t[t], b = mesh.t[t + 1], c = mesh.t[t + 2];
        if (!ok[a] || !ok[b] || !ok[c]) continue;
        bool isFront = true;
        if (!flat) {
            const float mx = (mesh.p[a * 3] + mesh.p[b * 3] + mesh.p[c * 3]) / 3.0f, my = (mesh.p[a * 3 + 1] + mesh.p[b * 3 + 1] + mesh.p[c * 3 + 1]) / 3.0f,
                        mz = (mesh.p[a * 3 + 2] + mesh.p[b * 3 + 2] + mesh.p[c * 3 + 2]) / 3.0f;
            const float nx = mesh.n[a * 3] + mesh.n[b * 3] + mesh.n[c * 3], ny = mesh.n[a * 3 + 1] + mesh.n[b * 3 + 1] + mesh.n[c * 3 + 1],
                        nz = mesh.n[a * 3 + 2] + mesh.n[b * 3 + 2] + mesh.n[c * 3 + 2];
            isFront = nx * (ex - mx) + ny * (ey - my) + nz * (ez - mz) > 0;
        }
        const float area = (sx[b] - sx[a]) * (sy[c] - sy[a]) - (sy[b] - sy[a]) * (sx[c] - sx[a]);
        if (!(std::fabs(area) > 1e-9f)) continue;
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({sx[a], sx[b], sx[c]}))));
        const int x1 = std::min(SW - 1, static_cast<int>(std::ceil(std::max({sx[a], sx[b], sx[c]}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({sy[a], sy[b], sy[c]}))));
        const int y1 = std::min(SH - 1, static_cast<int>(std::ceil(std::max({sy[a], sy[b], sy[c]}))));
        const float inv = 1.0f / area;
        Layer& layer = isFront ? front : back;
        for (int py = y0; py <= y1; ++py) {
            for (int px = x0; px <= x1; ++px) {
                const float fxp = static_cast<float>(px) + 0.5f, fyp = static_cast<float>(py) + 0.5f;
                float w0 = ((sx[b] - fxp) * (sy[c] - fyp) - (sy[b] - fyp) * (sx[c] - fxp)) * inv;
                float w1 = ((sx[c] - fxp) * (sy[a] - fyp) - (sy[c] - fyp) * (sx[a] - fxp)) * inv;
                float w2 = 1.0f - w0 - w1;
                if (w0 < -1e-5f || w1 < -1e-5f || w2 < -1e-5f) continue;
                float z = 1;
                if (!flat) {
                    const float q0 = w0 / sz[a], q1 = w1 / sz[b], q2 = w2 / sz[c];
                    const float s = q0 + q1 + q2;
                    z = 1.0f / s;
                    w0 = q0 * z;
                    w1 = q1 * z;
                }
                const std::size_t at = static_cast<std::size_t>(py) * static_cast<std::size_t>(SW) + static_cast<std::size_t>(px);
                if (z < layer.z[at]) {
                    layer.z[at] = z;
                    layer.tri[at] = static_cast<int>(t / 3);
                    layer.b0[at] = w0;
                    layer.b1[at] = w1;
                }
            }
        }
    }

    // Shade what is visible, and blend: the far surface first, then the near one over it.
    const bool additive = settings.blend == BlendMode::Additive;
    const bool border = family->isUi && P.v[g::UI_frameMode].x > 0.5f;
    const float autoHit = P.v[g::FF_autoHit].x;
    std::vector<float> disp(pixels * 3), hdr(pixels * 3);
    const int quality = std::clamp(settings.quality, 0, 3);
    const int octaves = qualityProfiles()[static_cast<std::size_t>(quality)].octaves;
    const std::string familyCode = family->code;
    const float lifeSeconds = static_cast<float>(std::max(settings.animation.duration, 0.1));

    auto shadeLayer = [&](const Layer& layer, std::size_t at, float rgb[3], float& alpha) -> bool {
        if (layer.tri[at] < 0) return false;
        const std::size_t t = static_cast<std::size_t>(layer.tri[at]) * 3;
        const std::uint32_t a = mesh.t[t], b = mesh.t[t + 1], c = mesh.t[t + 2];
        const float w0 = layer.b0[at], w1 = layer.b1[at], w2 = 1.0f - w0 - w1;
        auto mix3 = [&](const std::vector<float>& v, int k) { return w0 * v[a * 3 + static_cast<std::size_t>(k)] + w1 * v[b * 3 + static_cast<std::size_t>(k)] + w2 * v[c * 3 + static_cast<std::size_t>(k)]; };
        g::ShadeIn in;
        in.pos = hlsl::float3(mix3(mesh.p, 0), mix3(mesh.p, 1), mix3(mesh.p, 2));
        in.nrm = hlsl::normalize(hlsl::float3(mix3(mesh.n, 0), mix3(mesh.n, 1), mix3(mesh.n, 2)));
        in.uv = hlsl::float2(w0 * mesh.uv[a * 2] + w1 * mesh.uv[b * 2] + w2 * mesh.uv[c * 2], w0 * mesh.uv[a * 2 + 1] + w1 * mesh.uv[b * 2 + 1] + w2 * mesh.uv[c * 2 + 1]);
        in.view = flat ? hlsl::float3(0, 0, 1) : hlsl::normalize(hlsl::float3(ex - in.pos.x, ey - in.pos.y, ez - in.pos.z));
        in.t = shaderTime;
        in.life = family->isParticle ? static_cast<float>(std::fmod(std::max(opt.time, 0.0), lifeSeconds)) / lifeSeconds : 0.0f;
        in.hitPos = hlsl::normalize(hlsl::float3(0.55f, 0.55f, 0.62f));
        const float tHit = static_cast<float>(std::max(opt.time, 0.0));
        in.hitAge = opt.hitAge >= 0 ? static_cast<float>(opt.hitAge) : (autoHit > 0.05f ? tHit - autoHit * std::floor(tHit / autoHit) : 1.0e6f);
        in.spriteA = in.spriteOut = in.spriteGlow = 0;
        if (family->isUi) {
            const hlsl::float2 uv = in.uv;
            in.spriteA = spriteAlpha(shapeName, border, P, uv);
            const float ow = P.v[g::UI_outlineWidth].x;
            float mx = 0, glow = 0;
            for (int k = 0; k < 8; ++k) {
                const float ang = static_cast<float>(k) * kPi / 4.0f;
                const hlsl::float2 d(std::cos(ang), std::sin(ang));
                mx = std::max(mx, spriteAlpha(shapeName, border, P, uv + d * ow));
                glow += spriteAlpha(shapeName, border, P, uv + d * 0.05f) + spriteAlpha(shapeName, border, P, uv + d * 0.10f);
            }
            in.spriteOut = mx;
            in.spriteGlow = glow / 16.0f;
        }
        g::ShadeOut o;
        switch (family->number) {
            case 11: o = g::shade_EM(in, P); break;
            case 12: o = g::shade_HG(in, P); break;
            case 14: o = g::shade_FF(in, P); break;
            case 15: o = g::shade_PV(in, P); break;
            case 16: o = g::shade_UI(in, P); break;
            case 20: o = g::shade_LY(in, P); break;
            default: o = g::shade_IC(in, P); break;
        }
        rgb[0] = o.rgb.x;
        rgb[1] = o.rgb.y;
        rgb[2] = o.rgb.z;
        alpha = std::clamp(o.a, 0.0f, 1.0f);
        return true;
    };

    std::vector<float> dispSS(spixels * 3), hdrSS(spixels * 3);
    parallelRows(SH, [&](int r0, int r1) {
        g::g_quality = quality;
        g::g_octaves = octaves;
        for (int y = r0; y < r1; ++y) {
            for (int x = 0; x < SW; ++x) {
                const std::size_t at = static_cast<std::size_t>(y) * static_cast<std::size_t>(SW) + static_cast<std::size_t>(x);
                float d[3];
                background(opt.background, x / ss, y / ss, d);
                float h[3] = {0, 0, 0};
                auto blend = [&](const Layer& layer) {
                    float rgb[3], a = 0;
                    if (!shadeLayer(layer, at, rgb, a)) return;
                    for (int k = 0; k < 3; ++k) {
                        const float tm = tonemap(rgb[k]);
                        if (additive) {
                            d[k] += tm * a;
                            h[k] += rgb[k] * a;
                        } else {
                            d[k] = d[k] * (1.0f - a) + tm * a;
                            h[k] = h[k] * (1.0f - a) + rgb[k] * a;
                        }
                    }
                };
                if (!family->cullOff ? false : true) blend(back);
                blend(front);
                for (int k = 0; k < 3; ++k) {
                    dispSS[at * 3 + static_cast<std::size_t>(k)] = d[k];
                    hdrSS[at * 3 + static_cast<std::size_t>(k)] = h[k];
                }
            }
        }
    });
    // Down to the picture size.
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            for (int k = 0; k < 3; ++k) {
                float d = 0, h = 0;
                for (int j = 0; j < ss; ++j) {
                    for (int i = 0; i < ss; ++i) {
                        const std::size_t at = static_cast<std::size_t>(y * ss + j) * static_cast<std::size_t>(SW) + static_cast<std::size_t>(x * ss + i);
                        d += dispSS[at * 3 + static_cast<std::size_t>(k)];
                        h += hdrSS[at * 3 + static_cast<std::size_t>(k)];
                    }
                }
                const std::size_t o = (static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + static_cast<std::size_t>(x)) * 3 + static_cast<std::size_t>(k);
                disp[o] = d / static_cast<float>(ss * ss);
                hdr[o] = h / static_cast<float>(ss * ss);
            }
        }
    }

    // Bloom: what is brighter than the picture can hold spreads a little light round itself.
    if (opt.bloom) {
        const int bw = std::max(1, W / 4), bh = std::max(1, H / 4);
        std::vector<float> small(static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh) * 3, 0.0f), tmp = small;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const int bx = std::min(bw - 1, x / 4), by = std::min(bh - 1, y / 4);
                for (int k = 0; k < 3; ++k) {
                    const float v = std::max(hdr[(static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + static_cast<std::size_t>(x)) * 3 + static_cast<std::size_t>(k)] - 0.9f, 0.0f);
                    small[(static_cast<std::size_t>(by) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(bx)) * 3 + static_cast<std::size_t>(k)] += v / 16.0f;
                }
            }
        }
        for (int pass = 0; pass < 3; ++pass) {
            for (int dir = 0; dir < 2; ++dir) {
                for (int y = 0; y < bh; ++y) {
                    for (int x = 0; x < bw; ++x) {
                        for (int k = 0; k < 3; ++k) {
                            float sum = 0;
                            for (int d = -3; d <= 3; ++d) {
                                const int xx = std::clamp(dir == 0 ? x + d : x, 0, bw - 1), yy = std::clamp(dir == 1 ? y + d : y, 0, bh - 1);
                                sum += small[(static_cast<std::size_t>(yy) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(xx)) * 3 + static_cast<std::size_t>(k)];
                            }
                            tmp[(static_cast<std::size_t>(y) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(x)) * 3 + static_cast<std::size_t>(k)] = sum / 7.0f;
                        }
                    }
                }
                small.swap(tmp);
            }
        }
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                // bilinear from the small picture
                const float gx = (static_cast<float>(x) + 0.5f) / 4.0f - 0.5f, gy = (static_cast<float>(y) + 0.5f) / 4.0f - 0.5f;
                const int x0 = std::clamp(static_cast<int>(std::floor(gx)), 0, bw - 1), y0 = std::clamp(static_cast<int>(std::floor(gy)), 0, bh - 1);
                const int x1 = std::min(x0 + 1, bw - 1), y1 = std::min(y0 + 1, bh - 1);
                const float fx2 = std::clamp(gx - static_cast<float>(x0), 0.0f, 1.0f), fy2 = std::clamp(gy - static_cast<float>(y0), 0.0f, 1.0f);
                for (int k = 0; k < 3; ++k) {
                    auto s = [&](int xx, int yy) { return small[(static_cast<std::size_t>(yy) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(xx)) * 3 + static_cast<std::size_t>(k)]; };
                    const float v = (s(x0, y0) * (1 - fx2) + s(x1, y0) * fx2) * (1 - fy2) + (s(x0, y1) * (1 - fx2) + s(x1, y1) * fx2) * fy2;
                    disp[(static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + static_cast<std::size_t>(x)) * 3 + static_cast<std::size_t>(k)] += tonemap(v * 1.6f) * 0.45f;
                }
            }
        }
    }

    out.width = W;
    out.height = H;
    out.rgba.resize(pixels * 4);
    for (std::size_t i = 0; i < pixels; ++i) {
        for (std::size_t k = 0; k < 3; ++k) {
            out.rgba[i * 4 + k] = static_cast<std::uint8_t>(std::lround(std::clamp(disp[i * 3 + k], 0.0f, 1.0f) * 255.0f));
        }
        out.rgba[i * 4 + 3] = 255;
    }
    return out;
}

}  // namespace vfx::editor::shader
