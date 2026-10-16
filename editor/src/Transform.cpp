#include "vfx/editor/Transform.h"

#include <algorithm>
#include <cmath>

#include "EffectTools.h"

namespace vfx::editor {

namespace {

constexpr double kDeg = 3.14159265358979323846 / 180.0;

// Turns a point (or a direction) by `degrees`: about Z in 2D, about the
// upright in 3D (counter-clockwise seen from above: +X towards -Z).
Vec3 turned(Vec3 v, double degrees, bool threeD) {
    const double c = std::cos(degrees * kDeg), s = std::sin(degrees * kDeg);
    if (threeD) {
        return Vec3{v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
    }
    return Vec3{v.x * c - v.y * s, v.x * s + v.y * c, v.z};
}

void add(Scalar& value, double amount) {
    value.a += amount;
    value.b += amount;
    for (CurveKey& k : value.keys) {
        k.v += amount;
    }
}

}  // namespace

Layer transformLayer(const Layer& layer, const LayerTransform& t, bool threeD) {
    Layer out = layer;
    if (t.identity()) {
        return out;
    }
    const Vec3* was = tools::property<Vec3>(layer, "shape", "offset");
    const Vec3 from = was ? *was : Vec3{0, 0, 0};

    if (t.factor != 1.0 && t.factor > 0.0) {
        tools::scaleEverything(out, t.factor);  // (this scales Position too: it is set again below)
    }
    if (t.turn != 0.0) {
        if (Vec3* direction = tools::property<Vec3>(out, "initial", "direction")) {
            *direction = turned(*direction, t.turn, threeD);
        }
        // A sprite turns with the rest when it lies in the turning plane:
        // every sprite of a 2D effect; in 3D, one lying on the ground.
        const std::string* facing = tools::property<std::string>(out, "sprite", "facing");
        const std::string* plane = tools::property<std::string>(out, "shape", "plane");
        const bool onGround = facing && *facing == "plane" && plane && *plane == "ground";
        if (!threeD || onGround) {
            if (Scalar* rotation = tools::property<Scalar>(out, "initial", "rotation")) {
                add(*rotation, t.turn);
            }
        }
    }
    if (Vec3* offset = tools::property<Vec3>(out, "shape", "offset")) {
        const double f = t.factor > 0.0 ? t.factor : 1.0;
        Vec3 rel{(from.x - t.pivot.x) * f, (from.y - t.pivot.y) * f, (from.z - t.pivot.z) * f};
        rel = turned(rel, t.turn, threeD);
        *offset = Vec3{t.pivot.x + rel.x + t.move.x, t.pivot.y + rel.y + t.move.y, t.pivot.z + rel.z + t.move.z};
        if (!threeD) {
            offset->z = from.z;
        }
    }
    return out;
}

Vec3 middleOf(const std::vector<const Layer*>& layers) {
    Vec3 sum{0, 0, 0};
    int n = 0;
    for (const Layer* layer : layers) {
        if (const Vec3* offset = tools::property<Vec3>(*layer, "shape", "offset")) {
            sum.x += offset->x;
            sum.y += offset->y;
            sum.z += offset->z;
            ++n;
        }
    }
    if (n > 0) {
        sum.x /= n;
        sum.y /= n;
        sum.z /= n;
    }
    return sum;
}

}  // namespace vfx::editor
