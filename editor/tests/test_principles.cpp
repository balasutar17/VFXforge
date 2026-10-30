// The animation principles (0.12) in the editor layer: how they are drawn,
// the ready-made curves and one-click changes built on them, the presets
// that show them, and what Unity is told.
#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "vfx/Program.h"
#include "vfx/Simulation.h"
#include "vfx/Templates.h"
#include "vfx/editor/CurveTools.h"
#include "vfx/editor/Presets.h"
#include "vfx/editor/Refine.h"
#include "vfx/editor/Session.h"
#include "vfx/editor/SpriteMesh.h"
#include "vfx/editor/UnityExport.h"
#include "vfx/editor/UnityFiles.h"

using namespace vfx;
using namespace vfx::editor;
using J = nlohmann::json;

namespace {

View flatView() {
    View v;
    v.width = 800;
    v.height = 600;
    v.centerX = 0;
    v.centerY = 0;
    v.unitsHigh = 6;  // 100 pixels per unit
    return v;
}

RenderFrame oneParticle(SpriteInstance p, bool flat = true) {
    RenderFrame frame;
    frame.flat = flat;
    frame.instances.push_back(p);
    RenderBatch batch;
    batch.first = 0;
    batch.count = 1;
    frame.batches.push_back(batch);
    return frame;
}

struct Box {
    float width = 0, height = 0;
};

// How wide and how tall the first particle's four corners reach on screen.
Box extent(const SpriteMesh& mesh) {
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (std::size_t i = mesh.vertices.size() - 4; i < mesh.vertices.size(); ++i) {
        minX = std::min(minX, mesh.vertices[i].x);
        maxX = std::max(maxX, mesh.vertices[i].x);
        minY = std::min(minY, mesh.vertices[i].y);
        maxY = std::max(maxY, mesh.vertices[i].y);
    }
    return Box{maxX - minX, maxY - minY};
}

float distance(const SpriteVertex& a, const SpriteVertex& b) { return std::hypot(a.x - b.x, a.y - b.y); }

Effect preset(const char* id) {
    IdGenerator ids(5);
    const IdSource newId = [&ids]() { return ids.next(); };
    auto made = makePreset(id, newId);
    REQUIRE(made.ok());
    return made.value();
}

// One basic emitter with a few settings changed.
Effect single(const std::function<void(Layer&)>& change) {
    IdGenerator ids(9);
    const IdSource newId = [&ids]() { return ids.next(); };
    Effect effect = makeEmptyEffect(newId, "Test", false);
    effect.duration = 2.0;
    effect.loop = "once";
    Layer layer = makeBasicEmitter(newId, "Layer");
    layer.duration = 2.0;
    change(layer);
    effect.layers.push_back(layer);
    return effect;
}

void put(Layer& layer, const char* type, const char* key, Value value) {
    bool done = false;
    for (Module& m : layer.modules) {
        if (m.type == type) {
            if (Value* slot = m.find(key)) {
                REQUIRE(slot->index() == value.index());
                *slot = std::move(value);
                done = true;
            }
        }
    }
    REQUIRE(done);
}

template <class T>
const T& get(const Layer& layer, const char* type, const char* key) {
    for (const Module& m : layer.modules) {
        if (m.type == type) {
            if (const Value* v = m.find(key)) {
                return std::get<T>(*v);
            }
        }
    }
    FAIL("no property " << type << "." << key);
    static const T none{};
    return none;
}

const Layer& layerNamed(const Effect& effect, const std::string& name) {
    for (const Layer& layer : effect.layers) {
        if (layer.name == name) {
            return layer;
        }
    }
    FAIL("no layer called " << name);
    return effect.layers.front();
}

void burstOnly(Layer& layer, std::int64_t count) {
    put(layer, "emission", "rate", Scalar::constant(0));
    put(layer, "emission", "bursts", BurstList{{{0.0, count}}});
}

RenderFrame frameAt(const Effect& effect, std::int64_t step) {
    Simulation sim(compileEffect(effect));
    sim.seek(step);
    RenderFrame frame;
    sim.extract(frame);
    return frame;
}

// Where the far end of the first particle's trail is, on screen.
void trailEnd(const SpriteMesh& mesh, float& x, float& y) {
    // Trail quads come first, then the particle's own four corners.
    REQUIRE(mesh.vertices.size() > 8);
    const std::size_t trailVertices = mesh.vertices.size() - 4;
    const SpriteVertex& a = mesh.vertices[trailVertices - 2];
    const SpriteVertex& b = mesh.vertices[trailVertices - 1];
    REQUIRE(a.shape == kRibbonShape);
    x = 0.5f * (a.x + b.x);
    y = 0.5f * (a.y + b.y);
}

J describe(const Effect& effect) { return J::parse(unityDescription(effect)); }

double curveValueAt(const J& curve, double t) {
    const double multiplier = curve["multiplier"].get<double>();
    const J& keys = curve["keys"];
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        const double t0 = keys[i][0].get<double>(), t1 = keys[i + 1][0].get<double>();
        if (t <= t1) {
            const double f = t1 > t0 ? (t - t0) / (t1 - t0) : 0.0;
            return multiplier * (keys[i][1].get<double>() + (keys[i + 1][1].get<double>() - keys[i][1].get<double>()) * f);
        }
    }
    return multiplier * keys.back()[1].get<double>();
}

}  // namespace

// ------------------------------------------------------ squash and stretch

