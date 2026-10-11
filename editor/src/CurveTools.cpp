#include "vfx/editor/CurveTools.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace vfx::editor {

namespace {

double within(const PropertyDesc& desc, double v) {
    if (!std::isfinite(v)) {
        v = 0.0;
    }
    if (desc.min && v < *desc.min) {
        v = *desc.min;
    }
    if (desc.max && v > *desc.max) {
        v = *desc.max;
    }
    return v;
}

constexpr bool S = true;  // a smooth point

std::vector<CurveShape> makeShapes() {
    return {
        // ---- level: where it stands, start to end
        {"steady", "Steady", "Level", "The same all the way through.", {{0, 1}, {1, 1}}},
        {"fade-out", "Fade out", "Level", "From full to nothing at an even pace.", {{0, 1}, {1, 0}}},
        {"fade-in", "Fade in", "Level", "From nothing to full at an even pace.", {{0, 0}, {1, 1}}},
        {"in-and-out", "In, hold, out", "Level", "Up quickly, stay, and away at the end.",
         {{0, 0}, {0.15, 1}, {0.7, 1}, {1, 0}}},

        // ---- ease: the same journeys, with weight
        {"slow-start", "Slow start", "Ease", "Gathers speed from rest and arrives at full tilt (slow out).",
         {{0, 0, S}, {1, 1}}},
        {"slow-stop", "Slow stop", "Ease", "Sets off at once and settles gently (slow in).", {{0, 0}, {1, 1, S}}},
        {"slow-both", "Slow start and stop", "Ease", "Eases away from rest and eases to a stop.",
         {{0, 0, S}, {1, 1, S}}},
        {"ease-away", "Ease away", "Ease", "Holds on at first, then lets go and settles to nothing.",
         {{0, 1, S}, {1, 0, S}}},
        {"quick-drop", "Quick drop", "Ease", "Most of it gone at once; the rest lingers.",
         {{0, 1}, {0.2, 0.3, S}, {1, 0, S}}},
        {"linger", "Linger, then go", "Ease", "Stays almost full, then goes quickly at the end.",
         {{0, 1, S}, {0.65, 0.9, S}, {1, 0}}},

        // ---- life: shapes with character
        {"pop", "Pop and settle", "Life", "Shoots past where it is going and settles back (overshoot).",
         {{0, 0}, {0.18, 1, S}, {0.42, 0.8, S}, {1, 0.84, S}}},
        {"wind-up", "Wind up, then go", "Life", "Draws back a little before it goes (anticipation).",
         {{0, 0.25, S}, {0.18, 0.12, S}, {0.5, 1, S}, {1, 1}}},
        {"swell", "Swell", "Life", "Rises to full in the middle and sinks away again.",
         {{0, 0, S}, {0.5, 1, S}, {1, 0, S}}},
        {"pulse", "Pulse", "Life", "Breathes twice between low and full.",
         {{0, 0.55, S}, {0.25, 1, S}, {0.5, 0.55, S}, {0.75, 1, S}, {1, 0.55, S}}},
        {"flicker", "Flicker", "Life", "Jumps about unevenly and dies, like a flame or a failing light.",
         {{0, 1}, {0.1, 0.5}, {0.18, 0.9}, {0.3, 0.4}, {0.42, 1}, {0.55, 0.6}, {0.7, 0.95}, {0.82, 0.3}, {1, 0}}},

        // ---- squash and stretch: above 0 longer and thinner, below 0 shorter and wider
        {"stretch-settle", "Stretch, then settle", "Squash and stretch",
         "Long and thin as it sets off, a small squash as it slows, then its own shape.",
         {{0, 0.7}, {0.3, -0.12, S}, {0.5, 0, S}, {1, 0}}, "overLife.stretch"},
        {"squash-spring", "Squash, then spring", "Squash and stretch",
         "Lands flat, springs up past its own shape, and settles.",
         {{0, -0.5}, {0.18, 0.3, S}, {0.36, -0.12, S}, {0.55, 0, S}, {1, 0}}, "overLife.stretch"},
        {"wobble", "Wobble", "Squash and stretch", "Tall, flat, tall, flat, less each time, like jelly coming to rest.",
         {{0, 0.4, S}, {0.17, -0.4, S}, {0.33, 0.25, S}, {0.5, -0.15, S}, {0.67, 0.08, S}, {0.83, 0, S}, {1, 0}},
         "overLife.stretch"},
        {"stretch-away", "Stretch away", "Squash and stretch", "Its own shape at first, longer and thinner the further it goes.",
         {{0, 0, S}, {1, 0.9}}, "overLife.stretch"},
        {"breathe", "Breathe", "Squash and stretch", "A slow, slight swell and sink, twice over: something alive and waiting.",
         {{0, -0.12, S}, {0.25, 0.12, S}, {0.5, -0.12, S}, {0.75, 0.12, S}, {1, -0.12, S}}, "overLife.stretch"},

        // ---- pace: 1 is the particle's own speed, 0 standing still, below 0 backwards
        {"burst-coast", "Burst and coast", "Pace", "Off at once, then coasting to a stop (slow in).",
         {{0, 2.6}, {0.3, 0.5, S}, {1, 0.05, S}}, "motion.speedOverLife"},
        {"gather-speed", "Gather speed", "Pace", "From almost nothing to twice its speed (slow out).",
         {{0, 0.05, S}, {1, 2.0}}, "motion.speedOverLife"},
        {"slow-in-out", "Slow in and out", "Pace", "Eases away from rest, fastest in the middle, eases to rest.",
         {{0, 0.1, S}, {0.5, 1.9, S}, {1, 0.1, S}}, "motion.speedOverLife"},
        {"wind-up-go", "Back off, then go", "Pace", "Backs away first, then is off faster than it would have been (anticipation).",
         {{0, -0.7}, {0.16, -0.5, S}, {0.24, 2.4}, {1, 0.6, S}}, "motion.speedOverLife"},
        {"overshoot-settle", "Overshoot and settle", "Pace",
         "Flies past where it will rest, comes back a little, and stays (follow-through).",
         {{0, 3.0}, {0.14, 0.9, S}, {0.26, -0.75, S}, {0.42, 0.12, S}, {0.6, 0, S}, {1, 0}}, "motion.speedOverLife"},
        {"out-and-back", "Out and back", "Pace", "Thrown out, slowed, and drawn back to where it began.",
         {{0, 2.2}, {0.3, 0, S}, {0.55, -0.25, S}, {1, -0.75}}, "motion.speedOverLife"},
        {"hit-pause", "Hit pause", "Pace", "Stops dead for a beat, then carries on: the held moment of an impact.",
         {{0, 1.6}, {0.2, 1.6}, {0.21, 0}, {0.38, 0}, {0.39, 1.2}, {1, 0.8}}, "motion.speedOverLife"},
    };
}

}  // namespace

