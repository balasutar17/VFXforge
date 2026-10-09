// The promise these tests hold the simulation to: what you see at a moment
// depends only on the effect and the moment. Not on how playback got there,
// not on what was edited before, and not on the machine.
#include <catch2/catch_amalgamated.hpp>

#include <cstring>
#include <vector>

#include "random_values.h"
#include "sim_helpers.h"
#include "vfx/FileIO.h"

using namespace vfx;
using namespace testing;

namespace {

// An effect that uses every part of the simulation at once.
Effect busy(bool threeD, bool loop) {
    const std::uint64_t idSeed = threeD ? 5 : 6;
    Effect effect = sampleEffect(idSeed);
    // A layer's ID is part of its randomness. The second layer's ID is fixed
    // here as the 18th from the generator, where it stood when the
    // fingerprints below were recorded, so that adding a control to the
    // standard emitter can never change these effects.
    IdGenerator fixed(idSeed);
    for (int i = 0; i < 17; ++i) {
        fixed.next();
    }
    effect.layers[1].id = fixed.next();
    effect.space = threeD ? "3d" : "2d";
    effect.loop = loop ? "loop" : "once";
    effect.duration = 1.5;
    effect.seed = 2026;

    Layer& a = effect.layers[0];
    put(a, "emission", "rate", range(80, 160));
    put(a, "emission", "bursts", Value(BurstList{{{0.0, 30}, {0.7, 45}}}));
    put(a, "shape", "shape", text("sphere"));
    put(a, "shape", "radius", number(1.5));
    put(a, "initial", "lifetime", range(0.3, 2.5));
    put(a, "initial", "speed", range(0.5, 4));
    put(a, "initial", "direction", vec(0.2, 1, 0.4));
    put(a, "initial", "spread", number(70));
    put(a, "initial", "rotation", range(0, 360));
    put(a, "motion", "drag", number(0.8));
    put(a, "motion", "spin", range(-180, 180));
    put(a, "overLife", "size", Value(Scalar::curve({{0, 0.2}, {0.3, 1}, {1, 0}})));
    put(a, "overLife", "color",
        Value(Gradient{{{0, Color{1, 1, 0.6, 1}}, {1, Color{1, 0.2, 0, 0}}}}));
    put(a, "sprite", "columns", Value(std::int64_t{4}));
    put(a, "sprite", "rows", Value(std::int64_t{2}));
    put(a, "sprite", "frames", Value(std::int64_t{7}));
    put(a, "sprite", "animate", text("loop"));
    put(a, "sprite", "fps", number(15));
    put(a, "sprite", "randomStart", Value(true));

    Layer& b = effect.layers[1];
    b.start = 0.25;
    b.duration = 1.0;
    put(b, "emission", "rate", Value(Scalar::curve({{0, 300}, {1, 20}})));
    put(b, "shape", "shape", text("cone"));
    put(b, "shape", "radius", number(0.5));
    put(b, "shape", "angle", number(40));
    put(b, "initial", "lifetime", constant(0.9));
    put(b, "initial", "speed", Value(Scalar::curve({{0, 6}, {1, 1}})));
    put(b, "initial", "spread", number(15));
    put(b, "motion", "spin", Value(Scalar::curve({{0, 0}, {1, 720}})));
    put(b, "sprite", "blend", text("additive"));
    put(b, "sprite", "texture", Value(AssetRef{effect.assets[0].id}));
    put(b, "sprite", "columns", Value(std::int64_t{3}));
    put(b, "sprite", "rows", Value(std::int64_t{3}));
    put(b, "sprite", "animate", text("random"));
    return effect;
}

std::vector<std::uint64_t> hashesByStepping(const Effect& effect, std::int64_t steps) {
    Simulation sim(compileEffect(effect));
    std::vector<std::uint64_t> hashes;
    hashes.push_back(sim.stateHash());
    for (std::int64_t i = 0; i < steps; ++i) {
        sim.advance();
        hashes.push_back(sim.stateHash());
    }
    return hashes;
}

// A fingerprint of one layer's particles alone.
std::uint64_t layerHash(const Simulation& sim, std::size_t emitter) {
    const ParticleView v = sim.particles(emitter);
    std::uint64_t h = v.count;
    for (const float* data : {v.x, v.y, v.z, v.vx, v.vy, v.vz, v.age, v.lifetime, v.size,
                              v.rotation}) {
        for (std::uint32_t i = 0; i < v.count; ++i) {
            std::uint32_t word;
            std::memcpy(&word, &data[i], sizeof word);
            h = det::mix64(h ^ word);
        }
    }
    return h;
}

}  // namespace

