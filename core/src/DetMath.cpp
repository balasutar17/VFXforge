#include "vfx/DetMath.h"

#include <cmath>

// Only these <cmath> functions may be used anywhere in the simulation:
// sqrt, floor, fabs, copysign. Each is exactly specified, so each gives the
// same answer everywhere. A test scans the simulation sources for the others.

namespace vfx::det {

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

}  // namespace vfx::det