const std::vector<CurveShape>& curveShapes() {
    static const std::vector<CurveShape> shapes = makeShapes();
    return shapes;
}

const CurveShape* findCurveShape(std::string_view key) {
    for (const CurveShape& shape : curveShapes()) {
        if (key == shape.key) {
            return &shape;
        }
    }
    return nullptr;
}

std::vector<const CurveShape*> curveShapesFor(std::string_view moduleType, const PropertyDesc& desc) {
    const std::string name = std::string(moduleType) + "." + desc.key;
    std::vector<const CurveShape*> out;
    for (const CurveShape& shape : curveShapes()) {
        if (shape.only && name == shape.only) {
            out.push_back(&shape);
        }
    }
    for (const CurveShape& shape : curveShapes()) {
        if (!shape.only) {
            out.push_back(&shape);
        }
    }
    return out;
}

double levelOf(const Scalar& value) {
    switch (value.kind) {
        case Scalar::Kind::Constant:
            return std::fabs(value.a);
        case Scalar::Kind::Random:
            return std::max(std::fabs(value.a), std::fabs(value.b));
        case Scalar::Kind::Curve: {
            double top = 0.0;
            for (const CurveKey& k : value.keys) {
                top = std::max(top, std::fabs(k.v));
            }
            return top;
        }
    }
    return 0.0;
}

std::vector<CurveKey> curveFrom(const Scalar& value) {
    if (value.kind == Scalar::Kind::Curve && !value.keys.empty()) {
        return value.keys;
    }
    const double level = value.kind == Scalar::Kind::Random ? 0.5 * (value.a + value.b) : value.a;
    return {{0.0, level}, {1.0, level}};
}

Scalar shaped(const CurveShape& shape, const Scalar& now, const PropertyDesc& desc) {
    if (shape.only) {
        // Made for this setting, in its own numbers.
        return Scalar::curve(tidyCurve(shape.keys, desc));
    }
    double level = levelOf(now);
    if (!(level > 1e-9)) {
        // Standing at nothing: go by the property's ordinary value, or by
        // the top of its slider, or by 1.
        if (const auto* usual = std::get_if<Scalar>(&desc.defaultValue)) {
            level = levelOf(*usual);
        }
        if (!(level > 1e-9)) {
            level = desc.uiMax && *desc.uiMax > 0.0 ? *desc.uiMax : 1.0;
        }
    }
    // A value that stands below nothing (a backward spin) keeps its sign.
    const std::vector<CurveKey> was = curveFrom(now);
    double sum = 0.0;
    for (const CurveKey& k : was) {
        sum += k.v;
    }
    const double sign = sum < 0.0 ? -1.0 : 1.0;
    std::vector<CurveKey> keys = shape.keys;
    for (CurveKey& k : keys) {
        k.v *= sign * level;
    }
    return Scalar::curve(tidyCurve(std::move(keys), desc));
}

