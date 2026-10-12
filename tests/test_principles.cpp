// The animation principles (0.12): Speed over Life, Orbit, Squash and
// Stretch, and Hold Frames, as the simulation plays them.
#include <catch2/catch_amalgamated.hpp>

#include <cmath>
#include <set>
#include <vector>

#include "sim_helpers.h"
#include "vfx/DetMath.h"
#include "vfx/Program.h"
#include "vfx/Simulation.h"

using namespace vfx;
using namespace testing;

namespace {

// One particle (or a burst of them) flying straight to the right from the
// effect's centre at one unit a second, with nothing else acting on it.
Effect straight(std::int64_t count = 1, bool threeD = false) {
    Effect effect = oneLayer(threeD);
    Layer& layer = effect.layers[0];
    burstOnly(layer, count);
    put(layer, "initial", "lifetime", constant(2.0));
    put(layer, "initial", "speed", constant(1.0));
    put(layer, "initial", "direction", vec(1, 0, 0));
    put(layer, "initial", "spread", number(0.0));
    put(layer, "motion", "gravity", vec(0, 0, 0));
    return effect;
}

Value curve(std::vector<CurveKey> keys) { return Value(Scalar::curve(std::move(keys))); }

RenderFrame frameAt(const Effect& effect, std::int64_t step) {
    Simulation sim(compileEffect(effect));
    sim.seek(step);
    RenderFrame frame;
    sim.extract(frame);
    return frame;
}

// Everything that is drawn of a frame's particles, as one list of numbers.
std::vector<float> drawn(const RenderFrame& frame) {
    std::vector<float> out;
    for (const SpriteInstance& s : frame.instances) {
        for (float v : {s.x, s.y, s.z, s.size, s.rotation, s.r, s.g, s.b, s.a, s.vx, s.vy, s.vz, s.frame, s.age,
                        s.dissolve, s.seed, s.squashStretch, s.ease, s.life}) {
            out.push_back(v);
        }
    }
    return out;
}

}  // namespace

// ------------------------------------------------------- speed over life

TEST_CASE("Speed over Life multiplies how far a particle travels, and leaves its speed alone") {
    Effect effect = straight();
    Layer& layer = effect.layers[0];

    Simulation plain = simulate(effect, 60);
    CHECK(plain.particles(0).x[0] == Catch::Approx(1.0).epsilon(1e-4));

    put(layer, "motion", "speedOverLife", constant(0.5));
    Simulation half = simulate(effect, 60);
    CHECK(half.particles(0).x[0] == Catch::Approx(0.5).epsilon(1e-4));
    CHECK(half.particles(0).vx[0] == 1.0f);  // its own speed is what it was

    put(layer, "motion", "speedOverLife", constant(0.0));
    CHECK(simulate(effect, 60).particles(0).x[0] == 0.0f);

    put(layer, "motion", "speedOverLife", constant(-1.0));
    CHECK(simulate(effect, 60).particles(0).x[0] == Catch::Approx(-1.0).epsilon(1e-4));
}

TEST_CASE("As a curve, Speed over Life is read across the particle's life") {
    Effect effect = straight();
    Layer& layer = effect.layers[0];
    // From standing still to twice its speed over a life of two seconds.
    put(layer, "motion", "speedOverLife", curve({{0, 0}, {1, 2}}));

    // Half-way through its life it has averaged half its speed.
    Simulation sim = simulate(effect, 60);
    CHECK(sim.particles(0).x[0] == Catch::Approx(0.5).epsilon(0.02));
    // By the end it has averaged its own speed: where it would have been anyway.
    Simulation end = simulate(effect, 119);
    CHECK(end.particles(0).x[0] == Catch::Approx(119.0 / 60.0).epsilon(0.02));

    // Slow in and slow out: the same journey in the same time, but it has
    // covered less of it early on and makes it up in the middle.
    put(layer, "motion", "speedOverLife", curve({{0, 0.2}, {0.5, 1.8}, {1, 0.2}}));
    CHECK(simulate(effect, 30).particles(0).x[0] < 0.4f);
    CHECK(simulate(effect, 119).particles(0).x[0] == Catch::Approx(119.0 / 60.0).epsilon(0.03));

    // A wind-up: backwards first, then away.
    put(layer, "motion", "speedOverLife", curve({{0, -1}, {0.2, -1}, {0.25, 3}, {1, 3}}));
    CHECK(simulate(effect, 20).particles(0).x[0] < -0.25f);
    CHECK(simulate(effect, 100).particles(0).x[0] > 2.0f);
}

