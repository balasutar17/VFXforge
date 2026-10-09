#include "vfx/editor/UnityExport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <set>
#include <string_view>

#include <nlohmann/json.hpp>

#include "vfx/FileIO.h"
#include "vfx/Program.h"
#include "vfx/editor/Image.h"
#include "vfx/editor/Session.h"
#include "vfx/editor/Shapes.h"
#include "vfx/editor/UnityFiles.h"

namespace vfx::editor {

namespace {

using J = nlohmann::ordered_json;

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = 180.0 / kPi;
constexpr double kFullCircle = 179.5;  // degrees of spread treated as "every direction"

double round6(double v) { return std::round(v * 1e6) / 1e6; }

// ------------------------------------------------------------- curves

struct Key {
    double t = 0, v = 0;
};

// Unity curves are smooth by default; VFX Forge's are straight lines between
// keys, so each key carries the slopes that make Unity draw straight lines.
J curveJson(const std::vector<Key>& keys, double& multiplier) {
    double most = 0.0;
    for (const Key& k : keys) {
        most = std::max(most, std::fabs(k.v));
    }
    multiplier = most > 1e-9 ? most : 1.0;
    J out = J::array();
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const double v = keys[i].v / multiplier;
        auto slope = [&](std::size_t a, std::size_t b) {
            const double dt = keys[b].t - keys[a].t;
            return dt > 1e-9 ? (keys[b].v - keys[a].v) / multiplier / dt : 0.0;
        };
        const double in = i > 0 ? slope(i - 1, i) : 0.0;
        const double outSlope = i + 1 < keys.size() ? slope(i, i + 1) : 0.0;
        out.push_back(J::array({round6(keys[i].t), round6(v), round6(in), round6(outSlope)}));
    }
    return out;
}

J curveValue(const std::vector<Key>& keys) {
    double multiplier = 1.0;
    J curve = curveJson(keys, multiplier);
    J out = J::object();
    out["multiplier"] = round6(multiplier);
    out["keys"] = std::move(curve);
    return out;
}

// A number that may be fixed, random between two, or a curve. Curves in VFX
// Forge read across the layer's own time; Unity reads start values across
// the whole system's time, so curve times are moved from one to the other.
J minMax(const ScalarF& s, double scale, double layerStart, double layerLength, double total) {
    switch (s.mode) {
        case ScalarF::Mode::Constant:
            return round6(s.a * scale);
        case ScalarF::Mode::Random: {
            const double a = s.a * scale, b = s.b * scale;
            return J::array({round6(std::min(a, b)), round6(std::max(a, b))});
        }
        case ScalarF::Mode::Curve: {
            std::vector<Key> keys;
            for (const CurveKeyF& k : s.keys) {
                keys.push_back(Key{total > 0 ? (layerStart + k.t * layerLength) / total : k.t, k.v * scale});
            }
            if (keys.empty()) {
                return 0.0;
            }
            return curveValue(keys);
        }
    }
    return 0.0;
}

// A curve read across each particle's life: no change of time needed.
J lifeCurve(const ScalarF& s, double scale) { return minMax(s, scale, 0.0, 1.0, 1.0); }

double meanOf(const ScalarF& s) {
    switch (s.mode) {
        case ScalarF::Mode::Constant:
            return s.a;
        case ScalarF::Mode::Random:
            return 0.5 * (s.a + s.b);
        case ScalarF::Mode::Curve: {
            double sum = 0;
            for (int i = 0; i <= 16; ++i) {
                sum += evalCurve(s.keys, static_cast<float>(i) / 16.0f);
            }
            return sum / 17.0;
        }
    }
    return 0.0;
}

// ------------------------------------------------------------- shape

struct Rot {
    double x = 0, y = 0, z = 0;  // degrees, Unity's Euler order
};

// The rotation that turns Unity's emission axis (+Z) to face d.
Rot facing(double dx, double dy, double dz) {
    Rot r;
    const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(length > 1e-9)) {
        return r;
    }
    dx /= length;
    dy /= length;
    dz /= length;
    r.x = -std::asin(std::clamp(dy, -1.0, 1.0)) * kDeg;
    r.y = (std::fabs(dx) > 1e-12 || std::fabs(dz) > 1e-12) ? std::atan2(dx, dz) * kDeg : 0.0;
    return r;
}