TEST_CASE("Two runs of the same effect are identical at every step") {
    for (bool threeD : {false, true}) {
        const Effect effect = busy(threeD, true);
        const auto first = hashesByStepping(effect, 400);
        const auto second = hashesByStepping(effect, 400);
        CHECK(first == second);
        // And the effect really is doing something: the state keeps changing.
        CHECK(first[10] != first[11]);
        CHECK(first[200] != first[399]);
    }
}

TEST_CASE("Jumping to a moment gives exactly what playing to it gives") {
    for (bool loop : {false, true}) {
        for (bool threeD : {false, true}) {
            const Effect effect = busy(threeD, loop);
            const auto reference = hashesByStepping(effect, 600);
            const auto program = compileEffect(effect);

            SECTION(std::string("a fresh jump ") + (loop ? "loop " : "once ") +
                    (threeD ? "3d" : "2d")) {
                for (std::int64_t step : {0, 1, 2, 59, 60, 61, 89, 90, 91, 179, 180, 300, 599, 600}) {
                    Simulation sim(program);
                    sim.seek(step);
                    INFO("step " << step);
                    CHECK(sim.stateHash() == reference[static_cast<std::size_t>(step)]);
                }
            }
            SECTION(std::string("scrubbing about ") + (loop ? "loop " : "once ") +
                    (threeD ? "3d" : "2d")) {
                Simulation sim(program);
                Rng rng(threeD ? 1 : 2);
                for (int i = 0; i < 60; ++i) {
                    const auto step = static_cast<std::int64_t>(rng.below(601));
                    sim.seek(step);
                    REQUIRE(sim.step() == step);
                    REQUIRE(sim.stateHash() == reference[static_cast<std::size_t>(step)]);
                }
                sim.reset();
                CHECK(sim.stateHash() == reference[0]);
                sim.seek(-50);  // before the start is the start
                CHECK(sim.step() == 0);
            }
        }
    }
}

TEST_CASE("A looping effect can be entered at any pass without replaying the past") {
    // Lifetimes up to 2.5 s in a 1.5 s loop, so several passes overlap.
    const Effect effect = busy(true, true);
    const std::int64_t far = 60 * 45;  // 45 seconds: thirty passes
    const auto reference = hashesByStepping(effect, far);
    const auto program = compileEffect(effect);
    for (std::int64_t step : {far, far - 1, far - 37, far / 2, far / 3, std::int64_t{500},
                              std::int64_t{271}}) {
        Simulation sim(program);
        sim.seek(step);
        INFO("step " << step);
        CHECK(sim.stateHash() == reference[static_cast<std::size_t>(step)]);
    }

    // Jump far, play on, jump back, play on: always the same as the long run.
    Simulation sim(program);
    sim.seek(far - 200);
    for (int i = 0; i < 200; ++i) {
        sim.advance();
    }
    CHECK(sim.stateHash() == reference[static_cast<std::size_t>(far)]);
    sim.seek(900);
    for (int i = 0; i < 100; ++i) {
        sim.advance();
    }
    CHECK(sim.stateHash() == reference[1000]);
}

