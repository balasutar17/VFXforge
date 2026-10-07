#include <catch2/catch_amalgamated.hpp>

#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

#include "helpers.h"
#include "vfx/DetMath.h"

using namespace vfx;

TEST_CASE("Deterministic sine and cosine agree with the maths library") {
    double worst = 0.0;
    for (int i = -40000; i <= 40000; ++i) {
        const double x = i * 0.0005;  // -20 to 20 radians
        double s = 0, c = 0;
        det::sinCos(x, s, c);
        worst = std::max(worst, std::fabs(s - std::sin(x)));
        worst = std::max(worst, std::fabs(c - std::cos(x)));
    }
    CHECK(worst < 1e-14);

    // The largest angles the simulation can produce: 36000 degrees.
    worst = 0.0;
    for (int degrees = -36000; degrees <= 36000; degrees += 7) {
        const double x = degrees * det::kDegreesToRadians;
        double s = 0, c = 0;
        det::sinCos(x, s, c);
        worst = std::max(worst, std::fabs(s - std::sin(x)));
        worst = std::max(worst, std::fabs(c - std::cos(x)));
    }
    CHECK(worst < 1e-12);
}

TEST_CASE("Sine and cosine are exact where exactness matters") {
    double s = 1, c = 0;
    det::sinCos(0.0, s, c);
    CHECK(s == 0.0);
    CHECK(c == 1.0);

    for (double x : {0.3, 1.0, 2.5, -4.0, 100.0}) {
        det::sinCos(x, s, c);
        CHECK(std::fabs(s * s + c * c - 1.0) < 1e-15);
        double s2 = 0, c2 = 0;
        det::sinCos(-x, s2, c2);
        CHECK(s2 == -s);  // odd and even symmetry hold to the bit
        CHECK(c2 == c);
    }
}

TEST_CASE("Sine and cosine never return nonsense") {
    const double inf = std::numeric_limits<double>::infinity();
    for (double x : {std::nan(""), inf, -inf, 1e300, -1e300, 1e12, -1e12, 1e7}) {
        double s = 9, c = 9;
        det::sinCos(x, s, c);
        CHECK(std::isfinite(s));
        CHECK(std::isfinite(c));
        CHECK(std::fabs(s) <= 1.0000001);
        CHECK(std::fabs(c) <= 1.0000001);
    }
}

TEST_CASE("Deterministic cube root agrees with the maths library") {
    CHECK(det::cbrt01(0.0f) == 0.0f);
    CHECK(det::cbrt01(-1.0f) == 0.0f);
    CHECK(det::cbrt01(1.0f) == 1.0f);
    CHECK(det::cbrt01(0.125f) == 0.5f);
    for (float x = 1e-12f; x <= 1.0f; x *= 1.37f) {
        INFO(x);
        const float expected = std::cbrt(x);
        CHECK(std::fabs(det::cbrt01(x) - expected) <= expected * 1e-6f);
    }
}

TEST_CASE("The basis is three unit vectors at right angles") {
    testing::Rng rng(3);
    auto check = [](det::Vec3f n) {
        det::Vec3f t, b;
        det::basis(n, t, b);
        auto dot = [](det::Vec3f p, det::Vec3f q) { return p.x * q.x + p.y * q.y + p.z * q.z; };
        INFO(n.x << " " << n.y << " " << n.z);
        CHECK(std::fabs(dot(t, t) - 1.0f) < 1e-5f);
        CHECK(std::fabs(dot(b, b) - 1.0f) < 1e-5f);
        CHECK(std::fabs(dot(t, n)) < 1e-5f);
        CHECK(std::fabs(dot(b, n)) < 1e-5f);
        CHECK(std::fabs(dot(t, b)) < 1e-5f);
    };
    for (det::Vec3f axis : {det::Vec3f{1, 0, 0}, det::Vec3f{-1, 0, 0}, det::Vec3f{0, 1, 0},
                            det::Vec3f{0, -1, 0}, det::Vec3f{0, 0, 1}, det::Vec3f{0, 0, -1}}) {
        check(axis);
    }
    for (int i = 0; i < 2000; ++i) {
        const det::Vec3f v{static_cast<float>(rng.between(-1, 1)),
                           static_cast<float>(rng.between(-1, 1)),
                           static_cast<float>(rng.between(-1, 1))};
        check(det::normalizeOr(v, det::Vec3f{0, 1, 0}));
    }
}

