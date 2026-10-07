// Helpers for building small effects and looking at simulated particles.
#pragma once

#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#include "helpers.h"
#include "vfx/DetMath.h"
#include "vfx/Program.h"
#include "vfx/Simulation.h"

namespace testing {

// One basic emitter layer in an effect, with a fixed set of IDs.
inline vfx::Effect oneLayer(bool threeD = false, std::uint64_t idSeed = 11) {
    vfx::IdGenerator ids(idSeed);
    const vfx::IdSource newId = [&ids]() { return ids.next(); };
    vfx::Effect effect = vfx::makeEmptyEffect(newId, "Test", threeD);
    effect.duration = 2.0;
    effect.loop = "once";
    effect.seed = 7;
    effect.layers.push_back(vfx::makeBasicEmitter(newId, "Layer"));
    return effect;
}

inline vfx::Module& module(vfx::Layer& layer, const char* type) {
    for (auto& m : layer.modules) {
        if (m.type == type) {
            return m;
        }
    }
    throw std::runtime_error(std::string("no module of type ") + type);
}

// Sets a property directly. The value's type must match the property's.
inline void put(vfx::Layer& layer, const char* type, const char* key, vfx::Value value) {
    vfx::Value* slot = module(layer, type).find(key);
    if (!slot || slot->index() != value.index()) {
        throw std::runtime_error(std::string("bad property in test: ") + type + "." + key);
    }
    *slot = std::move(value);
}

inline void removeModule(vfx::Layer& layer, const char* type) {
    for (auto it = layer.modules.begin(); it != layer.modules.end(); ++it) {
        if (it->type == type) {
            layer.modules.erase(it);
            return;
        }
    }
}

inline vfx::Value constant(double v) { return vfx::Value(vfx::Scalar::constant(v)); }
inline vfx::Value range(double a, double b) { return vfx::Value(vfx::Scalar::random(a, b)); }
inline vfx::Value text(const char* s) { return vfx::Value(std::string(s)); }
inline vfx::Value number(double v) { return vfx::Value(v); }
inline vfx::Value vec(double x, double y, double z) { return vfx::Value(vfx::Vec3{x, y, z}); }

// A burst of `count` particles at time zero and nothing else: the simplest
// way to get a known set of particles to look at.
inline void burstOnly(vfx::Layer& layer, std::int64_t count, double at = 0.0) {
    put(layer, "emission", "rate", constant(0));
    put(layer, "emission", "bursts", vfx::Value(vfx::BurstList{{{at, count}}}));
}

inline vfx::Simulation simulate(const vfx::Effect& effect, std::int64_t steps) {
    vfx::Simulation sim(vfx::compileEffect(effect));
    sim.seek(steps);
    return sim;
}

inline std::int64_t stepsFor(double seconds) {
    return static_cast<std::int64_t>(seconds / vfx::kSimulationStep + 1e-6);
}

inline bool allFinite(const vfx::Simulation& sim) {
    for (std::size_t e = 0; e < sim.emitterCount(); ++e) {
        const vfx::ParticleView v = sim.particles(e);
        for (std::uint32_t i = 0; i < v.count; ++i) {
            for (float f : {v.x[i], v.y[i], v.z[i], v.vx[i], v.vy[i], v.vz[i], v.age[i],
                            v.lifetime[i], v.size[i], v.rotation[i]}) {
                if (!std::isfinite(f)) {
                    return false;
                }
            }
        }
    }
    return true;
}

}  // namespace testing