TEST_CASE("a stretched particle is drawn longer and thinner, with the same area", "[principles][mesh]") {
    SpriteInstance p;
    p.size = 1.0f;  // 100 pixels across
    RenderFrame frame = oneParticle(p);
    SpriteMesh mesh;

    buildSpriteMesh(frame, flatView(), mesh);
    Box plain = extent(mesh);
    CHECK(plain.width == Catch::Approx(100.0f));
    CHECK(plain.height == Catch::Approx(100.0f));

    // Half as tall again; the width gives way to keep the area.
    frame.batches[0].squashStretches = true;
    frame.instances[0].squashStretch = 0.5f;
    buildSpriteMesh(frame, flatView(), mesh);
    Box tall = extent(mesh);
    CHECK(tall.height == Catch::Approx(150.0f));
    CHECK(tall.width == Catch::Approx(100.0f / 1.5f));
    CHECK(tall.width * tall.height == Catch::Approx(10000.0f).epsilon(1e-4));
    // The picture is fitted to the new shape, edge to edge.
    CHECK(mesh.vertices[0].ownX == -1.0f);
    CHECK(mesh.vertices[2].ownY == 1.0f);
    CHECK(mesh.vertices[0].aaX == Catch::Approx(1.0f / (50.0f / 1.5f)));
    CHECK(mesh.vertices[0].aaY == Catch::Approx(1.0f / 75.0f));

    // Squashed: the same the other way round.
    frame.instances[0].squashStretch = -0.5f;
    buildSpriteMesh(frame, flatView(), mesh);
    Box flat = extent(mesh);
    CHECK(flat.width == Catch::Approx(150.0f));
    CHECK(flat.height == Catch::Approx(100.0f / 1.5f));

    // It is the particle's own upright that stretches: turned a quarter turn, it is wide.
    frame.instances[0].squashStretch = 0.5f;
    frame.instances[0].rotation = 3.14159265f / 2.0f;
    buildSpriteMesh(frame, flatView(), mesh);
    Box turned = extent(mesh);
    CHECK(turned.width == Catch::Approx(150.0f).epsilon(1e-3));
    CHECK(turned.height == Catch::Approx(100.0f / 1.5f).epsilon(1e-3));

    // A layer that does not squash and stretch ignores whatever a particle carries.
    frame.batches[0].squashStretches = false;
    buildSpriteMesh(frame, flatView(), mesh);
    CHECK(extent(mesh).height == Catch::Approx(100.0f).epsilon(1e-3));
}

TEST_CASE("a particle that points the way it moves stretches along its movement", "[principles][mesh]") {
    SpriteInstance p;
    p.size = 1.0f;
    p.vx = 3.0f;  // moving to the right
    RenderFrame frame = oneParticle(p);
    frame.batches[0].alongMotion = true;
    frame.batches[0].squashStretches = true;
    frame.instances[0].squashStretch = 1.0f;  // twice as long, half as wide
    SpriteMesh mesh;
    buildSpriteMesh(frame, flatView(), mesh);
    Box box = extent(mesh);
    CHECK(box.width == Catch::Approx(200.0f).epsilon(1e-3));
    CHECK(box.height == Catch::Approx(50.0f).epsilon(1e-3));

    // The streak setting adds its own length on top.
    frame.batches[0].stretch = 0.1f;  // a tenth of a second of travel: 30 pixels
    buildSpriteMesh(frame, flatView(), mesh);
    CHECK(extent(mesh).width == Catch::Approx(230.0f).epsilon(1e-3));
}

TEST_CASE("a particle lying on the plane of a 3D effect stretches in that plane", "[principles][mesh]") {
    SpriteInstance p;
    p.size = 1.0f;
    RenderFrame frame = oneParticle(p, false);
    frame.batches[0].facing = Facing::Plane;
    View view;
    view.width = 800;
    view.height = 600;
    view.targetX = view.targetY = view.targetZ = 0;
    view.yaw = 0;
    view.pitch = 0;
    view.distance = 20;
    SpriteMesh mesh;
    buildSpriteMesh(frame, view, mesh);
    REQUIRE(mesh.vertices.size() == 4);
    const float sideX = distance(mesh.vertices[0], mesh.vertices[1]);
    const float sideY = distance(mesh.vertices[1], mesh.vertices[2]);

    frame.batches[0].squashStretches = true;
    frame.instances[0].squashStretch = 0.5f;
    buildSpriteMesh(frame, view, mesh);
    REQUIRE(mesh.vertices.size() == 4);
    CHECK(distance(mesh.vertices[0], mesh.vertices[1]) == Catch::Approx(sideX / 1.5f).epsilon(1e-3));
    CHECK(distance(mesh.vertices[1], mesh.vertices[2]) == Catch::Approx(sideY * 1.5f).epsilon(1e-3));
}

// ---------------------------------------------------- trails and headings

TEST_CASE("a trail retraces a path that was eased and turned about the centre", "[principles][mesh][trail]") {
    const Effect effect = single([](Layer& layer) {
        burstOnly(layer, 1);
        put(layer, "shape", "offset", Vec3{0.5, -0.3, 0.0});
        put(layer, "initial", "lifetime", Scalar::constant(2.0));
        put(layer, "initial", "speed", Scalar::constant(3.0));
        put(layer, "initial", "direction", Vec3{1.0, 0.5, 0.0});
        put(layer, "initial", "spread", 0.0);
        put(layer, "motion", "gravity", Vec3{0.0, -4.0, 0.0});
        put(layer, "motion", "drag", 0.8);
        put(layer, "motion", "speedOverLife", Scalar::curve({{0, 2.5}, {0.15, 0.4, true}, {0.3, -0.6, true}, {1, 1.2}}));
        put(layer, "motion", "orbit", 140.0);
        put(layer, "sprite", "trail", 5.0);  // longer than it has lived: back to its birth
        put(layer, "sprite", "trailWidth", 1.0);
    });
    float bornX = 0, bornY = 0, scale = 0;
    REQUIRE(projectPoint(flatView(), true, 0.5f, -0.3f, 0, bornX, bornY, scale));

    for (const std::int64_t step : {12, 40, 75}) {
        RenderFrame frame = frameAt(effect, step);
        REQUIRE(frame.instances.size() == 1);
        REQUIRE(frame.batches[0].eases);
        REQUIRE(frame.batches[0].orbit != 0.0f);
        SpriteMesh mesh;
        buildSpriteMesh(frame, flatView(), mesh);
        float x = 0, y = 0;
        trailEnd(mesh, x, y);
        CAPTURE(step);
        // The ribbon's far end is where the particle was born, to within half a pixel.
        CHECK(x == Catch::Approx(bornX).margin(0.5));
        CHECK(y == Catch::Approx(bornY).margin(0.5));

        if (step < 40) {
            continue;
        }
        // Not knowing about either, it would end somewhere else: they really did move it.
        RenderFrame blind = frame;
        blind.batches[0].eases = false;
        buildSpriteMesh(blind, flatView(), mesh);
        trailEnd(mesh, x, y);
        CHECK(std::hypot(x - bornX, y - bornY) > 5.0f);
        blind = frame;
        blind.batches[0].orbit = 0.0f;
        buildSpriteMesh(blind, flatView(), mesh);
        trailEnd(mesh, x, y);
        CHECK(std::hypot(x - bornX, y - bornY) > 5.0f);
    }
}

