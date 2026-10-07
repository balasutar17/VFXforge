#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "sim_helpers.h"

using namespace vfx;
using namespace testing;

namespace {

const float kDt = static_cast<float>(kSimulationStep);

float length(float x, float y, float z) { return std::sqrt(x * x + y * y + z * z); }

// A layer whose particles stay exactly where they are born, so the shape can
// be inspected: no speed and no Motion module.
Effect still(bool threeD, const char* shape, const char* emitFrom, std::int64_t count = 4000) {
    Effect effect = oneLayer(threeD);
    Layer& layer = effect.layers[0];
    burstOnly(layer, count);
    put(layer, "shape", "shape", text(shape));
    put(layer, "shape", "emitFrom", text(emitFrom));
    put(layer, "initial", "speed", constant(0));
    put(layer, "initial", "lifetime", constant(10));
    removeModule(layer, "motion");
    return effect;
}

// A burst sent out at speed 1 with nothing acting on it, so each particle's
// velocity is exactly its direction.
Effect directions(bool threeD, double spreadDegrees, std::int64_t count = 4000) {
    Effect effect = oneLayer(threeD);
    Layer& layer = effect.layers[0];
    burstOnly(layer, count);
    put(layer, "initial", "speed", constant(1));
    put(layer, "initial", "spread", number(spreadDegrees));
    put(layer, "initial", "lifetime", constant(10));
    removeModule(layer, "motion");
    return effect;
}

}  // namespace

// ------------------------------------------------------------- emission

TEST_CASE("A steady rate emits that many particles each second") {
    Effect effect = oneLayer();
    put(effect.layers[0], "emission", "rate", constant(100));
    put(effect.layers[0], "initial", "lifetime", constant(10));

    Simulation sim(compileEffect(effect));
    CHECK(sim.aliveCount() == 0);
    sim.seek(stepsFor(1.0));
    CHECK(sim.aliveCount() >= 99);
    CHECK(sim.aliveCount() <= 100);
    sim.seek(stepsFor(1.5));
    CHECK(sim.aliveCount() >= 149);
    CHECK(sim.aliveCount() <= 150);
}

TEST_CASE("A one-shot effect stops emitting at its end; particles live on") {
    Effect effect = oneLayer();  // 2 seconds, once
    put(effect.layers[0], "emission", "rate", constant(100));
    put(effect.layers[0], "initial", "lifetime", constant(10));

    Simulation sim(compileEffect(effect));
    sim.seek(stepsFor(2.0));
    const std::uint32_t atEnd = sim.aliveCount();
    CHECK(atEnd >= 199);
    CHECK(atEnd <= 200);
    sim.seek(stepsFor(5.0));
    CHECK(sim.aliveCount() == atEnd);
    sim.seek(stepsFor(12.5));
    CHECK(sim.aliveCount() == 0);  // all have lived their ten seconds
}

TEST_CASE("A burst releases its particles in the step that contains its moment") {
    Effect effect = oneLayer();
    burstOnly(effect.layers[0], 24, 0.5);
    put(effect.layers[0], "initial", "lifetime", constant(10));

    Simulation sim(compileEffect(effect));
    sim.seek(30);  // time 0.5 has not been simulated yet: step 30 covers it
    CHECK(sim.aliveCount() == 0);
    sim.advance();
    CHECK(sim.aliveCount() == 24);
    sim.seek(stepsFor(1.9));
    CHECK(sim.aliveCount() == 24);  // once only

    // All born at the same instant.
    const ParticleView v = sim.particles(0);
    for (std::uint32_t i = 1; i < v.count; ++i) {
        CHECK(v.age[i] == v.age[0]);
    }
}

TEST_CASE("Several bursts at one moment and at different moments all fire") {
    Effect effect = oneLayer();
    put(effect.layers[0], "emission", "rate", constant(0));
    put(effect.layers[0], "emission", "bursts",
        Value(BurstList{{{0.0, 5}, {0.0, 7}, {1.0, 11}, {1.0 + 1e-9, 13}}}));
    put(effect.layers[0], "initial", "lifetime", constant(10));
    Simulation sim(compileEffect(effect));
    sim.seek(1);
    CHECK(sim.aliveCount() == 12);
    sim.seek(stepsFor(1.5));
    CHECK(sim.aliveCount() == 36);
}

TEST_CASE("Particles die when their lifetime is up") {
    Effect effect = oneLayer();
    burstOnly(effect.layers[0], 10);
    put(effect.layers[0], "initial", "lifetime", constant(0.5));

    Simulation sim(compileEffect(effect));
    sim.seek(29);
    CHECK(sim.aliveCount() == 10);
    sim.seek(31);
    CHECK(sim.aliveCount() == 0);
}

