#include "vfx/DetMath.h"

#include <cmath>

// Only these <cmath> functions may be used anywhere in the simulation:
// sqrt, floor, fabs, copysign. Each is exactly specified, so each gives the
// same answer everywhere. A test scans the simulation sources for the others.

namespace vfx::det {

double atan2(double y, double x) {
    if (!(x == x) || !(y == y)) {
        return 0.0;
    }
    const double ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    if (ax == 0.0 && ay == 0.0) {
        return 0.0;
    }
    // atan of the smaller over the larger, in [0, 1], halved twice so the
    // series converges fast: atan(t) = 2 atan(t / (1 + sqrt(1 + t*t))).
    const bool swap = ay > ax;
    double t = swap ? ax / ay : ay / ax;
    t = t / (1.0 + std::sqrt(1.0 + t * t));
    t = t / (1.0 + std::sqrt(1.0 + t * t));
    // t <= tan(pi/16) ~ 0.199: the Taylor series to t^23 is well within 1e-12.
    const double t2 = t * t;
    double sum = 0.0, term = t;
    for (int k = 0; k < 12; ++k) {
        sum += term / static_cast<double>(2 * k + 1) * (k % 2 == 0 ? 1.0 : -1.0);
        term *= t2;
    }
    double angle = 4.0 * sum;
    if (swap) {
        angle = kPi / 2.0 - angle;
    }
    if (x < 0) {
        angle = kPi - angle;
    }
    return y < 0 ? -angle : angle;
}

void sinCos(double x, double& sine, double& cosine) {
    if (!(x == x) || x > 1e300 || x < -1e300) {  // not a number, or infinite
        sine = 0.0;
        cosine = 1.0;
        return;
    }
    if (x > 1e6 || x < -1e6) {
        // Far outside anything the simulation produces. Bring it into range;
        // some precision is lost, but the answer is still the same everywhere.
        x -= kTwoPi * std::floor(x / kTwoPi);
    }

    // Split the angle into a whole number of quarter turns plus a remainder
    // within an eighth of a turn either side of zero.
    constexpr double kTwoOverPi = 0.63661977236758134308;
    constexpr double kHalfPiHigh = 1.57079632673412561417e+00;  // first 33 bits of pi/2
    constexpr double kHalfPiLow = 6.07710050650619224932e-11;   // pi/2 minus the above
    const double q = std::floor(x * kTwoOverPi + 0.5);
    const double r = (x - q * kHalfPiHigh) - q * kHalfPiLow;
    const double r2 = r * r;

    // Polynomials from fdlibm (k_sin.c, k_cos.c), good to about one unit in
    // the last place across the remainder's range.
    constexpr double S1 = -1.66666666666666324348e-01;
    constexpr double S2 = 8.33333333332248946124e-03;
    constexpr double S3 = -1.98412698298579493134e-04;
    constexpr double S4 = 2.75573137070700676789e-06;
    constexpr double S5 = -2.50507602534068634195e-08;
    constexpr double S6 = 1.58969099521155010221e-10;
    constexpr double C1 = 4.16666666666666019037e-02;
    constexpr double C2 = -1.38888888888741095749e-03;
    constexpr double C3 = 2.48015872894767294178e-05;
    constexpr double C4 = -2.75573143513906633035e-07;
    constexpr double C5 = 2.08757232129817482790e-09;
    constexpr double C6 = -1.13596475577881948265e-11;

    const double s =
        r + r * r2 * (S1 + r2 * (S2 + r2 * (S3 + r2 * (S4 + r2 * (S5 + r2 * S6)))));
    const double c =
        1.0 - 0.5 * r2 + r2 * r2 * (C1 + r2 * (C2 + r2 * (C3 + r2 * (C4 + r2 * (C5 + r2 * C6)))));

    // Which quarter turn: q modulo 4, as 0, 1, 2 or 3.
    const double quarter = q - 4.0 * std::floor(q * 0.25);
    if (quarter < 0.5) {
        sine = s;
        cosine = c;
    } else if (quarter < 1.5) {
        sine = c;
        cosine = -s;
    } else if (quarter < 2.5) {
        sine = -s;
        cosine = -c;
    } else {
        sine = -c;
        cosine = s;
    }
}

float cbrt01(float value) {
    if (!(value > 0.0f)) {
        return 0.0f;
    }
    const double x = value;
    // The fourth root is exact to compute and close to the cube root, so it
    // is a good place to start. Halley's method then triples the number of
    // correct digits each round; a fixed count keeps the result identical
    // everywhere.
    double y = std::sqrt(std::sqrt(x));
    for (int i = 0; i < 6; ++i) {
        const double y3 = y * y * y;
        y = y * (y3 + 2.0 * x) / (2.0 * y3 + x);
    }
    return static_cast<float>(y);
}

Vec3f normalizeOr(Vec3f v, Vec3f fallback) {
    const float lengthSquared = v.x * v.x + v.y * v.y + v.z * v.z;
    if (!(lengthSquared > 1e-20f) || !(lengthSquared < 1e30f)) {
        return fallback;
    }
    const float inverse = 1.0f / std::sqrt(lengthSquared);
    return Vec3f{v.x * inverse, v.y * inverse, v.z * inverse};
}

void basis(Vec3f n, Vec3f& tangent, Vec3f& bitangent) {
    // Duff et al., "Building an Orthonormal Basis, Revisited" (2017). No
    // branches and no special cases, so it is the same on every machine.
    const float sign = std::copysign(1.0f, n.z);
    const float a = -1.0f / (sign + n.z);
    const float b = n.x * n.y * a;
    tangent = Vec3f{1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x};
    bitangent = Vec3f{b, sign + n.y * n.y * a, -n.y};
}

namespace {

// A direction for each corner of the grid, in each slice of time: three
// numbers from -1 to 1. The pattern slopes that way at that corner. (A
// pattern made from plain heights at the corners would be level at every
// corner, and particles sitting on one would never move.)
struct Lean {
    float x, y, z;
};
inline Lean corner(std::uint64_t key, std::int64_t slice, std::int32_t ix, std::int32_t iy, std::int32_t iz) {
    std::uint64_t h = mix64(key + static_cast<std::uint64_t>(slice) * 0x9E3779B97F4A7C15ull);
    h = mix64(h ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(ix)) |
                   (static_cast<std::uint64_t>(static_cast<std::uint32_t>(iy)) << 32)));
    h = mix64(h + static_cast<std::uint64_t>(static_cast<std::uint32_t>(iz)) * 0xBF58476D1CE4E5B9ull);
    const std::uint64_t more = mix64(h + 0x94D049BB133111EBull);
    return Lean{unitFloat(h) * 2.0f - 1.0f, unitFloat(h << 24) * 2.0f - 1.0f, unitFloat(more) * 2.0f - 1.0f};
}