TEST_CASE("Speed over Life scales movement under gravity and drag without changing either") {
    Effect effect = oneLayer();
    Layer& layer = effect.layers[0];
    burstOnly(layer, 40);
    put(layer, "initial", "lifetime", constant(3.0));
    put(layer, "initial", "speed", range(1.0, 4.0));
    put(layer, "initial", "spread", number(60.0));
    put(layer, "motion", "gravity", vec(0.5, -6.0, 0));
    put(layer, "motion", "drag", number(0.8));
    Simulation plain = simulate(effect, 45);

    put(layer, "motion", "speedOverLife", constant(0.25));
    Simulation slowed = simulate(effect, 45);
    const ParticleView a = plain.particles(0), b = slowed.particles(0);
    REQUIRE(a.count == 40);
    REQUIRE(b.count == 40);
    for (std::uint32_t i = 0; i < a.count; ++i) {
        // The same speeds, a quarter of the distance.
        REQUIRE(b.vx[i] == a.vx[i]);
        REQUIRE(b.vy[i] == a.vy[i]);
        REQUIRE(b.x[i] == Catch::Approx(0.25 * a.x[i]).margin(1e-4));
        REQUIRE(b.y[i] == Catch::Approx(0.25 * a.y[i]).margin(1e-4));
    }
}

TEST_CASE("A random Speed over Life gives each particle its own pace, and keeps it") {
    Effect effect = straight(200);
    Layer& layer = effect.layers[0];
    put(layer, "motion", "speedOverLife", range(0.5, 1.5));

    Simulation early = simulate(effect, 30), late = simulate(effect, 90);
    const ParticleView a = early.particles(0), b = late.particles(0);
    REQUIRE(a.count == 200);
    float least = 1e9f, most = -1e9f;
    for (std::uint32_t i = 0; i < a.count; ++i) {
        const float pace = a.x[i] / 0.5f;
        least = std::min(least, pace);
        most = std::max(most, pace);
        REQUIRE(pace >= 0.5f - 1e-3f);
        REQUIRE(pace <= 1.5f + 1e-3f);
        // The same pace later on.
        REQUIRE(b.x[i] / 1.5f == Catch::Approx(pace).margin(1e-3));
    }
    // And the whole range is used.
    CHECK(least < 0.6f);
    CHECK(most > 1.4f);

    // The frame says how fast each is going, for streaks and trails.
    RenderFrame frame;
    early.extract(frame);
    REQUIRE(frame.batches.size() == 1);
    CHECK(frame.batches[0].eases);
    for (std::uint32_t i = 0; i < a.count; ++i) {
        REQUIRE(frame.instances[i].ease == Catch::Approx(a.x[i] / 0.5f).margin(1e-3));
        REQUIRE(frame.instances[i].life == 2.0f);
    }
}

TEST_CASE("Turbulence carries an eased particle as it carries any other") {
    Effect effect = straight(60);
    Layer& layer = effect.layers[0];
    put(layer, "initial", "speed", constant(0.0));
    put(layer, "shape", "shape", text("circle"));
    put(layer, "shape", "radius", number(2.0));
    put(layer, "motion", "turbulence", number(2.0));
    Simulation carried = simulate(effect, 40);
    // Standing still as far as its own movement goes: the air moves it all the same.
    put(layer, "motion", "speedOverLife", constant(0.0));
    Simulation still = simulate(effect, 40);
    const ParticleView a = carried.particles(0), b = still.particles(0);
    for (std::uint32_t i = 0; i < a.count; ++i) {
        REQUIRE(a.x[i] == b.x[i]);
        REQUIRE(a.y[i] == b.y[i]);
    }
}

