#include "vfx/Value.h"

#include <utility>

namespace vfx {

Scalar Scalar::constant(double value) {
    Scalar s;
    s.kind = Kind::Constant;
    s.a = value;
    return s;
}

Scalar Scalar::random(double minimum, double maximum) {
    Scalar s;
    s.kind = Kind::Random;
    s.a = minimum;
    s.b = maximum;
    return s;
}

Scalar Scalar::curve(std::vector<CurveKey> keys) {
    Scalar s;
    s.kind = Kind::Curve;
    s.keys = std::move(keys);
    return s;
}

const char* kindName(ValueKind kind) {
    switch (kind) {
        case ValueKind::Bool: return "bool";
        case ValueKind::Int: return "int";
        case ValueKind::Float: return "float";
        case ValueKind::Text: return "text";
        case ValueKind::Enum: return "enum";
        case ValueKind::Vec3: return "vec3";
        case ValueKind::Color: return "color";
        case ValueKind::Scalar: return "scalar";
        case ValueKind::Gradient: return "gradient";
        case ValueKind::Bursts: return "bursts";
        case ValueKind::Asset: return "asset";
    }
    return "unknown";
}

const char* kindDescription(ValueKind kind) {
    switch (kind) {
        case ValueKind::Bool: return "true or false";
        case ValueKind::Int: return "a whole number";
        case ValueKind::Float: return "a number";
        case ValueKind::Text: return "text";
        case ValueKind::Enum: return "one of its listed options";
        case ValueKind::Vec3: return "three numbers";
        case ValueKind::Color: return "a colour";
        case ValueKind::Scalar: return "a number, a random range or a curve";
        case ValueKind::Gradient: return "a colour gradient";
        case ValueKind::Bursts: return "a list of bursts";
        case ValueKind::Asset: return "an asset";
    }
    return "a value";
}

bool holdsKind(const Value& value, ValueKind kind) {
    switch (kind) {
        case ValueKind::Bool: return std::holds_alternative<bool>(value);
        case ValueKind::Int: return std::holds_alternative<std::int64_t>(value);
        case ValueKind::Float: return std::holds_alternative<double>(value);
        case ValueKind::Text:
        case ValueKind::Enum: return std::holds_alternative<std::string>(value);
        case ValueKind::Vec3: return std::holds_alternative<Vec3>(value);
        case ValueKind::Color: return std::holds_alternative<Color>(value);
        case ValueKind::Scalar: return std::holds_alternative<Scalar>(value);
        case ValueKind::Gradient: return std::holds_alternative<Gradient>(value);
        case ValueKind::Bursts: return std::holds_alternative<BurstList>(value);
        case ValueKind::Asset: return std::holds_alternative<AssetRef>(value);
    }
    return false;
}

}  // namespace vfx
