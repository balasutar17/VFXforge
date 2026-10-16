// The viewport's handles: moving, turning and sizing a layer or a group.
#include <catch2/catch_amalgamated.hpp>

#include <cmath>

#include "vfx/Simulation.h"
#include "vfx/editor/Session.h"
#include "vfx/editor/Transform.h"

using namespace vfx;
using namespace vfx::editor;
using Catch::Approx;

namespace {

const Value* valueOf(const Layer& layer, const char* type, const char* key) {
    for (const Module& m : layer.modules) {
        if (m.type == type) {
            return m.find(key);
        }
    }
    return nullptr;
}
Value* valueOf(Layer& layer, const char* type, const char* key) {
    for (Module& m : layer.modules) {
        if (m.type == type) {
            return m.find(key);
        }
    }
    return nullptr;
}
Vec3 offsetOf(const Layer& l) { return std::get<Vec3>(*valueOf(l, "shape", "offset")); }
Vec3 directionOf(const Layer& l) { return std::get<Vec3>(*valueOf(l, "initial", "direction")); }
double sizeOf(const Layer& l) { return std::get<Scalar>(*valueOf(l, "initial", "size")).a; }
double rotationOf(const Layer& l) { return std::get<Scalar>(*valueOf(l, "initial", "rotation")).a; }

const Layer& layerById(const Session& s, Id id) { return s.effect().layers[static_cast<std::size_t>(layerIndex(s.effect(), id))]; }

}  // namespace

TEST_CASE("a 2D layer turns, moves and grows about the pivot", "[transform]") {
    Session s;
    REQUIRE(s.addEmitter("Sparks"));
    Layer layer = s.effect().layers.back();
    *valueOf(layer, "shape", "offset") = Vec3{2, 0, 0};
    *valueOf(layer, "initial", "direction") = Vec3{1, 0, 0};

    LayerTransform t;
    t.pivot = Vec3{1, 0, 0};
    t.turn = 90;
    Layer out = transformLayer(layer, t, false);
    // A quarter turn counter-clockwise about (1, 0): (2, 0) goes to (1, 1),
    // heading up instead of right, and the sprite turned with it.
    CHECK(offsetOf(out).x == Approx(1.0));
    CHECK(offsetOf(out).y == Approx(1.0));
    CHECK(directionOf(out).x == Approx(0.0).margin(1e-9));
    CHECK(directionOf(out).y == Approx(1.0));
    CHECK(rotationOf(out) == Approx(rotationOf(layer) + 90.0));

    t = LayerTransform{};
    t.pivot = Vec3{1, 0, 0};
    t.factor = 2;
    t.move = Vec3{0, 0.5, 0};
    out = transformLayer(layer, t, false);
    CHECK(offsetOf(out).x == Approx(3.0));  // twice as far from the pivot
    CHECK(offsetOf(out).y == Approx(0.5));
    CHECK(sizeOf(out) == Approx(sizeOf(layer) * 2));
    const Layer same = transformLayer(layer, LayerTransform{}, false);
    CHECK(offsetOf(same) == offsetOf(layer));
    CHECK(directionOf(same) == directionOf(layer));
}

TEST_CASE("in 3D a layer turns about the upright, as a character turns", "[transform]") {
    Session s;
    s.newEffect(true);
    REQUIRE(s.addEmitter("Orbs"));
    Layer layer = s.effect().layers.back();
    *valueOf(layer, "shape", "offset") = Vec3{1, 1.5, 0};
    *valueOf(layer, "initial", "direction") = Vec3{1, 0, 0};
    const double rotation = rotationOf(layer);

    LayerTransform t;
    t.turn = 90;  // counter-clockwise seen from above: +X goes to -Z
    Layer out = transformLayer(layer, t, true);
    CHECK(offsetOf(out).x == Approx(0.0).margin(1e-9));
    CHECK(offsetOf(out).y == Approx(1.5));
    CHECK(offsetOf(out).z == Approx(-1.0));
    CHECK(directionOf(out).z == Approx(-1.0));
    CHECK(rotationOf(out) == rotation);  // a sprite facing the camera does not turn

    // One lying on the ground does.
    *valueOf(layer, "shape", "plane") = std::string("ground");
    *valueOf(layer, "sprite", "facing") = std::string("plane");
    CHECK(rotationOf(transformLayer(layer, t, true)) == Approx(rotation + 90.0));
}

TEST_CASE("a handle drag changes the layer's whole group, live, and undoes as one step", "[transform][session]") {
    Session s;
    REQUIRE(s.openPreset("portal-3d"));
    REQUIRE(s.addPreset("guardian-orbs-3d"));
    const Id orbs = s.effect().layers.back().id;
    const Id portalLayer = s.effect().layers.front().id;

    // A layer of a group brings the whole group; one of no group, itself.
    const auto group = s.transformTargets(orbs);
    REQUIRE(group.size() == 2);
    CHECK(s.transformTargets(portalLayer) == std::vector<Id>{portalLayer});

    const Vec3 pivot = s.pivotOf(group);
    CHECK(pivot.y == Approx(1.6));
    const Vec3 portalWas = offsetOf(layerById(s, portalLayer));
    const std::size_t undoBefore = s.commands().undoCount();

    REQUIRE(s.beginTransform(group, "Turn Guardian Orbs 3D"));
    {
        // A drag that changes a single setting is still named for the drag.
        LayerTransform turn;
        turn.pivot = pivot;
        turn.turn = 30;
        REQUIRE(s.updateTransform(turn));
    }
    s.endTransform();
    CHECK(s.commands().undoName() == "Turn Guardian Orbs 3D");
    REQUIRE(s.undo());

    REQUIRE(s.beginTransform(group, "Move Guardian Orbs 3D"));
    for (int i = 1; i <= 20; ++i) {  // a drag: each update measured from the start
        LayerTransform t;
        t.pivot = pivot;
        t.move = Vec3{0.05 * i, 0, 0};
        REQUIRE(s.updateTransform(t));
    }
    for (const Id id : group) {
        CHECK(offsetOf(layerById(s, id)).x == Approx(1.0));
    }
    s.endTransform();
    CHECK(s.commands().undoCount() == undoBefore + 1);
    CHECK(s.commands().undoName() == "Move Guardian Orbs 3D");
    CHECK(offsetOf(layerById(s, portalLayer)) == portalWas);  // the portal stays put
    REQUIRE(s.undo());
    for (const Id id : group) {
        CHECK(offsetOf(layerById(s, id)).x == Approx(0.0));
    }

    // Cancelled: as it was, and nothing to undo.
    REQUIRE(s.beginTransform(group, "Turn"));
    LayerTransform t;
    t.pivot = pivot;
    t.turn = 45;
    t.factor = 1.5;
    REQUIRE(s.updateTransform(t));
    s.cancelTransform();
    CHECK(s.commands().undoCount() == undoBefore);
    for (const Id id : group) {
        CHECK(offsetOf(layerById(s, id)).x == Approx(0.0));
    }

    // In one go.
    REQUIRE(s.transformLayers(group, t, "Size"));
    CHECK(s.commands().undoName() == "Size");
    // The simulation keeps up.
    for (int i = 0; i < 30; ++i) {
        s.tick(1.0 / 60.0);
    }
    CHECK(s.frame().instances.size() > 0);
}
