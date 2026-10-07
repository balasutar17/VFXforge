#include <catch2/catch_amalgamated.hpp>

#include <cmath>
#include <limits>
#include <set>

#include "vfx/Effect.h"
#include "vfx/Metadata.h"

using namespace vfx;

namespace {

std::vector<const ModuleTypeDesc*> allTables() {
    const auto& registry = Registry::builtin();
    std::vector<const ModuleTypeDesc*> tables{&registry.effectFields(), &registry.layerFields()};
    for (const auto& m : registry.modules()) {
        tables.push_back(&m);
    }
    return tables;
}

const PropertyDesc& prop(const char* module, const char* key) {
    const auto* m = Registry::builtin().findModule(module);
    REQUIRE(m != nullptr);
    const auto* p = m->find(key);
    REQUIRE(p != nullptr);
    return *p;
}

}  // namespace

TEST_CASE("Phase 1 ships the six module types the architecture names") {
    std::set<std::string> types;
    for (const auto& m : Registry::builtin().modules()) {
        types.insert(m.type);
    }
    CHECK(types == std::set<std::string>{"emission", "shape", "initial", "motion", "overLife",
                                         "sprite"});
    CHECK(Registry::builtin().findModule("turbulence") == nullptr);
}

TEST_CASE("Every property table is well formed") {
    for (const auto* table : allTables()) {
        std::set<std::string> keys;
        for (const auto& p : table->properties) {
            INFO(table->type << "." << p.key);
            CHECK_FALSE(p.key.empty());
            CHECK_FALSE(p.label.empty());
            CHECK(keys.insert(p.key).second);
            // These two names are stored beside a module's properties, so a
            // property using one would overwrite it in the file.
            CHECK(p.key != "id");
            CHECK(p.key != "type");
            CHECK(p.key.find('/') == std::string::npos);
            if (p.kind == ValueKind::Enum) {
                CHECK_FALSE(p.options.empty());
            }
            if (p.min && p.max) {
                CHECK(*p.min <= *p.max);
            }
        }
    }
}

TEST_CASE("Every default is itself a valid value") {
    for (const auto* table : allTables()) {
        for (const auto& p : table->properties) {
            INFO(table->type << "." << p.key);
            CHECK(validateValue(p, p.defaultValue).ok());
            CHECK(repairValue(p, p.defaultValue) == p.defaultValue);
        }
    }
}

TEST_CASE("Layer and effect struct defaults match their metadata defaults") {
    const auto& registry = Registry::builtin();
    const Effect e;
    const Layer l;
    CHECK(Value(e.name) == registry.effectFields().find("name")->defaultValue);
    CHECK(Value(e.space) == registry.effectFields().find("space")->defaultValue);
    CHECK(Value(e.seed) == registry.effectFields().find("seed")->defaultValue);
    CHECK(Value(e.duration) == registry.effectFields().find("duration")->defaultValue);
    CHECK(Value(e.loop) == registry.effectFields().find("loop")->defaultValue);
    CHECK(Value(e.frameRate) == registry.effectFields().find("frameRate")->defaultValue);
    CHECK(Value(l.name) == registry.layerFields().find("name")->defaultValue);
    CHECK(Value(l.enabled) == registry.layerFields().find("enabled")->defaultValue);
    CHECK(Value(l.start) == registry.layerFields().find("start")->defaultValue);
    CHECK(Value(l.duration) == registry.layerFields().find("duration")->defaultValue);
}

TEST_CASE("Validation rejects values of the wrong type") {
    const auto& spread = prop("initial", "spread");
    CHECK_FALSE(validateValue(spread, Value(std::string("wide"))).ok());
    CHECK_FALSE(validateValue(spread, Value(true)).ok());
    CHECK_FALSE(validateValue(spread, Value(Scalar::constant(3))).ok());
    CHECK(validateValue(spread, Value(45.0)).ok());
}

TEST_CASE("Validation rejects numbers that are not finite or out of range") {
    const auto& spread = prop("initial", "spread");
    CHECK_FALSE(validateValue(spread, Value(std::nan(""))).ok());
    CHECK_FALSE(validateValue(spread, Value(std::numeric_limits<double>::infinity())).ok());
    CHECK_FALSE(validateValue(spread, Value(-0.001)).ok());
    CHECK_FALSE(validateValue(spread, Value(180.001)).ok());
    CHECK(validateValue(spread, Value(0.0)).ok());
    CHECK(validateValue(spread, Value(180.0)).ok());
}

