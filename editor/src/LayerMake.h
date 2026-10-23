// VFX Forge editor: building a layer in a few readable lines.
//
// Shared by the preset library and by the reference reconstruction: both
// make ordinary layers out of ordinary modules, and this is the short way to
// write one down. Private to the editor library.
#pragma once

#include <cmath>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "vfx/Effect.h"
#include "vfx/Metadata.h"
#include "vfx/Templates.h"
#include "vfx/Value.h"
#include "vfx/editor/Session.h"
#include "vfx/editor/SoundLibrary.h"

namespace vfx::editor::make {

// ------------------------------------------------------------ small tools

// A colour as an artist writes it (0xRRGGBB, as on screen), stored linear.
inline Color hex(unsigned rgb, double alpha = 1.0) {
    return Color{srgbToLinear(((rgb >> 16) & 0xffu) / 255.0), srgbToLinear(((rgb >> 8) & 0xffu) / 255.0),
                 srgbToLinear((rgb & 0xffu) / 255.0), alpha};
}

using Keys = std::initializer_list<CurveKey>;

struct Tint {
    double t, r, g, b, a;
};

// One layer under construction. Starts as the standard emitter, so every
// preset layer has the same modules and the same Simple controls.
class Make {
public:
    Make(const IdSource& newId, const char* name) : newId_(newId), layer_(makeBasicEmitter(newId, name)) {}

    // ---- what it is for
    Make& role(const char* word) {
        layer_.role = word;
        return *this;
    }
    Make& colorLinear(const Color& c) { return put("initial", "color", c); }

    // ---- when
    Make& from(double start, double length) {
        layer_.start = start;
        layer_.duration = length;
        timed_ = true;
        return *this;
    }

    // ---- how many
    Make& rate(double perSecond) { return put("emission", "rate", Scalar::constant(perSecond)); }
    Make& burst(std::int64_t count, double at = 0.0) {
        bursts_.items.push_back(Burst{at, count});
        put("emission", "bursts", bursts_);
        if (!rated_) {
            put("emission", "rate", Scalar::constant(0.0));
        }
        return *this;
    }
    Make& alsoRate(double perSecond) {
        rated_ = true;
        return rate(perSecond);
    }

    // ---- where
    Make& at(double x, double y) { return put("shape", "offset", Vec3{x, y, 0.0}); }
    Make& circle(double radius, bool edge = false) {
        put("shape", "shape", std::string("circle"));
        put("shape", "radius", radius);
        return put("shape", "emitFrom", std::string(edge ? "edge" : "volume"));
    }
    Make& rect(double width, double height, bool edge = false) {
        put("shape", "shape", std::string("rectangle"));
        put("shape", "size", Vec3{width, height, 1.0});
        return put("shape", "emitFrom", std::string(edge ? "edge" : "volume"));
    }

    // ---- what each particle starts with
    Make& life(double a, double b) { return put("initial", "lifetime", range(a, b)); }
    Make& speed(double a, double b) { return put("initial", "speed", range(a, b)); }
    Make& size(double a, double b) { return put("initial", "size", range(a, b)); }
    Make& turn(double a, double b) { return put("initial", "rotation", range(a, b)); }
    // Heading in degrees: 0 is right, 90 is up.
    Make& aim(double heading, double spread) {
        put("initial", "direction", directionFromHeading(Heading{heading, 0.0}));
        return put("initial", "spread", spread);
    }
    Make& color(unsigned rgb, double alpha = 1.0) { return put("initial", "color", hex(rgb, alpha)); }

    // ---- how it moves
    Make& gravity(double x, double y) { return put("motion", "gravity", Vec3{x, y, 0.0}); }
    Make& drag(double amount) { return put("motion", "drag", amount); }
    Make& spin(double a, double b) { return put("motion", "spin", range(a, b)); }
    // Carried along a field of swirls: how fast, how large the swirls are,
    // and how quickly they are replaced by new ones.
    Make& turbulence(double speed, double swirlSize = 1.5, double change = 0.6) {
        put("motion", "turbulence", speed);
        put("motion", "turbulenceSize", swirlSize);
        return put("motion", "turbulenceChange", change);
    }

    // ---- the animation principles
    // How fast it travels its own path from birth to death, as a multiplier
    // on its own movement: slow in and out, a wind-up, an overshoot.
    Make& pace(Keys keys) { return put("motion", "speedOverLife", Scalar::curve(keys)); }
    // Turned about the layer's centre, in degrees a second: arcs and spirals.
    Make& orbit(double degreesPerSecond) { return put("motion", "orbit", degreesPerSecond); }
    // Squash and stretch over its life: above 0 longer and thinner, below 0
    // shorter and wider. Or one amount each, between two.
    Make& squash(Keys keys) { return put("overLife", "stretch", Scalar::curve(keys)); }
    Make& squash(double a, double b) { return put("overLife", "stretch", range(a, b)); }
    // Shown this many times a second, each drawing held: 12 is "on twos".
    Make& hold(double drawingsPerSecond) { return put("sprite", "hold", drawingsPerSecond); }