TEST_CASE("a held layer's trail belongs to the drawing that is showing", "[principles][mesh][trail]") {
    const Effect effect = single([](Layer& layer) {
        burstOnly(layer, 1);
        put(layer, "initial", "lifetime", Scalar::constant(3.0));
        put(layer, "initial", "speed", Scalar::constant(2.0));
        put(layer, "initial", "spread", 0.0);
        put(layer, "motion", "gravity", Vec3{0.0, 0.0, 0.0});
        put(layer, "motion", "turbulence", 2.0);
        put(layer, "sprite", "trail", 5.0);
        put(layer, "sprite", "trailWidth", 1.0);
        put(layer, "sprite", "hold", 6.0);  // a new drawing every ten steps
    });
    float bornX = 0, bornY = 0, scale = 0;
    REQUIRE(projectPoint(flatView(), true, 0.0f, 0.0f, 0, bornX, bornY, scale));
    // Step 38 shows the drawing taken at step 31.
    RenderFrame frame = frameAt(effect, 38);
    REQUIRE(frame.batches[0].shownStep == 31);
    SpriteMesh mesh;
    buildSpriteMesh(frame, flatView(), mesh);
    float x = 0, y = 0;
    trailEnd(mesh, x, y);
    CHECK(x == Catch::Approx(bornX).margin(0.75));
    CHECK(y == Catch::Approx(bornY).margin(0.75));
}

TEST_CASE("a streak points the way its particle is really going", "[principles][mesh]") {
    // Standing still as far as its own speed goes, one unit to the right of
    // a centre it is being turned about: it is travelling straight up.
    SpriteInstance p;
    p.x = 1.0f;
    p.size = 0.2f;
    RenderFrame frame = oneParticle(p);
    RenderBatch& b = frame.batches[0];
    b.alongMotion = true;
    b.stretch = 0.1f;
    b.orbit = 3.14159265f;  // half a turn a second
    b.orbitSin = std::sin(3.14159265f / 60.0f);
    b.orbitCos = std::cos(3.14159265f / 60.0f);
    SpriteMesh mesh;
    buildSpriteMesh(frame, flatView(), mesh);
    REQUIRE(mesh.vertices.size() == 4);
    Box box = extent(mesh);
    CHECK(box.height > box.width + 20.0f);  // a tenth of a second at 314 pixels a second

    // Going backwards along its own speed: the head leads the other way.
    SpriteInstance q;
    q.size = 0.2f;
    q.vx = 2.0f;
    q.ease = -1.0f;
    frame = oneParticle(q);
    frame.batches[0].alongMotion = true;
    frame.batches[0].eases = true;
    buildSpriteMesh(frame, flatView(), mesh);
    // Corners 0 and 1 lie along the particle's own heading.
    CHECK(mesh.vertices[1].x < mesh.vertices[0].x);
    frame.instances[0].ease = 1.0f;
    buildSpriteMesh(frame, flatView(), mesh);
    CHECK(mesh.vertices[1].x > mesh.vertices[0].x);
}

// ------------------------------------------------------- ready-made curves

