#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <memory>
#include <set>

#include "random_values.h"
#include "sim_helpers.h"

using namespace vfx;
using namespace testing;

TEST_CASE("Curves are straight lines between keys and flat beyond the ends") {
    const std::vector<CurveKeyF> keys{{0.2f, 1.0f}, {0.6f, 3.0f}, {0.8f, 0.0f}};
    CHECK(evalCurve(keys, -1.0f) == 1.0f);
    CHECK(evalCurve(keys, 0.0f) == 1.0f);
    CHECK(evalCurve(keys, 0.2f) == 1.0f);
    CHECK(evalCurve(keys, 0.4f) == Catch::Approx(2.0f));
    CHECK(evalCurve(keys, 0.6f) == 3.0f);
    CHECK(evalCurve(keys, 0.7f) == Catch::Approx(1.5f));
    CHECK(evalCurve(keys, 0.8f) == 0.0f);
    CHECK(evalCurve(keys, 5.0f) == 0.0f);
    CHECK(evalCurve({{0.5f, 7.0f}}, 0.1f) == 7.0f);
    CHECK(evalCurve({{0.5f, 7.0f}}, 0.9f) == 7.0f);
    CHECK(evalCurve({}, 0.5f) == 0.0f);
    CHECK(evalCurve(keys, std::nanf("")) == 1.0f);  // nonsense in, something sane out
}

TEST_CASE("A scalar is sampled according to its form") {
    ScalarF constant;
    constant.a = 4.0f;
    CHECK(sample(constant, 0.9f, 0.9f) == 4.0f);
    CHECK(constant.maxValue() == 4.0f);

    ScalarF random;
    random.mode = ScalarF::Mode::Random;
    random.a = 2.0f;
    random.b = 6.0f;
    CHECK(sample(random, 0.0f, 0.5f) == 2.0f);
    CHECK(sample(random, 0.5f, 0.5f) == 4.0f);
    CHECK(random.maxValue() == 6.0f);

    ScalarF curve;
    curve.mode = ScalarF::Mode::Curve;
    curve.keys = {{0.0f, 1.0f}, {1.0f, 5.0f}};
    CHECK(sample(curve, 0.0f, 0.25f) == 2.0f);
    CHECK(curve.maxValue() == 5.0f);
}

TEST_CASE("A layer compiles to plain numbers") {
    Effect effect = oneLayer();
    effect.seed = 99;
    Layer& layer = effect.layers[0];
    put(layer, "emission", "rate", constant(50));
    put(layer, "shape", "shape", text("circle"));
    put(layer, "shape", "radius", number(2.5));
    put(layer, "shape", "emitFrom", text("edge"));
    put(layer, "initial", "lifetime", range(0.5, 1.5));
    put(layer, "initial", "direction", vec(3, 4, 0));
    put(layer, "initial", "spread", number(90));
    put(layer, "initial", "color", Value(Color{0.25, 0.5, 0.75, 0.5}));
    put(layer, "motion", "drag", number(0.75));
    put(layer, "sprite", "blend", text("additive"));
    put(layer, "sprite", "glow", number(3));

    const auto e = compileLayer(effect, layer);
    CHECK(e->layer == layer.id);
    CHECK(e->key == det::emitterKey(99, layer.id.value));
    CHECK(e->flat);
    CHECK_FALSE(e->loop);
    CHECK(e->effectDuration == 2.0);
    CHECK(e->emits);
    CHECK(e->rate.a == 50.0f);
    CHECK(e->shape == ShapeType::Circle);
    CHECK(e->fromEdge);
    CHECK(e->radius == 2.5f);
    CHECK(e->lifetime.mode == ScalarF::Mode::Random);
    CHECK(e->lifetime.b == 1.5f);
    CHECK(e->maxLifetime == 1.5);
    CHECK(e->dirX == Catch::Approx(0.6f));  // made unit length
    CHECK(e->dirY == Catch::Approx(0.8f));
    CHECK(e->spread == Catch::Approx(1.5707964f));
    CHECK(std::fabs(e->cosSpread) < 1e-6f);
    CHECK(e->color[3] == 0.5f);
    CHECK(e->gravityY == Catch::Approx(-9.8f));
    CHECK(e->drag == 0.75f);
    CHECK(e->drawn);
    CHECK(e->blend == BlendMode::Additive);
    CHECK(e->glow == 3.0f);
    CHECK(e->hasOverLife);
    CHECK(e->colorOverLife.empty());  // the default gradient is all white: nothing to do
    CHECK(e->opacityOverLife.isCurve());
}

