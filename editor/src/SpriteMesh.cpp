#include "vfx/editor/SpriteMesh.h"

#include <cmath>

#include "vfx/editor/Session.h"

namespace vfx::editor {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kNear = 0.05f;  // 3D: nothing closer to the camera than this is drawn

// The camera worked out once per frame.
struct Camera {
    bool flat = true;
    float halfW = 0, halfH = 0;

    // 2D
    float cx = 0, cy = 0, pixelsPerUnit = 1;

    // 3D: camera position and its three axes in world space.
    float ex = 0, ey = 0, ez = 0;
    float rx = 1, ry = 0, rz = 0;  // right
    float ux = 0, uy = 1, uz = 0;  // up
    float fx = 0, fy = 0, fz = -1; // forward
    float focal = 1;               // pixels at one unit of depth
};

Camera makeCamera(const View& view, bool flat) {
    Camera c;
    c.flat = flat;
    c.halfW = view.width * 0.5f;
    c.halfH = view.height * 0.5f;
    if (flat) {
        c.cx = view.centerX;
        c.cy = view.centerY;
        c.pixelsPerUnit = view.height / (view.unitsHigh > 1e-6f ? view.unitsHigh : 1e-6f);
        return c;
    }
    const float yaw = view.yaw * kPi / 180.0f;
    float pitch = view.pitch;
    pitch = pitch > 89.0f ? 89.0f : (pitch < -89.0f ? -89.0f : pitch);
    pitch *= kPi / 180.0f;
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const float cyaw = std::cos(yaw), syaw = std::sin(yaw);

    // From the target back to the camera.
    const float bx = syaw * cp, by = sp, bz = cyaw * cp;
    const float distance = view.distance > 0.1f ? view.distance : 0.1f;
    c.ex = view.targetX + bx * distance;
    c.ey = view.targetY + by * distance;
    c.ez = view.targetZ + bz * distance;
    c.fx = -bx;
    c.fy = -by;
    c.fz = -bz;
    // right = forward x worldUp, which is never degenerate because pitch
    // stops short of straight up or down.
    c.rx = cyaw;
    c.ry = 0.0f;
    c.rz = -syaw;
    // up = right x forward
    c.ux = c.ry * c.fz - c.rz * c.fy;
    c.uy = c.rz * c.fx - c.rx * c.fz;
    c.uz = c.rx * c.fy - c.ry * c.fx;

    float fov = view.fieldOfView;
    fov = fov < 5.0f ? 5.0f : (fov > 120.0f ? 120.0f : fov);
    c.focal = c.halfH / std::tan(fov * 0.5f * kPi / 180.0f);
    return c;
}

bool project(const Camera& c, float x, float y, float z, float& px, float& py, float& scale) {
    if (c.flat) {
        px = c.halfW + (x - c.cx) * c.pixelsPerUnit;
        py = c.halfH - (y - c.cy) * c.pixelsPerUnit;
        scale = c.pixelsPerUnit;
        return true;
    }
    const float dx = x - c.ex, dy = y - c.ey, dz = z - c.ez;
    const float depth = dx * c.fx + dy * c.fy + dz * c.fz;
    if (!(depth > kNear)) {
        return false;
    }
    scale = c.focal / depth;
    px = c.halfW + (dx * c.rx + dy * c.ry + dz * c.rz) * scale;
    py = c.halfH - (dx * c.ux + dy * c.uy + dz * c.uz) * scale;
    return true;
}

float toScreen(float linear) { return static_cast<float>(linearToSrgb(static_cast<double>(linear))); }

}  // namespace

bool projectPoint(const View& view, bool flat, float x, float y, float z, float& px, float& py,
                  float& scale) {
    return project(makeCamera(view, flat), x, y, flat ? 0.0f : z, px, py, scale);
}

void buildSpriteMesh(const RenderFrame& frame, const View& view, SpriteMesh& mesh) {
    mesh.vertices.clear();
    mesh.indices.clear();
    mesh.drawn = 0;
    if (!(view.width > 0.0f) || !(view.height > 0.0f)) {
        return;
    }
    const Camera camera = makeCamera(view, frame.flat);

    static const float cornerX[4] = {-1.0f, 1.0f, 1.0f, -1.0f};
    static const float cornerY[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
    static const float cornerU[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    static const float cornerV[4] = {1.0f, 1.0f, 0.0f, 0.0f};

    for (const RenderBatch& batch : frame.batches) {
        const bool additive = batch.blend == BlendMode::Additive;
        const bool onPlane = !frame.flat && batch.facing == Facing::Plane;
        const float glow = batch.glow > 0.0f ? batch.glow : 0.0f;

        for (std::uint32_t n = 0; n < batch.count; ++n) {
            const SpriteInstance& p = frame.instances[batch.first + n];
            if (!(p.a > 0.0f) || !(p.size > 0.0f)) {
                continue;
            }
            float px = 0, py = 0, scale = 1;
            if (!project(camera, p.x, p.y, frame.flat ? 0.0f : p.z, px, py, scale)) {
                continue;
            }
            const float half = p.size * 0.5f;
            const float reach = half * scale * 1.5f;  // a turned square reaches further than its side
            if (px + reach < 0.0f || px - reach > view.width || py + reach < 0.0f ||
                py - reach > view.height) {
                continue;
            }

            const float alpha = p.a > 1.0f ? 1.0f : p.a;
            const float strength = alpha * glow;
            const float r = toScreen(p.r) * strength;
            const float g = toScreen(p.g) * strength;
            const float b = toScreen(p.b) * strength;
            const float a = additive ? 0.0f : alpha;

            const float cs = std::cos(p.rotation), sn = std::sin(p.rotation);
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            bool visible = true;
            for (int k = 0; k < 4; ++k) {
                // The corner, turned by the particle's rotation, in world units.
                const float ox = (cornerX[k] * cs - cornerY[k] * sn) * half;
                const float oy = (cornerX[k] * sn + cornerY[k] * cs) * half;
                SpriteVertex v;
                if (onPlane) {
                    // Lying flat on the effect's own plane: each corner is a
                    // real point in the world and is projected by itself.
                    float s = 1;
                    if (!project(camera, p.x + ox, p.y + oy, p.z, v.x, v.y, s)) {
                        visible = false;
                        break;
                    }
                } else {
                    // Facing the camera: offset on screen. Screen y runs down.
                    v.x = px + ox * scale;
                    v.y = py - oy * scale;
                }
                v.u = cornerU[k];
                v.v = cornerV[k];
                v.r = r;
                v.g = g;
                v.b = b;
                v.a = a;
                mesh.vertices.push_back(v);
            }
            if (!visible) {
                mesh.vertices.resize(base);
                continue;
            }
            mesh.indices.insert(mesh.indices.end(),
                                {base, base + 1, base + 2, base, base + 2, base + 3});
            ++mesh.drawn;
        }
    }
}

}  // namespace vfx::editor