TEST_CASE("At a steady rate the number alive settles at rate times lifetime") {
    Effect effect = oneLayer();
    effect.loop = "loop";
    put(effect.layers[0], "emission", "rate", constant(200));
    put(effect.layers[0], "initial", "lifetime", constant(0.5));

    Simulation sim(compileEffect(effect));
    for (double t : {1.0, 3.0, 7.3}) {
        sim.seek(stepsFor(t));
        INFO(t);
        CHECK(sim.aliveCount() >= 97);
        CHECK(sim.aliveCount() <= 103);
    }
    const auto stats = sim.stats();
    CHECK(stats[0].dropped == 0);
    CHECK(stats[0].alive <= stats[0].capacity);
}

TEST_CASE("A layer emits only between its start and its end") {
    Effect effect = oneLayer();
    Layer& layer = effect.layers[0];
    layer.start = 0.5;
    layer.duration = 0.5;
    put(layer, "emission", "rate", constant(100));
    put(layer, "initial", "lifetime", constant(10));

    Simulation sim(compileEffect(effect));
    sim.seek(stepsFor(0.5));
    CHECK(sim.aliveCount() == 0);
    sim.seek(stepsFor(0.75));
    CHECK(sim.aliveCount() >= 24);
    CHECK(sim.aliveCount() <= 26);
    sim.seek(stepsFor(1.0) + 1);
    const std::uint32_t after = sim.aliveCount();
    CHECK(after >= 49);
    CHECK(after <= 50);
    sim.seek(stepsFor(1.9));
    CHECK(sim.aliveCount() == after);
}

TEST_CASE("A rate curve follows the layer's duration") {
    Effect effect = oneLayer();  // layer duration 2
    put(effect.layers[0], "emission", "rate", Value(Scalar::curve({{0, 0}, {1, 200}})));
    put(effect.layers[0], "initial", "lifetime", constant(10));
    Simulation sim(compileEffect(effect));
    sim.seek(stepsFor(1.0));
    // Half-way: the rate has climbed from 0 to 100, so about 50 so far.
    CHECK(sim.aliveCount() >= 48);
    CHECK(sim.aliveCount() <= 52);
    sim.seek(stepsFor(2.0));
    CHECK(sim.aliveCount() >= 197);
    CHECK(sim.aliveCount() <= 201);
}

TEST_CASE("Particles born within one step are spread through it, not bunched") {
    Effect effect = directions(false, 0.0);
    Layer& layer = effect.layers[0];
    put(layer, "emission", "bursts", Value(BurstList{}));
    put(layer, "emission", "rate", constant(6000));  // 100 each step
    Simulation sim(compileEffect(effect));
    sim.advance();

    const ParticleView v = sim.particles(0);
    REQUIRE(v.count >= 99);
    std::set<float> heights;
    for (std::uint32_t i = 0; i < v.count; ++i) {
        CHECK(v.y[i] > 0.0f);
        CHECK(v.y[i] <= kDt * 1.0001f);
        CHECK(v.age[i] > 0.0f);
        CHECK(v.age[i] <= kDt * 1.0001f);
        CHECK(v.y[i] == Catch::Approx(v.age[i]).margin(1e-7));  // speed 1: distance equals age
        heights.insert(v.y[i]);
    }
    CHECK(heights.size() == v.count);  // every one at its own height
    // The oldest comes first, so it has travelled furthest.
    CHECK(v.y[0] > v.y[v.count - 1]);
}

TEST_CASE("A layer that is full drops new particles and says how many") {
    Effect effect = oneLayer();
    put(effect.layers[0], "emission", "rate", constant(100000));
    put(effect.layers[0], "initial", "lifetime", constant(100));
    const auto program = compileEffect(effect);
    REQUIRE(program->emitters[0]->capped());

    Simulation sim(program);
    sim.seek(stepsFor(2.0));  // would be 200,000 exactly; then more
    const auto stats = sim.stats();
    CHECK(stats[0].alive <= kMaxParticlesPerEmitter);
    CHECK(stats[0].alive >= kMaxParticlesPerEmitter - 2000);
    for (int i = 0; i < 30; ++i) {
        sim.advance();
    }
    CHECK(sim.aliveCount() <= kMaxParticlesPerEmitter);
    CHECK(allFinite(sim));
}

// --------------------------------------------------------------- motion

