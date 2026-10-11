// Curves, colour gradients and the Advanced view's list of settings.
#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "vfx/Metadata.h"
#include "vfx/Serialize.h"
#include "vfx/editor/CurveTools.h"
#include "vfx/editor/Session.h"

using namespace vfx;
using namespace vfx::editor;

namespace {

const PropertyDesc& property(const char* module, const char* key) {
    return *Registry::builtin().findModule(module)->find(key);
}

const ControlView* find(const std::vector<ControlView>& all, const char* section, const char* label) {
    for (const ControlView& c : all) {
        if (c.section == section && c.label == label) {
            return &c;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("every ready-made curve shape is a legal curve that does what it says", "[curves]") {
    const PropertyDesc& opacity = property("overLife", "opacity");
    std::set<std::string> keys, labels;
    REQUIRE(curveShapes().size() >= 12);
    for (const CurveShape& shape : curveShapes()) {
        INFO(shape.key);
        CHECK(keys.insert(shape.key).second);
        CHECK(labels.insert(shape.label).second);
        CHECK(findCurveShape(shape.key) == &shape);
        CHECK_FALSE(std::string(shape.help).empty());
        CHECK_FALSE(std::string(shape.group).empty());
        REQUIRE(shape.keys.size() >= 2);
        CHECK(shape.keys.front().t == 0.0);
        CHECK(shape.keys.back().t == 1.0);
        double top = 0.0;
        for (const CurveKey& k : shape.keys) {
            CHECK(k.v >= 0.0);
            top = std::max(top, k.v);
        }
        CHECK(top == 1.0);  // values are shares of full
        // Tidying a shape changes nothing, and it is accepted as an opacity.
        CHECK(tidyCurve(shape.keys, opacity) == shape.keys);
        CHECK(validateValue(opacity, Value(Scalar::curve(shape.keys))).ok());
    }
    CHECK(findCurveShape("no-such-shape") == nullptr);

    // What a few of them promise.
    const auto at = [](const char* key, double t) { return curveAt(findCurveShape(key)->keys, t); };
    CHECK(at("fade-out", 0.25) == Catch::Approx(0.75));
    CHECK(at("slow-start", 0.5) < 0.4);
    CHECK(at("slow-stop", 0.5) > 0.6);
    CHECK(at("slow-both", 0.5) == Catch::Approx(0.5));
    CHECK(at("slow-both", 0.1) < 0.05);
    // Pop and settle goes past where it ends up.
    CHECK(at("pop", 0.18) == 1.0);
    CHECK(at("pop", 1.0) < 0.9);
    CHECK(at("pop", 0.6) < at("pop", 1.0) + 0.02);
    // Wind up dips before it rises.
    CHECK(at("wind-up", 0.18) < at("wind-up", 0.0));
    CHECK(at("wind-up", 0.6) == Catch::Approx(1.0));
    // Swell is the same on the way down as on the way up.
    CHECK(at("swell", 0.3) == Catch::Approx(at("swell", 0.7)));
}

TEST_CASE("a shape is made to stand as high as the value does now", "[curves]") {
    const PropertyDesc& size = property("overLife", "size");
    const PropertyDesc& spin = property("motion", "spin");
    const PropertyDesc& rate = property("emission", "rate");
    const CurveShape& fade = *findCurveShape("fade-out");

    CHECK(levelOf(Scalar::constant(-3.0)) == 3.0);
    CHECK(levelOf(Scalar::random(1.0, 4.0)) == 4.0);
    CHECK(levelOf(Scalar::curve({{0, 0.5}, {0.5, 2.5}, {1, 0}})) == 2.5);

    // A size that stood at 1.6 now fades from 1.6.
    Scalar s = shaped(fade, Scalar::constant(1.6), size);
    REQUIRE(s.kind == Scalar::Kind::Curve);
    CHECK(s.keys == std::vector<CurveKey>{{0, 1.6}, {1, 0}});
    // A curve that peaked at 2 keeps its peak.
    s = shaped(*findCurveShape("swell"), Scalar::curve({{0, 0}, {0.3, 2.0}, {1, 0.5}}), size);
    CHECK(levelOf(s) == 2.0);
    CHECK(s.keys[1].smooth);
    // A spin of -180 a second stays a backward spin.
    s = shaped(fade, Scalar::constant(-180.0), spin);
    CHECK(s.keys.front().v == -180.0);
    CHECK(s.keys.back().v == 0.0);
    // Standing at nothing, it goes by the property's ordinary value.
    s = shaped(fade, Scalar::constant(0.0), rate);
    CHECK(s.keys.front().v == 20.0);
    s = shaped(fade, Scalar::constant(0.0), spin);  // whose ordinary value is nothing too
    CHECK(s.keys.front().v == 360.0);               // the top of its slider
    // And never past the property's limits.
    const PropertyDesc& opacity = property("overLife", "opacity");
    s = shaped(fade, Scalar::constant(1.0), opacity);
    CHECK(validateValue(opacity, Value(s)).ok());

    // A number or a range as a curve: a level line.
    CHECK(curveFrom(Scalar::constant(0.7)) == std::vector<CurveKey>{{0, 0.7}, {1, 0.7}});
    CHECK(curveFrom(Scalar::random(1.0, 3.0)) == std::vector<CurveKey>{{0, 2.0}, {1, 2.0}});
}

TEST_CASE("any list of points is tidied into a legal curve", "[curves]") {
    const PropertyDesc& opacity = property("overLife", "opacity");
    // Out of order, out of range, doubled up, and not numbers at all.
    std::vector<CurveKey> mess{{0.9, 0.5, true}, {0.2, 7.0}, {0.2, -3.0}, {std::nan(""), 0.25}, {1.5, std::nan("")}};
    const std::vector<CurveKey> tidy = tidyCurve(mess, opacity);
    REQUIRE(validateValue(opacity, Value(Scalar::curve(tidy))).ok());
    REQUIRE(tidy.size() == 5);
    for (std::size_t i = 1; i < tidy.size(); ++i) {
        CHECK(tidy[i].t >= tidy[i - 1].t + kCurveGap - 1e-12);
    }
    CHECK(tidy[0] == CurveKey{0.0, 0.25});           // the nonsense time became the start
    CHECK(tidy[1] == CurveKey{0.2, 1.0});            // 7 is held to the limit
    CHECK(tidy[2].v == 0.0);                         // -3 too
    CHECK(tidy[2].t == Catch::Approx(0.2 + kCurveGap));  // and moved off its twin
    CHECK(tidy[3] == CurveKey{0.9, 0.5, true});      // smooth points stay smooth
    CHECK(tidy[4].t == 1.0);

    // Never empty, never more than the editors make.
    CHECK(tidyCurve({}, opacity).size() == 1);
    std::vector<CurveKey> many;
    for (int i = 0; i < 200; ++i) {
        many.push_back(CurveKey{i / 200.0, 0.5});
    }
    CHECK(tidyCurve(many, opacity).size() == kEditorCurvePoints);
    // Points piled up at the very end do not run off it.
    const auto piled = tidyCurve({{1.0, 0.1}, {1.0, 0.2}, {1.0, 0.3}}, opacity);
    CHECK(piled.size() == 1);
    CHECK(validateValue(opacity, Value(Scalar::curve(piled))).ok());

    // Reading a curve between its points, bends and all; level beyond the ends.
    const std::vector<CurveKey> ease{{0.2, 0.0, true}, {0.8, 1.0, true}};
    CHECK(curveAt(ease, 0.0) == 0.0);
    CHECK(curveAt(ease, 0.5) == Catch::Approx(0.5));
    CHECK(curveAt(ease, 0.3) < 0.1);
    CHECK(curveAt(ease, 0.95) == 1.0);
    CHECK(curveAt({}, 0.5) == 0.0);
}

TEST_CASE("a colour gradient is tidied and read like a curve", "[curves][gradient]") {
    Gradient g;
    g.keys = {{1.0, Color{0, 0, 1, 0}}, {0.0, Color{1, 0, 0, 1}}, {0.0, Color{9000, -1, std::nan(""), 4}}};
    const Gradient tidy = tidyGradient(g);
    const PropertyDesc& colour = property("overLife", "color");
    REQUIRE(validateValue(colour, Value(tidy)).ok());
    REQUIRE(tidy.keys.size() == 3);
    CHECK(tidy.keys[0].color == Color{1, 0, 0, 1});
    CHECK(tidy.keys[1].color == Color{1000, 0, 0, 1});
    CHECK(tidy.keys[1].t == Catch::Approx(kCurveGap));
    CHECK(tidyGradient(Gradient{}).keys.size() == 1);

    Gradient two;
    two.keys = {{0.25, Color{1, 0, 0, 1}}, {0.75, Color{0, 0, 2, 0}}};
    CHECK(gradientAt(two, 0.0) == Color{1, 0, 0, 1});
    CHECK(gradientAt(two, 1.0) == Color{0, 0, 2, 0});
    const Color mid = gradientAt(two, 0.5);
    CHECK(mid.r == Catch::Approx(0.5));
    CHECK(mid.b == Catch::Approx(1.0));
    CHECK(mid.a == Catch::Approx(0.5));
    CHECK(gradientAt(Gradient{}, 0.3) == Color{1, 1, 1, 1});
}

TEST_CASE("bursts are tidied into time order within the layer", "[curves][bursts]") {
    BurstList list;
    list.items = {{1.5, 10}, {0.25, -4}, {9.0, 5000000}, {std::nan(""), 3}};
    const BurstList tidy = tidyBursts(list, 2.0);
    REQUIRE(tidy.items.size() == 4);
    CHECK(tidy.items[0] == Burst{0.0, 3});
    CHECK(tidy.items[1] == Burst{0.25, 0});
    CHECK(tidy.items[2] == Burst{1.5, 10});
    CHECK(tidy.items[3] == Burst{2.0, 100000});
    CHECK(validateValue(property("emission", "bursts"), Value(tidy)).ok());
    for (int i = 0; i < 100; ++i) {
        list.items.push_back(Burst{0.1, 1});
    }
    CHECK(tidyBursts(list, 2.0).items.size() == 64);
}

TEST_CASE("the Advanced view lists every setting of a layer and can change each one", "[session][advanced]") {
    Session s;
    const Id layer = s.effect().layers[0].id;
    const auto all = s.properties(layer);

    // The layer's own timing, then every property of every module.
    std::size_t expected = 2;
    for (const Module& m : s.effect().layers[0].modules) {
        REQUIRE(m.known());
        expected += m.desc->properties.size();
    }
    REQUIRE(all.size() == expected);
    CHECK(all[0].section == "Timing");
    CHECK(all[0].label == "Starts At");
    CHECK(all[1].label == "Length");
    std::set<std::uint64_t> ids;
    std::vector<std::string> sections;
    for (const ControlView& c : all) {
        INFO(c.section << " / " << c.label);
        REQUIRE(c.desc != nullptr);
        CHECK(holdsKind(c.value, c.desc->kind));
        CHECK(ids.insert(c.control.value).second);  // no two share an ID
        if (sections.empty() || sections.back() != c.section) {
            sections.push_back(c.section);
        }
    }
    CHECK(sections == std::vector<std::string>{"Timing", "Emission", "Shape", "Initial State", "Motion",
                                                 "Over Lifetime", "Sprite"});
    // Settings the Simple panel never showed are here.
    for (const auto& [section, label] : std::vector<std::pair<const char*, const char*>>{
             {"Emission", "Bursts"}, {"Shape", "Radius"}, {"Shape", "Position"}, {"Initial State", "Rotation"},
             {"Motion", "Gravity"}, {"Motion", "Drag"}, {"Motion", "Spin"}, {"Motion", "Swirl Change"},
             {"Over Lifetime", "Size"}, {"Over Lifetime", "Color"}, {"Over Lifetime", "Opacity"},
             {"Sprite", "Stretch"}, {"Sprite", "Trail"}, {"Sprite", "Align"}}) {
        INFO(section << " / " << label);
        CHECK(find(all, section, label) != nullptr);
    }
    // The IDs do not change from one look to the next.
    const auto again = s.properties(layer);
    for (std::size_t i = 0; i < all.size(); ++i) {
        CHECK(again[i].control == all[i].control);
    }

    // Changing things through it: a number, a vector, a curve with a smooth
    // point, a gradient, a burst list, the layer's own length. Each is one
    // undo step.
    const Id drag = find(all, "Motion", "Drag")->control;
    REQUIRE(s.setControl(layer, drag, Value{2.5}).ok());
    CHECK(std::get<double>(find(s.properties(layer), "Motion", "Drag")->value) == 2.5);

    const Id gravity = find(all, "Motion", "Gravity")->control;
    REQUIRE(s.setControl(layer, gravity, Value{Vec3{1, -2, 0}}).ok());
    CHECK(std::get<Vec3>(find(s.properties(layer), "Motion", "Gravity")->value) == Vec3{1, -2, 0});

    const Id size = find(all, "Over Lifetime", "Size")->control;
    const Scalar pop = shaped(*findCurveShape("pop"), Scalar::constant(1.0), *find(all, "Over Lifetime", "Size")->desc);
    REQUIRE(s.setControl(layer, size, Value{pop}).ok());
    CHECK(std::get<Scalar>(find(s.properties(layer), "Over Lifetime", "Size")->value) == pop);

    Gradient g;
    g.keys = {{0.0, Color{2, 1.5, 1, 1}}, {1.0, Color{0.2, 0, 0, 0}}};
    const Id colour = find(all, "Over Lifetime", "Color")->control;
    REQUIRE(s.setControl(layer, colour, Value{g}).ok());

    const Id bursts = find(all, "Emission", "Bursts")->control;
    REQUIRE(s.setControl(layer, bursts, Value{BurstList{{Burst{0.0, 12}, Burst{0.5, 30}}}}).ok());

    const Id length = find(all, "Timing", "Length")->control;
    REQUIRE(s.setControl(layer, length, Value{0.75}).ok());
    CHECK(s.effect().layers[0].duration == 0.75);
    const Id start = find(all, "Timing", "Starts At")->control;
    REQUIRE(s.setControl(layer, start, Value{0.25}).ok());
    CHECK(s.effect().layers[0].start == 0.25);

    // A value of the wrong kind, or out of range, is refused and changes nothing.
    CHECK_FALSE(s.setControl(layer, drag, Value{std::string("fast")}).ok());
    CHECK_FALSE(s.setControl(layer, drag, Value{-5.0}).ok());
    CHECK(std::get<double>(find(s.properties(layer), "Motion", "Drag")->value) == 2.5);

    // It all saves, loads and plays, and seven undos put everything back.
    const auto loaded = readEffect(writeEffect(s.effect()));
    REQUIRE(loaded.ok());
    CHECK(loaded.value().diagnostics.empty());
    CHECK(writeEffect(loaded.value().effect) == writeEffect(s.effect()));
    s.clock().play();
    for (int i = 0; i < 40; ++i) {
        s.tick(1.0 / 60.0);
    }
    CHECK(s.particleCount() > 0);
    for (int i = 0; i < 7; ++i) {
        REQUIRE(s.undo().ok());
    }
    const auto back = s.properties(layer);
    for (std::size_t i = 0; i < all.size(); ++i) {
        CHECK(back[i].value == all[i].value);
    }
    CHECK(s.properties(Id{12345}).empty());
}