TEST_CASE("Squash and Stretch and Speed over Life have ready-made curves of their own", "[principles][curves]") {
    const Registry& registry = Registry::builtin();
    const PropertyDesc& stretch = *registry.findModule("overLife")->find("stretch");
    const PropertyDesc& pace = *registry.findModule("motion")->find("speedOverLife");
    const PropertyDesc& size = *registry.findModule("overLife")->find("size");

    int general = 0, forStretch = 0, forPace = 0;
    for (const CurveShape& shape : curveShapes()) {
        INFO(shape.key);
        if (!shape.only) {
            ++general;
            continue;
        }
        const bool isStretch = std::string(shape.only) == "overLife.stretch";
        const bool isPace = std::string(shape.only) == "motion.speedOverLife";
        REQUIRE((isStretch || isPace));
        (isStretch ? forStretch : forPace) += 1;
        const PropertyDesc& desc = isStretch ? stretch : pace;
        // A legal curve for its own setting, from birth to death, used as it is.
        REQUIRE(shape.keys.size() >= 2);
        CHECK(shape.keys.front().t == 0.0);
        CHECK(shape.keys.back().t == 1.0);
        CHECK(tidyCurve(shape.keys, desc) == shape.keys);
        CHECK(validateValue(desc, Value(Scalar::curve(shape.keys))).ok());
        for (const Scalar& now : {Scalar::constant(0.0), Scalar::constant(3.0), Scalar::curve({{0, -2}, {1, 4}})}) {
            CHECK(shaped(shape, now, desc) == Scalar::curve(shape.keys));
        }
    }
    CHECK(general == 15);
    CHECK(forStretch >= 4);
    CHECK(forPace >= 5);

    // Each setting is offered its own first, then the general ones; any
    // other setting only the general ones.
    const auto offered = curveShapesFor("overLife", stretch);
    REQUIRE(offered.size() == static_cast<std::size_t>(forStretch + general));
    for (int i = 0; i < forStretch; ++i) {
        CHECK(std::string(offered[static_cast<std::size_t>(i)]->only) == "overLife.stretch");
    }
    CHECK(offered[static_cast<std::size_t>(forStretch)]->only == nullptr);
    CHECK(curveShapesFor("motion", pace).size() == static_cast<std::size_t>(forPace + general));
    CHECK(curveShapesFor("overLife", size).size() == static_cast<std::size_t>(general));
    // (Sprite has a Stretch of its own, the length of a streak: not this one.)
    CHECK(curveShapesFor("sprite", *registry.findModule("sprite")->find("stretch")).size() ==
          static_cast<std::size_t>(general));

    // What a few of them promise.
    const auto at = [](const char* key, double t) { return curveAt(findCurveShape(key)->keys, t); };
    CHECK(at("stretch-settle", 0.0) > 0.5);        // long as it sets off
    CHECK(at("stretch-settle", 0.3) < 0.0);        // a small squash
    CHECK(at("stretch-settle", 0.8) == 0.0);       // its own shape
    CHECK(at("squash-spring", 0.0) < -0.3);        // lands flat
    CHECK(at("squash-spring", 0.18) > 0.2);        // springs past
    CHECK(at("wind-up-go", 0.05) < 0.0);           // backs away
    CHECK(at("wind-up-go", 0.3) > 1.5);            // then off faster than it would have been
    CHECK(at("overshoot-settle", 0.26) < 0.0);     // comes back
    CHECK(at("overshoot-settle", 0.8) == 0.0);     // and stays
    CHECK(at("out-and-back", 0.1) > 1.0);          // thrown out
    CHECK(at("out-and-back", 0.9) < -0.4);         // drawn back
    CHECK(at("hit-pause", 0.3) == 0.0);            // stops dead
    CHECK(at("hit-pause", 0.6) > 0.5);             // carries on

    // A general shape on a stretch that stands at nothing goes by the top of its slider.
    const Scalar faded = shaped(*findCurveShape("fade-out"), Scalar::constant(0.0), stretch);
    CHECK(faded.keys.front().v == Catch::Approx(0.6));
    CHECK(faded.keys.back().v == 0.0);
}

TEST_CASE("the Advanced view lists the principles where they belong", "[principles][session]") {
    Session s;
    const Id layer = s.effect().layers[0].id;
    const auto all = s.properties(layer);
    const auto find = [&](const char* section, const char* label) -> const ControlView* {
        for (const ControlView& c : all) {
            if (c.section == section && c.label == label) {
                return &c;
            }
        }
        return nullptr;
    };
    REQUIRE(find("Motion", "Speed over Life") != nullptr);
    REQUIRE(find("Motion", "Orbit") != nullptr);
    REQUIRE(find("Over Lifetime", "Squash and Stretch") != nullptr);
    REQUIRE(find("Sprite", "Hold Frames") != nullptr);

    // Each can be changed, as one undo step, and plays at once.
    const ControlView* hold = find("Sprite", "Hold Frames");
    REQUIRE(s.setControl(layer, hold->control, Value(12.0)).ok());
    const ControlView* pace = find("Motion", "Speed over Life");
    REQUIRE(s.setControl(layer, pace->control, Value(Scalar::curve(findCurveShape("burst-coast")->keys))).ok());
    s.tick(0.5);
    REQUIRE(s.frame().batches.size() == 1);
    CHECK(s.frame().batches[0].hold == 12.0f);
    CHECK(s.frame().batches[0].eases);
    REQUIRE(s.undo().ok());
    REQUIRE(s.undo().ok());
    s.tick(0.0);
    CHECK(s.frame().batches[0].hold == 0.0f);
    CHECK_FALSE(s.frame().batches[0].eases);
    // Out of range is refused, not clamped behind the artist's back.
    CHECK_FALSE(s.setControl(layer, hold->control, Value(500.0)).ok());
}

// --------------------------------------------------------- one-click changes