// Where a box's own axes point in the world, for a Unity Euler rotation:
// z first, then x, then y.
void boxAxes(const Rot& r, double axes[3][3]) {
    const double cx = std::cos(r.x / kDeg), sx = std::sin(r.x / kDeg);
    const double cy = std::cos(r.y / kDeg), sy = std::sin(r.y / kDeg);
    const double cz = std::cos(r.z / kDeg), sz = std::sin(r.z / kDeg);
    // R = Ry * Rx * Rz; column i is local axis i.
    const double m[3][3] = {
        {cy * cz + sy * sx * sz, -cy * sz + sy * sx * cz, sy * cx},
        {cx * sz, cx * cz, -sx},
        {-sy * cz + cy * sx * sz, sy * sz + cy * sx * cz, cy * cx},
    };
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            axes[i][j] = m[j][i];
        }
    }
}

// The size a rotated box needs along its own axes to cover a region that is
// ex by ey by ez along the world axes.
J boxScale(const Rot& r, double ex, double ey, double ez) {
    double axes[3][3];
    boxAxes(r, axes);
    J out = J::array();
    for (auto& axis : axes) {
        out.push_back(round6(std::fabs(axis[0]) * ex + std::fabs(axis[1]) * ey + std::fabs(axis[2]) * ez));
    }
    return out;
}

J rotJson(const Rot& r) { return J::array({round6(r.x), round6(r.y), round6(r.z)}); }

J shapeJson(const EmitterProgram& e) {
    J s = J::object();
    const double spreadDeg = e.spread * kDeg;
    const bool everyWay = spreadDeg >= kFullCircle;
    const double heading = std::atan2(e.dirY, e.dirX) * kDeg;
    const double tiny = 0.0001;

    ShapeType shape = e.shape;
    if (e.flat && shape == ShapeType::Sphere) shape = ShapeType::Circle;
    if (e.flat && shape == ShapeType::Box) shape = ShapeType::Rectangle;

    // Spread that a Unity shape cannot express exactly is approximated by
    // mixing in random directions.
    const double mix = std::clamp(spreadDeg / 180.0, 0.0, 1.0);
    auto box = [&](double ex, double ey, double ez, bool edge, double randomDirection) {
        const Rot r = facing(e.dirX, e.dirY, e.dirZ);
        s["type"] = "box";
        s["scale"] = boxScale(r, ex, ey, ez);
        s["rotation"] = rotJson(r);
        s["edge"] = edge;
        s["randomDirection"] = round6(randomDirection);
        s["exact"] = randomDirection == 0.0;
    };

    switch (shape) {
        case ShapeType::Point:
            if (e.flat) {
                // A circle of no size sends particles out along its radius:
                // its arc is exactly a 2D spread.
                s["type"] = "circle";
                s["radius"] = tiny;
                s["radiusThickness"] = 1.0;
                s["arc"] = everyWay ? 360.0 : round6(std::max(2.0 * spreadDeg, 0.001));
                s["rotation"] = rotJson(Rot{0, 0, everyWay ? 0.0 : heading - spreadDeg});
                s["exact"] = true;
            } else if (everyWay) {
                s["type"] = "sphere";
                s["radius"] = tiny;
                s["radiusThickness"] = 1.0;
                s["exact"] = true;
            } else {
                s["type"] = "cone";
                s["radius"] = tiny;
                s["radiusThickness"] = 1.0;
                s["angle"] = round6(std::min(spreadDeg, 90.0));
                s["rotation"] = rotJson(facing(e.dirX, e.dirY, e.dirZ));
                s["randomDirection"] = spreadDeg > 90.0 ? round6((spreadDeg - 90.0) / 90.0) : 0.0;
                s["exact"] = spreadDeg <= 90.0;
            }
            break;

        case ShapeType::Circle:
            if (everyWay) {
                s["type"] = "circle";
                s["radius"] = round6(std::max(static_cast<double>(e.radius), tiny));
                s["radiusThickness"] = e.fromEdge ? 0.0 : 1.0;
                s["arc"] = 360.0;
                s["rotation"] = rotJson(Rot{});
                s["exact"] = false;  // Unity sends them outward, not every which way
            } else {
                box(2.0 * e.radius, 2.0 * e.radius, 0.0, e.fromEdge, mix);
                s["exact"] = false;  // a square stands in for the circle
            }
            break;

        case ShapeType::Rectangle:
            if (everyWay) {
                s["type"] = "box";
                s["scale"] = J::array({round6(e.sizeX), round6(e.sizeY), 0.0});
                s["rotation"] = rotJson(Rot{});
                s["edge"] = e.fromEdge;
                s["sphericalDirection"] = 1.0;
                s["exact"] = false;
            } else {
                box(e.sizeX, e.sizeY, 0.0, e.fromEdge, mix);
            }
            break;

        case ShapeType::Sphere:
            if (everyWay) {
                s["type"] = "sphere";
                s["radius"] = round6(std::max(static_cast<double>(e.radius), tiny));
                s["radiusThickness"] = e.fromEdge ? 0.0 : 1.0;
                s["exact"] = false;
            } else {
                box(2.0 * e.radius, 2.0 * e.radius, 2.0 * e.radius, e.fromEdge, mix);
                s["exact"] = false;
            }
            break;

        case ShapeType::Box:
            box(e.sizeX, e.sizeY, e.sizeZ, e.fromEdge, mix);
            break;

        case ShapeType::Cone:
            s["type"] = "cone";
            s["radius"] = round6(std::max(static_cast<double>(e.radius), tiny));
            s["radiusThickness"] = e.fromEdge ? 0.0 : 1.0;
            s["angle"] = round6(std::min(e.coneAngle * kDeg, 90.0));
            s["rotation"] = rotJson(facing(e.dirX, e.dirY, e.dirZ));
            s["randomDirection"] = round6(mix);
            s["exact"] = !e.flat && spreadDeg == 0.0;
            break;
    }
    return s;
}