// ------------------------------------------------------------------ orbit

TEST_CASE("Orbit turns a particle about the layer's centre as it travels") {
    Effect effect = straight();
    Layer& layer = effect.layers[0];
    put(layer, "motion", "orbit", number(90.0));

    // One second out along +x at one unit a second, turned a quarter of the
    // way round counter-clockwise: one unit up the y axis, and now heading up it.
    Simulation sim = simulate(effect, 60);
    const ParticleView v = sim.particles(0);
    CHECK(v.x[0] == Catch::Approx(0.0).margin(2e-3));
    CHECK(v.y[0] == Catch::Approx(1.0).margin(2e-3));
    CHECK(v.vx[0] == Catch::Approx(0.0).margin(1e-3));
    CHECK(v.vy[0] == Catch::Approx(1.0).margin(1e-3));

    // Below zero turns clockwise.
    put(layer, "motion", "orbit", number(-90.0));
    Simulation other = simulate(effect, 60);
    CHECK(other.particles(0).y[0] == Catch::Approx(-1.0).margin(2e-3));
}

TEST_CASE("Orbit changes where particles are, never how far from the centre") {
    Effect effect = oneLayer();
    Layer& layer = effect.layers[0];
    burstOnly(layer, 100);
    put(layer, "initial", "lifetime", constant(3.0));
    put(layer, "initial", "speed", range(0.5, 3.0));
    put(layer, "initial", "spread", number(180.0));
    put(layer, "motion", "gravity", vec(0, 0, 0));
    put(layer, "motion", "drag", number(1.2));
    put(layer, "shape", "offset", vec(1.5, -0.5, 0));
    Simulation plain = simulate(effect, 75);
    put(layer, "motion", "orbit", number(200.0));
    Simulation turned = simulate(effect, 75);

    const ParticleView a = plain.particles(0), b = turned.particles(0);
    REQUIRE(a.count == 100);
    double moved = 0;
    for (std::uint32_t i = 0; i < a.count; ++i) {
        // About the layer's own Position, not the middle of the effect.
        const double ra = std::hypot(a.x[i] - 1.5, a.y[i] + 0.5);
        const double rb = std::hypot(b.x[i] - 1.5, b.y[i] + 0.5);
        REQUIRE(rb == Catch::Approx(ra).margin(2e-3));
        moved += std::hypot(a.x[i] - b.x[i], a.y[i] - b.y[i]);
    }
    CHECK(moved / 100 > 0.3);
    CHECK(allFinite(turned));
}

TEST_CASE("In 3D, Orbit turns about the axis that points at the viewer and leaves depth alone") {
    Effect effect = straight(50, true);
    Layer& layer = effect.layers[0];
    put(layer, "initial", "spread", number(180.0));
    Simulation plain = simulate(effect, 40);
    put(layer, "motion", "orbit", number(120.0));
    Simulation turned = simulate(effect, 40);
    const ParticleView a = plain.particles(0), b = turned.particles(0);
    for (std::uint32_t i = 0; i < a.count; ++i) {
        REQUIRE(b.z[i] == a.z[i]);
        REQUIRE(b.vz[i] == a.vz[i]);
    }
}

// ------------------------------------------------------ squash and stretch

TEST_CASE("The stretch rule keeps a particle's area") {
    for (float amount : {-3.0f, -1.0f, -0.5f, -0.1f, 0.0f, 0.1f, 0.5f, 1.0f, 3.0f}) {
        float along = 0, across = 0;
        stretchFactors(amount, along, across);
        CAPTURE(amount);
        CHECK(along * across == Catch::Approx(1.0f).epsilon(1e-6));
        CHECK((along > 1.0f) == (amount > 0.0f));
    }
    float along = 0, across = 0;
    stretchFactors(0.5f, along, across);
    CHECK(along == 1.5f);  // half as long again
    // Squashing by an amount is stretching by it, turned a quarter turn.
    float flatAlong = 0, flatAcross = 0;
    stretchFactors(-0.5f, flatAlong, flatAcross);
    CHECK(flatAcross == along);
    CHECK(flatAlong == across);
}