TEST_CASE("Validation checks every form a scalar can take") {
    const auto& lifetime = prop("initial", "lifetime");
    CHECK(validateValue(lifetime, Value(Scalar::random(0.5, 1.5))).ok());
    CHECK(validateValue(lifetime, Value(Scalar::random(1.0, 1.0))).ok());
    CHECK_FALSE(validateValue(lifetime, Value(Scalar::random(2.0, 1.0))).ok());
    CHECK_FALSE(validateValue(lifetime, Value(Scalar::constant(0.0))).ok());
    CHECK(validateValue(lifetime, Value(Scalar::curve({{0, 1}, {0.5, 2}, {1, 1}}))).ok());
    CHECK_FALSE(validateValue(lifetime, Value(Scalar::curve({}))).ok());
    CHECK_FALSE(validateValue(lifetime, Value(Scalar::curve({{0.5, 1}, {0.5, 2}}))).ok());
    CHECK_FALSE(validateValue(lifetime, Value(Scalar::curve({{0.6, 1}, {0.5, 2}}))).ok());
    CHECK_FALSE(validateValue(lifetime, Value(Scalar::curve({{0, 1}, {1.5, 2}}))).ok());
    CHECK_FALSE(validateValue(lifetime, Value(Scalar::curve({{0, 1}, {1, -5}}))).ok());
}

TEST_CASE("Validation checks enums, colours, gradients, bursts and text") {
    const auto& blend = prop("sprite", "blend");
    CHECK(validateValue(blend, Value(std::string("additive"))).ok());
    CHECK_FALSE(validateValue(blend, Value(std::string("multiply"))).ok());

    const auto& color = prop("initial", "color");
    CHECK(validateValue(color, Value(Color{2.5, 0, 0, 1})).ok());  // brighter than white is fine
    CHECK_FALSE(validateValue(color, Value(Color{-0.1, 0, 0, 1})).ok());
    CHECK_FALSE(validateValue(color, Value(Color{1, 1, 1, 1.5})).ok());

    const auto& gradient = prop("overLife", "color");
    CHECK_FALSE(validateValue(gradient, Value(Gradient{})).ok());
    CHECK_FALSE(validateValue(gradient, Value(Gradient{{{0.5, Color{}}, {0.2, Color{}}}})).ok());

    const auto& bursts = prop("emission", "bursts");
    CHECK(validateValue(bursts, Value(BurstList{{{0.0, 24}, {0.5, 10}}})).ok());
    CHECK_FALSE(validateValue(bursts, Value(BurstList{{{-1.0, 24}}})).ok());
    CHECK_FALSE(validateValue(bursts, Value(BurstList{{{0.0, -3}}})).ok());

    const auto& name = *Registry::builtin().layerFields().find("name");
    CHECK(validateValue(name, Value(std::string("Étincelles ✨ 火花"))).ok());
    CHECK_FALSE(validateValue(name, Value(std::string("bad \xff\xfe bytes"))).ok());
    CHECK_FALSE(validateValue(name, Value(std::string(5000, 'x'))).ok());
}

TEST_CASE("Repair clamps, reorders or falls back to the default") {
    const auto& spread = prop("initial", "spread");
    CHECK(repairValue(spread, Value(500.0)) == Value(180.0));
    CHECK(repairValue(spread, Value(-5.0)) == Value(0.0));
    CHECK(repairValue(spread, Value(std::nan(""))) == spread.defaultValue);
    CHECK(repairValue(spread, Value(std::string("wide"))) == spread.defaultValue);

    const auto& lifetime = prop("initial", "lifetime");
    CHECK(repairValue(lifetime, Value(Scalar::random(2.0, 1.0))) == Value(Scalar::random(1.0, 2.0)));
    CHECK(repairValue(lifetime, Value(Scalar::curve({{1, 2}, {0, 1}}))) ==
          Value(Scalar::curve({{0, 1}, {1, 2}})));
    CHECK(repairValue(lifetime, Value(Scalar::curve({}))) == lifetime.defaultValue);

    const auto& blend = prop("sprite", "blend");
    CHECK(repairValue(blend, Value(std::string("multiply"))) == blend.defaultValue);
}

TEST_CASE("Whatever goes into repair, what comes out is valid") {
    const double nasty[] = {0.0, -1.0, 1e300, -1e300, std::nan(""),
                            std::numeric_limits<double>::infinity(), 0.5, 2.0};
    for (const auto* table : allTables()) {
        for (const auto& p : table->properties) {
            for (double a : nasty) {
                for (double b : nasty) {
                    const Value candidates[] = {
                        Value(a),
                        Value(Scalar::constant(a)),
                        Value(Scalar::random(a, b)),
                        Value(Scalar::curve({{a, b}, {b, a}})),
                        Value(Vec3{a, b, a}),
                        Value(Color{a, b, a, b}),
                        Value(Gradient{{{a, Color{b, a, b, a}}, {b, Color{}}}}),
                        Value(BurstList{{{a, 5}, {b, -5}}}),
                    };
                    for (const auto& candidate : candidates) {
                        INFO(table->type << "." << p.key);
                        CHECK(validateValue(p, repairValue(p, candidate)).ok());
                    }
                }
            }
        }
    }
}

TEST_CASE("A module made from metadata holds one default per property") {
    for (const auto& desc : Registry::builtin().modules()) {
        const Module m = makeModule(desc, Id{5});
        CHECK(m.type == desc.type);
        CHECK(m.known());
        REQUIRE(m.values.size() == desc.properties.size());
        for (const auto& p : desc.properties) {
            REQUIRE(m.find(p.key) != nullptr);
            CHECK(*m.find(p.key) == p.defaultValue);
        }
        CHECK(m.find("no-such-property") == nullptr);
    }
}