    // ---- how it changes over its life
    Make& sizeOver(Keys keys) { return put("overLife", "size", Scalar::curve(keys)); }
    Make& fade(Keys keys) { return put("overLife", "opacity", Scalar::curve(keys)); }
    Make& sizeOver(std::vector<CurveKey> keys) { return put("overLife", "size", Scalar::curve(std::move(keys))); }
    Make& fade(std::vector<CurveKey> keys) { return put("overLife", "opacity", Scalar::curve(std::move(keys))); }
    Make& tint(const std::vector<Tint>& keys) {
        Gradient gradient;
        for (const Tint& k : keys) {
            gradient.keys.push_back(GradientKey{k.t, Color{k.r, k.g, k.b, k.a}});
        }
        return put("overLife", "color", gradient);
    }
    Make& solid() { return put("overLife", "opacity", Scalar::constant(1.0)); }
    // Eaten away in a ragged pattern over this last share of its life.
    // Eaten away in ragged patches over the last share of its life. edgeGlow
    // is how brightly the eaten edge burns (0 for smoke and mist); soft is
    // how soft that edge is (0 torn like paper, 1 thinning like smoke).
    Make& dissolve(double share, double edgeGlow = 0.0, double soft = 0.15) {
        put("overLife", "dissolveEdge", edgeGlow);
        put("overLife", "dissolveSoft", soft);
        return put("overLife", "dissolve", share);
    }
    Make& tint(std::initializer_list<Tint> keys) {
        Gradient gradient;
        for (const Tint& k : keys) {
            gradient.keys.push_back(GradientKey{k.t, Color{k.r, k.g, k.b, k.a}});
        }
        return put("overLife", "color", gradient);
    }

    // ---- how it is drawn
    Make& look(const char* shape) { return put("sprite", "shape", std::string(shape)); }
    Make& additive() { return put("sprite", "blend", std::string("additive")); }
    Make& glow(double amount) { return put("sprite", "glow", amount); }
    // Draws each particle as a streak along its movement, and gives the
    // layer a Simple control for the streak's length.
    Make& streak(double seconds) {
        put("sprite", "align", std::string("movement"));
        put("sprite", "stretch", seconds);
        for (const Module& m : layer_.modules) {
            if (m.type == "sprite") {
                SimpleControl control;
                control.id = newId_();
                control.label = "Streak";
                control.targets.push_back(ControlTarget{m.id, "stretch"});
                layer_.controls.push_back(std::move(control));
            }
        }
        return *this;
    }

    // Lays each particle back as if on the ground, seen from an angle. It
    // still turns in its own plane. (Every layer has a Lie Flat control.)
    Make& lieFlat(double degrees) { return put("sprite", "tilt", degrees); }

    // ---- 3D
    // The layer's flat parts lie on the ground: its circle or rectangle,
    // and the turn of Orbit.
    Make& ground() { return put("shape", "plane", std::string("ground")); }
    // Each particle lies flat on the layer's plane (with ground(), on the floor).
    Make& flatOnPlane() { return put("sprite", "facing", std::string("plane")); }
    Make& sphere(double radius, bool edge = false) {
        put("shape", "shape", std::string("sphere"));
        put("shape", "radius", radius);
        return put("shape", "emitFrom", std::string(edge ? "edge" : "volume"));
    }
    Make& at3(double x, double y, double z) { return put("shape", "offset", Vec3{x, y, z}); }

    // ---- the Mesh shape (see vfx/MeshData.h): "surface", "edges" or "silhouette"
    Make& mesh(const char* kind, PointList data, std::int64_t samples = 16000) {
        put("shape", "shape", std::string("mesh"));
        put("shape", "meshKind", std::string(kind));
        put("shape", "meshData", std::move(data));
        return put("shape", "meshDensity", Value(samples));
    }
    // Restricts the surface to a band that crosses the model: "height" or "ripple".
    Make& scan(const char* mode, double sweepsPerSecond, double width, std::int64_t bands = 1,
               Vec3 origin = Vec3{0, 0, 0}) {
        put("shape", "scanMode", std::string(mode));
        put("shape", "scanSpeed", sweepsPerSecond);
        put("shape", "scanWidth", width);
        put("shape", "scanBands", Value(bands));
        return put("shape", "scanOrigin", origin);
    }
    // How far off the surface (along its normal) particles are born.
    Make& surfaceOffset(double units) { return put("shape", "padding", units); }

