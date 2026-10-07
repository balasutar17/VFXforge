// Random but always valid property values, for property tests.
#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "helpers.h"
#include "vfx/Effect.h"
#include "vfx/Metadata.h"

namespace testing {

using namespace vfx;

inline double randomNumber(const PropertyDesc& p, Rng& rng) {
    double lo = p.min.value_or(-100.0);
    double hi = p.max.value_or(100.0);
    if (rng.chance(0.6)) {
        // Mostly stay in the range an artist would actually drag through.
        lo = std::max(lo, p.uiMin.value_or(lo));
        hi = std::min(hi, p.uiMax.value_or(hi));
    }
    switch (rng.below(8)) {
        case 0: return lo;
        case 1: return hi;
        case 2: return std::clamp(0.1, lo, hi);
        case 3: return std::clamp(1.0 / 3.0, lo, hi);
        default: return rng.between(lo, hi);
    }
}

inline Color randomColor(Rng& rng) {
    return Color{rng.between(0, 2), rng.between(0, 2), rng.between(0, 2), rng.unit()};
}

inline Value randomValue(const PropertyDesc& p, Rng& rng, const Effect& effect) {
    switch (p.kind) {
        case ValueKind::Bool:
            return Value(rng.chance(0.5));
        case ValueKind::Int:
            return Value(static_cast<std::int64_t>(randomNumber(p, rng)));
        case ValueKind::Float:
            return Value(randomNumber(p, rng));
        case ValueKind::Text: {
            static const char* names[] = {"Sparks", "", "Étincelles", "火花 ✨", "a \"quoted\" name",
                                          "line\nbreak", "tab\there", "back\\slash", "שלום"};
            return Value(std::string(names[rng.below(std::size(names))]));
        }
        case ValueKind::Enum:
            return Value(p.options[rng.below(p.options.size())]);
        case ValueKind::Vec3:
            return Value(Vec3{randomNumber(p, rng), randomNumber(p, rng), randomNumber(p, rng)});
        case ValueKind::Color:
            return Value(randomColor(rng));
        case ValueKind::Scalar: {
            switch (rng.below(3)) {
                case 0:
                    return Value(Scalar::constant(randomNumber(p, rng)));
                case 1: {
                    double a = randomNumber(p, rng), b = randomNumber(p, rng);
                    if (a > b) {
                        std::swap(a, b);
                    }
                    return Value(Scalar::random(a, b));
                }
                default: {
                    const std::size_t n = 1 + rng.below(5);
                    std::vector<CurveKey> keys;
                    for (std::size_t i = 0; i < n; ++i) {
                        const double t = (static_cast<double>(i) + rng.unit() * 0.9) /
                                         static_cast<double>(n);
                        keys.push_back(CurveKey{t, randomNumber(p, rng)});
                    }
                    return Value(Scalar::curve(std::move(keys)));
                }
            }
        }
        case ValueKind::Gradient: {
            const std::size_t n = 1 + rng.below(4);
            Gradient g;
            for (std::size_t i = 0; i < n; ++i) {
                const double t =
                    (static_cast<double>(i) + rng.unit() * 0.9) / static_cast<double>(n);
                g.keys.push_back(GradientKey{t, randomColor(rng)});
            }
            return Value(std::move(g));
        }
        case ValueKind::Bursts: {
            BurstList list;
            const std::size_t n = rng.below(4);
            for (std::size_t i = 0; i < n; ++i) {
                list.items.push_back(
                    Burst{rng.between(0, 5), static_cast<std::int64_t>(rng.below(200))});
            }
            return Value(std::move(list));
        }
        case ValueKind::Asset:
            if (effect.assets.empty() || rng.chance(0.3)) {
                return Value(AssetRef{});
            }
            return Value(AssetRef{effect.assets[rng.below(effect.assets.size())].id});
    }
    return p.defaultValue;
}

}  // namespace testing