TEST_CASE("In 2D depth is removed; in 3D it is kept") {
    Effect flat = oneLayer(false);
    put(flat.layers[0], "initial", "direction", vec(0, 0, 5));
    put(flat.layers[0], "motion", "gravity", vec(1, 2, 3));
    auto e = compileLayer(flat, flat.layers[0]);
    CHECK(e->flat);
    CHECK(e->dirZ == 0.0f);
    CHECK(e->dirY == 1.0f);  // pointing into the screen means nothing in 2D: use up
    CHECK(e->gravityZ == 0.0f);
    CHECK(e->gravityX == 1.0f);

    Effect deep = oneLayer(true);
    put(deep.layers[0], "initial", "direction", vec(0, 0, 5));
    put(deep.layers[0], "motion", "gravity", vec(1, 2, 3));
    e = compileLayer(deep, deep.layers[0]);
    CHECK_FALSE(e->flat);
    CHECK(e->dirZ == 1.0f);
    CHECK(e->gravityZ == 3.0f);

    put(deep.layers[0], "initial", "direction", vec(0, 0, 0));
    e = compileLayer(deep, deep.layers[0]);
    CHECK(e->dirY == 1.0f);  // no direction at all: use up
}

TEST_CASE("A layer cannot emit past the end of the effect") {
    Effect effect = oneLayer();
    Layer& layer = effect.layers[0];
    layer.start = 1.5;
    layer.duration = 3.0;
    auto e = compileLayer(effect, layer);
    CHECK(e->start == 1.5);
    CHECK(e->duration == 0.5);
    CHECK(e->emits);

    layer.start = 2.5;
    e = compileLayer(effect, layer);
    CHECK(e->duration == 0.0);
    CHECK_FALSE(e->emits);
    CHECK(e->capacity == 0);
}

TEST_CASE("Each missing module has a defined meaning") {
    const Effect base = oneLayer();

    SECTION("no Emission: nothing is ever born") {
        Effect e = base;
        removeModule(e.layers[0], "emission");
        CHECK_FALSE(compileLayer(e, e.layers[0])->emits);
    }
    SECTION("no Shape: a point") {
        Effect e = base;
        put(e.layers[0], "shape", "shape", text("box"));
        removeModule(e.layers[0], "shape");
        CHECK(compileLayer(e, e.layers[0])->shape == ShapeType::Point);
    }
    SECTION("no Initial State: the module's defaults") {
        Effect e = base;
        removeModule(e.layers[0], "initial");
        const auto p = compileLayer(e, e.layers[0]);
        CHECK(p->lifetime.a == 1.0f);
        CHECK(p->speed.a == 2.0f);
        CHECK(p->size.a == 0.2f);
    }
    SECTION("no Motion: no forces at all") {
        Effect e = base;
        removeModule(e.layers[0], "motion");
        const auto p = compileLayer(e, e.layers[0]);
        CHECK(p->gravityY == 0.0f);
        CHECK(p->drag == 0.0f);
    }
    SECTION("no Over Lifetime: nothing changes with age") {
        Effect e = base;
        removeModule(e.layers[0], "overLife");
        const auto p = compileLayer(e, e.layers[0]);
        CHECK_FALSE(p->hasOverLife);
        CHECK(p->sizeOverLife.a == 1.0f);
    }
    SECTION("no Sprite: simulated but not drawn") {
        Effect e = base;
        removeModule(e.layers[0], "sprite");
        const auto p = compileLayer(e, e.layers[0]);
        CHECK_FALSE(p->drawn);
        CHECK(p->emits);
    }
}