TEST_CASE("refinements built on the principles change what they say and nothing else", "[principles][refine]") {
    IdGenerator ids(77);
    const IdSource newId = [&ids]() { return ids.next(); };
    const Effect start = preset("toon-puff");
    std::string said;

    // On twos, then on threes, then smooth again.
    Effect smooth = start;
    for (Layer& l : smooth.layers) {
        put(l, "sprite", "hold", 0.0);
    }
    const Effect twos = refine(smooth, Refine::OnTwos, nullptr, newId, &said).value();
    for (const Layer& l : twos.layers) {
        CHECK(get<double>(l, "sprite", "hold") == 12.0);
    }
    CHECK(said.find("twos") != std::string::npos);
    const Effect threes = refine(twos, Refine::OnTwos, nullptr, newId, &said).value();
    for (const Layer& l : threes.layers) {
        CHECK(get<double>(l, "sprite", "hold") == 8.0);
    }
    CHECK(said.find("threes") != std::string::npos);
    const Effect again = refine(threes, Refine::SmoothTiming, nullptr, newId, &said).value();
    CHECK(writeEffect(again) == writeEffect(smooth));
    CHECK(writeEffect(refine(again, Refine::SmoothTiming, nullptr, newId, &said).value()) == writeEffect(smooth));
    CHECK(said == "Every layer already moves smoothly.");

    // Squash and stretch: the round flying dots point along their flight and
    // stretch; the spinning puffs, which have an upright of their own, are left.
    const Effect bouncy = refine(smooth, Refine::SquashStretch, nullptr, newId, &said).value();
    const Layer dots = layerNamed(bouncy, "Dots");
    CHECK(get<std::string>(dots, "sprite", "align") == "movement");
    CHECK(get<Scalar>(dots, "overLife", "stretch").kind == Scalar::Kind::Curve);
    CHECK(get<Scalar>(dots, "overLife", "stretch").keys.front().v > 0.5);
    CHECK(get<Scalar>(layerNamed(bouncy, "Puffs"), "overLife", "stretch") == Scalar::constant(0.0));
    // A single big shape lands flat and springs.
    const Effect orb = refine(preset("magic-orb"), Refine::SquashStretch, nullptr, newId).value();
    CHECK(get<Scalar>(layerNamed(orb, "Orb"), "overLife", "stretch") ==
          Scalar::curve(findCurveShape("squash-spring")->keys));
    CHECK(get<Scalar>(layerNamed(orb, "Halo"), "overLife", "stretch") == Scalar::constant(0.0));

    // Anticipation and overshoot are curves on the pace of flying pieces.
    const Effect wound = refine(smooth, Refine::Anticipate, nullptr, newId, &said).value();
    CHECK(get<Scalar>(layerNamed(wound, "Puffs"), "motion", "speedOverLife") ==
          Scalar::curve(findCurveShape("wind-up-go")->keys));
    // The one glint is a flash, not a shape of its own: its size is left alone.
    CHECK(get<Scalar>(layerNamed(wound, "Glint"), "overLife", "size") ==
          get<Scalar>(layerNamed(smooth, "Glint"), "overLife", "size"));
    const Effect drawnIn = refine(preset("magic-orb"), Refine::Anticipate, nullptr, newId).value();
    const Scalar orbSize = get<Scalar>(layerNamed(drawnIn, "Orb"), "overLife", "size");
    REQUIRE(orbSize.keys.size() >= 3);
    CHECK(orbSize.keys[1].v < orbSize.keys[0].v);  // draws in first
    const Effect over = refine(smooth, Refine::Overshoot, nullptr, newId, &said).value();
    CHECK(get<Scalar>(layerNamed(over, "Dots"), "motion", "speedOverLife") ==
          Scalar::curve(findCurveShape("overshoot-settle")->keys));
    CHECK(get<Scalar>(layerNamed(over, "Glint"), "motion", "speedOverLife") == Scalar::constant(1.0));

    // Curl: layers turn opposite ways; again for half as much more.
    const Effect curled = refine(smooth, Refine::Curl, nullptr, newId, &said).value();
    const double first = get<double>(layerNamed(curled, "Puffs"), "motion", "orbit");
    const double second = get<double>(layerNamed(curled, "Small puffs"), "motion", "orbit");
    CHECK(first > 0.0);
    CHECK(second < 0.0);
    CHECK(get<double>(layerNamed(curled, "Glint"), "motion", "orbit") == 0.0);
    const Effect more = refine(curled, Refine::Curl, nullptr, newId, &said).value();
    CHECK(get<double>(layerNamed(more, "Puffs"), "motion", "orbit") == Catch::Approx(1.5 * first));

    // Faster: round the centre as many times in the shorter life. Held drawings stay as they are.
    const Effect faster = refine(refine(curled, Refine::OnTwos, nullptr, newId).value(), Refine::Faster, nullptr, newId).value();
    CHECK(get<double>(layerNamed(faster, "Puffs"), "motion", "orbit") == Catch::Approx(1.25 * first));
    CHECK(get<double>(layerNamed(faster, "Puffs"), "sprite", "hold") == 12.0);

    // A locked layer is never touched.
    Effect held = smooth;
    held.layers[0].locked = true;
    const Effect kept = refine(held, Refine::OnTwos, nullptr, newId, &said).value();
    CHECK(get<double>(kept.layers[0], "sprite", "hold") == 0.0);
    CHECK(get<double>(kept.layers[1], "sprite", "hold") == 12.0);
    CHECK(said.find("locked") != std::string::npos);

    // Every result is still an effect that saves, loads and plays.
    for (const Effect& e : {twos, bouncy, wound, over, curled, faster}) {
        const auto loaded = readEffect(writeEffect(e));
        REQUIRE(loaded.ok());
        CHECK(loaded.value().diagnostics.empty());
        Simulation sim(compileEffect(e));
        sim.seek(20);
        CHECK(sim.aliveCount() > 0);
    }
}

// ------------------------------------------------------------------ presets

TEST_CASE("the Cartoon motion presets each show a principle at work", "[principles][presets]") {
    const auto uses = [](const Effect& effect, const char* type, const char* key) {
        const PropertyDesc& desc = *Registry::builtin().findModule(type)->find(key);
        for (const Layer& layer : effect.layers) {
            for (const Module& m : layer.modules) {
                if (m.type == type && !(*m.find(key) == desc.defaultValue)) {
                    return true;
                }
            }
        }
        return false;
    };
    int found = 0;
    for (const PresetInfo& info : presets()) {
        if (info.category == "Cartoon motion") {
            ++found;
        }
    }
    CHECK(found == 4);
    CHECK(uses(preset("bouncy-pop"), "overLife", "stretch"));
    CHECK(uses(preset("bouncy-pop"), "sprite", "hold"));
    CHECK(uses(preset("wind-up-blast"), "motion", "speedOverLife"));
    CHECK(uses(preset("spiral-burst"), "motion", "orbit"));
    CHECK(uses(preset("springy-poof"), "motion", "speedOverLife"));
    CHECK(uses(preset("springy-poof"), "overLife", "stretch"));
    // The hand-drawn presets are shown on twos.
    for (const char* id : {"toon-puff", "toon-hit", "toon-explosion", "slash", "toon-fire", "cartoon-pop"}) {
        INFO(id);
        for (const Layer& layer : preset(id).layers) {
            CHECK(get<double>(layer, "sprite", "hold") == 12.0);
        }
    }
    // And presets that are not hand-drawn in manner are left smooth.
    CHECK_FALSE(uses(preset("soft-glow"), "sprite", "hold"));
    CHECK_FALSE(uses(preset("campfire"), "sprite", "hold"));

    // The wind-up really does come back before the blast: at the moment the
    // blast goes off, the motes thrown out at the start are nearly home.
    const Effect blast = preset("wind-up-blast");
    Simulation sim(compileEffect(blast));
    double furthest = 0, atBlast = 0;
    for (std::int64_t step = 1; step <= 36; ++step) {
        sim.seek(step);
        const ParticleView v = sim.particles(0);  // "Gather" is the first layer
        double mean = 0;
        for (std::uint32_t i = 0; i < v.count; ++i) {
            mean += std::hypot(v.x[i], v.y[i]);
        }
        mean = v.count ? mean / v.count : 0.0;
        furthest = std::max(furthest, mean);
        if (step == 34) {
            atBlast = mean;
        }
    }
    CHECK(furthest > 1.0);
    CHECK(atBlast < 0.45 * furthest);
}

