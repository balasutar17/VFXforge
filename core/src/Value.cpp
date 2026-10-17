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

std::vector<CurveKey> flattenCurve(const std::vector<CurveKey>& keys, int pieces) {
    const std::size_t n = keys.size();
    bool bent = false;
    for (const CurveKey& k : keys) {
        bent = bent || k.smooth;
    }
    std::vector<CurveKey> out;
    if (!bent || n < 2 || pieces < 2) {
        out = keys;
        for (CurveKey& k : out) {
            k.smooth = false;
        }
        return out;
    }

    // The steepness of each straight stretch, and its length.
    std::vector<double> rise(n - 1), run(n - 1);
    for (std::size_t i = 0; i + 1 < n; ++i) {
        run[i] = keys[i + 1].t - keys[i].t;
        rise[i] = run[i] > 0.0 ? (keys[i + 1].v - keys[i].v) / run[i] : 0.0;
    }
    // The steepness of the curve at each smooth point. Level at an end, a
    // peak or a dip. On the way up or down, a blend of the two sides that
    // leans toward the gentler one, which is what keeps the curve from
    // swinging past its points (the rule of Fritsch and Butland).
    std::vector<double> slope(n, 0.0);
    for (std::size_t i = 1; i + 1 < n; ++i) {
        if (!keys[i].smooth) {
            continue;
        }
        const double before = rise[i - 1], after = rise[i];
        if (before * after > 0.0) {
            const double w1 = 2.0 * run[i] + run[i - 1], w2 = run[i] + 2.0 * run[i - 1];
            slope[i] = (w1 + w2) / (w1 / before + w2 / after);
        }
    }

    out.reserve(n + (n - 1) * static_cast<std::size_t>(pieces - 1));
    out.push_back(CurveKey{keys[0].t, keys[0].v, false});
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const CurveKey& a = keys[i];
        const CurveKey& b = keys[i + 1];
        // Two corners, or points too close together to bend between.
        if ((a.smooth || b.smooth) && run[i] > 1e-4) {
            // A corner next to a smooth point leaves as directly as the bend
            // allows: no curl of its own.
            const double m0 = a.smooth ? slope[i] : 0.5 * (3.0 * rise[i] - slope[i + 1]);
            const double m1 = b.smooth ? slope[i + 1] : 0.5 * (3.0 * rise[i] - slope[i]);
            const double low = a.v < b.v ? a.v : b.v, high = a.v < b.v ? b.v : a.v;
            for (int j = 1; j < pieces; ++j) {
                const double s = static_cast<double>(j) / static_cast<double>(pieces);
                const double s2 = s * s, s3 = s2 * s;
                double v = (2.0 * s3 - 3.0 * s2 + 1.0) * a.v + (s3 - 2.0 * s2 + s) * run[i] * m0 +
                           (-2.0 * s3 + 3.0 * s2) * b.v + (s3 - s2) * run[i] * m1;
                v = v < low ? low : (v > high ? high : v);
                out.push_back(CurveKey{a.t + s * run[i], v, false});
            }
        }
        out.push_back(CurveKey{b.t, b.v, false});
    }
    return out;
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
        case ValueKind::Points: return "points";
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
        case ValueKind::Points: return "a list of points";
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
        case ValueKind::Points: return std::holds_alternative<PointList>(value);
    }
    return false;
}

}  // namespace vfx