TEST_CASE("Bursts are put in time order and useless ones are dropped") {
    Effect effect = oneLayer();
    Layer& layer = effect.layers[0];
    layer.duration = 1.0;
    put(layer, "emission", "rate", constant(0));
    put(layer, "emission", "bursts",
        Value(BurstList{{{0.75, 5}, {0.25, 7}, {1.5, 9}, {0.5, 0}, {0.0, 3}}}));
    const auto e = compileLayer(effect, layer);
    REQUIRE(e->bursts.size() == 3);
    CHECK(e->bursts[0].time == 0.0);
    CHECK(e->bursts[0].count == 3);
    CHECK(e->bursts[1].count == 7);
    CHECK(e->bursts[2].count == 5);
    CHECK(e->emits);

    put(layer, "emission", "bursts", Value(BurstList{}));
    CHECK_FALSE(compileLayer(effect, layer)->emits);  // no rate and no bursts
}

TEST_CASE("Room for particles is worked out at compile time") {
    Effect effect = oneLayer();
    effect.loop = "loop";
    Layer& layer = effect.layers[0];
    put(layer, "emission", "rate", constant(100));
    put(layer, "initial", "lifetime", constant(2));
    auto e = compileLayer(effect, layer);
    CHECK(e->capacity >= 200);
    CHECK(e->capacity <= 230);
    CHECK_FALSE(e->capped());

    put(layer, "emission", "rate", constant(100000));
    put(layer, "initial", "lifetime", constant(100));
    e = compileLayer(effect, layer);
    CHECK(e->capacity == kMaxParticlesPerEmitter);
    CHECK(e->wanted > kMaxParticlesPerEmitter);
    CHECK(e->capped());

    // A one-shot burst needs room for the burst and little more.
    effect.loop = "once";
    burstOnly(layer, 24);
    e = compileLayer(effect, layer);
    CHECK(e->capacity >= 24);
    CHECK(e->capacity <= 48);
}

TEST_CASE("A colour gradient is kept only when it changes something") {
    Effect effect = oneLayer();
    Layer& layer = effect.layers[0];
    put(layer, "overLife", "color",
        Value(Gradient{{{0.0, Color{1, 1, 1, 1}}, {1.0, Color{1, 0.5, 0, 1}}}}));
    const auto e = compileLayer(effect, layer);
    REQUIRE(e->colorOverLife.size() == 2);
    CHECK(e->colorOverLife[1].g == 0.5f);
}

TEST_CASE("The whole effect compiles to its enabled layers, in stack order") {
    Effect effect = sampleEffect();
    effect.layers.push_back(effect.layers[0]);
    effect.layers[2].id = Id{0x333};
    effect.layers[1].enabled = false;

    const auto program = compileEffect(effect);
    REQUIRE(program->emitters.size() == 2);
    CHECK(program->emitters[0]->layer == effect.layers[0].id);
    CHECK(program->emitters[1]->layer == Id{0x333});
    CHECK(program->seed == static_cast<std::uint64_t>(effect.seed));
    CHECK(program->loop);
    CHECK(program->flat);
    // Each layer has its own stream of random numbers.
    CHECK(program->emitters[0]->key != program->emitters[1]->key);
}

TEST_CASE("Modules this version does not know, and second copies, are ignored") {
    Effect effect = oneLayer();
    Layer& layer = effect.layers[0];
    put(layer, "motion", "drag", number(2));

    Module future;
    future.id = Id{0x4242};
    future.type = "turbulence";
    layer.modules.insert(layer.modules.begin(), future);

    Module second = module(layer, "motion");
    second.id = Id{0x4343};
    *second.find("drag") = 9.0;
    layer.modules.push_back(second);

    const auto e = compileLayer(effect, layer);
    CHECK(e->drag == 2.0f);  // the first Motion module is the one that counts
}

