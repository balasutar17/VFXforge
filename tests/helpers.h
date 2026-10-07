// Shared test helpers.
#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "vfx/Command.h"
#include "vfx/CommandStack.h"
#include "vfx/Document.h"
#include "vfx/Effect.h"
#include "vfx/Serialize.h"
#include "vfx/Templates.h"

namespace testing {

// A deterministic effect with two emitter layers and one texture asset that
// the first layer's sprite refers to.
inline vfx::Effect sampleEffect(std::uint64_t seed = 42) {
    vfx::IdGenerator ids(seed);
    const vfx::IdSource newId = [&ids]() { return ids.next(); };

    vfx::Effect effect = vfx::makeEmptyEffect(newId, "Sample", false);

    vfx::Asset asset;
    asset.id = newId();
    asset.path = "textures/spark.png";
    asset.hash = "sha256:0000";
    effect.assets.push_back(asset);

    effect.layers.push_back(vfx::makeBasicEmitter(newId, "Sparks"));
    effect.layers.push_back(vfx::makeBasicEmitter(newId, "Smoke"));

    auto& sprite = effect.layers[0].modules.back();
    *sprite.find("texture") = vfx::AssetRef{asset.id};
    return effect;
}

inline std::string text(const vfx::Document& document) {
    return vfx::writeEffect(document.effect());
}

inline const vfx::Module& moduleOfType(const vfx::Layer& layer, const char* type) {
    for (const auto& m : layer.modules) {
        if (m.type == type) {
            return m;
        }
    }
    throw std::runtime_error(std::string("no module of type ") + type);
}

inline vfx::Path propertyPath(const vfx::Effect& effect, std::size_t layer, const char* type,
                              const char* key) {
    const auto& l = effect.layers.at(layer);
    return vfx::Path::property(l.id, moduleOfType(l, type).id, key);
}

inline vfx::CommandPtr set(vfx::Path path, vfx::Value value) {
    return std::make_unique<vfx::SetPropertyCommand>(std::move(path), std::move(value));
}

// splitmix64, so test randomness is identical on every platform and compiler.
class Rng {
public:
    explicit Rng(std::uint64_t seed) : state_(seed) {}

    std::uint64_t next() {
        state_ += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // 0 <= result < n
    std::size_t below(std::size_t n) { return n == 0 ? 0 : static_cast<std::size_t>(next() % n); }
    // 0 <= result < 1
    double unit() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
    double between(double lo, double hi) { return lo + (hi - lo) * unit(); }
    bool chance(double p) { return unit() < p; }

private:
    std::uint64_t state_;
};

}  // namespace testing