// Eases from 0 to 1 with no jolt at either end, and how steeply.
inline float ease(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
inline float easeSlope(float t) { return 30.0f * t * t * (t * (t - 2.0f) + 1.0f); }
inline float mixed(float a, float b, float t) { return a + (b - a) * t; }

// Which grid cell a coordinate is in, and how far across it. Far outside
// anything an effect uses, the cell number is held so it cannot overflow.
inline void cellOf(float v, std::int32_t& cell, float& across) {
    if (!(v > -1.0e6f)) {
        v = -1.0e6f;
    } else if (!(v < 1.0e6f)) {
        v = 1.0e6f;
    }
    float whole = static_cast<float>(static_cast<std::int32_t>(v));
    if (whole > v) {
        whole -= 1.0f;
    }
    cell = static_cast<std::int32_t>(whole);
    across = v - whole;
}

// The slope of a smooth 2D pattern, at one slice of time. The pattern's
// height at a point is each corner's lean times the way to the point, the
// four eased together.
inline void slope2(std::uint64_t key, std::int64_t slice, std::int32_t ix, std::int32_t iy, float tx, float ty,
                   float& du, float& dv) {
    const Lean g00 = corner(key, slice, ix, iy, 0), g10 = corner(key, slice, ix + 1, iy, 0);
    const Lean g01 = corner(key, slice, ix, iy + 1, 0), g11 = corner(key, slice, ix + 1, iy + 1, 0);
    const float n00 = g00.x * tx + g00.y * ty, n10 = g10.x * (tx - 1.0f) + g10.y * ty;
    const float n01 = g01.x * tx + g01.y * (ty - 1.0f), n11 = g11.x * (tx - 1.0f) + g11.y * (ty - 1.0f);
    const float fx = ease(tx), fy = ease(ty);
    du = mixed(mixed(g00.x, g10.x, fx), mixed(g01.x, g11.x, fx), fy) + easeSlope(tx) * mixed(n10 - n00, n11 - n01, fy);
    dv = mixed(mixed(g00.y, g10.y, fx), mixed(g01.y, g11.y, fx), fy) +
         easeSlope(ty) * (mixed(n01, n11, fx) - mixed(n00, n10, fx));
}

// The slope of a smooth 3D pattern, at one slice of time.
inline void slope3(std::uint64_t key, std::int64_t slice, std::int32_t ix, std::int32_t iy, std::int32_t iz, float tx,
                   float ty, float tz, float& du, float& dv, float& dw) {
    Lean g[2][2][2];
    float n[2][2][2];
    for (int k = 0; k < 2; ++k) {
        for (int j = 0; j < 2; ++j) {
            for (int i = 0; i < 2; ++i) {
                g[k][j][i] = corner(key, slice, ix + i, iy + j, iz + k);
                n[k][j][i] = g[k][j][i].x * (tx - static_cast<float>(i)) + g[k][j][i].y * (ty - static_cast<float>(j)) +
                             g[k][j][i].z * (tz - static_cast<float>(k));
            }
        }
    }
    const float fx = ease(tx), fy = ease(ty), fz = ease(tz);
    // Eases the eight corners' values of something together.
    const auto all = [&](const auto& of) {
        const float x00 = mixed(of(0, 0, 0), of(0, 0, 1), fx), x01 = mixed(of(0, 1, 0), of(0, 1, 1), fx);
        const float x10 = mixed(of(1, 0, 0), of(1, 0, 1), fx), x11 = mixed(of(1, 1, 0), of(1, 1, 1), fx);
        return mixed(mixed(x00, x01, fy), mixed(x10, x11, fy), fz);
    };
    const float leanX = all([&](int k, int j, int i) { return g[k][j][i].x; });
    const float leanY = all([&](int k, int j, int i) { return g[k][j][i].y; });
    const float leanZ = all([&](int k, int j, int i) { return g[k][j][i].z; });
    // How the height changes across the cell in each direction, the other two eased.
    const float acrossX = mixed(mixed(n[0][0][1] - n[0][0][0], n[0][1][1] - n[0][1][0], fy),
                                mixed(n[1][0][1] - n[1][0][0], n[1][1][1] - n[1][1][0], fy), fz);
    const float acrossY = mixed(mixed(n[0][1][0], n[0][1][1], fx) - mixed(n[0][0][0], n[0][0][1], fx),
                                mixed(n[1][1][0], n[1][1][1], fx) - mixed(n[1][0][0], n[1][0][1], fx), fz);
    const float acrossZ = mixed(mixed(n[1][0][0], n[1][0][1], fx), mixed(n[1][1][0], n[1][1][1], fx), fy) -
                          mixed(mixed(n[0][0][0], n[0][0][1], fx), mixed(n[0][1][0], n[0][1][1], fx), fy);
    du = leanX + easeSlope(tx) * acrossX;
    dv = leanY + easeSlope(ty) * acrossY;
    dw = leanZ + easeSlope(tz) * acrossZ;
}

// So that the speed averages about one. (Measured: see the turbulence test.)
constexpr float kFlatScale = 1.577f;
constexpr float kDeepScale = 0.927f;

}  // namespace