TEST_CASE("Normalising a vector with no direction gives the fallback") {
    const det::Vec3f up{0, 1, 0};
    const det::Vec3f zero = det::normalizeOr(det::Vec3f{0, 0, 0}, up);
    CHECK(zero.y == 1.0f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const det::Vec3f bad = det::normalizeOr(det::Vec3f{nan, 0, 0}, up);
    CHECK(bad.y == 1.0f);
    const det::Vec3f scaled = det::normalizeOr(det::Vec3f{0, 0, 250}, up);
    CHECK(scaled.z == 1.0f);
    CHECK(scaled.y == 0.0f);
}

TEST_CASE("Random numbers are pinned: these exact values on every machine") {
    // If any of these change, every saved effect would look different.
    CHECK(det::mix64(1) == 0x5692161d100b05e5ull);
    CHECK(det::emitterKey(1337, 0x7f3a) == 0x72d95b7fb7c0760bull);
    CHECK(det::particleKey(0x9bca7a0b3e0dd0d5ull, 0, 0) == 0xedd80bacc5052bf1ull);
    CHECK(det::particleKey(0x9bca7a0b3e0dd0d5ull, 3, 41) == 0xe78de6b5e00c75d1ull);
    CHECK(det::channel(0x1234567890abcdefull, 0) == 0x6a1a712de177af18ull);
    CHECK(det::channel(0x1234567890abcdefull, 7) == 0x7159732c2c2b0462ull);
    CHECK(det::unitFloat(0) == 0.0f);
    CHECK(det::unitFloat(~0ull) == 0.99999994f);  // the largest float below 1
    CHECK(det::unitFloat(0x8000000000000000ull) == 0.5f);
}

TEST_CASE("Random numbers are evenly spread and never reach 1") {
    const int buckets = 16, samples = 160000;
    int counts[16] = {};
    double sum = 0.0;
    const std::uint64_t emitter = det::emitterKey(42, 99);
    for (int i = 0; i < samples; ++i) {
        const float u = det::unitFloat(
            det::channel(det::particleKey(emitter, 0, static_cast<std::uint64_t>(i)), 0));
        REQUIRE(u >= 0.0f);
        REQUIRE(u < 1.0f);
        ++counts[static_cast<int>(u * buckets)];
        sum += u;
    }
    CHECK(std::fabs(sum / samples - 0.5) < 0.005);
    for (int b = 0; b < buckets; ++b) {
        INFO("bucket " << b);
        CHECK(std::abs(counts[b] - samples / buckets) < samples / buckets / 20);  // within 5%
    }
}

namespace {

// How closely two streams of numbers move together: 0 is not at all.
template <class A, class B>
double correlation(int n, A a, B b) {
    double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
    for (int i = 0; i < n; ++i) {
        const double x = a(i), y = b(i);
        sa += x; sb += y; saa += x * x; sbb += y * y; sab += x * y;
    }
    const double cov = sab / n - (sa / n) * (sb / n);
    const double va = saa / n - (sa / n) * (sa / n);
    const double vb = sbb / n - (sb / n) * (sb / n);
    return cov / std::sqrt(va * vb);
}

}  // namespace

TEST_CASE("Different properties, particles, passes and layers get unrelated numbers") {
    const int n = 50000;
    const std::uint64_t emitter = det::emitterKey(1, 2);
    auto value = [](std::uint64_t em, std::uint64_t pass, int index, std::uint32_t ch) {
        return static_cast<double>(det::unitFloat(
            det::channel(det::particleKey(em, pass, static_cast<std::uint64_t>(index)), ch)));
    };
    const double limit = 0.02;
    // Two properties of the same particle.
    CHECK(std::fabs(correlation(n, [&](int i) { return value(emitter, 0, i, 0); },
                                [&](int i) { return value(emitter, 0, i, 1); })) < limit);
    // The same property of neighbouring particles.
    CHECK(std::fabs(correlation(n, [&](int i) { return value(emitter, 0, i, 0); },
                                [&](int i) { return value(emitter, 0, i + 1, 0); })) < limit);
    // The same particle number in two passes of a loop.
    CHECK(std::fabs(correlation(n, [&](int i) { return value(emitter, 0, i, 0); },
                                [&](int i) { return value(emitter, 1, i, 0); })) < limit);
    // The same particle number in two layers.
    const std::uint64_t other = det::emitterKey(1, 3);
    CHECK(std::fabs(correlation(n, [&](int i) { return value(emitter, 0, i, 0); },
                                [&](int i) { return value(other, 0, i, 0); })) < limit);
    // The same particle with the effect's seed changed by one.
    const std::uint64_t reseeded = det::emitterKey(2, 2);
    CHECK(std::fabs(correlation(n, [&](int i) { return value(emitter, 0, i, 0); },
                                [&](int i) { return value(reseeded, 0, i, 0); })) < limit);
}

TEST_CASE("The simulation sources use no platform-dependent maths") {
    // sin, cos, pow and friends give slightly different answers on different
    // systems, which would make the same effect differ between machines.
    const char* files[] = {"core/src/Simulation.cpp", "core/src/Compile.cpp",
                           "core/src/DetMath.cpp", "core/src/Clock.cpp",
                           "core/include/vfx/DetMath.h", "core/include/vfx/Program.h",
                           "core/include/vfx/Simulation.h", "core/include/vfx/Clock.h"};
    const char* forbidden[] = {"std::sin",  "std::cos",   "std::tan",  "std::pow",  "std::exp",
                               "std::log",  "std::cbrt",  "std::atan", "std::asin", "std::acos",
                               "std::hypot", "std::fmod", "std::lerp", "sinf(",     "cosf(",
                               "powf(",     "expf(",      "<random>",  "rand(",     "std::chrono",
                               "std::thread", "std::fma", "long double"};
    for (const char* file : files) {
        std::ifstream in(std::string(VFX_SAMPLES_DIR) + "/../" + file, std::ios::binary);
        REQUIRE(in.good());
        std::stringstream buffer;
        buffer << in.rdbuf();
        const std::string source = buffer.str();
        REQUIRE(source.size() > 100);
        for (const char* word : forbidden) {
            INFO(file << " must not use " << word);
            CHECK(source.find(word) == std::string::npos);
        }
    }
}