// ------------------------------------------------------------- colour

double toScreen(double linear) { return std::clamp(linearToSrgb(linear), 0.0, 1.0); }

GradientKeyF gradientAt(const std::vector<GradientKeyF>& keys, float t) {
    if (keys.empty()) {
        return GradientKeyF{t, 1, 1, 1, 1};
    }
    if (t <= keys.front().t) {
        return keys.front();
    }
    if (t >= keys.back().t) {
        return keys.back();
    }
    for (std::size_t i = 1; i < keys.size(); ++i) {
        if (t <= keys[i].t) {
            const GradientKeyF& a = keys[i - 1];
            const GradientKeyF& b = keys[i];
            const float span = b.t - a.t;
            const float f = span > 1e-6f ? (t - a.t) / span : 0.0f;
            return GradientKeyF{t, a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f,
                                a.a + (b.a - a.a) * f};
        }
    }
    return keys.back();
}

// Unity gradients hold at most 8 keys of each kind.
std::vector<float> keyTimes(std::vector<float> times) {
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(),
                            [](float a, float b) { return std::fabs(a - b) < 1e-5f; }),
                times.end());
    if (times.size() <= 8) {
        return times;
    }
    std::vector<float> even;
    for (int i = 0; i < 8; ++i) {
        even.push_back(static_cast<float>(i) / 7.0f);
    }
    return even;
}

// ------------------------------------------------------------- one layer

const UnityPicture* pictureFor(const std::vector<UnityPicture>& pictures, Id asset) {
    for (const UnityPicture& p : pictures) {
        if (p.asset == asset) {
            return &p;
        }
    }
    return nullptr;
}

// How Unity's Texture Sheet Animation plays a sprite sheet. Its "frame over
// time" runs from 0 to 1 across every cell of the sheet; its start frame
// counts cells.
J sheetJson(const EmitterProgram& e) {
    const int cells = e.columns * e.rows;
    const int frames = std::max(1, e.frames);
    J sheet = J::object();
    sheet["columns"] = e.columns;
    sheet["rows"] = e.rows;
    // Just short of the end, so the last moment never shows the next cell.
    const double end = (static_cast<double>(frames) - 0.01) / static_cast<double>(cells);
    J through = frames > 1 ? curveValue({Key{0.0, 0.0}, Key{1.0, end}}) : J(0.0);
    J randomCell = frames > 1 ? J::array({0.0, round6(static_cast<double>(frames) - 0.01)}) : J(0.0);
    switch (e.animate) {
        case Animate::Life:
            sheet["frameOverTime"] = std::move(through);
            sheet["startFrame"] = 0.0;
            sheet["cycles"] = 1;
            break;
        case Animate::Loop: {
            // Unity loops a whole number of times per life; pick the count
            // that comes closest to the frame rate for a typical particle.
            const double plays = meanOf(e.lifetime) * e.fps / frames;
            sheet["frameOverTime"] = std::move(through);
            sheet["startFrame"] = e.randomStart ? std::move(randomCell) : J(0.0);
            sheet["cycles"] = std::max(1, static_cast<int>(std::lround(plays)));
            break;
        }
        case Animate::Random:
            sheet["frameOverTime"] = 0.0;
            sheet["startFrame"] = std::move(randomCell);
            sheet["cycles"] = 1;
            break;
    }
    return sheet;
}