Vec3f turbulence(std::uint64_t key, float perUnit, float perSecond, bool flat, float x, float y, float z,
                 double time) {
    // The pattern at one whole moment, the pattern at the next, and an eased
    // blend between the two.
    const double moment = time * static_cast<double>(perSecond);
    auto slice = static_cast<std::int64_t>(moment);
    if (static_cast<double>(slice) > moment) {
        --slice;
    }
    const float blend = ease(static_cast<float>(moment - static_cast<double>(slice)));

    std::int32_t ix = 0, iy = 0, iz = 0;
    float tx = 0, ty = 0, tz = 0;
    cellOf(x * perUnit, ix, tx);
    cellOf(y * perUnit, iy, ty);
    Vec3f out;
    if (flat) {
        float du0 = 0, dv0 = 0, du1 = 0, dv1 = 0;
        slope2(key, slice, ix, iy, tx, ty, du0, dv0);
        slope2(key, slice + 1, ix, iy, tx, ty, du1, dv1);
        const float du = du0 + (du1 - du0) * blend, dv = dv0 + (dv1 - dv0) * blend;
        // Round the hills of the pattern, never up or down them.
        out.x = dv * kFlatScale;
        out.y = -du * kFlatScale;
        return out;
    }
    cellOf(z * perUnit, iz, tz);
    float d[3][3];  // d[which pattern][which direction]
    for (int which = 0; which < 3; ++which) {
        const std::uint64_t one = mix64(key + static_cast<std::uint64_t>(which + 1) * 0x94D049BB133111EBull);
        float a0 = 0, b0 = 0, c0 = 0, a1 = 0, b1 = 0, c1 = 0;
        slope3(one, slice, ix, iy, iz, tx, ty, tz, a0, b0, c0);
        slope3(one, slice + 1, ix, iy, iz, tx, ty, tz, a1, b1, c1);
        d[which][0] = a0 + (a1 - a0) * blend;
        d[which][1] = b0 + (b1 - b0) * blend;
        d[which][2] = c0 + (c1 - c0) * blend;
    }
    out.x = (d[2][1] - d[1][2]) * kDeepScale;
    out.y = (d[0][2] - d[2][0]) * kDeepScale;
    out.z = (d[1][0] - d[0][1]) * kDeepScale;
    return out;
}

}  // namespace vfx::det