TEST_CASE("Squash and Stretch reaches the frame: a curve by age, a number as it is, a range one each") {
    Effect effect = straight(100);
    Layer& layer = effect.layers[0];

    // Unset: nothing is carried and nothing is flagged.
    RenderFrame frame = frameAt(effect, 30);
    REQUIRE(frame.batches.size() == 1);
    CHECK_FALSE(frame.batches[0].squashStretches);
    CHECK_FALSE(frame.batches[0].eases);
    CHECK(frame.batches[0].hold == 0.0f);
    CHECK(frame.batches[0].shownStep == 30);
    for (const SpriteInstance& s : frame.instances) {
        REQUIRE(s.squashStretch == 0.0f);
        REQUIRE(s.ease == 1.0f);
        REQUIRE(s.life == 0.0f);
    }

    put(layer, "overLife", "stretch", constant(0.4));
    frame = frameAt(effect, 30);
    CHECK(frame.batches[0].squashStretches);
    for (const SpriteInstance& s : frame.instances) {
        REQUIRE(s.squashStretch == 0.4f);
    }

    // Stretched at birth, squashed at a quarter of its life, settled by half.
    put(layer, "overLife", "stretch", curve({{0, 0.6}, {0.25, -0.4}, {0.5, 0}, {1, 0}}));
    CHECK(frameAt(effect, 1).instances[0].squashStretch == Catch::Approx(0.6).margin(0.04));
    CHECK(frameAt(effect, 30).instances[0].squashStretch == Catch::Approx(-0.4).margin(1e-4));
    CHECK(frameAt(effect, 45).instances[0].squashStretch == Catch::Approx(-0.2).margin(1e-4));
    CHECK(frameAt(effect, 90).instances[0].squashStretch == 0.0f);

    // A range: each particle its own, the same at every age.
    put(layer, "overLife", "stretch", range(-0.3, 0.5));
    const RenderFrame early = frameAt(effect, 10), late = frameAt(effect, 100);
    REQUIRE(early.instances.size() == 100);
    REQUIRE(late.instances.size() == 100);
    std::set<float> different;
    for (std::size_t i = 0; i < 100; ++i) {
        const float s = early.instances[i].squashStretch;
        REQUIRE(s >= -0.3f);
        REQUIRE(s <= 0.5f);
        REQUIRE(late.instances[i].squashStretch == s);
        different.insert(s);
    }
    CHECK(different.size() > 90);
}

TEST_CASE("A layer with no Over Lifetime module neither squashes nor stretches") {
    Effect effect = straight();
    put(effect.layers[0], "overLife", "stretch", constant(0.5));
    removeModule(effect.layers[0], "overLife");
    const auto program = compileLayer(effect, effect.layers[0]);
    CHECK_FALSE(program->squashStretches);
    CHECK_FALSE(frameAt(effect, 20).batches[0].squashStretches);
}

// ------------------------------------------------------------ hold frames

TEST_CASE("On twos, a new drawing is taken every fifth step, the first as soon as anything has moved") {
    Effect effect = straight();
    put(effect.layers[0], "sprite", "hold", number(12.0));
    const auto program = compileLayer(effect, effect.layers[0]);
    REQUIRE(program->hold == 12.0f);

    std::vector<std::int64_t> taken;
    for (std::int64_t step = 1; step <= 60; ++step) {
        if (heldDrawing(*program, step) != heldDrawing(*program, step - 1)) {
            taken.push_back(step);
        }
    }
    // Twelve in the first second, five steps apart.
    REQUIRE(taken.size() == 12);
    CHECK(taken.front() == 1);
    for (std::size_t i = 1; i < taken.size(); ++i) {
        CHECK(taken[i] - taken[i - 1] == 5);
    }
}