TEST_CASE("Editing a document recompiles only the layers that changed") {
    Document document(sampleEffect());
    CommandStack stack(document);
    LiveProgram live(document);

    const auto first = live.current();
    REQUIRE(first->emitters.size() == 2);
    CHECK(live.layersCompiled() == 2);
    CHECK(live.current() == first);  // nothing changed: the very same program
    CHECK(live.layersCompiled() == 2);

    SECTION("a property edit recompiles one layer and reuses the other") {
        REQUIRE(stack.push(set(propertyPath(document.effect(), 0, "initial", "spread"),
                               Value(90.0))).ok());
        const auto second = live.current();
        CHECK(second != first);
        CHECK(second->emitters[0] != first->emitters[0]);
        CHECK(second->emitters[1] == first->emitters[1]);
        CHECK(live.layersCompiled() == 3);

        REQUIRE(stack.undo().ok());
        const auto third = live.current();
        CHECK(third->emitters[1] == first->emitters[1]);
        CHECK(third->emitters[0]->spread == first->emitters[0]->spread);
    }
    SECTION("a whole drag recompiles the layer once per frame shown, not per edit") {
        stack.beginTransaction("Change Spread");
        for (int i = 1; i <= 100; ++i) {
            REQUIRE(stack.push(set(propertyPath(document.effect(), 0, "initial", "spread"),
                                   Value(static_cast<double>(i)))).ok());
        }
        stack.endTransaction();
        (void)live.current();
        CHECK(live.layersCompiled() == 3);
    }
    SECTION("names and the frame rate do not reach the simulation") {
        REQUIRE(stack.push(set(Path::effect("name"), Value(std::string("New")))).ok());
        REQUIRE(stack.push(set(Path::effect("frameRate"), Value(24.0))).ok());
        REQUIRE(stack.push(set(Path::layerField(document.effect().layers[0].id, "name"),
                               Value(std::string("Renamed")))).ok());
        CHECK(live.current() == first);
        CHECK(live.layersCompiled() == 2);
    }
    SECTION("the seed, duration, loop mode and space affect every layer") {
        for (const auto& edit : {std::pair<const char*, Value>{"seed", Value(std::int64_t{5})},
                                 {"duration", Value(3.0)},
                                 {"loop", Value(std::string("once"))},
                                 {"space", Value(std::string("3d"))}}) {
            const auto before = live.current();
            const auto compiled = live.layersCompiled();
            REQUIRE(stack.push(set(Path::effect(edit.first), edit.second)).ok());
            const auto after = live.current();
            INFO(edit.first);
            CHECK(after->emitters[0] != before->emitters[0]);
            CHECK(after->emitters[1] != before->emitters[1]);
            CHECK(live.layersCompiled() == compiled + 2);
        }
    }
    SECTION("reordering layers compiles nothing") {
        REQUIRE(stack.push(std::make_unique<MoveLayerCommand>(document.effect().layers[0].id, 1))
                    .ok());
        const auto second = live.current();
        CHECK(second != first);
        CHECK(second->emitters[0] == first->emitters[1]);
        CHECK(second->emitters[1] == first->emitters[0]);
        CHECK(live.layersCompiled() == 2);
    }
    SECTION("turning a layer off removes it; turning it on compiles it") {
        const Path enabled = Path::layerField(document.effect().layers[0].id, "enabled");
        REQUIRE(stack.push(set(enabled, Value(false))).ok());
        const auto off = live.current();
        REQUIRE(off->emitters.size() == 1);
        CHECK(off->emitters[0] == first->emitters[1]);
        CHECK(live.layersCompiled() == 2);

        REQUIRE(stack.push(set(enabled, Value(true))).ok());
        CHECK(live.current()->emitters.size() == 2);
        CHECK(live.layersCompiled() == 3);
    }
    SECTION("adding, removing and changing modules recompiles that layer") {
        const Id layer = document.effect().layers[1].id;
        const Id motion = moduleOfType(document.effect().layers[1], "motion").id;
        REQUIRE(stack.push(std::make_unique<RemoveModuleCommand>(layer, motion)).ok());
        const auto second = live.current();
        CHECK(second->emitters[0] == first->emitters[0]);
        CHECK(second->emitters[1]->gravityY == 0.0f);
        CHECK(live.layersCompiled() == 3);
    }
    SECTION("removing a layer and adding assets") {
        Asset asset;
        asset.id = document.newId();
        asset.path = "textures/new.png";
        REQUIRE(stack.push(std::make_unique<AddAssetCommand>(asset)).ok());
        CHECK(live.current() == first);  // an unused asset changes nothing simulated

        REQUIRE(stack.push(std::make_unique<RemoveLayerCommand>(document.effect().layers[0].id))
                    .ok());
        const auto second = live.current();
        REQUIRE(second->emitters.size() == 1);
        CHECK(second->emitters[0] == first->emitters[1]);
        CHECK(live.layersCompiled() == 2);
    }
}