TEST_CASE("Long lifetimes in a short loop still match the long run") {
    Effect effect = oneLayer();
    effect.loop = "loop";
    effect.duration = 0.4;
    Layer& layer = effect.layers[0];
    put(layer, "emission", "rate", constant(40));
    put(layer, "emission", "bursts", Value(BurstList{{{0.1, 6}}}));
    put(layer, "initial", "lifetime", range(1, 12));
    put(layer, "initial", "spread", number(180));

    const std::int64_t far = 60 * 40;
    const auto reference = hashesByStepping(effect, far);
    const auto program = compileEffect(effect);
    for (std::int64_t step : {far, far - 5, std::int64_t{60 * 13 + 7}, std::int64_t{60 * 25}}) {
        Simulation sim(program);
        sim.seek(step);
        INFO("step " << step);
        CHECK(sim.stateHash() == reference[static_cast<std::size_t>(step)]);
    }
}

TEST_CASE("Jumping an hour into a looping effect is immediate and exact") {
    const auto program = compileEffect(busy(false, true));
    const std::int64_t far = 60LL * 3600 * 24 * 365;  // a year of playback

    Simulation direct(program);
    direct.seek(far);

    Simulation walked(program);
    walked.seek(far - 500);
    for (int i = 0; i < 500; ++i) {
        walked.advance();
    }
    CHECK(direct.step() == far);
    CHECK(direct.aliveCount() > 50);
    CHECK(direct.stateHash() == walked.stateHash());
    CHECK(allFinite(direct));
}

TEST_CASE("A saved state can be restored and continues identically") {
    const Effect effect = busy(true, true);
    const auto reference = hashesByStepping(effect, 300);
    Simulation sim(compileEffect(effect));
    sim.seek(100);
    const Simulation::Snapshot snapshot = sim.snapshot();
    CHECK(snapshot.step() == 100);

    sim.seek(250);
    CHECK(sim.stateHash() == reference[250]);
    sim.restore(snapshot);
    CHECK(sim.step() == 100);
    CHECK(sim.stateHash() == reference[100]);
    for (int i = 0; i < 200; ++i) {
        sim.advance();
    }
    CHECK(sim.stateHash() == reference[300]);

    // The snapshot is a copy: it can be used again, and by another simulation.
    Simulation other(compileEffect(oneLayer()));
    other.restore(snapshot);
    other.advance();
    CHECK(other.stateHash() == reference[101]);

    Simulation::Snapshot empty;
    other.restore(empty);  // an empty snapshot changes nothing
    CHECK(other.stateHash() == reference[101]);
}

TEST_CASE("Layers never affect one another") {
    const Effect both = busy(true, true);
    Simulation together = simulate(both, 200);
    const std::uint64_t a = layerHash(together, 0);
    const std::uint64_t b = layerHash(together, 1);
    CHECK(a != b);

    Effect onlyA = both;
    onlyA.layers.pop_back();
    CHECK(layerHash(simulate(onlyA, 200), 0) == a);

    Effect onlyB = both;
    onlyB.layers.erase(onlyB.layers.begin());
    CHECK(layerHash(simulate(onlyB, 200), 0) == b);

    Effect swapped = both;
    std::swap(swapped.layers[0], swapped.layers[1]);
    Simulation reordered = simulate(swapped, 200);
    CHECK(layerHash(reordered, 0) == b);
    CHECK(layerHash(reordered, 1) == a);

    Effect disabled = both;
    disabled.layers[0].enabled = false;
    CHECK(layerHash(simulate(disabled, 200), 0) == b);

    // Renaming things changes nothing either.
    Effect renamed = both;
    renamed.name = "Something else";
    renamed.layers[0].name = "Renamed";
    renamed.frameRate = 24;
    CHECK(simulate(renamed, 200).stateHash() == together.stateHash());
}

TEST_CASE("The seed changes the result; the same seed repeats it") {
    Effect effect = busy(false, true);
    const std::uint64_t original = simulate(effect, 150).stateHash();
    effect.seed = 2027;
    const std::uint64_t reseeded = simulate(effect, 150).stateHash();
    CHECK(reseeded != original);
    effect.seed = 2026;
    CHECK(simulate(effect, 150).stateHash() == original);

    // The particle counts stay the same when only the seed changes: the look
    // varies, the amount does not (the random rate is the exception, so pin it).
    put(effect.layers[0], "emission", "rate", constant(120));
    const std::uint32_t count = simulate(effect, 80).aliveCount();
    put(effect.layers[0], "initial", "lifetime", constant(1));
    effect.seed = 1;
    const std::uint32_t one = simulate(effect, 80).aliveCount();
    effect.seed = 999;
    CHECK(simulate(effect, 80).aliveCount() == one);
    CHECK(count > 0);
}

