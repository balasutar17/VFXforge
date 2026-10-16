// The handles change the selection and nothing else: one layer, a group (at
// any depth), or several layers from anywhere, each once, never the locked.
#include <catch2/catch_amalgamated.hpp>

#include <chrono>
#include <cmath>
#include <map>

#include "vfx/Serialize.h"
#include "vfx/editor/Selection.h"
#include "vfx/editor/Session.h"

using namespace vfx;
using namespace vfx::editor;
using Catch::Approx;

namespace {

Vec3 offsetOf(const Layer& l) {
    for (const Module& m : l.modules) {
        if (m.type == "shape") {
            return std::get<Vec3>(*m.find("offset"));
        }
    }
    return {};
}
Vec3 directionOf(const Layer& l) {
    for (const Module& m : l.modules) {
        if (m.type == "initial") {
            return std::get<Vec3>(*m.find("direction"));
        }
    }
    return {};
}
double sizeOf(const Layer& l) {
    for (const Module& m : l.modules) {
        if (m.type == "initial") {
            return std::get<Scalar>(*m.find("size")).a;
        }
    }
    return 0;
}

// Every value of every layer, to see which layers a change touched.
using Snapshot = std::map<std::uint64_t, std::vector<Value>>;
Snapshot snapshot(const Effect& e) {
    Snapshot s;
    for (const Layer& l : e.layers) {
        auto& values = s[l.id.value];
        for (const Module& m : l.modules) {
            values.insert(values.end(), m.values.begin(), m.values.end());
        }
    }
    return s;
}
std::vector<Id> changed(const Effect& e, const Snapshot& before) {
    std::vector<Id> out;
    const Snapshot now = snapshot(e);
    for (const Layer& l : e.layers) {
        if (now.at(l.id.value) != before.at(l.id.value)) {
            out.push_back(l.id);
        }
    }
    return out;
}

// An effect of ten layers: two loose, a group Fire with Flames and Smoke and
// a group Sparks inside it, and a group Glow; spread out so a turn shows.
struct Scene {
    Session s;
    std::map<std::string, Id> id;
    explicit Scene(bool threeD = false) {
        s.newEffect(threeD);
        while (!s.effect().layers.empty()) {
            REQUIRE(s.removeLayer(s.effect().layers.front().id));
        }
        const char* names[] = {"Loose A", "Loose B", "Flames", "Smoke", "Spark 1", "Spark 2", "Glow 1", "Glow 2", "Loose C", "Loose D"};
        const char* groups[] = {"", "", "Fire", "Fire", "Fire/Sparks", "Fire/Sparks", "Glow", "Glow", "", ""};
        for (int i = 0; i < 10; ++i) {
            Id made;
            REQUIRE(s.addEmitter(names[i], &made));
            id[names[i]] = made;
            const Layer& l = *findLayer(s.effect(), made);
            for (const Module& m : l.modules) {
                if (m.type == "shape") {
                    REQUIRE(s.set(Path::property(made, m.id, "offset"), Value(Vec3{double(i), double(i % 3), 0.0})));
                }
            }
            REQUIRE(s.set(Path::layerField(made, "group"), Value(std::string(groups[i]))));
        }
    }
    const Layer& layer(const char* name) const { return *findLayer(s.effect(), id.at(name)); }
    std::vector<Id> targets(Selection sel) const { return s.resolve(sel).targets; }
};

Selection layers(std::initializer_list<Id> ids) {
    Selection sel;
    sel.layers = ids;
    sel.active = *ids.begin();
    return sel;
}
Selection groups(std::initializer_list<const char*> paths) {
    Selection sel;
    for (const char* p : paths) {
        sel.groups.push_back(p);
    }
    return sel;
}

}  // namespace

