// The built-in library: every preset must be a valid, ordinary effect that
// shows something, and that an artist can edit with the Simple controls.
#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <set>
#include <string>

#include "vfx/Metadata.h"
#include "vfx/Program.h"
#include "vfx/Serialize.h"
#include "vfx/Simulation.h"
#include "vfx/editor/Picture.h"
#include "vfx/editor/CurveTools.h"
#include "vfx/editor/Refine.h"
#include "vfx/editor/Presets.h"
#include "vfx/editor/Session.h"
#include "vfx/editor/SpriteMesh.h"

using namespace vfx;
using namespace vfx::editor;

namespace {

Effect build(const std::string& id, std::uint64_t seed = 99) {
    IdGenerator ids(seed);
    const IdSource newId = [&ids]() { return ids.next(); };
    auto made = makePreset(id, newId);
    REQUIRE(made.ok());
    return made.value();
}

View viewOf(const PresetInfo& info, int width, int height) {
    View view;
    view.width = static_cast<float>(width);
    view.height = static_cast<float>(height);
    view.centerX = info.viewX;
    view.centerY = info.viewY;
    view.unitsHigh = info.viewHeight;
    return view;
}

}  // namespace

TEST_CASE("the library has presets in every promised category", "[presets]") {
    const auto& all = presets();
    CHECK(all.size() >= 24);

    std::set<std::string> ids, names;
    for (const PresetInfo& info : all) {
        INFO(info.id);
        CHECK_FALSE(info.id.empty());
        CHECK_FALSE(info.name.empty());
        CHECK_FALSE(info.category.empty());
        CHECK_FALSE(info.description.empty());
        CHECK(info.description.back() == '.');
        CHECK(info.viewHeight > 0.5f);
        CHECK(info.previewTime >= 0.0);
        CHECK(ids.insert(info.id).second);      // no two share an ID
        CHECK(names.insert(info.name).second);  // or a name
        CHECK(findPreset(info.id) == &info);
        for (const char c : info.id) {
            CHECK(((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'));
        }
    }

    const auto categories = presetCategories();
    for (const char* wanted : {"Toon", "Blasts", "Fire", "Water", "Glow and magic", "Frames", "Shooting",
                               "Rewards", "Weather", "3D", "3D Combat"}) {
        INFO(wanted);
        CHECK(std::find(categories.begin(), categories.end(), wanted) != categories.end());
    }
    // Presets of one category sit together, so the library can show them in order.
    std::vector<std::string> runs;
    for (const PresetInfo& info : all) {
        if (runs.empty() || runs.back() != info.category) {
            runs.push_back(info.category);
        }
    }
    CHECK(runs == categories);

    CHECK(findPreset("no-such-preset") == nullptr);
    IdGenerator generator(1);
    const IdSource newId = [&generator]() { return generator.next(); };
    const auto missing = makePreset("no-such-preset", newId);
    REQUIRE_FALSE(missing.ok());
    CHECK_FALSE(missing.error().message.empty());
}

TEST_CASE("every preset is a valid effect that saves and loads unchanged", "[presets]") {
    for (const PresetInfo& info : presets()) {
        INFO(info.id);
        const Effect effect = build(info.id);
        CHECK(effect.name == info.name);
        CHECK(effect.space == (info.category.rfind("3D", 0) == 0 ? "3d" : "2d"));
        CHECK(effect.loop == "loop");
        REQUIRE_FALSE(effect.layers.empty());

        // Every stored value obeys its property's rules.
        for (const Layer& layer : effect.layers) {
            INFO(layer.name);
            CHECK_FALSE(layer.name.empty());
            CHECK(layer.start >= 0.0);
            CHECK(layer.start + layer.duration <= effect.duration + 1e-9);
            for (const Module& module : layer.modules) {
                REQUIRE(module.known());
                for (std::size_t i = 0; i < module.values.size(); ++i) {
                    const Status ok = validateValue(module.desc->properties[i], module.values[i]);
                    INFO(module.type << "." << module.desc->properties[i].key);
                    CHECK(ok.ok());
                }
            }
        }

        // No ID is used twice.
        std::vector<Id> ids = collectIds(effect);
        std::sort(ids.begin(), ids.end());
        CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());

        // Saved and loaded, nothing is repaired and nothing changes.
        const std::string text = writeEffect(effect);
        auto loaded = readEffect(text);
        REQUIRE(loaded.ok());
        CHECK(loaded.value().diagnostics.empty());
        CHECK_FALSE(loaded.value().repaired);
        CHECK(writeEffect(loaded.value().effect) == text);

        // The same IDs in give the same effect out.
        CHECK(writeEffect(build(info.id)) == text);
    }
}

TEST_CASE("every preset shows something and stays within its limits", "[presets]") {
    for (const PresetInfo& info : presets()) {
        INFO(info.id);
        const Effect effect = build(info.id);
        const auto program = compileEffect(effect);
        REQUIRE(program->emitters.size() == effect.layers.size());
        std::uint64_t room = 0;
        for (const auto& emitter : program->emitters) {
            CHECK(emitter->drawn);
            CHECK(emitter->emits);
            CHECK_FALSE(emitter->capped());
            room += emitter->capacity;
        }
        CHECK(room < 20000);  // light enough that a whole gallery can play at once

        Simulation simulation(program);
        simulation.seek(static_cast<std::int64_t>(info.previewTime / kSimulationStep + 0.5));
        CHECK(simulation.aliveCount() > 0);

        RenderFrame frame;
        simulation.extract(frame);
        SpriteMesh mesh;
        buildSpriteMesh(frame, viewOf(info, 240, 180), mesh);
        CHECK(mesh.drawn > 0);

        // In the preview picture, a fair number of pixels are not background.
        const Picture picture = drawPicture(mesh, 240, 180, ScreenColor{0, 0, 0});
        int lit = 0;
        for (int y = 0; y < picture.height; ++y) {
            for (int x = 0; x < picture.width; ++x) {
                const std::uint8_t* p = picture.pixel(x, y);
                lit += (p[0] > 40 || p[1] > 40 || p[2] > 40) ? 1 : 0;
            }
        }
        CHECK(lit > 40);

        // It keeps going: the second time round is not empty either.
        simulation.seek(static_cast<std::int64_t>((effect.duration + info.previewTime) / kSimulationStep));
        CHECK(simulation.aliveCount() > 0);
    }
}

TEST_CASE("every preset layer can be edited with the Simple controls", "[presets]") {
    Session s;
    for (const PresetInfo& info : presets()) {
        INFO(info.id);
        REQUIRE(s.openPreset(info.id));
        CHECK_FALSE(s.dirty());
        CHECK_FALSE(s.hasFile());
        CHECK(s.clock().playing());
        for (const Layer& layer : s.effect().layers) {
            INFO(layer.name);
            const auto controls = s.controls(layer.id);
            CHECK(controls.size() >= 10);
            for (const ControlView& control : controls) {
                INFO(control.label);
                REQUIRE(control.desc != nullptr);
                // A Simple control may be a curve (Squash and Stretch in the
                // Cartoon motion presets is): the Simple view shows the curve
                // editor for it, so it must be a curve that editor can hold.
                if (const auto* scalar = std::get_if<Scalar>(&control.value)) {
                    if (scalar->kind == Scalar::Kind::Curve) {
                        CHECK(scalar->keys.size() <= kEditorCurvePoints);
                        CHECK(tidyCurve(scalar->keys, *control.desc) == scalar->keys);
                    }
                }
            }
        }
    }
}

TEST_CASE("a preset opens as a fresh effect and can be added to another", "[presets][session]") {
    Session s;
    REQUIRE(s.set(Path::effect("name"), Value(std::string("Mine"))));
    const auto generation = s.generation();

    // An unknown preset changes nothing.
    CHECK_FALSE(s.openPreset("no-such-preset"));
    CHECK_FALSE(s.addPreset("no-such-preset"));
    CHECK(s.effect().name == "Mine");
    CHECK(s.generation() == generation);

    // Adding keeps the open effect and brings the preset's layers in.
    const std::size_t before = s.effect().layers.size();
    int added = 0;
    REQUIRE(s.addPreset("campfire", &added));
    CHECK(added == 4);
    CHECK(s.effect().name == "Mine");
    CHECK(s.effect().layers.size() == before + 4);
    CHECK(s.commands().undoName() == "Add Campfire");
    for (int i = 0; i < 90; ++i) {
        s.tick(1.0 / 60.0);
    }
    CHECK(s.frame().batches.size() == before + 4);

    // Twice is fine: the second copy gets IDs of its own.
    REQUIRE(s.addPreset("campfire"));
    std::vector<Id> ids = collectIds(s.effect());
    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());

    // One undo each.
    REQUIRE(s.undo());
    REQUIRE(s.undo());
    CHECK(s.effect().layers.size() == before);

    // Opening replaces the effect.
    REQUIRE(s.openPreset("fire-blast"));
    CHECK(s.effect().name == "Fire Blast");
    CHECK(s.generation() == generation + 1);
    CHECK_FALSE(s.commands().canUndo());
}

namespace {

void put(Layer& layer, const char* type, const char* key, Value value) {
    for (Module& m : layer.modules) {
        if (m.type == type) {
            *m.find(key) = std::move(value);
        }
    }
}

const Value* findValue(const Layer& layer, const char* type, const char* key) {
    for (const Module& m : layer.modules) {
        if (m.type == type) {
            return m.find(key);
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("no looping preset jumps when its loop starts again", "[presets][loop]") {
    for (const PresetInfo& info : presets()) {
        INFO(info.id);
        CHECK(loopJumps(build(info.id)).empty());
    }
}

TEST_CASE("a ring that turns part of a turn each loop is found, and Seamless loop mends it", "[presets][loop]") {
    Effect effect = build("portal-3d");
    REQUIRE(effect.duration == Catch::Approx(4.0).margin(0.02));
    // Turning 140 degrees a second, as Portal 3D first did, the whirl is
    // 200 degrees short of a whole turn when the loop starts again.
    for (Layer& l : effect.layers) {
        if (l.name == "Vortex") {
            put(l, "motion", "spin", Value(Scalar::constant(140.0)));
        }
    }
    const auto jumping = loopJumps(effect);
    REQUIRE(jumping.size() == 1);
    CHECK(jumping[0] == "Vortex");

    IdGenerator ids(3);
    const IdSource newId = [&ids]() { return ids.next(); };
    std::string said;
    const auto mended = refine(effect, Refine::SmoothLoop, nullptr, newId, &said);
    REQUIRE(mended.ok());
    CHECK(loopJumps(mended.value()).empty());
    CHECK(said.find("Vortex") != std::string::npos);
    for (const Layer& l : mended.value().layers) {
        if (l.name == "Vortex") {
            const Value* spin = findValue(l, "motion", "spin");
            REQUIRE(spin);
            CHECK(std::get<Scalar>(*spin).a * mended.value().duration == Catch::Approx(720.0));  // two whole turns
        }
    }
    // A loop that plays once has nothing to mend.
    effect.loop = "once";
    CHECK(loopJumps(effect).empty());
}

TEST_CASE("an effect added from the library is a group, moved, delayed, sized and removed as one",
          "[presets][session][groups]") {
    Session s;
    REQUIRE(s.openPreset("portal-3d"));
    const std::size_t own = s.effect().layers.size();
    CHECK(s.groups().empty());  // an opened preset is the effect itself, not a group in it

    REQUIRE(s.addPreset("guardian-orbs-3d"));
    REQUIRE(s.addPreset("guardian-orbs-3d"));
    REQUIRE(s.groups() == std::vector<std::string>{"Guardian Orbs 3D", "Guardian Orbs 3D 2"});
    const std::string g = "Guardian Orbs 3D";
    auto members = [&](const std::string& name) {
        std::vector<const Layer*> out;
        for (const Layer& l : s.effect().layers) {
            if (l.group == name) {
                out.push_back(&l);
            }
        }
        return out;
    };
    auto offsetOf = [](const Layer& l) {
        for (const Module& m : l.modules) {
            if (m.type == "shape") {
                return std::get<Vec3>(*m.find("offset"));
            }
        }
        return Vec3{};
    };
    REQUIRE(members(g).size() == 2);
    for (const Layer* l : members(g)) {
        CHECK_FALSE(l->id == Id{});
    }

    // A 2-second loop added to a 4-second one plays twice over.
    for (const Layer* l : members(g)) {
        CHECK(l->duration == Catch::Approx(s.effect().duration));
    }

    // Moved together, one undo step; the other group stays put.
    const Vec3 was = offsetOf(*members(g)[0]);
    const Vec3 other = offsetOf(*members(g + " 2")[0]);
    REQUIRE(s.moveGroup(g, 0.5, 0.25, -1.0));
    CHECK(s.commands().undoName() == "Move Guardian Orbs 3D");
    CHECK(offsetOf(*members(g)[0]).x == Catch::Approx(was.x + 0.5));
    CHECK(offsetOf(*members(g)[0]).y == Catch::Approx(was.y + 0.25));
    CHECK(offsetOf(*members(g)[0]).z == Catch::Approx(was.z - 1.0));
    CHECK(offsetOf(*members(g + " 2")[0]).y == Catch::Approx(other.y));
    REQUIRE(s.undo());
    CHECK(offsetOf(*members(g)[0]).y == Catch::Approx(was.y));

    // Delayed: it starts later. A loop keeps its length (so the portal still
    // joins up), and the group is cut off at its end.
    const double length = s.effect().duration;
    REQUIRE(s.delayGroup(g, 0.5));
    for (const Layer* l : members(g)) {
        CHECK(l->start == Catch::Approx(0.5));
        CHECK(l->start + l->duration == Catch::Approx(length));
    }
    CHECK(s.effect().duration == length);
    CHECK(loopJumps(s.effect()).empty());
    REQUIRE(s.delayGroup(g, -2.0));  // not before the start
    for (const Layer* l : members(g)) {
        CHECK(l->start == 0.0);
    }
    REQUIRE(s.undo());
    REQUIRE(s.undo());
    // An effect that plays once grows to fit instead.
    REQUIRE(s.set(Path::effect("loop"), Value(std::string("once"))));
    REQUIRE(s.delayGroup(g, 0.5));
    CHECK(s.effect().duration == Catch::Approx(length + 0.5));
    REQUIRE(s.undo());
    REQUIRE(s.undo());

    // Sized about the middle of its emitters: they all sit at one point, so
    // that point stays where it is.
    REQUIRE(s.sizeGroup(g, 2.0));
    CHECK(offsetOf(*members(g)[0]).y == Catch::Approx(was.y));
    CHECK_FALSE(s.sizeGroup(g, 0.0));

    // Hidden and shown, removed, and ungrouped.
    REQUIRE(s.setGroupEnabled(g, false));
    for (const Layer* l : members(g)) {
        CHECK_FALSE(l->enabled);
    }
    REQUIRE(s.removeGroup(g));
    CHECK(members(g).empty());
    CHECK(s.effect().layers.size() == own + 2);
    REQUIRE(s.undo());
    CHECK(members(g).size() == 2);
    REQUIRE(s.ungroup(g + " 2"));
    CHECK(s.groups() == std::vector<std::string>{g});
    CHECK(s.effect().layers.size() == own + 4);

    // Unknown groups are refused.
    CHECK_FALSE(s.moveGroup("no such group", 1, 0, 0));
    CHECK_FALSE(s.removeGroup("no such group"));

    // A group is saved with the effect and read back.
    const auto text = writeEffect(s.effect());
    const auto back = readEffect(text);
    REQUIRE(back.ok());
    int grouped = 0;
    for (const Layer& l : back.value().effect.layers) {
        grouped += l.group == g ? 1 : 0;
    }
    CHECK(grouped == 2);
}