    // ---- the Path shape
    Make& path(PointList points, bool closed = true) {
        put("shape", "shape", std::string("path"));
        put("shape", "path", std::move(points));
        put("shape", "closed", closed);
        return put("shape", "emitFrom", std::string("edge"));
    }
    // A straight line from (x0, y0) to (x1, y1).
    Make& line(double x0, double y0, double x1, double y1) {
        return path(PointList{{{x0, y0}, {x1, y1}}}, false);
    }
    Make& fillPath() { return put("shape", "emitFrom", std::string("volume")); }
    // Follows the effect's target picture when there is one.
    Make& fitTarget() { return put("shape", "fitTarget", true); }
    Make& padding(double units) { return put("shape", "padding", units); }
    Make& band(double width) { return put("shape", "thickness", width); }
    Make& travel(double roundsPerSecond, double span = 0.2, std::int64_t heads = 1) {
        put("shape", "travel", roundsPerSecond);
        put("shape", "travelSpan", span);
        return put("shape", "travelHeads", Value(heads));
    }
    Make& drawOn(double roundsPerSecond) {
        put("shape", "travel", roundsPerSecond);
        return put("shape", "travelMode", std::string("draw"));
    }
    Make& sendOut() { return put("shape", "aim", std::string("outward")); }
    Make& sendAlong() { return put("shape", "aim", std::string("along")); }
    Make& alongPath() { return put("shape", "alignToPath", true); }
    Make& toward(double x, double y, double z, double spread) {
        put("initial", "direction", Vec3{x, y, z});
        return put("initial", "spread", spread);
    }

    // A ribbon behind each particle, with a Simple control for its length.
    Make& trail(double seconds, double width) {
        put("sprite", "trail", seconds);
        put("sprite", "trailWidth", width);
        for (const Module& m : layer_.modules) {
            if (m.type == "sprite") {
                SimpleControl control;
                control.id = newId_();
                control.label = "Trail";
                control.targets.push_back(ControlTarget{m.id, "trail"});
                layer_.controls.push_back(std::move(control));
            }
        }
        return *this;
    }

    // A sound from the built-in library, played when the layer starts (or at
    // each burst), with a delay and a volume.
    Make& sound(const char* id, bool atBursts = false, double delay = 0.0, double volume = 1.0,
                double randomPitch = 0.0) {
        sound_ = id;
        soundBursts_ = atBursts;
        soundDelay_ = delay;
        soundVolume_ = volume;
        soundRandomPitch_ = randomPitch;
        return *this;
    }
    Make& soundLoop() {
        soundLoop_ = true;
        return *this;
    }
    const std::string& soundId() const { return sound_; }

    // Adds the layer's sound to the effect: the library sound as an asset
    // (shared by every layer that uses it) and a Sound module.
    void attachSound(Effect& effect) {
        if (sound_.empty()) {
            return;
        }
        const std::string path = librarySoundPath(sound_);
        Id asset;
        for (const Asset& a : effect.assets) {
            if (a.path == path) {
                asset = a.id;
            }
        }
        if (!asset.valid()) {
            Asset a;
            a.id = newId_();
            a.kind = "sound";
            a.path = path;
            asset = a.id;
            effect.assets.push_back(a);
        }
        Module m = makeModule(*Registry::builtin().findModule("sound"), newId_());
        *m.find("sound") = AssetRef{asset};
        *m.find("play") = std::string(soundBursts_ ? "bursts" : "start");
        *m.find("delay") = soundDelay_;
        *m.find("volume") = soundVolume_;
        *m.find("randomPitch") = soundRandomPitch_;
        *m.find("loop") = soundLoop_;
        if (soundLoop_) {
            *m.find("fadeIn") = 0.3;
            *m.find("fadeOut") = 0.4;
        }
        layer_.modules.push_back(std::move(m));
    }

    Layer done(double effectDuration) {
        if (!timed_) {
            layer_.start = 0.0;
            layer_.duration = effectDuration;
        }
        return std::move(layer_);
    }

private:
    static Scalar range(double a, double b) {
        return a == b ? Scalar::constant(a) : Scalar::random(a < b ? a : b, a < b ? b : a);
    }

public:
    Make& put(const char* type, const char* key, Value value) {
        for (Module& m : layer_.modules) {
            if (m.type == type) {
                if (Value* slot = m.find(key)) {
                    *slot = std::move(value);
                }
            }
        }
        return *this;
    }

private:
    const IdSource& newId_;
    Layer layer_;
    std::string sound_;
    bool soundBursts_ = false;
    double soundDelay_ = 0.0, soundVolume_ = 1.0, soundRandomPitch_ = 0.0;
    bool soundLoop_ = false;
    BurstList bursts_;
    bool timed_ = false;
    bool rated_ = false;
};

inline Effect begin(const IdSource& newId, const char* name, double duration) {
    Effect effect = makeEmptyEffect(newId, name, false);
    effect.duration = duration;
    effect.loop = "loop";
    effect.seed = 7;
    return effect;
}

// A 3D effect: the same, with depth.
inline Effect begin3d(const IdSource& newId, const char* name, double duration) {
    Effect effect = begin(newId, name, duration);
    effect.space = "3d";
    return effect;
}

inline void add(Effect& effect, Make& layer) {
    layer.attachSound(effect);
    effect.layers.push_back(layer.done(effect.duration));
}

}  // namespace vfx::editor::make