// -------------------------------------------------------------------- Unity

TEST_CASE("the principles are handed to Unity", "[principles][unity]") {
    Effect effect = single([](Layer& layer) {
        put(layer, "overLife", "size", Scalar::curve({{0, 0.5}, {0.5, 1.0}, {1, 0.0}}));
    });
    Layer& layer = effect.layers[0];

    // Nothing is said about any of them until they are used.
    {
        const J l = describe(effect)["layers"][0];
        CHECK(l["speedModifier"].is_null());
        CHECK(l["orbit"].is_null());
        CHECK(l["hold"].is_null());
        CHECK(l["squash"].is_null());
        CHECK(l["sizeOverLife"].is_object());
    }

    put(layer, "motion", "speedOverLife", Scalar::curve({{0, 2.0}, {1, 0.5}}));
    put(layer, "motion", "orbit", 90.0);
    put(layer, "shape", "offset", Vec3{1.0, 2.0, 0.0});
    put(layer, "sprite", "hold", 12.0);
    put(layer, "overLife", "stretch", Scalar::curve({{0, 0.5}, {0.5, -0.5}, {1, 0.0}}));
    J l = describe(effect)["layers"][0];

    // Speed over Life is Unity's Speed Modifier, across the particle's life.
    REQUIRE(l["speedModifier"].is_object());
    CHECK(curveValueAt(l["speedModifier"], 0.0) == Catch::Approx(2.0));
    CHECK(curveValueAt(l["speedModifier"], 1.0) == Catch::Approx(0.5));

    // Orbit is Orbital Z in radians a second, about the layer's position.
    REQUIRE(l["orbit"].is_object());
    CHECK(l["orbit"]["z"].get<double>() == Catch::Approx(3.14159265 / 2.0).epsilon(1e-5));
    CHECK(l["orbit"]["center"][0] == 1.0);
    CHECK(l["orbit"]["center"][1] == 2.0);
    CHECK(l["orbit"]["exact"] == false);

    CHECK(l["hold"] == 12.0);

    // Squash and stretch: width and height on their own curves, which hold
    // the size over life as well and keep the area at every moment.
    REQUIRE(l["squash"].is_object());
    CHECK(l["squash"]["exact"] == true);
    for (const double t : {0.0, 0.1, 0.25, 0.5, 0.6, 0.75, 0.9, 1.0}) {
        const double size = t < 0.5 ? 0.5 + t : 2.0 * (1.0 - t);
        const double x = curveValueAt(l["squash"]["x"], t), y = curveValueAt(l["squash"]["y"], t);
        CAPTURE(t);
        CHECK(curveValueAt(l["squash"]["z"], t) == Catch::Approx(size).margin(1e-4));
        CHECK(x * y == Catch::Approx(size * size).margin(0.01));
        if (t < 0.24) {
            CHECK(y > x);  // stretched
        } else if (t > 0.26 && t < 0.99) {
            CHECK(x > y);  // squashed
        }
    }
    CHECK(curveValueAt(l["squash"]["y"], 0.0) == Catch::Approx(0.5 * 1.5));
    CHECK(curveValueAt(l["squash"]["x"], 0.5) == Catch::Approx(1.0 * 1.5));

    // One amount each at random cannot be told to Unity exactly: every
    // particle gets the middle one, and the description says so.
    put(layer, "overLife", "stretch", Scalar::random(0.0, 1.0));
    l = describe(effect)["layers"][0];
    CHECK(l["squash"]["exact"] == false);
    CHECK(curveValueAt(l["squash"]["y"], 0.5) == Catch::Approx(1.5));

    // A streak has one length in Unity for its whole life.
    put(layer, "overLife", "stretch", Scalar::constant(1.0));
    put(layer, "sprite", "align", std::string("movement"));
    l = describe(effect)["layers"][0];
    CHECK(l["render"]["mode"] == "stretched");
    CHECK(l["render"]["lengthScale"].get<double>() == Catch::Approx(4.0));  // twice as long, half as wide
    CHECK(l["squash"]["exact"] == false);
    CHECK_FALSE(l["squash"].contains("x"));

    // The files that go to Unity know what to do with all of it.
    std::string importer, holder, readme;
    for (const UnityFile& f : unityHelperSources()) {
        if (f.path == "Editor/VFXForgeImporter.cs") {
            importer = f.text;
        } else if (f.path == "Runtime/VFXForgeHold.cs") {
            holder = f.text;
        } else if (f.path == "README.txt") {
            readme = f.text;
        }
    }
    for (const char* piece : {"velocityOverLifetime", "speedModifier", "orbitalZ", "orbitalOffsetX", "separateAxes",
                              "VFXForge.VFXForgeHold", "drawingsPerSecond"}) {
        CAPTURE(piece);
        CHECK(importer.find(piece) != std::string::npos);
    }
    REQUIRE_FALSE(holder.empty());
    CHECK(holder.find("class VFXForgeHold") != std::string::npos);
    CHECK(holder.find("Simulate(") != std::string::npos);
    CHECK(readme.find("VFXForgeHold") != std::string::npos);

    // Every Cartoon motion preset translates.
    for (const char* id : {"bouncy-pop", "wind-up-blast", "spiral-burst", "springy-poof"}) {
        INFO(id);
        const J root = describe(preset(id));
        CHECK(root["layers"].size() == preset(id).layers.size());
    }
}

// ------------------------------------------------ Unity's motion modules (0.44)