TEST_CASE("Gravity moves a particle exactly as the step rule says") {
    Effect effect = oneLayer();
    Layer& layer = effect.layers[0];
    burstOnly(layer, 1);
    put(layer, "initial", "speed", constant(3));
    put(layer, "initial", "spread", number(0));
    put(layer, "initial", "direction", vec(0, 1, 0));
    put(layer, "initial", "lifetime", constant(10));
    put(layer, "motion", "gravity", vec(0.5, -9.8, 0));

    Simulation sim(compileEffect(effect));
    float x = 0, y = 0, vx = 0, vy = 3.0f;
    for (int n = 1; n <= 90; ++n) {
        sim.advance();
        vx = vx + 0.5f * kDt;
        vy = vy + -9.8f * kDt;
        x += vx * kDt;
        y += vy * kDt;
        const ParticleView v = sim.particles(0);
        REQUIRE(v.count == 1);
        // Not approximately: the very same bits.
        REQUIRE(v.x[0] == x);
        REQUIRE(v.y[0] == y);
        REQUIRE(v.vy[0] == vy);
        REQUIRE(v.z[0] == 0.0f);
    }
    // And it is where physics says it should be, near enough: y = vt - gt²/2.
    CHECK(y == Catch::Approx(3.0 * 1.5 - 0.5 * 9.8 * 1.5 * 1.5).epsilon(0.02));
}

TEST_CASE("Drag slows a particle and never reverses it") {
    Effect effect = oneLayer();
    Layer& layer = effect.layers[0];
    burstOnly(layer, 1);
    put(layer, "initial", "speed", constant(10));
    put(layer, "initial", "spread", number(0));
    put(layer, "initial", "lifetime", constant(10));
    put(layer, "motion", "gravity", vec(0, 0, 0));
    put(layer, "motion", "drag", number(2));

    Simulation sim(compileEffect(effect));
    float vy = 10.0f, previous = 10.0f;
    for (int n = 0; n < 120; ++n) {
        sim.advance();
        vy = vy * (1.0f / (1.0f + 2.0f * kDt));
        const float now = sim.particles(0).vy[0];
        REQUIRE(now == vy);
        REQUIRE(now < previous);
        REQUIRE(now > 0.0f);
        previous = now;
    }
    CHECK(vy < 10.0f * 0.03f);  // after two seconds at drag 2, almost stopped

    // Even an extreme setting stays stable.
    put(layer, "motion", "drag", number(1000));
    Simulation harsh(compileEffect(effect));
    harsh.seek(60);
    CHECK(allFinite(harsh));
    CHECK(harsh.particles(0).vy[0] >= 0.0f);
}

TEST_CASE("Spin turns a particle at a steady rate") {
    Effect effect = oneLayer();
    Layer& layer = effect.layers[0];
    burstOnly(layer, 1);
    put(layer, "initial", "rotation", constant(15));
    put(layer, "initial", "lifetime", constant(10));
    put(layer, "motion", "spin", constant(90));

    Simulation sim = simulate(effect, stepsFor(2.0));
    CHECK(sim.particles(0).rotation[0] == Catch::Approx(15.0 + 180.0).epsilon(1e-4));

    // A curve reads across the particle's life: no spin, rising to 100.
    put(layer, "initial", "lifetime", constant(2));
    put(layer, "motion", "spin", Value(Scalar::curve({{0, 0}, {1, 100}})));
    Simulation curved = simulate(effect, stepsFor(1.0));
    // Half its life at an average of 25 degrees a second.
    CHECK(curved.particles(0).rotation[0] == Catch::Approx(15.0 + 25.0).epsilon(0.03));
}

// ------------------------------------------------------------ direction

TEST_CASE("With no spread every particle leaves in exactly the same direction") {
    for (bool threeD : {false, true}) {
        Effect effect = directions(threeD, 0.0, 200);
        put(effect.layers[0], "initial", "direction", vec(1, 2, threeD ? 2 : 0));
        Simulation sim = simulate(effect, 1);
        const ParticleView v = sim.particles(0);
        REQUIRE(v.count == 200);
        for (std::uint32_t i = 1; i < v.count; ++i) {
            REQUIRE(v.vx[i] == v.vx[0]);
            REQUIRE(v.vy[i] == v.vy[0]);
            REQUIRE(v.vz[i] == v.vz[0]);
        }
        CHECK(length(v.vx[0], v.vy[0], v.vz[0]) == Catch::Approx(1.0f).epsilon(1e-5));
    }
}

