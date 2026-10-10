#include "EffectTools.h"

#include <algorithm>
#include <cmath>

namespace vfx::editor::tools {

Module* moduleOf(Layer& layer, std::string_view type) {
    for (Module& m : layer.modules) {
        if (m.type == type && m.known()) {
            return &m;
        }
    }
    return nullptr;
}

const Module* moduleOf(const Layer& layer, std::string_view type) {
    for (const Module& m : layer.modules) {
        if (m.type == type && m.known()) {
            return &m;
        }
    }
    return nullptr;
}

void scale(Scalar& value, double factor) {
    value.a *= factor;
    value.b *= factor;
    for (CurveKey& k : value.keys) {
        k.v *= factor;
    }
}

void scale(Vec3& value, double factor) {
    value.x *= factor;
    value.y *= factor;
    value.z *= factor;
}

namespace {

double high(const Scalar& s) {
    switch (s.kind) {
        case Scalar::Kind::Constant: return s.a;
        case Scalar::Kind::Random: return std::max(s.a, s.b);
        case Scalar::Kind::Curve: {
            double top = 0;
            for (const CurveKey& k : s.keys) {
                top = std::max(top, k.v);
            }
            return top;
        }
    }
    return s.a;
}

}  // namespace

double busiest(const Layer& layer) {
    double alive = 0;
    const Scalar* life = property<Scalar>(layer, "initial", "lifetime");
    const double seconds = life ? high(*life) : 1.0;
    if (const Scalar* rate = property<Scalar>(layer, "emission", "rate")) {
        alive += high(*rate) * std::min(seconds, layer.duration);
    }
    if (const BurstList* bursts = property<BurstList>(layer, "emission", "bursts")) {
        // Bursts close together overlap; ones far apart do not.
        double most = 0;
        for (const Burst& b : bursts->items) {
            double together = 0;
            for (const Burst& other : bursts->items) {
                if (other.time <= b.time && b.time - other.time < seconds) {
                    together += static_cast<double>(other.count);
                }
            }
            most = std::max(most, together);
        }
        alive += most;
    }
    return alive;
}

bool isParticles(const Layer& layer) { return busiest(layer) > 2.5; }

bool isAdditive(const Layer& layer) {
    const std::string* blend = property<std::string>(layer, "sprite", "blend");
    return blend && *blend == "additive";
}

std::string shapeOf(const Layer& layer) {
    const std::string* shape = property<std::string>(layer, "sprite", "shape");
    return shape ? *shape : std::string();
}

std::string roleOf(const Layer& layer) {
    if (!layer.role.empty()) {
        return layer.role;
    }
    const std::string shape = shapeOf(layer);
    const double* stretchSeconds = property<double>(layer, "sprite", "stretch");
    if (isParticles(layer)) {
        if (shape == "smoke" || shape == "puff") {
            return shape == "smoke" ? "smoke" : "body";
        }
        if (shape == "streak" || shape == "sliver" || (stretchSeconds && *stretchSeconds > 0.0)) {
            return "streaks";
        }
        return "sparks";
    }
    if (shape == "ring" || shape == "shockwave" || shape == "shardring") {
        return "ring";
    }
    if (shape == "soft" || shape == "flare") {
        return "glow";
    }
    if (shape == "starflash" || shape == "burst" || shape == "glint" || shape == "twinkle" || shape == "rays" ||
        shape == "sparkle") {
        return "rays";
    }
    return "body";
}

void scaleSize(Layer& layer, double factor) {
    if (Scalar* size = property<Scalar>(layer, "initial", "size")) {
        scale(*size, factor);
    }
}

void scaleReach(Layer& layer, double factor) {
    if (Scalar* speed = property<Scalar>(layer, "initial", "speed")) {
        scale(*speed, factor);
    }
    if (double* radius = property<double>(layer, "shape", "radius")) {
        *radius *= factor;
    }
    if (Vec3* size = property<Vec3>(layer, "shape", "size")) {
        scale(*size, factor);
    }
    if (Vec3* offset = property<Vec3>(layer, "shape", "offset")) {
        scale(*offset, factor);
    }
    if (Vec3* gravity = property<Vec3>(layer, "motion", "gravity")) {
        scale(*gravity, factor);
    }
}

void scaleEverything(Layer& layer, double factor) {
    scaleSize(layer, factor);
    scaleReach(layer, factor);
}

void scaleCount(Layer& layer, double factor, std::int64_t least) {
    if (Scalar* rate = property<Scalar>(layer, "emission", "rate")) {
        scale(*rate, factor);
    }
    if (BurstList* bursts = property<BurstList>(layer, "emission", "bursts")) {
        for (Burst& b : bursts->items) {
            b.count = std::max<std::int64_t>(std::min(least, b.count), std::llround(static_cast<double>(b.count) * factor));
        }
    }
}

void scaleBrightness(Layer& layer, double factor) {
    Color* colour = property<Color>(layer, "initial", "color");
    if (!colour) {
        return;
    }
    double* glow = isAdditive(layer) ? property<double>(layer, "sprite", "glow") : nullptr;
    // Opacity carries brightness up to full; past that, an additive layer's
    // Glow takes over.
    double level = colour->a * (glow ? std::max(*glow, 0.0) : 1.0) * factor;
    if (glow) {
        if (level > 1.0) {
            colour->a = 1.0;
            *glow = std::min(level, 50.0);
        } else {
            colour->a = std::max(level, 0.0);
            *glow = 1.0;
        }
    } else {
        colour->a = std::clamp(level, 0.0, 1.0);
    }
}

void scaleTime(Layer& layer, double factor) {
    if (!(factor > 0.0)) {
        return;
    }
    layer.start /= factor;
    layer.duration = std::max(0.01, layer.duration / factor);
    if (Scalar* life = property<Scalar>(layer, "initial", "lifetime")) {
        scale(*life, 1.0 / factor);
        life->a = std::max(life->a, 0.001);
        life->b = std::max(life->b, 0.001);
    }
    if (Scalar* speed = property<Scalar>(layer, "initial", "speed")) {
        scale(*speed, factor);
    }
    if (Vec3* gravity = property<Vec3>(layer, "motion", "gravity")) {
        scale(*gravity, factor * factor);
    }
    if (double* drag = property<double>(layer, "motion", "drag")) {
        *drag *= factor;
    }
    if (Scalar* spin = property<Scalar>(layer, "motion", "spin")) {
        scale(*spin, factor);
    }
    if (Scalar* rate = property<Scalar>(layer, "emission", "rate")) {
        scale(*rate, factor);
    }
    if (BurstList* bursts = property<BurstList>(layer, "emission", "bursts")) {
        for (Burst& b : bursts->items) {
            b.time /= factor;
        }
    }
    if (double* stretch = property<double>(layer, "sprite", "stretch")) {
        *stretch /= factor;
    }
    if (double* trail = property<double>(layer, "sprite", "trail")) {
        *trail /= factor;
    }
    if (double* fps = property<double>(layer, "sprite", "fps")) {
        *fps = std::clamp(*fps * factor, 0.1, 240.0);
    }
}

void setColour(Layer& layer, const Color& colour, bool keepOpacity) {
    if (Color* c = property<Color>(layer, "initial", "color")) {
        const double alpha = c->a;
        *c = colour;
        if (keepOpacity) {
            c->a = alpha;
        }
    }
}

void setShape(Layer& layer, const char* shape) {
    if (std::string* s = property<std::string>(layer, "sprite", "shape")) {
        *s = shape;
    }
}

}  // namespace vfx::editor::tools