TEST_CASE("A held layer shows, at every step, exactly what it looked like when its drawing was taken") {
    Effect effect = oneLayer();
    Layer& layer = effect.layers[0];
    put(layer, "emission", "rate", constant(40));
    put(layer, "emission", "bursts", Value(BurstList{{{0.0, 12}, {0.5, 20}}}));
    put(layer, "initial", "lifetime", range(0.3, 0.9));
    put(layer, "initial", "speed", range(1, 4));
    put(layer, "motion", "spin", range(-90, 90));
    put(layer, "overLife", "size", curve({{0, 0.2}, {0.3, 1}, {1, 0}}));
    put(layer, "overLife", "dissolve", number(0.5));
    Effect smooth = effect;
    put(layer, "sprite", "hold", number(8.0));  // on threes: seven and a half steps a drawing

    Simulation held(compileEffect(effect));
    RenderFrame frame;
    std::set<std::int64_t> drawings;
    for (std::int64_t step = 0; step <= 150; ++step) {
        held.seek(step);
        held.extract(frame);
        REQUIRE(frame.batches.size() == 1);
        const RenderBatch& batch = frame.batches[0];
        CAPTURE(step);
        CHECK(batch.hold == 8.0f);
        REQUIRE(batch.shownStep <= step);
        REQUIRE(step - batch.shownStep < 8);
        drawings.insert(batch.shownStep);
        // The very particles of that moment, as the same layer not held shows them.
        const RenderFrame then = frameAt(smooth, batch.shownStep);
        REQUIRE(drawn(frame) == drawn(then));
    }
    // 2.5 seconds at eight a second, and the empty moment before anything moved.
    CHECK(drawings.size() == 21);
    // Nothing is late: the burst at time zero is in the first drawing, one step in.
    held.seek(1);
    held.extract(frame);
    CHECK(frame.batches[0].shownStep == 1);
    CHECK(frame.instances.size() >= 12);
}

TEST_CASE("Hold Frames above the simulation's own rate, or at 0, is smooth") {
    Effect effect = straight();
    Layer& layer = effect.layers[0];
    for (double rate : {0.0, 60.0, 61.0}) {
        put(layer, "sprite", "hold", number(rate));
        CAPTURE(rate);
        CHECK(compileLayer(effect, layer)->hold == 0.0f);
    }
    put(layer, "sprite", "hold", number(30.0));
    CHECK(compileLayer(effect, layer)->hold == 30.0f);
}

TEST_CASE("Every pass of a loop is drawn at the same moments") {
    Effect effect = straight(6);
    effect.loop = "loop";
    effect.duration = 1.0;
    Layer& layer = effect.layers[0];
    put(layer, "initial", "lifetime", constant(0.6));
    put(layer, "initial", "spread", number(180.0));
    put(layer, "sprite", "hold", number(8.0));

    // Where its particles are, at each step of the first pass and of the fifth.
    Simulation sim(compileEffect(effect));
    RenderFrame first, fifth;
    for (std::int64_t step = 1; step <= 60; ++step) {
        sim.seek(step);
        sim.extract(first);
        sim.seek(step + 240);
        sim.extract(fifth);
        CAPTURE(step);
        REQUIRE(fifth.batches[0].shownStep - 240 == first.batches[0].shownStep);
        REQUIRE(first.instances.size() == fifth.instances.size());
        for (std::size_t i = 0; i < first.instances.size(); ++i) {
            REQUIRE(first.instances[i].age == Catch::Approx(fifth.instances[i].age).margin(1e-4));
        }
    }
    // The burst that opens a pass is seen one step into it, every time.
    sim.seek(241);
    sim.extract(fifth);
    CHECK(fifth.batches[0].shownStep == 241);
    CHECK(fifth.instances.size() == 6);
}

