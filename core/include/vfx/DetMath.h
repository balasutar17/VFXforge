// VFX Forge core: maths that gives the same bits on every machine.
//
// An effect must look the same on Windows and macOS, on Intel and Apple
// Silicon, in the editor and later in a game engine. Plain arithmetic and
// square roots already behave identically everywhere. The system maths
// library does not: sin, cos, pow, exp and cbrt differ slightly between
// platforms. So the simulation never calls them. It uses the functions here,
// which are built only from operations that are identical everywhere.
//
// The build also forbids fused multiply-add (-ffp-contract=off), which would
// otherwise make Arm and Intel round differently.
#pragma once

#include <cstdint>

namespace vfx::det {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 6.28318530717958647692;
inline constexpr double kDegreesToRadians = kPi / 180.0;

// ---------------------------------------------------------------- hashing

// A strong 64-bit mixer (the splitmix64 finaliser). Every random number in
// the simulation is a mix of things that identify what it is for, so there
// is no shared generator whose state could depend on timing or thread order.
inline constexpr std::uint64_t mix64(std::uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// One key per emitter: the effect's seed mixed with the layer's ID. Layers
// therefore never share random numbers, and removing one layer cannot
// change how another looks.
inline constexpr std::uint64_t emitterKey(std::uint64_t effectSeed, std::uint64_t layerId) {
    return mix64(mix64(effectSeed + 0x9E3779B97F4A7C15ull) ^ layerId);
}

// One key per particle: which pass of the effect it was born in and its
// number within that pass.
inline constexpr std::uint64_t particleKey(std::uint64_t emitter, std::uint64_t pass,
                                           std::uint64_t index) {
    return mix64(mix64(emitter + pass * 0x9E3779B97F4A7C15ull) + index * 0xBF58476D1CE4E5B9ull);
}

// One independent stream per property of a particle.
inline constexpr std::uint64_t channel(std::uint64_t particle, std::uint32_t which) {
    return mix64(particle + (static_cast<std::uint64_t>(which) + 1) * 0x94D049BB133111EBull);
}

// 0 <= result < 1, using the top 24 bits so every value is exact in a float.
inline constexpr float unitFloat(std::uint64_t h) {
    return static_cast<float>(h >> 40) * (1.0f / 16777216.0f);
}

// ------------------------------------------------------------ trigonometry

// Sine and cosine of an angle in radians, accurate to about 1e-15 for the
// angles the simulation uses, and identical on every platform.
void sinCos(double radians, double& sine, double& cosine);

// Cube root for 0 <= x <= 1 (used to spread particles evenly through a ball).
float cbrt01(float x);

// ----------------------------------------------------------------- vectors

struct Vec3f {
    float x = 0, y = 0, z = 0;
};

// Length 1, or the fallback when the input is too short to have a direction.
Vec3f normalizeOr(Vec3f v, Vec3f fallback);

// Two unit vectors at right angles to n and to each other. n must be unit.
void basis(Vec3f n, Vec3f& tangent, Vec3f& bitangent);

}  // namespace vfx::det