TEST_CASE("Editing while playing shows exactly what the new settings produce") {
    Document document(busy(true, true));
    CommandStack stack(document);
    LiveProgram live(document);
    Simulation sim(live.current());
    sim.seek(140);

    const std::uint64_t untouchedBefore = layerHash(sim, 1);

    // Drag a slider on the first layer while paused at step 140.
    stack.beginTransaction("Change Spread");
    for (double spread : {60.0, 50.0, 35.0, 20.0}) {
        REQUIRE(stack.push(set(propertyPath(document.effect(), 0, "initial", "spread"),
                               Value(spread))).ok());
        sim.setProgram(live.current());

        // The same as a fresh simulation of the edited effect, run to here.
        Simulation fresh(compileEffect(document.effect()));
        fresh.seek(140);
        REQUIRE(sim.step() == 140);
        REQUIRE(sim.stateHash() == fresh.stateHash());
        // The layer that was not edited kept its particles untouched.
        REQUIRE(layerHash(sim, 1) == untouchedBefore);
    }
    stack.endTransaction();

    // Undo, and the picture is exactly what it was.
    const std::uint64_t edited = sim.stateHash();
    REQUIRE(stack.undo().ok());
    sim.setProgram(live.current());
    Simulation original(compileEffect(document.effect()));
    original.seek(140);
    CHECK(sim.stateHash() == original.stateHash());
    CHECK(sim.stateHash() != edited);

    // Structural edits keep working too: remove a layer, add one, reorder.
    const Id first = document.effect().layers[0].id;
    REQUIRE(stack.push(std::make_unique<RemoveLayerCommand>(first)).ok());
    sim.setProgram(live.current());
    CHECK(sim.emitterCount() == 1);
    CHECK(layerHash(sim, 0) == untouchedBefore);
    REQUIRE(stack.undo().ok());
    REQUIRE(stack.push(std::make_unique<MoveLayerCommand>(first, 1)).ok());
    sim.setProgram(live.current());
    CHECK(layerHash(sim, 0) == untouchedBefore);
    Simulation moved(compileEffect(document.effect()));
    moved.seek(140);
    CHECK(sim.stateHash() == moved.stateHash());

    // And playback carries on from there without a hitch.
    sim.advance();
    moved.advance();
    CHECK(sim.stateHash() == moved.stateHash());
    sim.setProgram(nullptr);
    CHECK(sim.aliveCount() == 0);
}

TEST_CASE("These exact results on every machine") {
    // Fingerprints recorded once. (The frame fingerprints were recorded again
    // when frames gained particle velocity and shape, and both again when
    // particles gained a sprite-sheet pick; with that one field left out the
    // state fingerprints were checked to be the originals.) The same effect must give the same bits on
    // Windows, macOS and Linux, on Intel and on Arm. If a platform ever
    // differs, this is the test that says so.
    auto bytes = readFile(pathFromUtf8(std::string(VFX_SAMPLES_DIR) + "/coin_burst.vfx"));
    REQUIRE(bytes.ok());
    auto loaded = readEffect(bytes.value());
    REQUIRE(loaded.ok());
    Simulation coins(compileEffect(loaded.value().effect));
    RenderFrame frame;

    coins.seek(15);
    CHECK(coins.aliveCount() == 24);
    CHECK(coins.stateHash() == 0xb51e9ebf1a6f6aa6ull);
    coins.extract(frame);
    CHECK(hashFrame(frame) == 0x71b643ffcaa43430ull);
    coins.seek(60);
    CHECK(coins.aliveCount() == 15);
    CHECK(coins.stateHash() == 0x92a9228aee86e130ull);
    coins.seek(75);
    CHECK(coins.stateHash() == 0x0c447cd3e7b67e3aull);
    coins.extract(frame);
    CHECK(hashFrame(frame) == 0x8d6731a49c2585c3ull);

    // The busy effects go through every shape, curve and trigonometric path.
    struct Pin {
        bool threeD, loop;
        std::int64_t step;
        std::uint32_t alive;
        std::uint64_t state, frame;
    };
    const Pin pins[] = {
        {false, false, 100, 254u, 0x21ab6ad9011299e4ull, 0x1eb459172fd88cedull},
        {false, true, 400, 326u, 0x92a3f5ec81107348ull, 0x614376932ae4623full},
        {true, false, 100, 240u, 0xb76d6dc331cbb741ull, 0x15318b1ded0dfabaull},
        {true, true, 400, 344u, 0xa00abf80c71c3c99ull, 0x3885cc60ff31b1f8ull},
        {true, true, 100000, 316u, 0x17ec92d8d3065986ull, 0xff7d612b5fcef903ull},
    };
    for (const Pin& pin : pins) {
        Simulation sim(compileEffect(busy(pin.threeD, pin.loop)));
        sim.seek(pin.step);
        sim.extract(frame);
        INFO((pin.threeD ? "3d" : "2d") << (pin.loop ? " loop" : " once") << " step " << pin.step);
        CHECK(sim.aliveCount() == pin.alive);
        CHECK(sim.stateHash() == pin.state);
        CHECK(hashFrame(frame) == pin.frame);
    }
}