J layerJson(const Effect& effect, const Layer& layer, int order,
            const std::vector<UnityPicture>& pictures) {
    const std::shared_ptr<const EmitterProgram> program = compileLayer(effect, layer);
    const EmitterProgram& e = *program;
    const double total = effect.duration > 0.0 ? effect.duration : 0.01;

    J out = J::object();
    out["name"] = layer.name;
    out["enabled"] = layer.enabled;
    out["drawn"] = e.drawn;
    out["seed"] = static_cast<std::uint32_t>(e.key & 0xffffffffu);
    out["maxParticles"] = std::max<std::uint32_t>(e.capacity + 16u, 16u);

    // ---- start values
    out["startLifetime"] = minMax(e.lifetime, 1.0, e.start, e.duration, total);
    out["startSpeed"] = minMax(e.speed, 1.0, e.start, e.duration, total);

    // A size or opacity "over life" that is a fixed number or a random range
    // is really a per-particle multiplier chosen at birth: fold it into the
    // start value, where Unity can express it.
    ScalarF size = e.size;
    double alphaLow = e.color[3], alphaHigh = e.color[3];
    if (e.hasOverLife && !e.sizeOverLife.isCurve()) {
        const double low = e.sizeOverLife.mode == ScalarF::Mode::Random ? e.sizeOverLife.a : e.sizeOverLife.a;
        const double high = e.sizeOverLife.mode == ScalarF::Mode::Random ? e.sizeOverLife.b : e.sizeOverLife.a;
        if (size.mode == ScalarF::Mode::Curve) {
            for (CurveKeyF& k : size.keys) {
                k.v *= static_cast<float>(0.5 * (low + high));
            }
        } else {
            const float a = size.a, b = size.mode == ScalarF::Mode::Random ? size.b : size.a;
            size.mode = low == high && a == b ? ScalarF::Mode::Constant : ScalarF::Mode::Random;
            size.a = static_cast<float>(a * low);
            size.b = static_cast<float>(b * high);
        }
    }
    if (e.hasOverLife && !e.opacityOverLife.isCurve()) {
        const double low = e.opacityOverLife.a;
        const double high = e.opacityOverLife.mode == ScalarF::Mode::Random ? e.opacityOverLife.b : low;
        alphaLow *= low;
        alphaHigh *= high;
    }
    out["startSize"] = minMax(size, 1.0, e.start, e.duration, total);

    // Unity turns particles clockwise for positive angles; VFX Forge turns
    // them counter-clockwise. Unity also wants radians.
    out["startRotation"] = minMax(e.rotation, -1.0 / kDeg, e.start, e.duration, total);

    const double r = toScreen(e.color[0]), g = toScreen(e.color[1]), b = toScreen(e.color[2]);
    if (alphaLow == alphaHigh) {
        out["startColor"] = J::array({round6(r), round6(g), round6(b), round6(std::clamp(alphaLow, 0.0, 1.0))});
    } else {
        out["startColor"] = J::array(
            {J::array({round6(r), round6(g), round6(b), round6(std::clamp(alphaLow, 0.0, 1.0))}),
             J::array({round6(r), round6(g), round6(b), round6(std::clamp(alphaHigh, 0.0, 1.0))})});
    }

    // ---- emission, across the whole system's time
    const double start = e.start / total;
    const double end = (e.start + e.duration) / total;
    const bool whole = e.start <= 1e-9 && std::fabs(e.start + e.duration - total) < 1e-6;
    if (!e.emits) {
        out["rate"] = 0.0;
    } else if (whole) {
        out["rate"] = minMax(e.rate, 1.0, 0.0, total, total);
    } else {
        // Zero outside the layer's time, its rate inside, with sharp steps.
        const double eps = 1e-4;
        std::vector<Key> keys;
        auto rateAt = [&](float t) {
            return e.rate.isCurve() ? static_cast<double>(evalCurve(e.rate.keys, t)) : meanOf(e.rate);
        };
        if (start > eps) {
            keys.push_back(Key{0.0, 0.0});
            keys.push_back(Key{start - eps, 0.0});
        }
        keys.push_back(Key{start, rateAt(0.0f)});
        if (e.rate.isCurve()) {
            for (const CurveKeyF& k : e.rate.keys) {
                if (k.t > 0.0f && k.t < 1.0f) {
                    keys.push_back(Key{start + k.t * (end - start), k.v});
                }
            }
        }
        keys.push_back(Key{std::max(end - eps, start + eps * 0.5), rateAt(1.0f)});
        if (end < 1.0 - eps) {
            keys.push_back(Key{end, 0.0});
            keys.push_back(Key{1.0, 0.0});
        }
        out["rate"] = curveValue(keys);
    }
    J bursts = J::array();
    if (e.emits) {
        for (const BurstF& burst : e.bursts) {
            bursts.push_back(J::array({round6(e.start + burst.time), burst.count}));
        }
    }
    out["bursts"] = std::move(bursts);

    out["shape"] = shapeJson(e);

    // ---- movement
    if (e.gravityX != 0.0f || e.gravityY != 0.0f || e.gravityZ != 0.0f) {
        out["force"] = J::array({round6(e.gravityX), round6(e.gravityY), e.flat ? 0.0 : round6(e.gravityZ)});
    } else {
        out["force"] = nullptr;
    }
    out["drag"] = e.drag > 0.0f ? J(round6(e.drag)) : J(nullptr);
    const bool spins = e.spin.mode != ScalarF::Mode::Constant || e.spin.a != 0.0f;
    out["spin"] = spins ? lifeCurve(e.spin, -1.0 / kDeg) : J(nullptr);

    // ---- over life
    out["sizeOverLife"] = e.hasOverLife && e.sizeOverLife.isCurve() ? lifeCurve(e.sizeOverLife, 1.0) : J(nullptr);

    double glow = e.glow;
    const bool tints = !e.colorOverLife.empty();
    const bool fades = e.hasOverLife && e.opacityOverLife.isCurve();
    if (tints || fades) {
        // Unity gradient colours cannot go above 1. Anything brighter is
        // divided down here and the difference moved into the glow.
        double brightest = 1.0;
        for (const GradientKeyF& k : e.colorOverLife) {
            brightest = std::max({brightest, static_cast<double>(k.r), static_cast<double>(k.g),
                                  static_cast<double>(k.b)});
        }
        glow *= brightest;

        std::vector<float> colourTimes, alphaTimes;
        for (const GradientKeyF& k : e.colorOverLife) {
            colourTimes.push_back(k.t);
            alphaTimes.push_back(k.t);
        }
        if (fades) {
            for (const CurveKeyF& k : e.opacityOverLife.keys) {
                alphaTimes.push_back(k.t);
            }
        }
        if (colourTimes.empty()) {
            colourTimes = {0.0f, 1.0f};
        }
        if (alphaTimes.empty()) {
            alphaTimes = {0.0f, 1.0f};
        }
        J colours = J::array(), alphas = J::array();
        for (const float t : keyTimes(colourTimes)) {
            const GradientKeyF c = gradientAt(e.colorOverLife, t);
            colours.push_back(J::array({round6(t), round6(toScreen(c.r / brightest)),
                                        round6(toScreen(c.g / brightest)), round6(toScreen(c.b / brightest))}));
        }
        for (const float t : keyTimes(alphaTimes)) {
            double a = gradientAt(e.colorOverLife, t).a;
            if (fades) {
                a *= evalCurve(e.opacityOverLife.keys, t);
            }
            alphas.push_back(J::array({round6(t), round6(std::clamp(a, 0.0, 1.0))}));
        }
        J gradient = J::object();
        gradient["colors"] = std::move(colours);
        gradient["alphas"] = std::move(alphas);
        out["colorOverLife"] = std::move(gradient);
    } else {
        out["colorOverLife"] = nullptr;
    }

    // ---- drawing
    J render = J::object();
    const bool onPlane = !e.flat && e.facing == Facing::Plane;
    if (e.alongMotion && !onPlane) {
        render["mode"] = "stretched";
        render["velocityScale"] = round6(e.stretch);
        render["lengthScale"] = 1.0;
    } else {
        render["mode"] = onPlane ? "plane" : "billboard";
    }
    render["shape"] = static_cast<int>(e.spriteShape);
    render["shapeName"] = shapeName(e.spriteShape);
    if (const UnityPicture* picture = e.texture.valid() ? pictureFor(pictures, e.texture) : nullptr) {
        render["picture"] = picture->path;
        if (e.columns * e.rows > 1) {
            render["sheet"] = sheetJson(e);
        }
    }
    render["additive"] = e.blend == BlendMode::Additive;
    render["glow"] = round6(glow);
    render["order"] = order;
    out["render"] = std::move(render);
    return out;
}