namespace {

// Where the one particle of a burst is after `steps`.
SpriteInstance onlyParticle(const Effect& effect, std::int64_t steps) {
    RenderFrame frame = frameAt(effect, steps);
    REQUIRE(frame.instances.size() == 1);
    return frame.instances[0];
}

Effect still(const std::function<void(Layer&)>& more) {
    return single([&](Layer& layer) {
        burstOnly(layer, 1);
        put(layer, "initial", "lifetime", Scalar::constant(2.0));
        put(layer, "initial", "speed", Scalar::constant(0.0));
        put(layer, "initial", "spread", 0.0);
        put(layer, "motion", "gravity", Vec3{0.0, 0.0, 0.0});
        more(layer);
    });
}

}  // namespace

TEST_CASE("Force over Lifetime pushes, and its ramp scales the push across the particle's life", "[motion][unity]") {
    const Effect none = still([](Layer&) {});
    const Effect pushed = still([](Layer& l) { put(l, "motion", "force", Vec3{4.0, 0.0, 0.0}); });
    const Effect ramped = still([](Layer& l) {
        put(l, "motion", "force", Vec3{4.0, 0.0, 0.0});
        put(l, "motion", "forceRamp", Scalar::curve({{0, 0}, {1, 0}}));
    });
    CHECK(onlyParticle(none, 30).x == Catch::Approx(0.0f).margin(1e-5));
    CHECK(onlyParticle(pushed, 30).x > 0.5f);
    CHECK(onlyParticle(ramped, 30).x == Catch::Approx(0.0f).margin(1e-5));

    const J l = describe(pushed)["layers"][0];
    REQUIRE(l["extraForce"].is_array());
    CHECK(l["extraForce"][0].get<double>() == Catch::Approx(4.0));
    CHECK(describe(none)["layers"][0]["extraForce"].is_null());
}

TEST_CASE("Velocity over Lifetime moves particles at a steady speed that drag does not change", "[motion][unity]") {
    const Effect drifting = still([](Layer& l) {
        put(l, "motion", "drift", Vec3{2.0, 0.0, 0.0});
        put(l, "motion", "drag", 5.0);
    });
    const SpriteInstance p = onlyParticle(drifting, 30);  // half a second
    CHECK(p.x == Catch::Approx(1.0f).margin(0.05));
    CHECK(describe(drifting)["layers"][0]["drift"][0].get<double>() == Catch::Approx(2.0));
}

TEST_CASE("Limit Velocity holds a fast particle back", "[motion][unity]") {
    const auto fast = [](double limit) {
        return single([limit](Layer& l) {
            burstOnly(l, 1);
            put(l, "initial", "lifetime", Scalar::constant(2.0));
            put(l, "initial", "speed", Scalar::constant(20.0));
            put(l, "initial", "direction", Vec3{1.0, 0.0, 0.0});
            put(l, "initial", "spread", 0.0);
            put(l, "motion", "gravity", Vec3{0.0, 0.0, 0.0});
            put(l, "motion", "speedLimit", limit);
            put(l, "motion", "speedLimitDampen", 1.0);
        });
    };
    const float free = onlyParticle(fast(0.0), 60).x;
    const float held = onlyParticle(fast(4.0), 60).x;
    CHECK(free == Catch::Approx(20.0f).margin(0.1));
    CHECK(held < free * 0.5f);
    CHECK(describe(fast(4.0))["layers"][0]["speedLimit"]["limit"].get<double>() == Catch::Approx(4.0));
}

TEST_CASE("By Speed modules tint and size a particle by how fast it goes", "[motion][unity]") {
    const auto at = [](double speed) {
        return single([speed](Layer& l) {
            burstOnly(l, 1);
            put(l, "initial", "lifetime", Scalar::constant(2.0));
            put(l, "initial", "speed", Scalar::constant(speed));
            put(l, "initial", "direction", Vec3{1.0, 0.0, 0.0});
            put(l, "initial", "spread", 0.0);
            put(l, "motion", "gravity", Vec3{0.0, 0.0, 0.0});
            put(l, "overLife", "speedMin", 0.0);
            put(l, "overLife", "speedMax", 10.0);
            put(l, "overLife", "colorBySpeed", Gradient{{{0.0, Color{1, 0, 0, 1}}, {1.0, Color{0, 0, 1, 1}}}});
            put(l, "overLife", "sizeBySpeed", Scalar::curve({{0, 1}, {1, 3}}));
        });
    };
    const SpriteInstance slow = onlyParticle(at(0.0), 1), fast = onlyParticle(at(10.0), 1);
    CHECK(slow.r > 0.9f);
    CHECK(slow.b < 0.1f);
    CHECK(fast.b > 0.9f);
    CHECK(fast.r < 0.1f);
    CHECK(fast.size == Catch::Approx(slow.size * 3.0f).epsilon(0.02));
    const J l = describe(at(10.0))["layers"][0];
    REQUIRE(l["bySpeed"].is_object());
    CHECK(l["bySpeed"]["range"][1].get<double>() == Catch::Approx(10.0));
    CHECK(l["bySpeed"]["color"].is_object());
}