TEST_CASE("Spread keeps every particle within the given angle") {
    for (bool threeD : {false, true}) {
        for (double spread : {10.0, 45.0, 90.0, 135.0}) {
            Effect effect = directions(threeD, spread);
            put(effect.layers[0], "initial", "direction", vec(0.6, 0.8, 0));
            Simulation sim = simulate(effect, 1);
            const ParticleView v = sim.particles(0);
            double sine = 0, cosine = 1;
            det::sinCos(spread * det::kDegreesToRadians, sine, cosine);
            float widest = 1.0f;
            for (std::uint32_t i = 0; i < v.count; ++i) {
                const float along = v.vx[i] * 0.6f + v.vy[i] * 0.8f;
                REQUIRE(along >= static_cast<float>(cosine) - 1e-4f);
                REQUIRE(length(v.vx[i], v.vy[i], v.vz[i]) == Catch::Approx(1.0f).epsilon(1e-4));
                widest = std::min(widest, along);
            }
            INFO((threeD ? "3d " : "2d ") << spread);
            // And the whole angle is used, not just the middle of it.
            CHECK(widest < static_cast<float>(cosine) + 0.02f);
        }
    }
}

TEST_CASE("Full spread in 3D covers the whole sphere evenly") {
    Effect effect = directions(true, 180.0, 20000);
    Simulation sim = simulate(effect, 1);
    const ParticleView v = sim.particles(0);
    double sx = 0, sy = 0, sz = 0;
    int octants[8] = {};
    for (std::uint32_t i = 0; i < v.count; ++i) {
        sx += v.vx[i];
        sy += v.vy[i];
        sz += v.vz[i];
        ++octants[(v.vx[i] > 0 ? 1 : 0) | (v.vy[i] > 0 ? 2 : 0) | (v.vz[i] > 0 ? 4 : 0)];
    }
    CHECK(std::fabs(sx / v.count) < 0.02);
    CHECK(std::fabs(sy / v.count) < 0.02);
    CHECK(std::fabs(sz / v.count) < 0.02);
    for (int count : octants) {
        CHECK(std::abs(count - 2500) < 250);
    }
}

TEST_CASE("In 2D nothing ever leaves the plane, whatever the shape") {
    for (const char* shape : {"point", "circle", "rectangle", "sphere", "box", "cone"}) {
        for (const char* from : {"volume", "edge"}) {
            Effect effect = oneLayer(false);
            Layer& layer = effect.layers[0];
            put(layer, "emission", "rate", constant(500));
            put(layer, "shape", "shape", text(shape));
            put(layer, "shape", "emitFrom", text(from));
            put(layer, "shape", "size", vec(2, 3, 4));
            put(layer, "initial", "direction", vec(0.3, 0.5, 0.8));
            put(layer, "initial", "spread", number(180));
            put(layer, "motion", "gravity", vec(1, -9.8, 5));
            Simulation sim = simulate(effect, 45);
            const ParticleView v = sim.particles(0);
            REQUIRE(v.count > 100);
            INFO(shape << " " << from);
            bool flat = true;
            for (std::uint32_t i = 0; i < v.count; ++i) {
                flat = flat && v.z[i] == 0.0f && v.vz[i] == 0.0f;
            }
            CHECK(flat);
            CHECK(allFinite(sim));
        }
    }
}

// ---------------------------------------------------------------- shapes

TEST_CASE("Point: everything starts at the centre") {
    Simulation sim = simulate(still(true, "point", "volume", 50), 1);
    const ParticleView v = sim.particles(0);
    for (std::uint32_t i = 0; i < v.count; ++i) {
        CHECK(length(v.x[i], v.y[i], v.z[i]) == 0.0f);
    }
}

TEST_CASE("Circle: filled evenly, or only the rim") {
    Effect filled = still(false, "circle", "volume");
    put(filled.layers[0], "shape", "radius", number(2));
    Simulation sim = simulate(filled, 1);
    ParticleView v = sim.particles(0);
    int inner = 0;
    double mx = 0, my = 0;
    for (std::uint32_t i = 0; i < v.count; ++i) {
        const float r = length(v.x[i], v.y[i], 0);
        REQUIRE(r <= 2.0001f);
        inner += r < 1.0f ? 1 : 0;
        mx += v.x[i];
        my += v.y[i];
    }
    // Half the radius holds a quarter of the area.
    CHECK(inner == Catch::Approx(v.count * 0.25).margin(v.count * 0.03));
    CHECK(std::fabs(mx / v.count) < 0.08);
    CHECK(std::fabs(my / v.count) < 0.08);

    Effect rim = still(false, "circle", "edge");
    put(rim.layers[0], "shape", "radius", number(2));
    sim = simulate(rim, 1);
    v = sim.particles(0);
    for (std::uint32_t i = 0; i < v.count; ++i) {
        REQUIRE(length(v.x[i], v.y[i], 0) == Catch::Approx(2.0f).epsilon(1e-5));
    }
}