// Unity wants a 32-digit hex GUID per asset. A hash of the path keeps it the
// same from one export to the next.
std::string guidFor(const std::string& path) {
    std::uint64_t a = 0x9e3779b97f4a7c15ull, b = 0xc2b2ae3d27d4eb4full;
    for (const char c : path) {
        a = (a ^ static_cast<std::uint8_t>(c)) * 0x100000001b3ull;
        b = (b + static_cast<std::uint8_t>(c)) * 0xff51afd7ed558ccdull;
        b ^= b >> 29;
    }
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (const std::uint64_t part : {a, b}) {
        for (int i = 60; i >= 0; i -= 4) {
            out.push_back(digits[(part >> i) & 0xfu]);
        }
    }
    return out;
}

}  // namespace

std::vector<UnityPicture> unityPictures(const Effect& effect, const std::filesystem::path& folder) {
    std::vector<UnityPicture> out;
    if (folder.empty()) {
        return out;
    }
    for (const Asset& asset : effect.assets) {
        if (asset.kind != "texture" || !isAssetReferenced(effect, asset.id)) {
            continue;
        }
        auto bytes = readFile(folder / pathFromUtf8(asset.path));
        if (!bytes || !decodePng(bytes.value())) {
            continue;
        }
        std::string file = asset.path.substr(asset.path.find_last_of('/') + 1);
        out.push_back(UnityPicture{asset.id, "Assets/VFXForge/Images/" + file, std::move(bytes.value())});
    }
    return out;
}