TEST_CASE("Any valid effect simulates without producing nonsense") {
    // Random but valid values for every property, including the extremes:
    // huge speeds, tiny lifetimes, maximum rates, zero sizes.
    Rng rng(8086);
    RenderFrame frame;
    const int rounds = 120;
    for (int round = 0; round < rounds; ++round) {
        Effect effect = sampleEffect(static_cast<std::uint64_t>(round) + 100);
        effect.space = rng.chance(0.5) ? "3d" : "2d";
        effect.loop = rng.chance(0.5) ? "loop" : "once";
        effect.duration = rng.chance(0.2) ? 0.01 : rng.between(0.05, 4.0);
        effect.seed = static_cast<std::int64_t>(rng.below(1000000));
        for (auto& layer : effect.layers) {
            layer.start = rng.chance(0.5) ? 0.0 : rng.between(0.0, 1.0);
            layer.duration = rng.between(0.01, 5.0);
            for (auto& m : layer.modules) {
                for (std::size_t i = 0; i < m.values.size(); ++i) {
                    const auto& desc = m.desc->properties[i];
                    m.values[i] = randomValue(desc, rng, effect);
                    // Keep the amount of work sane: this test is about
                    // strange values, not about a hundred thousand particles.
                    if (m.type == "emission" && desc.key == "rate" && rng.chance(0.9)) {
                        m.values[i] = Scalar::random(0, rng.between(1, 400));
                    }
                }
            }
        }
        INFO("round " << round);
        const auto program = compileEffect(effect);
        Simulation sim(program);
        const std::int64_t steps = 20 + static_cast<std::int64_t>(rng.below(200));
        std::vector<std::uint64_t> hashes;
        for (std::int64_t i = 0; i < steps; ++i) {
            sim.advance();
            hashes.push_back(sim.stateHash());
        }
        REQUIRE(allFinite(sim));
        for (const auto& st : sim.stats()) {
            REQUIRE(st.alive <= st.capacity);
        }
        sim.extract(frame);
        for (const auto& s : frame.instances) {
            for (float v : {s.x, s.y, s.z, s.size, s.rotation, s.r, s.g, s.b, s.a}) {
                REQUIRE(std::isfinite(v));
            }
            REQUIRE(s.a >= 0.0f);
            REQUIRE(s.size >= 0.0f);
        }
        // Jumping back and forth lands on the same states.
        const auto back = static_cast<std::int64_t>(rng.below(static_cast<std::size_t>(steps)));
        sim.seek(back + 1);
        REQUIRE(sim.stateHash() == hashes[static_cast<std::size_t>(back)]);
        Simulation fresh(program);
        fresh.seek(steps);
        REQUIRE(fresh.stateHash() == hashes.back());
    }
}