TEST_CASE("Rectangle: filled evenly, or only the outline") {
    Effect filled = still(false, "rectangle", "volume");
    put(filled.layers[0], "shape", "size", vec(4, 2, 9));
    Simulation sim = simulate(filled, 1);
    ParticleView v = sim.particles(0);
    int right = 0, upper = 0;
    for (std::uint32_t i = 0; i < v.count; ++i) {
        REQUIRE(std::fabs(v.x[i]) <= 2.0f);
        REQUIRE(std::fabs(v.y[i]) <= 1.0f);
        right += v.x[i] > 0 ? 1 : 0;
        upper += v.y[i] > 0 ? 1 : 0;
    }
    CHECK(right == Catch::Approx(v.count * 0.5).margin(v.count * 0.04));
    CHECK(upper == Catch::Approx(v.count * 0.5).margin(v.count * 0.04));

    Effect outline = still(false, "rectangle", "edge");
    put(outline.layers[0], "shape", "size", vec(4, 2, 9));
    sim = simulate(outline, 1);
    v = sim.particles(0);
    int onLongSides = 0;
    for (std::uint32_t i = 0; i < v.count; ++i) {
        const bool onX = std::fabs(std::fabs(v.x[i]) - 2.0f) < 1e-5f && std::fabs(v.y[i]) <= 1.00001f;
        const bool onY = std::fabs(std::fabs(v.y[i]) - 1.0f) < 1e-5f && std::fabs(v.x[i]) <= 2.00001f;
        REQUIRE((onX || onY));
        onLongSides += onY ? 1 : 0;
    }
    // The long sides are twice the short ones, so they get two thirds.
    CHECK(onLongSides == Catch::Approx(v.count * 2.0 / 3.0).margin(v.count * 0.04));
}

TEST_CASE("Sphere: filled evenly, or only the shell") {
    Effect filled = still(true, "sphere", "volume", 20000);
    put(filled.layers[0], "shape", "radius", number(2));
    Simulation sim = simulate(filled, 1);
    ParticleView v = sim.particles(0);
    int inner = 0, top = 0;
    for (std::uint32_t i = 0; i < v.count; ++i) {
        const float r = length(v.x[i], v.y[i], v.z[i]);
        REQUIRE(r <= 2.0001f);
        inner += r < 1.0f ? 1 : 0;
        top += v.z[i] > 0 ? 1 : 0;
    }
    // Half the radius holds one eighth of the volume.
    CHECK(inner == Catch::Approx(v.count * 0.125).margin(v.count * 0.01));
    CHECK(top == Catch::Approx(v.count * 0.5).margin(v.count * 0.02));

    Effect shell = still(true, "sphere", "edge");
    put(shell.layers[0], "shape", "radius", number(2));
    sim = simulate(shell, 1);
    v = sim.particles(0);
    for (std::uint32_t i = 0; i < v.count; ++i) {
        REQUIRE(length(v.x[i], v.y[i], v.z[i]) == Catch::Approx(2.0f).epsilon(1e-5));
    }
}

TEST_CASE("Box: filled evenly, or only the surface") {
    Effect filled = still(true, "box", "volume");
    put(filled.layers[0], "shape", "size", vec(2, 4, 6));
    Simulation sim = simulate(filled, 1);
    ParticleView v = sim.particles(0);
    bool deep = false;
    for (std::uint32_t i = 0; i < v.count; ++i) {
        REQUIRE(std::fabs(v.x[i]) <= 1.0f);
        REQUIRE(std::fabs(v.y[i]) <= 2.0f);
        REQUIRE(std::fabs(v.z[i]) <= 3.0f);
        deep = deep || std::fabs(v.z[i]) > 2.0f;
    }
    CHECK(deep);

    Effect surface = still(true, "box", "edge", 12000);
    put(surface.layers[0], "shape", "size", vec(2, 4, 6));
    sim = simulate(surface, 1);
    v = sim.particles(0);
    int faces[3] = {};
    for (std::uint32_t i = 0; i < v.count; ++i) {
        const bool onX = std::fabs(std::fabs(v.x[i]) - 1.0f) < 1e-5f;
        const bool onY = std::fabs(std::fabs(v.y[i]) - 2.0f) < 1e-5f;
        const bool onZ = std::fabs(std::fabs(v.z[i]) - 3.0f) < 1e-5f;
        REQUIRE((onX || onY || onZ));
        faces[onX ? 0 : (onY ? 1 : 2)]++;
    }
    // Face areas: x faces 4x6=24, y faces 2x6=12, z faces 2x4=8, each twice. Total 88.
    CHECK(faces[0] == Catch::Approx(v.count * 48.0 / 88.0).margin(v.count * 0.03));
    CHECK(faces[1] == Catch::Approx(v.count * 24.0 / 88.0).margin(v.count * 0.03));
    CHECK(faces[2] == Catch::Approx(v.count * 16.0 / 88.0).margin(v.count * 0.03));
}