// ------------------------------------------- one rule holds for all of them

TEST_CASE("With every principle in play, scrubbing to a moment gives exactly what playing to it gives") {
    for (const bool loop : {true, false}) {
        for (const double hold : {0.0, 2.0, 8.0, 12.0, 24.0}) {
            Effect effect = oneLayer();
            effect.loop = loop ? "loop" : "once";
            effect.duration = loop ? 0.7 : 9.0;  // a pass that does not end on a step
            Layer& layer = effect.layers[0];
            layer.duration = effect.duration;
            put(layer, "emission", "rate", constant(70));
            put(layer, "emission", "bursts", Value(BurstList{{{0.0, 9}, {0.33, 5}}}));
            put(layer, "initial", "lifetime", range(0.15, 0.5));  // short lives, long holds
            put(layer, "initial", "speed", range(0.5, 2.0));
            put(layer, "shape", "offset", vec(0.4, 0.2, 0));
            put(layer, "motion", "drag", number(1.5));
            put(layer, "motion", "turbulence", number(1.0));
            put(layer, "motion", "speedOverLife", curve({{0, -0.5}, {0.2, 2.0}, {1, 0.3}}));
            put(layer, "motion", "orbit", number(150.0));
            put(layer, "overLife", "stretch", curve({{0, 0.5}, {0.3, -0.3}, {1, 0}}));
            put(layer, "sprite", "hold", number(hold));

            Simulation played(compileEffect(effect));
            RenderFrame a, b;
            for (const std::int64_t step : {1, 4, 5, 6, 29, 30, 31, 41, 42, 43, 133, 240, 401, 402, 3001}) {
                while (played.step() < step) {
                    played.advance();
                }
                played.extract(a);
                Simulation scrubbed(compileEffect(effect));
                scrubbed.seek(step);
                scrubbed.extract(b);
                CAPTURE(loop, hold, step);
                REQUIRE(hashFrame(a) == hashFrame(b));
                REQUIRE(drawn(a) == drawn(b));
                REQUIRE(played.stateHash() == scrubbed.stateHash());
            }
            // Backwards, and in jumps.
            for (const std::int64_t step : {2999, 133, 7, 2000, 61}) {
                played.seek(step);
                played.extract(a);
                Simulation fresh(compileEffect(effect));
                fresh.seek(step);
                fresh.extract(b);
                CAPTURE(loop, hold, step);
                REQUIRE(drawn(a) == drawn(b));
            }
        }
    }
}

TEST_CASE("Changing a held layer while it plays shows what the new settings give at this moment") {
    Effect effect = oneLayer();
    Layer& layer = effect.layers[0];
    put(layer, "emission", "rate", constant(50));
    put(layer, "initial", "lifetime", range(0.4, 1.0));
    put(layer, "sprite", "hold", number(12.0));

    Simulation sim(compileEffect(effect));
    sim.seek(37);
    put(layer, "initial", "speed", constant(5.0));
    put(layer, "sprite", "hold", number(6.0));
    sim.setProgram(compileEffect(effect));
    RenderFrame a, b;
    sim.extract(a);
    Simulation fresh(compileEffect(effect));
    fresh.seek(37);
    fresh.extract(b);
    CHECK(drawn(a) == drawn(b));
    CHECK(hashFrame(a) == hashFrame(b));

    // A snapshot keeps the drawing that was showing.
    const Simulation::Snapshot kept = sim.snapshot();
    sim.seek(90);
    sim.restore(kept);
    sim.extract(a);
    CHECK(drawn(a) == drawn(b));
}