std::string unityDescription(const Effect& effect, const std::vector<UnityPicture>& pictures) {
    J root = J::object();
    root["format"] = "vfxforge.unity";
    root["formatVersion"] = kUnityFormatVersion;
    root["name"] = effect.name;
    root["space"] = effect.space;
    root["duration"] = round6(effect.duration);
    root["loop"] = effect.loop == "loop";
    root["frameRate"] = round6(effect.frameRate);
    J atlas = J::object();
    atlas["columns"] = kAtlasColumns;
    atlas["rows"] = kAtlasRows;
    root["atlas"] = std::move(atlas);
    J layers = J::array();
    int order = 0;
    for (const Layer& layer : effect.layers) {
        layers.push_back(layerJson(effect, layer, order++, pictures));
    }
    root["layers"] = std::move(layers);
    return root.dump(2) + "\n";
}

std::string unityFileStem(const Effect& effect) {
    std::string out;
    for (const char c : effect.name) {
        const auto u = static_cast<unsigned char>(c);
        const bool plain = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') ||
                           c == ' ' || c == '-' || c == '_' || u >= 0x80;
        out.push_back(plain ? c : '_');
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
        out.pop_back();
    }
    while (!out.empty() && out.front() == ' ') {
        out.erase(out.begin());
    }
    return out.empty() ? std::string("Effect") : out.substr(0, 80);
}

std::string unityShapeAtlasPng() {
    const int width = kAtlasColumns * kAtlasTile, height = kAtlasRows * kAtlasTile;
    std::string pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 2u, '\0');
    const float pixel = 2.0f / static_cast<float>(kAtlasTile);
    for (int i = 0; i < kSpriteShapeCount; ++i) {
        const int col = i % kAtlasColumns, row = i / kAtlasColumns;
        for (int y = 0; y < kAtlasTile; ++y) {
            for (int x = 0; x < kAtlasTile; ++x) {
                const float px = (static_cast<float>(x) + 0.5f) * pixel - 1.0f;
                const float py = 1.0f - (static_cast<float>(y) + 0.5f) * pixel;
                const ShapeSample s = sampleShape(static_cast<SpriteShape>(i), px, py, pixel, pixel);
                const std::size_t at = (static_cast<std::size_t>(row * kAtlasTile + y) * static_cast<std::size_t>(width) +
                                        static_cast<std::size_t>(col * kAtlasTile + x)) * 2u;
                const float tone = std::clamp(s.tone * 0.5f + 0.5f, 0.0f, 1.0f);
                pixels[at] = static_cast<char>(std::lround(tone * 255.0f));
                pixels[at + 1] = static_cast<char>(std::lround(std::clamp(s.cover, 0.0f, 1.0f) * 255.0f));
            }
        }
    }
    return encodePngImage(width, height, 2, pixels);
}