TEST_CASE("Cone: a disc across the direction, fanning out towards the rim") {
    for (bool threeD : {false, true}) {
        INFO((threeD ? "3d" : "2d"));
        Effect effect = oneLayer(threeD);
        Layer& layer = effect.layers[0];
        burstOnly(layer, 4000);
        put(layer, "shape", "shape", text("cone"));
        put(layer, "shape", "radius", number(2));
        put(layer, "shape", "angle", number(30));
        put(layer, "initial", "direction", vec(0.6, 0.8, 0));
        put(layer, "initial", "spread", number(0));
        put(layer, "initial", "speed", constant(1));
        put(layer, "initial", "lifetime", constant(10));
        removeModule(layer, "motion");

        // Look at the instant of birth: age zero means nothing has moved yet.
        Simulation sim(compileEffect(effect));
        sim.advance();
        const ParticleView v = sim.particles(0);
        const float cos30 = 0.8660254f;
        for (std::uint32_t i = 0; i < v.count; ++i) {
            // Undo the one step of travel to get the birth position.
            const float bx = v.x[i] - v.vx[i] * v.age[i];
            const float by = v.y[i] - v.vy[i] * v.age[i];
            const float bz = v.z[i] - v.vz[i] * v.age[i];
            const float alongAxis = bx * 0.6f + by * 0.8f;
            REQUIRE(std::fabs(alongAxis) < 1e-4f);  // on the disc
            const float r = length(bx, by, bz);
            REQUIRE(r <= 2.0002f);
            const float heading = v.vx[i] * 0.6f + v.vy[i] * 0.8f;
            REQUIRE(heading >= cos30 - 1e-4f);      // never wider than the cone
            // The further from the centre, the more it leans outward.
            double s = 0, c = 1;
            det::sinCos(30.0 * det::kDegreesToRadians * (r / 2.0f), s, c);
            REQUIRE(heading == Catch::Approx(c).margin(2e-4));
            if (r > 1e-3f) {
                const float outward = (v.vx[i] * bx + v.vy[i] * by + v.vz[i] * bz) / r;
                REQUIRE(outward >= -1e-4f);
            }
        }
    }
}

// --------------------------------------------------------------- looping

TEST_CASE("A looping effect fires its bursts on every pass") {
    Effect effect = oneLayer();
    effect.loop = "loop";
    effect.duration = 1.0;
    burstOnly(effect.layers[0], 10, 0.25);
    put(effect.layers[0], "initial", "lifetime", constant(0.3));

    Simulation sim(compileEffect(effect));
    for (int pass = 0; pass < 5; ++pass) {
        INFO("pass " << pass);
        sim.seek(stepsFor(pass + 0.2));
        CHECK(sim.aliveCount() == 0);
        sim.seek(stepsFor(pass + 0.4));
        CHECK(sim.aliveCount() == 10);
        sim.seek(stepsFor(pass + 0.9));
        CHECK(sim.aliveCount() == 0);
    }
}

TEST_CASE("Each pass of a loop gets fresh random numbers") {
    Effect effect = directions(false, 180.0, 50);
    effect.loop = "loop";
    effect.duration = 1.0;
    put(effect.layers[0], "initial", "lifetime", constant(0.5));

    Simulation sim(compileEffect(effect));
    sim.seek(10);
    std::vector<float> first(sim.particles(0).vx, sim.particles(0).vx + sim.particles(0).count);
    sim.seek(70);
    const ParticleView v = sim.particles(0);
    REQUIRE(v.count == first.size());
    int same = 0;
    for (std::uint32_t i = 0; i < v.count; ++i) {
        same += v.vx[i] == first[i] ? 1 : 0;
    }
    CHECK(same == 0);
}

TEST_CASE("Particles live on across the end of a pass") {
    Effect effect = oneLayer();
    effect.loop = "loop";
    effect.duration = 1.0;
    put(effect.layers[0], "emission", "rate", constant(100));
    put(effect.layers[0], "initial", "lifetime", constant(1.5));

    Simulation sim(compileEffect(effect));
    sim.seek(stepsFor(3.5));
    // 100 a second living 1.5 seconds, with no gap at the wrap.
    CHECK(sim.aliveCount() >= 147);
    CHECK(sim.aliveCount() <= 153);
    const ParticleView v = sim.particles(0);
    float oldest = 0;
    for (std::uint32_t i = 0; i < v.count; ++i) {
        oldest = std::max(oldest, v.age[i]);
    }
    CHECK(oldest > 1.4f);
}

