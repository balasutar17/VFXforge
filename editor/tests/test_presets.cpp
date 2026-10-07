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
            CHECK(((c >= 'a' && c <= 'z') || c == '-'));
        }
    }

    const auto categories = presetCategories();
    for (const char* wanted : {"Toon", "Blasts", "Fire", "Water", "Glow and magic", "Frames", "Shooting",
                               "Rewards", "Weather"}) {
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
        CHECK(effect.space == "2d");
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
                // Curves cannot be edited in the Simple view yet, so no
                // Simple control of a preset may be one.
                if (const auto* scalar = std::get_if<Scalar>(&control.value)) {
                    CHECK(scalar->kind != Scalar::Kind::Curve);
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
