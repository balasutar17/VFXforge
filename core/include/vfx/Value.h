// VFX Forge core: the value types a property can hold.
#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "vfx/Id.h"

namespace vfx {

struct Vec3 {
    double x = 0, y = 0, z = 0;
    bool operator==(const Vec3&) const = default;
};

// Linear colour, straight (not premultiplied) alpha. RGB may exceed 1.
struct Color {
    double r = 1, g = 1, b = 1, a = 1;
    bool operator==(const Color&) const = default;
};

struct CurveKey {
    double t = 0;  // 0..1 across the particle's life
    double v = 0;
    bool operator==(const CurveKey&) const = default;
};

// A number that may be fixed, random per particle, or change over life.
// Phase 1 evaluates curves linearly; tangents arrive with the curve editor.
struct Scalar {
    enum class Kind { Constant, Random, Curve };

    Kind kind = Kind::Constant;
    double a = 0;                // constant value, or random minimum
    double b = 0;                // random maximum
    std::vector<CurveKey> keys;  // used when kind == Curve

    static Scalar constant(double value);
    static Scalar random(double minimum, double maximum);
    static Scalar curve(std::vector<CurveKey> keys);

    bool operator==(const Scalar&) const = default;
};

struct GradientKey {
    double t = 0;
    Color color;
    bool operator==(const GradientKey&) const = default;
};

struct Gradient {
    std::vector<GradientKey> keys;
    bool operator==(const Gradient&) const = default;
};

struct Burst {
    double time = 0;  // seconds from the layer's start
    std::int64_t count = 0;
    bool operator==(const Burst&) const = default;
};

struct BurstList {
    std::vector<Burst> items;
    bool operator==(const BurstList&) const = default;
};

// A reference to an asset in the effect's asset list. An invalid ID is "none".
struct AssetRef {
    Id id;
    bool operator==(const AssetRef&) const = default;
};

using Value = std::variant<bool, std::int64_t, double, std::string, Vec3, Color,
                           Scalar, Gradient, BurstList, AssetRef>;

// What a property is declared to hold. Text and Enum both store a string.
enum class ValueKind { Bool, Int, Float, Text, Enum, Vec3, Color, Scalar, Gradient, Bursts, Asset };

const char* kindName(ValueKind kind);

// The same thing in words an artist would use, for error messages:
// "a number", "a colour", "a number, a random range or a curve".
const char* kindDescription(ValueKind kind);

// True when the variant alternative is the one this kind stores.
bool holdsKind(const Value& value, ValueKind kind);

}  // namespace vfx