std::vector<TarEntry> unityExportFiles(const Effect& effect, const std::filesystem::path& pictureFolder) {
    std::vector<TarEntry> files;
    for (const UnityFile& f : unityHelperSources()) {
        files.push_back(TarEntry{std::string("Assets/VFXForge/") + f.path, std::string(f.text)});
    }
    files.push_back(TarEntry{"Assets/VFXForge/Textures/VFXForgeShapes.png", unityShapeAtlasPng()});
    const std::vector<UnityPicture> pictures = unityPictures(effect, pictureFolder);
    // Pictures before the effect, so Unity has them when it builds the prefab.
    for (const UnityPicture& p : pictures) {
        files.push_back(TarEntry{p.path, p.png});
    }
    files.push_back(TarEntry{"Assets/VFXForge/Effects/" + unityFileStem(effect) + ".vfxforge",
                             unityDescription(effect, pictures)});
    return files;
}

bool isUnityProject(const std::filesystem::path& folder) {
    std::error_code ec;
    return std::filesystem::is_directory(folder / "Assets", ec) &&
           std::filesystem::is_directory(folder / "ProjectSettings", ec);
}

Status exportToUnityProject(const Effect& effect, const std::filesystem::path& projectFolder,
                            std::filesystem::path* written, const std::filesystem::path& pictureFolder) {
    if (!isUnityProject(projectFolder)) {
        return makeError(
            "That folder is not a Unity project. Choose the project's own folder: the one that "
            "holds the Assets and ProjectSettings folders.");
    }
    for (const TarEntry& f : unityExportFiles(effect, pictureFolder)) {
        const std::filesystem::path target = projectFolder / pathFromUtf8(f.path);
        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) {
            return makeError("A folder could not be made inside the Unity project.", ec.message());
        }
        if (Status ok = writeFileAtomic(target, f.bytes); !ok) {
            return ok;
        }
        if (written && f.path.size() > 9 && f.path.compare(f.path.size() - 9, 9, ".vfxforge") == 0) {
            *written = target;
        }
    }
    return {};
}

std::string makeUnityPackage(const std::vector<TarEntry>& files) {
    // A .unitypackage is a gzipped tar with one folder per asset, named by
    // its GUID, holding the asset, its .meta file and its path. Folders are
    // listed too, so Unity makes them with stable GUIDs.
    std::set<std::string> folders;
    for (const TarEntry& f : files) {
        for (std::size_t cut = f.path.find('/'); cut != std::string::npos; cut = f.path.find('/', cut + 1)) {
            const std::string folder = f.path.substr(0, cut);
            if (folder != "Assets") {
                folders.insert(folder);
            }
        }
    }
    std::vector<TarEntry> entries;
    for (const std::string& folder : folders) {
        const std::string guid = guidFor(folder);
        entries.push_back(TarEntry{guid + "/pathname", folder});
        entries.push_back(TarEntry{guid + "/asset.meta",
                                   "fileFormatVersion: 2\nguid: " + guid +
                                       "\nfolderAsset: yes\nDefaultImporter:\n  externalObjects: {}\n"
                                       "  userData: \n  assetBundleName: \n  assetBundleVariant: \n"});
    }
    for (const TarEntry& f : files) {
        const std::string guid = guidFor(f.path);
        entries.push_back(TarEntry{guid + "/pathname", f.path});
        entries.push_back(TarEntry{guid + "/asset", f.bytes});
        entries.push_back(TarEntry{guid + "/asset.meta", "fileFormatVersion: 2\nguid: " + guid + "\n"});
    }
    return gzipCompress(makeTar(entries));
}

Status exportUnityPackage(const Effect& effect, const std::filesystem::path& file,
                          const std::filesystem::path& pictureFolder) {
    return writeFileAtomic(file, makeUnityPackage(unityExportFiles(effect, pictureFolder)));
}

}  // namespace vfx::editor