TEST_CASE("Each principle shows in a frame's fingerprint, and none changes a layer that does not use it") {
    Effect effect = oneLayer();
    Layer& layer = effect.layers[0];
    put(layer, "emission", "rate", constant(30));
    const std::uint64_t plain = hashFrame(frameAt(effect, 47));
    const std::uint64_t state = simulate(effect, 47).stateHash();

    struct Change {
        const char* module;
        const char* key;
        Value on, off;
    };
    const Change changes[] = {
        {"motion", "speedOverLife", constant(0.6), constant(1.0)},
        {"motion", "orbit", number(45.0), number(0.0)},
        {"overLife", "stretch", constant(0.3), constant(0.0)},
        {"sprite", "hold", number(12.0), number(0.0)},
    };
    for (const Change& change : changes) {
        put(layer, change.module, change.key, change.on);
        CAPTURE(change.key);
        CHECK(hashFrame(frameAt(effect, 47)) != plain);
        put(layer, change.module, change.key, change.off);
        CHECK(hashFrame(frameAt(effect, 47)) == plain);
        CHECK(simulate(effect, 47).stateHash() == state);
    }
}

TEST_CASE("The principles compile into plain numbers") {
    Effect effect = straight();
    Layer& layer = effect.layers[0];
    auto program = compileLayer(effect, layer);
    CHECK_FALSE(program->eases);
    CHECK_FALSE(program->squashStretches);
    CHECK(program->orbit == 0.0f);
    CHECK(program->orbitCos == 1.0f);
    CHECK(program->orbitSin == 0.0f);
    CHECK(program->hold == 0.0f);

    put(layer, "motion", "orbit", number(180.0));
    put(layer, "motion", "speedOverLife", curve({{0, 0, true}, {1, 1, true}}));
    program = compileLayer(effect, layer);
    CHECK(program->orbit == Catch::Approx(det::kPi).epsilon(1e-6));
    // One step's turn: three degrees.
    CHECK(program->orbitSin == Catch::Approx(std::sin(det::kPi / 60.0)).epsilon(1e-6));
    CHECK(program->orbitCos == Catch::Approx(std::cos(det::kPi / 60.0)).epsilon(1e-6));
    CHECK(program->eases);
    // A bent curve is played as short straight pieces, like every other.
    CHECK(program->speedOverLife.keys.size() == static_cast<std::size_t>(kCurvePieces + 1));

    // With no Motion module nothing eases or turns.
    removeModule(layer, "motion");
    program = compileLayer(effect, layer);
    CHECK_FALSE(program->eases);
    CHECK(program->orbit == 0.0f);
}

TEST_CASE("The principles give these exact results on every machine") {
    // Recorded on one machine; every platform the app is built for must
    // reproduce them to the last bit.
    struct Pin {
        bool threeD;
        std::int64_t step;
        std::uint32_t alive;
        std::uint64_t state, frame;
    };
    const Pin pins[] = {
        {false, 47, 49u, 0x87fd2c4ab5d9a4d8ull, 0x76a9855036abab5aull},
        {false, 400, 58u, 0xc0994598d3a898c1ull, 0xf714e98a22ae8e4full},
        {true, 133, 52u, 0xa921be3ceef60eb5ull, 0x70fe4c811887faeeull},
        {true, 100000, 63u, 0x2ebdc1c2b7bb324full, 0xc9273a42d6095658ull},
    };
    RenderFrame frame;
    for (const Pin& pin : pins) {
        Effect effect = oneLayer(pin.threeD);
        effect.loop = "loop";
        effect.duration = 1.3;
        Layer& layer = effect.layers[0];
        layer.duration = 1.3;
        put(layer, "emission", "rate", constant(60));
        put(layer, "emission", "bursts", Value(BurstList{{{0.0, 15}, {0.6, 9}}}));
        put(layer, "initial", "lifetime", range(0.3, 1.1));
        put(layer, "initial", "speed", range(1.0, 5.0));
        put(layer, "initial", "spread", number(70.0));
        put(layer, "shape", "offset", vec(0.7, -0.2, 0.3));
        put(layer, "motion", "drag", number(1.1));
        put(layer, "motion", "turbulence", number(0.8));
        put(layer, "motion", "speedOverLife", curve({{0, 3.0}, {0.14, 0.9, true}, {0.26, -0.75, true}, {0.42, 0.12, true}, {1, 0.4}}));
        put(layer, "motion", "orbit", number(-170.0));
        put(layer, "overLife", "stretch", range(-0.4, 0.8));
        put(layer, "sprite", "hold", number(8.0));

        Simulation sim(compileEffect(effect));
        sim.seek(pin.step);
        sim.extract(frame);
        INFO((pin.threeD ? "3d" : "2d") << " step " << pin.step << ": {" << sim.aliveCount() << "u, 0x" << std::hex
                                        << sim.stateHash() << "ull, 0x" << hashFrame(frame) << "ull}");
        CHECK(sim.aliveCount() == pin.alive);
        CHECK(sim.stateHash() == pin.state);
        CHECK(hashFrame(frame) == pin.frame);
    }
}