TEST_CASE("A floor bounces particles and loses their energy; a kill zone removes them", "[motion][unity]") {
    const auto dropped = [](bool collide, double bounce, double lifeLoss) {
        return single([=](Layer& l) {
            burstOnly(l, 1);
            put(l, "emission", "bursts", Value(BurstList{{{0.0, 1}}}));
            put(l, "initial", "lifetime", Scalar::constant(2.0));
            put(l, "initial", "speed", Scalar::constant(0.0));
            put(l, "initial", "spread", 0.0);
            put(l, "shape", "offset", Vec3{0.0, 2.0, 0.0});
            put(l, "motion", "gravity", Vec3{0.0, -20.0, 0.0});
            put(l, "motion", "collide", collide);
            put(l, "motion", "floorHeight", 0.0);
            put(l, "motion", "bounce", bounce);
            put(l, "motion", "collisionDampen", 0.0);
            put(l, "motion", "collisionLifeLoss", lifeLoss);
        });
    };
    // Without a floor it falls on; with one it never goes below it.
    float lowest = 1e9f, lowestFree = 1e9f;
    for (std::int64_t s = 1; s < 100; ++s) {
        RenderFrame a = frameAt(dropped(true, 0.6, 0.0), s), b = frameAt(dropped(false, 0.6, 0.0), s);
        if (!a.instances.empty()) lowest = std::min(lowest, a.instances[0].y);
        if (!b.instances.empty()) lowestFree = std::min(lowestFree, b.instances[0].y);
    }
    CHECK(lowest > -0.1f);
    CHECK(lowestFree < -5.0f);
    // It comes back up after landing.
    bool rose = false;
    float previous = 1e9f;
    for (std::int64_t s = 1; s < 100 && !rose; ++s) {
        RenderFrame a = frameAt(dropped(true, 0.6, 0.0), s);
        if (a.instances.empty()) continue;
        rose = a.instances[0].y > previous + 0.01f && a.instances[0].y < 1.9f && s > 20;
        previous = a.instances[0].y;
    }
    CHECK(rose);
    // A hit that takes all its life away ends it at once.
    CHECK(frameAt(dropped(true, 0.6, 1.0), 99).instances.empty());

    const J hit = describe(dropped(true, 0.6, 0.0))["layers"][0]["collision"];
    REQUIRE(hit.is_object());
    CHECK(hit["bounce"].get<double>() == Catch::Approx(0.6));

    const Effect zoned = single([](Layer& l) {
        burstOnly(l, 1);
        put(l, "initial", "lifetime", Scalar::constant(2.0));
        put(l, "initial", "speed", Scalar::constant(5.0));
        put(l, "initial", "direction", Vec3{1.0, 0.0, 0.0});
        put(l, "initial", "spread", 0.0);
        put(l, "motion", "gravity", Vec3{0.0, 0.0, 0.0});
        put(l, "motion", "zone", std::string("kill inside"));
        put(l, "motion", "zoneCenter", Vec3{3.0, 0.0, 0.0});
        put(l, "motion", "zoneSize", Vec3{1.0, 4.0, 4.0});
    });
    CHECK(frameAt(zoned, 20).instances.size() == 1);  // short of the box
    CHECK(frameAt(zoned, 40).instances.empty());      // inside it, gone
    CHECK(describe(zoned)["layers"][0]["zone"]["kill"].get<std::string>() == "inside");
}

TEST_CASE("A sub emitter releases its particles where its parent's particles are born, die or bounce", "[motion][unity][sub]") {
    const auto build = [](const char* when, double chance, bool disabledParent) {
        IdGenerator ids(21);
        const IdSource newId = [&ids]() { return ids.next(); };
        Effect effect = makeEmptyEffect(newId, "Test", false);
        effect.duration = 4.0;
        effect.loop = "once";
        Layer parent = makeBasicEmitter(newId, "Rocket");
        parent.duration = 4.0;
        parent.enabled = !disabledParent;
        burstOnly(parent, 1);
        put(parent, "initial", "lifetime", Scalar::constant(1.0));
        put(parent, "initial", "speed", Scalar::constant(4.0));
        put(parent, "initial", "direction", Vec3{1.0, 0.0, 0.0});
        put(parent, "initial", "spread", 0.0);
        put(parent, "motion", "gravity", Vec3{0.0, 0.0, 0.0});
        Layer child = makeBasicEmitter(newId, "Sparks");
        child.duration = 4.0;
        put(child, "initial", "lifetime", Scalar::constant(0.5));
        put(child, "initial", "speed", Scalar::constant(0.0));
        put(child, "initial", "spread", 0.0);
        put(child, "motion", "gravity", Vec3{0.0, 0.0, 0.0});
        put(child, "emission", "trigger", std::string(when));
        put(child, "emission", "parent", std::string("Rocket"));
        put(child, "emission", "triggerCount", 5.0);
        put(child, "emission", "triggerChance", chance);
        effect.layers.push_back(parent);
        effect.layers.push_back(child);
        return effect;
    };
    const auto sparks = [](const Effect& effect, std::int64_t step) {
        RenderFrame frame = frameAt(effect, step);
        std::vector<SpriteInstance> out;
        for (const RenderBatch& b : frame.batches) {
            if (b.layer == effect.layers[1].id) {
                for (std::uint32_t i = 0; i < b.count; ++i) out.push_back(frame.instances[b.first + i]);
            }
        }
        return out;
    };

    // Born with the parent: sparks wait at the start. Dies after a second: they appear at its far end.
    const Effect birth = build("parent born", 1.0, false), death = build("parent dies", 1.0, false);
    CHECK(sparks(birth, 3).size() == 5);
    for (const SpriteInstance& s : sparks(birth, 3)) CHECK(s.x < 0.5f);
    CHECK(sparks(death, 30).empty());   // the rocket is still flying
    const auto after = sparks(death, 66);  // just after it ends, at about 4 units out
    REQUIRE(after.size() == 5);
    for (const SpriteInstance& s : after) CHECK(s.x == Catch::Approx(4.0f).margin(0.3));
    // No chance, no sparks; and a switched-off parent still sets them off.
    CHECK(sparks(build("parent dies", 0.0, false), 66).empty());
    CHECK(sparks(build("parent dies", 1.0, true), 66).size() == 5);
    // Seeking straight to a moment gives what playing up to it does.
    Simulation played(compileEffect(death)), jumped(compileEffect(death));
    for (int i = 0; i < 70; ++i) played.advance();
    jumped.seek(70);
    CHECK(played.stateHash() == jumped.stateHash());

    const J sub = describe(death)["layers"][1]["subEmitter"];
    REQUIRE(sub.is_object());
    CHECK(sub["trigger"].get<std::string>() == "death");
    CHECK(sub["parent"].get<std::string>() == "Rocket");
    CHECK(sub["count"].get<int>() == 5);
}