TEST_CASE("one layer selected: only that layer moves, turns or sizes", "[selection]") {
    Scene sc;
    const Id smoke = sc.id["Smoke"];  // in the group Fire
    const auto targets = sc.targets(layers({smoke}));
    REQUIRE(targets == std::vector<Id>{smoke});

    for (int kind = 0; kind < 3; ++kind) {
        const Snapshot before = snapshot(sc.s.effect());
        LayerTransform t;
        t.pivot = sc.s.pivotOf(targets);
        if (kind == 0) t.move = Vec3{1, 0, 0};
        if (kind == 1) t.turn = 30;
        if (kind == 2) t.factor = 1.5;
        REQUIRE(sc.s.transformLayers(targets, t, "Change"));
        CHECK(changed(sc.s.effect(), before) == std::vector<Id>{smoke});
        REQUIRE(sc.s.undo());
        CHECK(snapshot(sc.s.effect()) == before);
    }
}

TEST_CASE("a group selected: all of it follows, groups inside included, nothing outside", "[selection]") {
    Scene sc;
    const auto fire = sc.targets(groups({"Fire"}));
    CHECK(fire == std::vector<Id>{sc.id["Flames"], sc.id["Smoke"], sc.id["Spark 1"], sc.id["Spark 2"]});
    // A group inside it, alone.
    CHECK(sc.targets(groups({"Fire/Sparks"})) == std::vector<Id>{sc.id["Spark 1"], sc.id["Spark 2"]});
    // "Fire" is not "Fire Explosion": paths match whole steps only.
    CHECK(inGroup(sc.layer("Spark 1"), "Fire"));
    Layer other = sc.layer("Spark 1");
    other.group = "Fire Explosion";
    CHECK_FALSE(inGroup(other, "Fire"));

    const Snapshot before = snapshot(sc.s.effect());
    const Vec3 flames = offsetOf(sc.layer("Flames")), spark = offsetOf(sc.layer("Spark 2"));
    LayerTransform t;
    t.pivot = sc.s.pivotOf(fire);
    t.turn = 90;
    REQUIRE(sc.s.transformLayers(fire, t, "Turn Fire"));
    const auto touched = changed(sc.s.effect(), before);
    CHECK(touched == fire);
    // They keep their places relative to each other: distances unchanged.
    const Vec3 f2 = offsetOf(sc.layer("Flames")), s2 = offsetOf(sc.layer("Spark 2"));
    CHECK(std::hypot(f2.x - s2.x, f2.y - s2.y) == Approx(std::hypot(flames.x - spark.x, flames.y - spark.y)));
}

TEST_CASE("two layers from different groups: only those two", "[selection]") {
    Scene sc;
    const Id a = sc.id["Spark 1"], b = sc.id["Glow 2"];
    Selection sel = layers({a, b});
    const auto targets = sc.targets(sel);
    REQUIRE(targets.size() == 2);
    const Snapshot before = snapshot(sc.s.effect());
    LayerTransform t;
    t.move = Vec3{0, 2, 0};
    REQUIRE(sc.s.transformLayers(targets, t, "Move"));
    const auto touched = changed(sc.s.effect(), before);
    CHECK(touched == std::vector<Id>{a, b});
}

TEST_CASE("a group and one of its own layers: that layer is moved once, not twice", "[selection]") {
    Scene sc;
    Selection sel = groups({"Fire", "Fire/Sparks"});
    sel.layers = {sc.id["Spark 1"], sc.id["Smoke"]};
    const auto targets = sc.targets(sel);
    CHECK(targets.size() == 4);
    const double was = offsetOf(sc.layer("Spark 1")).x;
    LayerTransform t;
    t.move = Vec3{1, 0, 0};
    REQUIRE(sc.s.transformLayers(targets, t, "Move"));
    CHECK(offsetOf(sc.layer("Spark 1")).x == Approx(was + 1.0));
}