TEST_CASE("A random rate is picked once for each pass") {
    Effect effect = oneLayer();
    effect.loop = "loop";
    effect.duration = 1.0;
    put(effect.layers[0], "emission", "rate", range(50, 150));
    put(effect.layers[0], "initial", "lifetime", constant(0.9));

    Simulation sim(compileEffect(effect));
    std::set<std::uint32_t> counts;
    for (int pass = 0; pass < 6; ++pass) {
        sim.seek(stepsFor(pass + 0.95));
        const std::uint32_t alive = sim.aliveCount();
        CHECK(alive >= 40);    // 0.9 seconds' worth at 50 a second, give or take
        CHECK(alive <= 140);
        counts.insert(alive);
    }
    CHECK(counts.size() >= 4);  // different passes, different rates
}

// --------------------------------------------------------- birth values

TEST_CASE("Random ranges stay in range and use all of it") {
    Effect effect = oneLayer(true);
    Layer& layer = effect.layers[0];
    burstOnly(layer, 5000);
    put(layer, "initial", "lifetime", range(2, 4));
    put(layer, "initial", "speed", range(1, 3));
    put(layer, "initial", "size", range(0.5, 1.5));
    put(layer, "initial", "rotation", range(-90, 90));
    put(layer, "initial", "spread", number(180));
    removeModule(layer, "motion");
    removeModule(layer, "overLife");

    Simulation sim = simulate(effect, 1);
    const ParticleView v = sim.particles(0);
    auto spanOf = [&](const float* data) {
        float lo = data[0], hi = data[0];
        for (std::uint32_t i = 0; i < v.count; ++i) {
            lo = std::min(lo, data[i]);
            hi = std::max(hi, data[i]);
        }
        return std::pair<float, float>(lo, hi);
    };
    auto [lifeLo, lifeHi] = spanOf(v.lifetime);
    CHECK(lifeLo >= 2.0f);
    CHECK(lifeLo < 2.02f);
    CHECK(lifeHi <= 4.0f);
    CHECK(lifeHi > 3.98f);
    auto [sizeLo, sizeHi] = spanOf(v.size);
    CHECK(sizeLo >= 0.5f);
    CHECK(sizeHi <= 1.5f);
    CHECK(sizeHi - sizeLo > 0.98f);
    auto [rotLo, rotHi] = spanOf(v.rotation);
    CHECK(rotLo >= -90.0f);
    CHECK(rotHi <= 90.0f);
    for (std::uint32_t i = 0; i < v.count; ++i) {
        const float speed = length(v.vx[i], v.vy[i], v.vz[i]);
        REQUIRE(speed >= 0.9999f);
        REQUIRE(speed <= 3.0001f);
    }
}

TEST_CASE("A birth curve reads across the layer's duration") {
    Effect effect = oneLayer();  // layer runs for 2 seconds
    Layer& layer = effect.layers[0];
    put(layer, "emission", "rate", constant(60));
    put(layer, "initial", "size", Value(Scalar::curve({{0, 1}, {1, 3}})));
    put(layer, "initial", "lifetime", constant(10));
    removeModule(layer, "overLife");

    Simulation sim = simulate(effect, stepsFor(2.0));
    const ParticleView v = sim.particles(0);
    REQUIRE(v.count > 100);
    CHECK(v.size[0] == Catch::Approx(1.0f).margin(0.02));             // born first
    CHECK(v.size[v.count - 1] == Catch::Approx(3.0f).margin(0.02));   // born last
    CHECK(v.size[v.count / 2] == Catch::Approx(2.0f).margin(0.05));
    for (std::uint32_t i = 1; i < v.count; ++i) {
        REQUIRE(v.size[i] >= v.size[i - 1]);
    }
}

// --------------------------------------------------------- frame output

TEST_CASE("A frame holds one batch per drawn layer, in stack order") {
    Effect effect = sampleEffect();
    effect.layers[1].modules.pop_back();  // the second layer has no Sprite module
    Layer third = effect.layers[0];
    third.id = Id{0x333};
    for (auto& m : third.modules) {
        m.id = Id{m.id.value ^ 0xffff};
    }
    put(third, "sprite", "blend", text("additive"));
    put(third, "sprite", "glow", number(4));
    put(third, "sprite", "facing", text("plane"));
    put(third, "sprite", "texture", Value(AssetRef{}));
    effect.layers.push_back(third);

    Simulation sim = simulate(effect, 40);
    RenderFrame frame;
    sim.extract(frame);

    CHECK(frame.step == 40);
    CHECK(frame.time == Catch::Approx(40.0 / 60.0));
    CHECK(frame.flat);
    REQUIRE(frame.batches.size() == 2);  // the layer with no sprite is not drawn
    CHECK(frame.batches[0].layer == effect.layers[0].id);
    CHECK(frame.batches[0].texture == effect.assets[0].id);
    CHECK(frame.batches[0].blend == BlendMode::Alpha);
    CHECK(frame.batches[0].first == 0);
    CHECK(frame.batches[1].layer == Id{0x333});
    CHECK_FALSE(frame.batches[1].texture.valid());
    CHECK(frame.batches[1].blend == BlendMode::Additive);
    CHECK(frame.batches[1].facing == Facing::Plane);
    CHECK(frame.batches[1].glow == 4.0f);
    CHECK(frame.batches[1].first == frame.batches[0].count);
    CHECK(frame.instances.size() == frame.batches[0].count + frame.batches[1].count);
    CHECK(sim.aliveCount() > frame.instances.size());  // the undrawn layer is still simulated

    // Taking a frame does not disturb the simulation, and is repeatable.
    const std::uint64_t before = sim.stateHash();
    RenderFrame again;
    sim.extract(again);
    CHECK(hashFrame(again) == hashFrame(frame));
    CHECK(sim.stateHash() == before);
}

