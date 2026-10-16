#include "vfx/editor/Transform.h"

#include <algorithm>
#include <cmath>

#include "EffectTools.h"

namespace vfx::editor {

namespace {

constexpr double kDeg = 3.14159265358979323846 / 180.0;

// Turns a point or a direction about an axis through the origin, right-handed:
// about Y, +X goes towards -Z (counter-clockwise seen from above).
Vec3 turned(Vec3 v, double degrees, int axis, Vec3 k = Vec3{0, 1, 0}) {
    const double c = std::cos(degrees * kDeg), s = std::sin(degrees * kDeg);
    if (axis == 3) {
        // About any axis k (Rodrigues).
        const Vec3 kv{k.y * v.z - k.z * v.y, k.z * v.x - k.x * v.z, k.x * v.y - k.y * v.x};
        const double kd = (k.x * v.x + k.y * v.y + k.z * v.z) * (1 - c);
        return Vec3{v.x * c + kv.x * s + k.x * kd, v.y * c + kv.y * s + k.y * kd, v.z * c + kv.z * s + k.z * kd};
    }
    switch (axis) {
        case 0: return Vec3{v.x, v.y * c - v.z * s, v.y * s + v.z * c};
        case 1: return Vec3{v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
        default: return Vec3{v.x * c - v.y * s, v.x * s + v.y * c, v.z};
    }
}

void add(Scalar& value, double amount) {
    value.a += amount;
    value.b += amount;
    for (CurveKey& k : value.keys) {
        k.v += amount;
    }
}

bool isText(const Layer& layer, const char* type, const char* key, const char* want) {
    const std::string* v = tools::property<std::string>(layer, type, key);
    return v && *v == want;
}

}  // namespace

Layer transformLayer(const Layer& layer, const LayerTransform& t, bool threeD) {
    Layer out = layer;
    if (t.identity()) {
        return out;
    }
    int axis = !threeD ? 2 : t.axis < 0 ? 1 : std::clamp(t.axis, 0, 3);
    Vec3 k = t.axisVector;
    if (axis == 3) {
        const double len = std::sqrt(k.x * k.x + k.y * k.y + k.z * k.z);
        if (!(len > 1e-9)) {
            axis = 1;
        } else {
            k = Vec3{k.x / len, k.y / len, k.z / len};
            // Along a world axis after all: treated as that axis, so sprites turn too.
            if (std::fabs(k.y) > 0.9999) {
                axis = 1;
            } else if (std::fabs(k.z) > 0.9999) {
                axis = 2;
            } else if (std::fabs(k.x) > 0.9999) {
                axis = 0;
            }
            if (axis != 3 && (k.x + k.y + k.z) < 0) {
                k = Vec3{-k.x, -k.y, -k.z};  // the same axis, the other way: the turn is the other way
            }
        }
    }
    const double degrees = (axis != 3 && t.axis == 3 && (t.axisVector.x + t.axisVector.y + t.axisVector.z) < 0) ? -t.turn : t.turn;
    const bool ground = threeD && isText(layer, "shape", "plane", "ground");
    const Vec3* was = tools::property<Vec3>(layer, "shape", "offset");
    const Vec3 from = was ? *was : Vec3{0, 0, 0};
    const Vec3 pivot = t.individual ? from : t.pivot;
    const Vec3 sc{t.scale.x > 0 ? t.scale.x : 1, t.scale.y > 0 ? t.scale.y : 1, threeD && t.scale.z > 0 ? t.scale.z : 1};
    const double f = t.factor > 0.0 ? t.factor : 1.0;

    // Size: the whole of it.
    if (f != 1.0) {
        tools::scaleEverything(out, f);  // (this scales Position too: it is set again below)
    }
    // Stretch along one axis: the emitter's own extent that way. A circle
    // grows by the mean of the two axes it lies across.
    if (sc.x != 1 || sc.y != 1 || sc.z != 1) {
        // The emitter's width, height and depth along the world's axes. On
        // the ground its second number runs along Z.
        if (Vec3* size = tools::property<Vec3>(out, "shape", "size")) {
            size->x *= sc.x;
            size->y *= ground ? sc.z : sc.y;
            size->z *= ground ? sc.y : sc.z;
        }
        if (double* radius = tools::property<double>(out, "shape", "radius")) {
            *radius *= ground ? 0.5 * (sc.x + sc.z) : threeD ? (sc.x + sc.y + sc.z) / 3.0 : 0.5 * (sc.x + sc.y);
        }
    }
    // Turn.
    if (t.turn != 0.0) {
        if (Vec3* direction = tools::property<Vec3>(out, "initial", "direction")) {
            *direction = turned(*direction, degrees, axis, k);
        }
        // A sprite turns with the rest when it lies across the axis: every
        // sprite of a 2D effect; in 3D, one lying flat on the ground (about
        // the upright) or standing flat facing the front (about Z).
        const bool flatSprite = isText(layer, "sprite", "facing", "plane");
        const bool spriteTurns = !threeD || (flatSprite && ((axis == 1 && ground) || (axis == 2 && !ground)));
        if (spriteTurns) {
            if (Scalar* rotation = tools::property<Scalar>(out, "initial", "rotation")) {
                add(*rotation, degrees);
            }
        }
    }
    // Where the emitter is.
    if (Vec3* offset = tools::property<Vec3>(out, "shape", "offset")) {
        Vec3 rel{(from.x - pivot.x) * f * sc.x, (from.y - pivot.y) * f * sc.y, (from.z - pivot.z) * f * sc.z};
        if (t.turn != 0.0) {
            rel = turned(rel, degrees, axis, k);
        }
        *offset = Vec3{pivot.x + rel.x + t.move.x, pivot.y + rel.y + t.move.y, pivot.z + rel.z + t.move.z};
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