// ------------------------------------------------------------ ground plane

TEST_CASE("In 3D, a circle or rectangle laid on the ground spreads over the floor, not the upright") {
    for (const char* shape : {"circle", "rectangle"}) {
        INFO(shape);
        Effect effect = oneLayer(true);
        Layer& layer = effect.layers[0];
        burstOnly(layer, 400);
        put(layer, "shape", "shape", text(shape));
        put(layer, "shape", "radius", number(2));
        put(layer, "shape", "size", vec(3, 2, 0));
        put(layer, "shape", "plane", text("ground"));
        put(layer, "initial", "speed", constant(0));
        put(layer, "initial", "lifetime", constant(10));
        removeModule(layer, "motion");
        Simulation sim = simulate(effect, 1);
        const ParticleView v = sim.particles(0);
        REQUIRE(v.count == 400);
        double spreadZ = 0;
        for (std::uint32_t i = 0; i < v.count; ++i) {
            REQUIRE(v.y[i] == Catch::Approx(0.0).margin(1e-6));
            spreadZ += std::fabs(v.z[i]);
        }
        CHECK(spreadZ / v.count > 0.3);
    }
}

TEST_CASE("In 3D, a ground layer's Orbit turns about the upright and leaves height alone") {
    Effect effect = straight(50, true);
    Layer& layer = effect.layers[0];
    put(layer, "initial", "spread", number(180.0));
    put(layer, "shape", "plane", text("ground"));
    put(layer, "shape", "offset", vec(1.0, 0.5, -2.0));
    Simulation plain = simulate(effect, 40);
    put(layer, "motion", "orbit", number(120.0));
    Simulation turned = simulate(effect, 40);
    const ParticleView a = plain.particles(0), b = turned.particles(0);
    REQUIRE(a.count == b.count);
    double moved = 0;
    for (std::uint32_t i = 0; i < a.count; ++i) {
        REQUIRE(b.y[i] == Catch::Approx(a.y[i]).margin(1e-5));
        REQUIRE(b.vy[i] == Catch::Approx(a.vy[i]).margin(1e-5));
        const double ra = std::hypot(a.x[i] - 1.0, a.z[i] + 2.0);
        const double rb = std::hypot(b.x[i] - 1.0, b.z[i] + 2.0);
        REQUIRE(rb == Catch::Approx(ra).margin(2e-3));
        moved += std::hypot(a.x[i] - b.x[i], a.z[i] - b.z[i]);
    }
    CHECK(moved / a.count > 0.1);
}

TEST_CASE("The ground plane means nothing in 2D, and upright changes nothing") {
    Effect effect = straight(50, false);
    Layer& layer = effect.layers[0];
    put(layer, "motion", "orbit", number(90.0));
    Simulation before = simulate(effect, 30);
    put(layer, "shape", "plane", text("ground"));
    Simulation flat = simulate(effect, 30);
    const ParticleView a = before.particles(0), b = flat.particles(0);
    for (std::uint32_t i = 0; i < a.count; ++i) {
        REQUIRE(a.x[i] == b.x[i]);
        REQUIRE(a.y[i] == b.y[i]);
    }
}
