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

inline void add(Effect& effect, Make& layer) {
    layer.attachSound(effect);
    effect.layers.push_back(layer.done(effect.duration));
}

}  // namespace vfx::editor::make