TEST_CASE("locked layers are left alone, even inside a selected group", "[selection]") {
    Scene sc;
    REQUIRE(sc.s.set(Path::layerField(sc.id["Smoke"], "locked"), Value(true)));
    const ResolvedSelection r = sc.s.resolve(groups({"Fire"}));
    CHECK(r.locked == std::vector<Id>{sc.id["Smoke"]});
    CHECK(std::find(r.targets.begin(), r.targets.end(), sc.id["Smoke"]) == r.targets.end());
    // The group buttons too.
    const Vec3 smoke = offsetOf(sc.layer("Smoke"));
    REQUIRE(sc.s.moveGroup("Fire", 1, 0, 0));
    CHECK(offsetOf(sc.layer("Smoke")) == smoke);
    CHECK(offsetOf(sc.layer("Flames")).x == Approx(3.0));
    // A selection of nothing but locked layers changes nothing.
    CHECK(sc.s.resolve(layers({sc.id["Smoke"]})).empty());
}

TEST_CASE("undo and redo bring back every changed layer exactly", "[selection]") {
    Scene sc(true);
    Selection sel = groups({"Fire"});
    sel.layers = {sc.id["Glow 1"]};
    const auto targets = sc.targets(sel);
    const std::string before = writeEffect(sc.s.effect());
    REQUIRE(sc.s.beginTransform(targets, "Turn"));
    for (int i = 1; i <= 10; ++i) {
        LayerTransform t;
        t.pivot = sc.s.pivotOf(targets);
        t.turn = 9.0 * i;
        t.axis = 0;  // tipped over about X
        t.factor = 1.0 + 0.05 * i;
        REQUIRE(sc.s.updateTransform(t));
    }
    sc.s.endTransform();
    const std::string after = writeEffect(sc.s.effect());
    CHECK(after != before);
    REQUIRE(sc.s.undo());
    CHECK(writeEffect(sc.s.effect()) == before);
    REQUIRE(sc.s.redo());
    CHECK(writeEffect(sc.s.effect()) == after);
}

TEST_CASE("the pivot: the middle, the active layer, or each layer's own", "[selection]") {
    Scene sc;
    const Id a = sc.id["Loose A"], c = sc.id["Loose C"];  // at (0, 0) and (8, 2)
    const std::vector<Id> both{a, c};
    // Each about its own Position: a turn moves neither, only turns them.
    LayerTransform t;
    t.turn = 90;
    t.individual = true;
    REQUIRE(sc.s.transformLayers(both, t, "Turn"));
    CHECK(offsetOf(sc.layer("Loose C")).x == Approx(8.0));
    CHECK(directionOf(sc.layer("Loose C")).x == Approx(-1.0));  // (0, 1) turned a quarter
    REQUIRE(sc.s.undo());
    // About the active layer: it stays, the other swings round it.
    t = LayerTransform{};
    t.pivot = offsetOf(sc.layer("Loose A"));
    t.turn = 90;
    REQUIRE(sc.s.transformLayers(both, t, "Turn"));
    CHECK(offsetOf(sc.layer("Loose A")).x == Approx(0.0).margin(1e-9));
    CHECK(offsetOf(sc.layer("Loose C")).x == Approx(-2.0));
    CHECK(offsetOf(sc.layer("Loose C")).y == Approx(8.0));
}

TEST_CASE("in 3D a selection tips over about X and Z as well as turning about the upright", "[selection]") {
    Scene sc(true);
    const Id a = sc.id["Loose A"];
    LayerTransform t;
    t.pivot = offsetOf(sc.layer("Loose A"));
    t.turn = 90;
    t.axis = 0;  // about X: up (0, 1, 0) tips towards the front, +Z
    REQUIRE(sc.s.transformLayers({a}, t, "Tip"));
    CHECK(directionOf(sc.layer("Loose A")).z == Approx(1.0));
    REQUIRE(sc.s.undo());
    t.axis = 2;  // about Z: up tips to the left
    REQUIRE(sc.s.transformLayers({a}, t, "Tip"));
    CHECK(directionOf(sc.layer("Loose A")).x == Approx(-1.0));
}