TEST_CASE("Compiling never fails, whatever valid values a layer holds") {
    Rng rng(404);
    for (int round = 0; round < 500; ++round) {
        Effect effect = sampleEffect(static_cast<std::uint64_t>(round) + 1);
        effect.space = rng.chance(0.5) ? "3d" : "2d";
        effect.loop = rng.chance(0.5) ? "loop" : "once";
        effect.duration = rng.between(0.01, 20.0);
        for (auto& layer : effect.layers) {
            layer.start = rng.between(0.0, 5.0);
            layer.duration = rng.between(0.01, 30.0);
            for (auto& m : layer.modules) {
                for (std::size_t i = 0; i < m.values.size(); ++i) {
                    m.values[i] = randomValue(m.desc->properties[i], rng, effect);
                }
            }
            const auto e = compileLayer(effect, layer);
            CHECK(e->capacity <= kMaxParticlesPerEmitter);
            CHECK(e->maxLifetime > 0.0);
            CHECK(e->duration >= 0.0);
            if (e->duration > 0.0) {
                CHECK(e->start + e->duration <= e->effectDuration + 1e-9);
            } else {
                CHECK_FALSE(e->emits);  // starts after the effect has ended
            }
            const float length = e->dirX * e->dirX + e->dirY * e->dirY + e->dirZ * e->dirZ;
            CHECK(std::fabs(length - 1.0f) < 1e-5f);
            if (e->flat) {
                CHECK(e->dirZ == 0.0f);
                CHECK(e->gravityZ == 0.0f);
            }
        }
    }
}

TEST_CASE("Compile: every sprite shape, alignment and stretch") {
    Effect effect = oneLayer();
    Layer& layer = effect.layers[0];

    // A new layer draws upright soft dots.
    auto e = compileLayer(effect, layer);
    CHECK(e->spriteShape == SpriteShape::Soft);
    CHECK_FALSE(e->alongMotion);
    CHECK(e->stretch == 0.0f);

    // The enum follows the order the property lists its options in.
    const auto& options = Registry::builtin().findModule("sprite")->find("shape")->options;
    REQUIRE(options.size() == static_cast<std::size_t>(kSpriteShapeCount));
    CHECK(options.front() == "soft");
    CHECK(options[static_cast<std::size_t>(SpriteShape::Bubble)] == "bubble");
    CHECK(options[static_cast<std::size_t>(SpriteShape::Heart)] == "heart");
    CHECK(options[static_cast<std::size_t>(SpriteShape::Flame)] == "flame");
    CHECK(options[static_cast<std::size_t>(SpriteShape::Puff)] == "puff");
    CHECK(options[static_cast<std::size_t>(SpriteShape::Glint)] == "glint");
    CHECK(options.back() == "blaze");
    for (std::size_t i = 0; i < options.size(); ++i) {
        put(layer, "sprite", "shape", text(options[i].c_str()));
        CHECK(static_cast<std::size_t>(compileLayer(effect, layer)->spriteShape) == i);
    }

    // Stretch only means something when particles point along their movement.
    put(layer, "sprite", "stretch", number(0.2));
    CHECK(compileLayer(effect, layer)->stretch == 0.0f);
    put(layer, "sprite", "align", text("movement"));
    e = compileLayer(effect, layer);
    CHECK(e->alongMotion);
    CHECK(e->stretch == 0.2f);
}