TEST_CASE("Size, opacity and colour over lifetime are applied in the frame") {
    Effect effect = oneLayer();
    Layer& layer = effect.layers[0];
    burstOnly(layer, 1);
    put(layer, "initial", "lifetime", constant(2));
    put(layer, "initial", "size", constant(4));
    put(layer, "initial", "rotation", constant(90));
    put(layer, "initial", "color", Value(Color{1, 0.5, 0.25, 0.8}));
    put(layer, "overLife", "size", Value(Scalar::curve({{0, 1}, {1, 0}})));
    put(layer, "overLife", "opacity", Value(Scalar::curve({{0, 1}, {1, 0}})));
    put(layer, "overLife", "color",
        Value(Gradient{{{0, Color{1, 1, 1, 1}}, {1, Color{0, 1, 0, 1}}}}));

    Simulation sim = simulate(effect, stepsFor(1.0));  // half-way through its life
    RenderFrame frame;
    sim.extract(frame);
    REQUIRE(frame.instances.size() == 1);
    const SpriteInstance& s = frame.instances[0];
    CHECK(s.size == Catch::Approx(2.0f).epsilon(1e-3));
    CHECK(s.a == Catch::Approx(0.4f).epsilon(1e-3));
    CHECK(s.r == Catch::Approx(0.5f).epsilon(1e-3));
    CHECK(s.g == Catch::Approx(0.5f).epsilon(1e-3));
    CHECK(s.b == Catch::Approx(0.125f).epsilon(1e-3));
    CHECK(s.rotation == Catch::Approx(1.5707964f).epsilon(1e-5));  // radians for the renderer
    CHECK(sim.particles(0).size[0] == 4.0f);  // the stored size is untouched
}

TEST_CASE("A fixed or random Over Lifetime value is a per-particle multiplier") {
    Effect effect = oneLayer();
    Layer& layer = effect.layers[0];
    burstOnly(layer, 2000);
    put(layer, "initial", "lifetime", constant(5));
    put(layer, "initial", "size", constant(2));
    put(layer, "overLife", "size", range(0.5, 1.5));
    put(layer, "overLife", "opacity", constant(0.25));

    Simulation sim = simulate(effect, 30);
    RenderFrame frame;
    sim.extract(frame);
    float lo = 100, hi = 0;
    for (const auto& s : frame.instances) {
        lo = std::min(lo, s.size);
        hi = std::max(hi, s.size);
        REQUIRE(s.a == 0.25f);
    }
    CHECK(lo >= 1.0f);
    CHECK(lo < 1.05f);
    CHECK(hi <= 3.0f);
    CHECK(hi > 2.95f);
}

TEST_CASE("Reusing a frame object does not keep old particles") {
    Effect effect = oneLayer();
    burstOnly(effect.layers[0], 50);
    put(effect.layers[0], "initial", "lifetime", constant(0.5));
    Simulation sim(compileEffect(effect));
    RenderFrame frame;
    sim.seek(10);
    sim.extract(frame);
    CHECK(frame.instances.size() == 50);
    sim.seek(60);
    sim.extract(frame);
    CHECK(frame.instances.empty());
    REQUIRE(frame.batches.size() == 1);
    CHECK(frame.batches[0].count == 0);
}

TEST_CASE("An effect with nothing in it simulates to nothing") {
    Effect empty;
    empty.id = Id{1};
    Simulation sim(compileEffect(empty));
    sim.seek(1000);
    CHECK(sim.aliveCount() == 0);
    CHECK(sim.emitterCount() == 0);
    RenderFrame frame;
    sim.extract(frame);
    CHECK(frame.batches.empty());
    CHECK(sim.particles(5).count == 0);  // asking for a layer that is not there is harmless

    Simulation none(nullptr);
    none.advance();
    CHECK(none.aliveCount() == 0);
}