TEST_CASE("stretching along one axis spreads the emitters that way only, particles keep their size", "[selection]") {
    Scene sc;
    const auto fire = sc.targets(groups({"Fire"}));
    const double size = sizeOf(sc.layer("Flames"));
    const double y = offsetOf(sc.layer("Spark 1")).y;
    LayerTransform t;
    t.pivot = Vec3{0, 0, 0};
    t.scale = Vec3{2, 1, 1};
    REQUIRE(sc.s.transformLayers(fire, t, "Stretch"));
    CHECK(offsetOf(sc.layer("Flames")).x == Approx(4.0));
    CHECK(offsetOf(sc.layer("Spark 1")).y == Approx(y));
    CHECK(sizeOf(sc.layer("Flames")) == Approx(size));
}

TEST_CASE("selected layers become a group inside the group they share, together in the list", "[selection][groups]") {
    Scene sc;
    std::string made;
    REQUIRE(sc.s.groupLayers({sc.id["Spark 2"], sc.id["Flames"]}, "Core", &made));
    CHECK(made == "Fire/Core");
    CHECK(sc.layer("Flames").group == "Fire/Core");
    CHECK(sc.layer("Spark 2").group == "Fire/Core");  // out of Sparks, into the new group
    // Next to each other.
    const int f = layerIndex(sc.s.effect(), sc.id["Flames"]);
    CHECK(layerIndex(sc.s.effect(), sc.id["Spark 2"]) == f + 1);
    // From different top-level groups: a new top-level group.
    REQUIRE(sc.s.groupLayers({sc.id["Loose A"], sc.id["Glow 1"]}, "Mix/ed", &made));
    CHECK(made == "Mix-ed");
    // Ungroup goes up one level only.
    REQUIRE(sc.s.ungroup("Fire/Core"));
    CHECK(sc.layer("Flames").group == "Fire");
    CHECK(sc.layer("Spark 2").group == "Fire");
    CHECK(groupPaths(sc.s.effect()) == std::vector<std::string>{"Mix-ed", "Fire", "Fire/Sparks", "Glow"});
    // All of it undoes.
    REQUIRE(sc.s.undo());
    REQUIRE(sc.s.undo());
    REQUIRE(sc.s.undo());
    CHECK(sc.layer("Flames").group == "Fire");
    CHECK(layerIndex(sc.s.effect(), sc.id["Flames"]) == 2);
}

TEST_CASE("a selection of hundreds of layers in an effect of a thousand changes quickly", "[selection][slow]") {
    Session s;
    std::vector<Id> half, rest;
    for (int i = 0; i < 1000; ++i) {
        Id made;
        REQUIRE(s.addEmitter("L", &made));
        (i % 2 == 0 ? half : rest).push_back(made);
    }
    const auto start = std::chrono::steady_clock::now();
    REQUIRE(s.beginTransform(half, "Move"));
    for (int i = 1; i <= 10; ++i) {
        LayerTransform t;
        t.move = Vec3{0.1 * i, 0, 0};
        REQUIRE(s.updateTransform(t));
    }
    s.endTransform();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(seconds < 2.0);
    CHECK(offsetOf(*findLayer(s.effect(), half.back())).x == Approx(1.0));
    CHECK(offsetOf(*findLayer(s.effect(), rest.back())).x == 0.0);
}

TEST_CASE("turning about a layer's own axes (any direction)", "[selection]") {
    Scene sc(true);
    const Id a = sc.id["Loose A"];  // at the origin, sending particles up
    LayerTransform t;
    t.axis = 3;
    t.axisVector = Vec3{0, 0, -1};  // = about Z the other way
    t.turn = 90;
    REQUIRE(sc.s.transformLayers({a}, t, "Turn"));
    CHECK(directionOf(sc.layer("Loose A")).x == Approx(1.0));
    REQUIRE(sc.s.undo());
    t.axisVector = Vec3{1, 1, 0};  // half a turn about the diagonal: up becomes right
    t.turn = 180;
    REQUIRE(sc.s.transformLayers({a}, t, "Turn"));
    CHECK(directionOf(sc.layer("Loose A")).x == Approx(1.0));
    CHECK(directionOf(sc.layer("Loose A")).y == Approx(0.0).margin(1e-9));
}