TEST_CASE("Sprite sheets: grid, frame count and which picture each particle shows") {
    Effect effect = oneLayer();
    Layer& layer = effect.layers[0];
    Asset sheet;
    sheet.id = Id{0x5eed5eedull};
    sheet.path = "images/fire.png";
    effect.assets.push_back(sheet);

    // Without a texture there is no sheet, whatever the grid says.
    put(layer, "sprite", "columns", Value(std::int64_t{4}));
    put(layer, "sprite", "rows", Value(std::int64_t{2}));
    auto e = compileLayer(effect, layer);
    CHECK(e->columns == 1);
    CHECK(e->rows == 1);
    CHECK(e->frames == 1);

    put(layer, "sprite", "texture", Value(AssetRef{sheet.id}));
    e = compileLayer(effect, layer);
    CHECK(e->columns == 4);
    CHECK(e->rows == 2);
    CHECK(e->frames == 8);  // 0 means every cell
    CHECK(e->animate == Animate::Life);
    put(layer, "sprite", "frames", Value(std::int64_t{6}));
    CHECK(compileLayer(effect, layer)->frames == 6);
    put(layer, "sprite", "frames", Value(std::int64_t{99}));
    CHECK(compileLayer(effect, layer)->frames == 8);  // never more than the grid holds
    put(layer, "sprite", "frames", Value(std::int64_t{0}));

    auto framesOf = [&](std::int64_t step) {
        Simulation sim(compileEffect(effect));
        sim.seek(step);
        RenderFrame frame;
        sim.extract(frame);
        const ParticleView v = sim.particles(0);
        REQUIRE(frame.batches.size() == 1);
        CHECK(frame.batches[0].columns == 4);
        CHECK(frame.batches[0].rows == 2);
        REQUIRE(v.count == frame.instances.size());
        std::vector<std::pair<float, float>> out;  // (age / lifetime, frame)
        for (std::uint32_t i = 0; i < v.count; ++i) {
            out.emplace_back(v.age[i], frame.instances[i].frame);
            CHECK(frame.instances[i].frame >= 0.0f);
            CHECK(frame.instances[i].frame <= 7.0f);
            CHECK(frame.instances[i].frame == std::floor(frame.instances[i].frame));
        }
        return std::make_pair(out, std::vector<float>(v.lifetime, v.lifetime + v.count));
    };

    // Once over each particle's life.
    {
        auto [ages, lives] = framesOf(50);
        REQUIRE_FALSE(ages.empty());
        for (std::size_t i = 0; i < ages.size(); ++i) {
            const float lived = std::clamp(ages[i].first / lives[i], 0.0f, 1.0f);
            const int expected = std::min(7, static_cast<int>(lived * 8.0f));
            CHECK(ages[i].second == static_cast<float>(expected));
        }
    }

    // Looping at the frame rate.
    put(layer, "sprite", "animate", text("loop"));
    put(layer, "sprite", "fps", number(10));
    {
        auto [ages, lives] = framesOf(50);
        for (const auto& [age, frame] : ages) {
            const auto expected = static_cast<std::int64_t>(std::floor(age * 10.0)) % 8;
            CHECK(frame == static_cast<float>(expected));
        }
    }

    // One random picture each, held for life, and different particles differ.
    put(layer, "sprite", "animate", text("random"));
    {
        auto [early, lives] = framesOf(40);
        auto [later, lives2] = framesOf(41);
        std::set<float> seen;
        for (const auto& p : early) {
            seen.insert(p.second);
        }
        CHECK(seen.size() > 3);
        // A particle alive in both keeps its picture. The dead leave from the
        // front, the newborn join at the back, and the rest keep their order.
        std::size_t newborn = 0;
        for (const auto& p : later) {
            if (p.first < static_cast<float>(kSimulationStep)) {
                ++newborn;
            }
        }
        const std::size_t kept = later.size() - newborn;
        REQUIRE(kept <= early.size());
        const std::size_t died = early.size() - kept;
        REQUIRE(kept > 10);
        for (std::size_t k = 0; k < kept; ++k) {
            CHECK(early[died + k].second == later[k].second);
        }
    }
}
