#include "vfx/Metadata.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Utf8.h"

namespace vfx {

namespace {

// Small fluent builder so the tables below read like a specification.
struct P {
    PropertyDesc d;

    P(std::string key, std::string label, ValueKind kind, Value def) {
        d.key = std::move(key);
        d.label = std::move(label);
        d.kind = kind;
        d.defaultValue = std::move(def);
    }
    P& simple() { d.level = Level::Simple; return *this; }
    P& unit(std::string u) { d.unit = std::move(u); return *this; }
    P& help(std::string h) { d.help = std::move(h); return *this; }
    P& range(double lo, double hi) { d.min = lo; d.max = hi; return *this; }
    P& ui(double lo, double hi) { d.uiMin = lo; d.uiMax = hi; return *this; }
    P& options(std::vector<std::string> o) { d.options = std::move(o); return *this; }
    operator PropertyDesc() const { return d; }
};

Value text(const char* s) { return Value(std::string(s)); }
Value num(double v) { return Value(v); }
Value integer(std::int64_t v) { return Value(v); }

ModuleTypeDesc module(std::string type, std::string label, Stage stage,
                      std::vector<PropertyDesc> properties) {
    ModuleTypeDesc m;
    m.type = std::move(type);
    m.label = std::move(label);
    m.stage = stage;
    m.properties = std::move(properties);
    return m;
}

}  // namespace

const PropertyDesc* ModuleTypeDesc::find(std::string_view key) const {
    const int i = indexOf(key);
    return i < 0 ? nullptr : &properties[static_cast<std::size_t>(i)];
}

int ModuleTypeDesc::indexOf(std::string_view key) const {
    for (std::size_t i = 0; i < properties.size(); ++i) {
        if (properties[i].key == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

Registry::Registry() {
    const Color white{1, 1, 1, 1};

    modules_.push_back(module("emission", "Emission", Stage::Spawn, {
        P("rate", "Amount", ValueKind::Scalar, Scalar::constant(20))
            .simple().unit("per second").range(0, 100000).ui(0, 200)
            .help("How many particles appear each second."),
        P("bursts", "Bursts", ValueKind::Bursts, BurstList{})
            .help("Groups of particles released at set moments."),
    }));

    modules_.push_back(module("shape", "Shape", Stage::Spawn, {
        P("shape", "Shape", ValueKind::Enum, text("point"))
            .options({"point", "circle", "rectangle", "sphere", "box", "cone"})
            .help("Where new particles appear."),
        P("radius", "Radius", ValueKind::Float, num(1.0))
            .range(0, 10000).ui(0, 10)
            .help("Size of a circle, sphere or cone base."),
        P("size", "Size", ValueKind::Vec3, Vec3{1, 1, 1})
            .range(0, 10000).ui(0, 10)
            .help("Width, height and depth of a rectangle or box."),
        P("angle", "Cone Angle", ValueKind::Float, num(25.0))
            .unit("degrees").range(0, 90).ui(0, 90),
        P("emitFrom", "Emit From", ValueKind::Enum, text("volume"))
            .options({"volume", "edge"})
            .help("Fill the whole shape, or only its outline."),
    }));

    modules_.push_back(module("initial", "Initial State", Stage::Spawn, {
        P("lifetime", "Lifetime", ValueKind::Scalar, Scalar::constant(1.0))
            .simple().unit("seconds").range(0.001, 3600).ui(0, 10)
            .help("How long each particle lasts."),
        P("speed", "Speed", ValueKind::Scalar, Scalar::constant(2.0))
            .simple().unit("units per second").range(-10000, 10000).ui(0, 20)
            .help("How fast particles leave the emitter."),
        P("direction", "Direction", ValueKind::Vec3, Vec3{0, 1, 0})
            .simple().range(-1000, 1000).ui(-1, 1)
            .help("Which way particles are sent."),
        P("spread", "Spread", ValueKind::Float, num(30.0))
            .simple().unit("degrees").range(0, 180).ui(0, 180)
            .help("How far particles fan out from the direction."),
        P("size", "Size", ValueKind::Scalar, Scalar::constant(0.2))
            .simple().unit("units").range(0, 10000).ui(0, 5),
        P("rotation", "Rotation", ValueKind::Scalar, Scalar::constant(0.0))
            .unit("degrees").range(-36000, 36000).ui(-180, 180),
        P("color", "Color", ValueKind::Color, white).simple(),
    }));

    modules_.push_back(module("motion", "Motion", Stage::Update, {
        P("gravity", "Gravity", ValueKind::Vec3, Vec3{0, -9.8, 0})
            .unit("units per second squared").range(-10000, 10000).ui(-20, 20),
        P("drag", "Drag", ValueKind::Float, num(0.0))
            .range(0, 1000).ui(0, 10)
            .help("Slows particles down over time."),
        P("spin", "Spin", ValueKind::Scalar, Scalar::constant(0.0))
            .unit("degrees per second").range(-36000, 36000).ui(-360, 360),
    }));

    modules_.push_back(module("overLife", "Over Lifetime", Stage::Update, {
        P("size", "Size", ValueKind::Scalar, Scalar::curve({{0, 1}, {1, 1}}))
            .unit("multiplier").range(0, 1000).ui(0, 2)
            .help("Scales each particle from birth to death."),
        P("color", "Color", ValueKind::Gradient, Gradient{{{0, white}, {1, white}}})
            .help("Tints each particle from birth to death."),
        P("opacity", "Opacity", ValueKind::Scalar, Scalar::curve({{0, 1}, {1, 0}}))
            .range(0, 1).ui(0, 1)
            .help("Fades each particle from birth to death."),
    }));

    modules_.push_back(module("sprite", "Sprite", Stage::Render, {
        P("texture", "Texture", ValueKind::Asset, AssetRef{})
            .help("Your own picture or sprite sheet, drawn for each particle. None draws the Shape."),
        P("blend", "Blend Mode", ValueKind::Enum, text("alpha"))
            .simple()
            .options({"alpha", "additive"})
            .help("Alpha covers what is behind it. Additive adds light, so overlaps glow."),
        P("glow", "Glow", ValueKind::Float, num(1.0))
            .simple().range(0, 1000).ui(0, 10)
            .help("Brightness multiplier. Above 1 the particle glows."),
        P("facing", "Facing", ValueKind::Enum, text("camera"))
            .options({"camera", "plane"})
            .help("Turn toward the camera, or stay flat on the effect's plane."),
        P("shape", "Shape", ValueKind::Enum, text("soft"))
            .simple()
            .options({"soft", "disc", "ring", "bubble", "sparkle", "star", "smoke", "square",
                      "diamond", "heart", "streak", "flame", "puff", "burst", "crescent", "orb",
                      "glint", "blaze"})
            .help("What each particle looks like when it has no texture."),
        P("align", "Align", ValueKind::Enum, text("none"))
            .options({"none", "movement"})
            .help("Keep each particle upright, or point it the way it is moving."),
        P("stretch", "Stretch", ValueKind::Float, num(0.0))
            .unit("seconds").range(0, 10).ui(0, 0.3)
            .help("Draws a moving particle as a streak this many seconds of travel long. "
                  "Needs Align set to movement."),
        P("columns", "Columns", ValueKind::Int, integer(1)).range(1, 64)
            .help("For a sprite sheet: how many pictures across the image."),
        P("rows", "Rows", ValueKind::Int, integer(1)).range(1, 64)
            .help("For a sprite sheet: how many pictures down the image."),
        P("frames", "Frames", ValueKind::Int, integer(0)).range(0, 4096)
            .help("How many pictures the sheet holds, read left to right and top to bottom. "
                  "0 uses every cell."),
        P("animate", "Animate", ValueKind::Enum, text("life"))
            .options({"life", "loop", "random"})
            .help("Play the sheet once over each particle's life, loop it at the frame rate, "
                  "or give each particle one random picture."),
        P("fps", "Frame Rate", ValueKind::Float, num(12.0))
            .unit("frames per second").range(0.1, 240).ui(1, 60)
            .help("How fast a looping sheet plays."),
        P("randomStart", "Random Start", ValueKind::Bool, Value(false))
            .help("Start each particle's loop on a different picture."),
    }));

    effect_ = module("effect", "Effect", Stage::Update, {
        P("name", "Name", ValueKind::Text, text("Untitled")),
        P("space", "Space", ValueKind::Enum, text("2d")).options({"2d", "3d"}),
        P("seed", "Seed", ValueKind::Int, integer(1)).range(0, 4294967295.0)
            .help("Same seed, same result. Change it for a new variation."),
        P("duration", "Duration", ValueKind::Float, num(2.0))
            .unit("seconds").range(0.01, 3600).ui(0, 10),
        P("loop", "Loop", ValueKind::Enum, text("loop")).options({"once", "loop"}),
        P("frameRate", "Frame Rate", ValueKind::Float, num(30.0))
            .unit("frames per second").range(1, 240).ui(12, 60),
    });

    layer_ = module("layer", "Layer", Stage::Update, {
        P("name", "Name", ValueKind::Text, text("Layer")),
        P("enabled", "Enabled", ValueKind::Bool, Value(true)),
        P("start", "Start", ValueKind::Float, num(0.0))
            .unit("seconds").range(0, 3600).ui(0, 10),
        P("duration", "Duration", ValueKind::Float, num(2.0))
            .unit("seconds").range(0.01, 3600).ui(0, 10),
    });
}

const Registry& Registry::builtin() {
    static const Registry instance;
    return instance;
}

const ModuleTypeDesc* Registry::findModule(std::string_view type) const {
    for (const auto& m : modules_) {
        if (m.type == type) {
            return &m;
        }
    }
    return nullptr;
}

namespace {

Error bad(const PropertyDesc& d, const std::string& why) {
    return makeError(d.label + " " + why, "property '" + d.key + "'");
}

std::string trimNumber(double v) {
    std::string s = std::to_string(v);
    while (!s.empty() && s.back() == '0') {
        s.pop_back();
    }
    if (!s.empty() && s.back() == '.') {
        s.pop_back();
    }
    return s;
}

Status checkNumber(const PropertyDesc& d, double v) {
    if (!std::isfinite(v)) {
        return bad(d, "must be a real number.");
    }
    if (d.min && v < *d.min) {
        return bad(d, "cannot be less than " + trimNumber(*d.min) + ".");
    }
    if (d.max && v > *d.max) {
        return bad(d, "cannot be more than " + trimNumber(*d.max) + ".");
    }
    return {};
}

Status checkColor(const PropertyDesc& d, const Color& c) {
    for (double v : {c.r, c.g, c.b, c.a}) {
        if (!std::isfinite(v) || v < 0) {
            return bad(d, "has an invalid colour value.");
        }
    }
    if (c.r > 1000 || c.g > 1000 || c.b > 1000 || c.a > 1) {
        return bad(d, "has a colour value that is out of range.");
    }
    return {};
}

double clampNumber(const PropertyDesc& d, double v) {
    if (d.min && v < *d.min) {
        v = *d.min;
    }
    if (d.max && v > *d.max) {
        v = *d.max;
    }
    return v;
}

Color clampColor(const Color& c) {
    auto fix = [](double v, double hi) {
        if (!std::isfinite(v)) {
            return 1.0;
        }
        return std::clamp(v, 0.0, hi);
    };
    return Color{fix(c.r, 1000), fix(c.g, 1000), fix(c.b, 1000), fix(c.a, 1)};
}

}  // namespace

Status validateValue(const PropertyDesc& d, const Value& value) {
    if (!holdsKind(value, d.kind)) {
        return bad(d, std::string("needs ") + kindDescription(d.kind) + ".");
    }
    switch (d.kind) {
        case ValueKind::Bool:
        case ValueKind::Asset:
            return {};
        case ValueKind::Int: {
            const auto v = static_cast<double>(std::get<std::int64_t>(value));
            return checkNumber(d, v);
        }
        case ValueKind::Float:
            return checkNumber(d, std::get<double>(value));
        case ValueKind::Text: {
            const auto& s = std::get<std::string>(value);
            if (s.size() > kMaxTextLength) {
                return bad(d, "is too long.");
            }
            if (!detail::isValidUtf8(s)) {
                return bad(d, "contains characters that cannot be saved.");
            }
            return {};
        }
        case ValueKind::Enum: {
            const auto& s = std::get<std::string>(value);
            if (std::find(d.options.begin(), d.options.end(), s) == d.options.end()) {
                return bad(d, "does not have an option called \"" + s + "\".");
            }
            return {};
        }
        case ValueKind::Vec3: {
            const auto& v = std::get<Vec3>(value);
            for (double c : {v.x, v.y, v.z}) {
                if (auto s = checkNumber(d, c); !s) {
                    return s;
                }
            }
            return {};
        }
        case ValueKind::Color:
            return checkColor(d, std::get<Color>(value));
        case ValueKind::Scalar: {
            const auto& s = std::get<Scalar>(value);
            switch (s.kind) {
                case Scalar::Kind::Constant:
                    return checkNumber(d, s.a);
                case Scalar::Kind::Random:
                    if (auto r = checkNumber(d, s.a); !r) {
                        return r;
                    }
                    if (auto r = checkNumber(d, s.b); !r) {
                        return r;
                    }
                    if (s.a > s.b) {
                        return bad(d, "has a minimum above its maximum.");
                    }
                    return {};
                case Scalar::Kind::Curve: {
                    if (s.keys.empty() || s.keys.size() > kMaxCurveKeys) {
                        return bad(d, "needs between 1 and 1024 curve points.");
                    }
                    double previous = -1;
                    for (const auto& k : s.keys) {
                        if (!std::isfinite(k.t) || k.t < 0 || k.t > 1 || k.t <= previous) {
                            return bad(d, "has curve points that are out of order.");
                        }
                        previous = k.t;
                        if (auto r = checkNumber(d, k.v); !r) {
                            return r;
                        }
                    }
                    return {};
                }
            }
            return bad(d, "has an unknown form.");
        }
        case ValueKind::Gradient: {
            const auto& g = std::get<Gradient>(value);
            if (g.keys.empty() || g.keys.size() > kMaxCurveKeys) {
                return bad(d, "needs between 1 and 1024 colour stops.");
            }
            double previous = -1;
            for (const auto& k : g.keys) {
                if (!std::isfinite(k.t) || k.t < 0 || k.t > 1 || k.t <= previous) {
                    return bad(d, "has colour stops that are out of order.");
                }
                previous = k.t;
                if (auto r = checkColor(d, k.color); !r) {
                    return r;
                }
            }
            return {};
        }
        case ValueKind::Bursts: {
            const auto& b = std::get<BurstList>(value);
            if (b.items.size() > kMaxBursts) {
                return bad(d, "has too many bursts.");
            }
            for (const auto& item : b.items) {
                if (!std::isfinite(item.time) || item.time < 0 || item.time > 3600) {
                    return bad(d, "has a burst at an invalid time.");
                }
                if (item.count < 0 || item.count > 1000000) {
                    return bad(d, "has a burst with an invalid particle count.");
                }
            }
            return {};
        }
    }
    return bad(d, "has an unknown type.");
}

Value repairValue(const PropertyDesc& d, const Value& value) {
    if (!holdsKind(value, d.kind)) {
        return d.defaultValue;
    }
    Value out = value;
    switch (d.kind) {
        case ValueKind::Bool:
        case ValueKind::Asset:
        case ValueKind::Enum:
            break;
        case ValueKind::Int: {
            const auto v = std::get<std::int64_t>(out);
            const double clamped = clampNumber(d, static_cast<double>(v));
            out = static_cast<std::int64_t>(clamped);
            break;
        }
        case ValueKind::Float:
            out = clampNumber(d, std::get<double>(out));
            break;
        case ValueKind::Text: {
            auto& s = std::get<std::string>(out);
            if (detail::isValidUtf8(s)) {
                s = detail::truncateUtf8(s, kMaxTextLength);
            }
            break;
        }
        case ValueKind::Vec3: {
            auto& v = std::get<Vec3>(out);
            v.x = clampNumber(d, v.x);
            v.y = clampNumber(d, v.y);
            v.z = clampNumber(d, v.z);
            break;
        }
        case ValueKind::Color:
            out = clampColor(std::get<Color>(out));
            break;
        case ValueKind::Scalar: {
            // Only the fields the current form uses are touched; the others
            // stay at zero so equal values always compare equal.
            auto& s = std::get<Scalar>(out);
            if (s.kind == Scalar::Kind::Constant) {
                s.a = clampNumber(d, s.a);
            } else if (s.kind == Scalar::Kind::Random) {
                s.a = clampNumber(d, s.a);
                s.b = clampNumber(d, s.b);
                if (s.a > s.b) {
                    std::swap(s.a, s.b);
                }
            } else if (s.kind == Scalar::Kind::Curve) {
                if (s.keys.size() > kMaxCurveKeys) {
                    s.keys.resize(kMaxCurveKeys);
                }
                for (auto& k : s.keys) {
                    if (std::isfinite(k.t)) {
                        k.t = std::clamp(k.t, 0.0, 1.0);
                    }
                    k.v = clampNumber(d, k.v);
                }
                std::stable_sort(s.keys.begin(), s.keys.end(),
                                 [](const CurveKey& x, const CurveKey& y) { return x.t < y.t; });
                s.keys.erase(std::unique(s.keys.begin(), s.keys.end(),
                                         [](const CurveKey& x, const CurveKey& y) {
                                             return x.t == y.t;
                                         }),
                             s.keys.end());
            }
            break;
        }
        case ValueKind::Gradient: {
            auto& g = std::get<Gradient>(out);
            if (g.keys.size() > kMaxCurveKeys) {
                g.keys.resize(kMaxCurveKeys);
            }
            for (auto& k : g.keys) {
                if (std::isfinite(k.t)) {
                    k.t = std::clamp(k.t, 0.0, 1.0);
                }
                k.color = clampColor(k.color);
            }
            std::stable_sort(g.keys.begin(), g.keys.end(),
                             [](const GradientKey& x, const GradientKey& y) { return x.t < y.t; });
            g.keys.erase(std::unique(g.keys.begin(), g.keys.end(),
                                     [](const GradientKey& x, const GradientKey& y) {
                                         return x.t == y.t;
                                     }),
                         g.keys.end());
            break;
        }
        case ValueKind::Bursts: {
            auto& b = std::get<BurstList>(out);
            if (b.items.size() > kMaxBursts) {
                b.items.resize(kMaxBursts);
            }
            for (auto& item : b.items) {
                if (std::isfinite(item.time)) {
                    item.time = std::clamp(item.time, 0.0, 3600.0);
                }
                item.count = std::clamp<std::int64_t>(item.count, 0, 1000000);
            }
            break;
        }
    }
    if (!validateValue(d, out)) {
        return d.defaultValue;
    }
    return out;
}

}  // namespace vfx