std::vector<CurveKey> tidyCurve(std::vector<CurveKey> keys, const PropertyDesc& desc) {
    for (CurveKey& k : keys) {
        k.t = std::isfinite(k.t) ? std::clamp(k.t, 0.0, 1.0) : 0.0;
        k.v = within(desc, k.v);
    }
    std::stable_sort(keys.begin(), keys.end(), [](const CurveKey& a, const CurveKey& b) { return a.t < b.t; });
    // Keep them apart, pushing later points along; one that would be pushed
    // off the end is dropped.
    std::vector<CurveKey> out;
    for (const CurveKey& k : keys) {
        CurveKey next = k;
        if (!out.empty() && next.t < out.back().t + kCurveGap) {
            next.t = out.back().t + kCurveGap;
        }
        if (next.t > 1.0) {
            continue;
        }
        out.push_back(next);
        if (out.size() == kEditorCurvePoints) {
            break;
        }
    }
    if (out.empty()) {
        double usual = 0.0;
        if (const auto* s = std::get_if<Scalar>(&desc.defaultValue)) {
            usual = curveFrom(*s).front().v;
        }
        out.push_back(CurveKey{0.0, within(desc, usual), false});
    }
    return out;
}

double curveAt(const std::vector<CurveKey>& keys, double t) {
    const std::vector<CurveKey> flat = flattenCurve(keys);
    if (flat.empty()) {
        return 0.0;
    }
    if (!(t > flat.front().t)) {
        return flat.front().v;
    }
    if (!(t < flat.back().t)) {
        return flat.back().v;
    }
    for (std::size_t i = 0; i + 1 < flat.size(); ++i) {
        if (t < flat[i + 1].t) {
            const double f = (t - flat[i].t) / (flat[i + 1].t - flat[i].t);
            return flat[i].v + (flat[i + 1].v - flat[i].v) * f;
        }
    }
    return flat.back().v;
}

Gradient tidyGradient(Gradient gradient) {
    for (GradientKey& k : gradient.keys) {
        k.t = std::isfinite(k.t) ? std::clamp(k.t, 0.0, 1.0) : 0.0;
        const auto channel = [](double v, double top) { return std::isfinite(v) ? std::clamp(v, 0.0, top) : 0.0; };
        k.color.r = channel(k.color.r, 1000.0);
        k.color.g = channel(k.color.g, 1000.0);
        k.color.b = channel(k.color.b, 1000.0);
        k.color.a = channel(k.color.a, 1.0);
    }
    std::stable_sort(gradient.keys.begin(), gradient.keys.end(),
                     [](const GradientKey& a, const GradientKey& b) { return a.t < b.t; });
    Gradient out;
    for (const GradientKey& k : gradient.keys) {
        GradientKey next = k;
        if (!out.keys.empty() && next.t < out.keys.back().t + kCurveGap) {
            next.t = out.keys.back().t + kCurveGap;
        }
        if (next.t > 1.0) {
            continue;
        }
        out.keys.push_back(next);
        if (out.keys.size() == kEditorCurvePoints) {
            break;
        }
    }
    if (out.keys.empty()) {
        out.keys.push_back(GradientKey{0.0, Color{1, 1, 1, 1}});
    }
    return out;
}

Color gradientAt(const Gradient& gradient, double t) {
    const auto& keys = gradient.keys;
    if (keys.empty()) {
        return Color{1, 1, 1, 1};
    }
    if (!(t > keys.front().t)) {
        return keys.front().color;
    }
    if (!(t < keys.back().t)) {
        return keys.back().color;
    }
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        if (t < keys[i + 1].t) {
            const double f = (t - keys[i].t) / (keys[i + 1].t - keys[i].t);
            const Color& a = keys[i].color;
            const Color& b = keys[i + 1].color;
            return Color{a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f,
                         a.a + (b.a - a.a) * f};
        }
    }
    return keys.back().color;
}

BurstList tidyBursts(BurstList bursts, double layerLength) {
    const double last = std::isfinite(layerLength) && layerLength > 0.0 ? layerLength : 3600.0;
    for (Burst& b : bursts.items) {
        b.time = std::isfinite(b.time) ? std::clamp(b.time, 0.0, last) : 0.0;
        b.count = std::clamp<std::int64_t>(b.count, 0, 100000);
    }
    std::stable_sort(bursts.items.begin(), bursts.items.end(),
                     [](const Burst& a, const Burst& b) { return a.time < b.time; });
    if (bursts.items.size() > 64) {
        bursts.items.resize(64);
    }
    return bursts;
}

}  // namespace vfx::editor
