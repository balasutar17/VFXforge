// How heavy an effect is on a phone, and making it lighter.
#include <catch2/catch_amalgamated.hpp>

#include <cmath>
#include <string>

#include "vfx/Templates.h"
#include "vfx/editor/Budget.h"
#include "vfx/editor/Presets.h"

using namespace vfx;
using namespace vfx::editor;

namespace {

void put(Layer& layer, const char* type, const char* key, Value value) {
    for (Module& m : layer.modules) {
        if (m.type == type) {
            if (Value* slot = m.find(key)) {
                REQUIRE(slot->index() == value.index());
                *slot = std::move(value);
                return;
            }
        }
    }
    FAIL("no such property");
}

// A one-shot burst of `count` still particles of a fixed size.
Effect still(int count, double size, int layers = 1) {
    IdGenerator ids(21);
    const IdSource newId = [&ids]() { return ids.next(); };
    Effect effect = makeEmptyEffect(newId, "Budget", false);
    effect.duration = 1.0;
    effect.loop = "once";
    for (int i = 0; i < layers; ++i) {
        Layer layer = makeBasicEmitter(newId, "Puffs " + std::to_string(i + 1));
        layer.duration = 1.0;
        put(layer, "emission", "rate", Scalar::constant(0.0));
        BurstList bursts;
        bursts.items.push_back(Burst{0.0, count});
        put(layer, "emission", "bursts", bursts);
        put(layer, "initial", "lifetime", Scalar::constant(0.5));
        put(layer, "initial", "speed", Scalar::constant(0.0));
        put(layer, "initial", "size", Scalar::constant(size));
        put(layer, "motion", "gravity", Vec3{0, 0, 0});
        effect.layers.push_back(std::move(layer));
    }
    return effect;
}

Effect preset(const char* id) {
    IdGenerator ids(5);
    const IdSource newId = [&ids]() { return ids.next(); };
    auto made = makePreset(id, newId);
    REQUIRE(made.ok());
    return made.value();
}

}  // namespace

TEST_CASE("Fill counts each particle's whole square against a 10-unit-tall phone screen", "[budget]") {
    // 20 particles 2 units across: 80 square units, on a 10 x 5.625 screen of 56.25.
    const BudgetReport r = measureBudget(still(20, 2.0));
    CHECK(r.peakParticles == 20);
    CHECK(r.peakFill == Catch::Approx(80.0 / 56.25).epsilon(0.01));
    CHECK(r.draws == 1);
    CHECK(r.fillGrade == BudgetGrade::Light);
    REQUIRE(r.layers.size() == 1);
    CHECK(r.layers[0].share == Catch::Approx(1.0));
}

TEST_CASE("Grades follow particles, fill and draws, the worst deciding", "[budget]") {
    CHECK(measureBudget(still(100, 0.1)).grade == BudgetGrade::Light);
    CHECK(measureBudget(still(300, 0.1)).particlesGrade == BudgetGrade::Fine);
    CHECK(measureBudget(still(1200, 0.1)).grade == BudgetGrade::TooHeavy);
    // 30 squares of 3 units: 270 / 56.25 = 4.8 screens.
    const BudgetReport big = measureBudget(still(30, 3.0));
    CHECK(big.fillGrade == BudgetGrade::Heavy);
    CHECK(big.grade == BudgetGrade::Heavy);
    CHECK_FALSE(big.notes.empty());
    // Fourteen layers of a few small sparks: light in every way but the draws.
    const BudgetReport many = measureBudget(still(5, 0.1, 14));
    CHECK(many.draws == 14);
    CHECK(many.drawsGrade == BudgetGrade::Heavy);
    CHECK(many.grade == BudgetGrade::Heavy);
}

TEST_CASE("Make mobile-safe trims what costs most, within limits, and leaves locked layers alone", "[budget]") {
    // Too many particles: halved at most.
    std::string said;
    const Effect crowded = still(1200, 0.1);
    const Effect lighter = makeMobileSafe(crowded, &said);
    const BudgetReport after = measureBudget(lighter);
    CHECK(after.peakParticles == 600);
    CHECK(after.grade == BudgetGrade::Heavy);
    CHECK(said.find("50% fewer particles") != std::string::npos);
    CHECK(said.find("change the look") != std::string::npos);

    // Too much fill from a few big sprites: they lose size, not count.
    const Effect glows = still(3, 9.0);
    const Effect smaller = makeMobileSafe(glows, &said);
    const BudgetReport glowsAfter = measureBudget(smaller);
    CHECK(glowsAfter.peakParticles == 3);
    CHECK(glowsAfter.peakFill < measureBudget(glows).peakFill);
    CHECK(said.find("smaller") != std::string::npos);

    // A locked layer is never changed.
    Effect locked = crowded;
    locked.layers[0].locked = true;
    const Effect same = makeMobileSafe(locked, &said);
    CHECK(measureBudget(same).peakParticles == 1200);
    CHECK(said.find("nothing could be trimmed") != std::string::npos);

    // Already fine: unchanged.
    const Effect fine = still(50, 0.2);
    makeMobileSafe(fine, &said);
    CHECK(said.find("nothing to change") != std::string::npos);
}

TEST_CASE("Make mobile-safe brings a library effect from too heavy to OK", "[budget]") {
    const Effect mist = preset("ground-mist");
    REQUIRE(measureBudget(mist).grade == BudgetGrade::TooHeavy);
    const Effect lighter = makeMobileSafe(mist);
    CHECK(measureBudget(lighter).grade == BudgetGrade::Fine);
    CHECK(lighter.layers.size() == mist.layers.size());
}
