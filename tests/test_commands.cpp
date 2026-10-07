#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include "helpers.h"

using namespace vfx;
using testing::propertyPath;
using testing::sampleEffect;
using testing::set;
using testing::text;

namespace {

// The contract every command must honour, checked through the saved file so
// nothing can hide: apply changes the document, revert restores it byte for
// byte, and applying again gives the same result as the first time.
void checkRoundTrip(Document& document, Command& command) {
    const std::string before = text(document);
    REQUIRE(command.apply(document).ok());
    const std::string after = text(document);
    CHECK(after != before);

    command.revert(document);
    CHECK(text(document) == before);

    REQUIRE(command.apply(document).ok());
    CHECK(text(document) == after);

    command.revert(document);
    CHECK(text(document) == before);

    // The result must also be a file that loads cleanly.
    REQUIRE(command.apply(document).ok());
    auto loaded = readEffect(text(document));
    REQUIRE(loaded.ok());
    CHECK_FALSE(loaded.value().repaired);
    CHECK(writeEffect(loaded.value().effect) == after);
    command.revert(document);
}

// A failed command must leave the document exactly as it was.
void checkRefused(Document& document, Command& command) {
    const std::string before = text(document);
    const Status status = command.apply(document);
    REQUIRE_FALSE(status.ok());
    CHECK_FALSE(status.error().message.empty());
    CHECK(text(document) == before);
}

}  // namespace

TEST_CASE("Set property: every property of every module applies and reverts") {
    Document document(sampleEffect());
    const auto& effect = document.effect();
    int checked = 0;
    for (const auto& layer : effect.layers) {
        for (const auto& module : layer.modules) {
            for (const auto& p : module.desc->properties) {
                INFO(module.type << "." << p.key);
                Value changed;
                switch (p.kind) {
                    case ValueKind::Float: changed = std::get<double>(p.defaultValue) + 0.25; break;
                    case ValueKind::Scalar: changed = Scalar::random(0.5, 0.75); break;
                    case ValueKind::Vec3: changed = Vec3{0.25, 0.5, 0.75}; break;
                    case ValueKind::Color: changed = Color{0.25, 0.5, 0.75, 0.5}; break;
                    case ValueKind::Gradient:
                        changed = Gradient{{{0, Color{1, 0, 0, 1}}, {0.5, Color{0, 1, 0, 0.5}}}};
                        break;
                    case ValueKind::Bursts: changed = BurstList{{{0.25, 12}}}; break;
                    case ValueKind::Enum: changed = p.options.back(); break;
                    case ValueKind::Asset: {
                        const bool has = std::get<AssetRef>(*module.find(p.key)).id.valid();
                        changed = has ? AssetRef{} : AssetRef{effect.assets[0].id};
                        break;
                    }
                    default: FAIL("unhandled kind in test");
                }
                SetPropertyCommand command(Path::property(layer.id, module.id, p.key), changed);
                checkRoundTrip(document, command);
                ++checked;
            }
        }
    }
    CHECK(checked == 48);
}

TEST_CASE("Set property: effect and layer fields") {
    Document document(sampleEffect());
    const Id layer = document.effect().layers[0].id;
    const std::pair<Path, Value> edits[] = {
        {Path::effect("name"), Value(std::string("Renamed"))},
        {Path::effect("space"), Value(std::string("3d"))},
        {Path::effect("seed"), Value(std::int64_t{98765})},
        {Path::effect("duration"), Value(4.5)},
        {Path::effect("loop"), Value(std::string("once"))},
        {Path::effect("frameRate"), Value(24.0)},
        {Path::layerField(layer, "name"), Value(std::string("Glitter ✨"))},
        {Path::layerField(layer, "enabled"), Value(false)},
        {Path::layerField(layer, "start"), Value(0.5)},
        {Path::layerField(layer, "duration"), Value(1.25)},
    };
    for (const auto& [path, value] : edits) {
        INFO(path.str());
        SetPropertyCommand command(path, value);
        checkRoundTrip(document, command);

        REQUIRE(command.apply(document).ok());
        auto read = document.get(path);
        REQUIRE(read.ok());
        CHECK(read.value() == value);
        command.revert(document);
    }
}

TEST_CASE("Set property: invalid edits are refused and change nothing") {
    Document document(sampleEffect());
    const auto& effect = document.effect();
    const Path spread = propertyPath(effect, 0, "initial", "spread");
    const Path texture = propertyPath(effect, 0, "sprite", "texture");
    const Id layer = effect.layers[0].id;

    std::vector<std::pair<Path, Value>> bad = {
        {spread, Value(500.0)},
        {spread, Value(std::nan(""))},
        {spread, Value(std::string("wide"))},
        {spread, Value(Scalar::constant(30))},
        {texture, Value(AssetRef{Id{0xdead}})},
        {propertyPath(effect, 0, "sprite", "blend"), Value(std::string("dodge"))},
        {propertyPath(effect, 0, "initial", "lifetime"), Value(Scalar::random(3, 1))},
        {Path::effect("duration"), Value(0.0)},
        {Path::effect("seed"), Value(std::int64_t{-1})},
        {Path::effect("nothing"), Value(1.0)},
        {Path::layerField(layer, "name"), Value(std::string("\xff bad"))},
        {Path::layerField(layer, "colour"), Value(1.0)},
        {Path::layerField(Id{0xbeef}, "name"), Value(std::string("x"))},
        {Path::property(layer, Id{0xbeef}, "spread"), Value(1.0)},
        {Path::property(layer, effect.layers[1].modules[0].id, "rate"),
         Value(Scalar::constant(1))},  // module belongs to another layer
        {propertyPath(effect, 0, "initial", "id"), Value(1.0)},
        {propertyPath(effect, 0, "initial", "type"), Value(std::string("motion"))},
    };
    for (auto& [path, value] : bad) {
        INFO(path.str());
        SetPropertyCommand command(path, value);
        checkRefused(document, command);
    }
}

TEST_CASE("Layers: add, remove and reorder") {
    Document document(sampleEffect());
    const IdSource newId = [&document]() { return document.newId(); };

    SECTION("add at the end and in the middle") {
        AddLayerCommand atEnd(makeBasicEmitter(newId, "Glow"));
        checkRoundTrip(document, atEnd);

        AddLayerCommand inMiddle(makeBasicEmitter(newId, "Flash"), 1);
        REQUIRE(inMiddle.apply(document).ok());
        REQUIRE(document.effect().layers.size() == 3);
        CHECK(document.effect().layers[1].name == "Flash");
        inMiddle.revert(document);
        CHECK(document.effect().layers.size() == 2);
    }
    SECTION("remove puts back the identical layer") {
        const Layer original = document.effect().layers[0];
        RemoveLayerCommand remove(original.id);
        checkRoundTrip(document, remove);

        REQUIRE(remove.apply(document).ok());
        CHECK(findLayer(document.effect(), original.id) == nullptr);
        remove.revert(document);
        const Layer* restored = findLayer(document.effect(), original.id);
        REQUIRE(restored != nullptr);
        CHECK(layerIndex(document.effect(), original.id) == 0);
        // Same IDs all the way down, so anything pointing at them still works.
        for (std::size_t i = 0; i < original.modules.size(); ++i) {
            CHECK(restored->modules[i].id == original.modules[i].id);
        }
        for (std::size_t i = 0; i < original.controls.size(); ++i) {
            CHECK(restored->controls[i].id == original.controls[i].id);
        }
    }
    SECTION("reorder") {
        const Id first = document.effect().layers[0].id;
        MoveLayerCommand move(first, 1);
        checkRoundTrip(document, move);

        MoveLayerCommand tooFar(first, 99);  // clamps to the last position
        REQUIRE(tooFar.apply(document).ok());
        CHECK(layerIndex(document.effect(), first) == 1);
        tooFar.revert(document);
        CHECK(layerIndex(document.effect(), first) == 0);

        MoveLayerCommand nowhere(first, 0);
        REQUIRE(nowhere.apply(document).ok());
        CHECK(nowhere.isNoOp());
    }
    SECTION("refusals") {
        Layer clash = makeBasicEmitter(newId, "Clash");
        clash.id = document.effect().layers[0].id;
        AddLayerCommand duplicateLayerId(clash);
        checkRefused(document, duplicateLayerId);

        Layer moduleClash = makeBasicEmitter(newId, "Clash");
        moduleClash.modules[0].id = document.effect().layers[0].modules[0].id;
        AddLayerCommand duplicateModuleId(moduleClash);
        checkRefused(document, duplicateModuleId);

        Layer selfClash = makeBasicEmitter(newId, "Clash");
        selfClash.modules[1].id = selfClash.modules[0].id;
        AddLayerCommand internalDuplicate(selfClash);
        checkRefused(document, internalDuplicate);

        Layer noId = makeBasicEmitter(newId, "No ID");
        noId.id = Id{};
        AddLayerCommand missingId(noId);
        checkRefused(document, missingId);

        Layer badValue = makeBasicEmitter(newId, "Bad");
        *badValue.modules[2].find("spread") = 999.0;
        AddLayerCommand invalidValue(badValue);
        checkRefused(document, invalidValue);

        Layer badTime = makeBasicEmitter(newId, "Bad");
        badTime.duration = -1;
        AddLayerCommand invalidField(badTime);
        checkRefused(document, invalidField);

        RemoveLayerCommand missing(Id{0xbeef});
        checkRefused(document, missing);
        MoveLayerCommand missingMove(Id{0xbeef}, 0);
        checkRefused(document, missingMove);
    }
}

TEST_CASE("Modules: add, remove and reorder") {
    Document document(sampleEffect());
    const Id layer = document.effect().layers[0].id;
    const auto& registry = Registry::builtin();
    const Module motion = testing::moduleOfType(document.effect().layers[0], "motion");

    SECTION("remove then add back a fresh one") {
        RemoveModuleCommand remove(layer, motion.id);
        checkRoundTrip(document, remove);

        REQUIRE(remove.apply(document).ok());
        AddModuleCommand add(layer, makeModule(*registry.findModule("motion"), document.newId()), 0);
        checkRoundTrip(document, add);
        REQUIRE(add.apply(document).ok());
        CHECK(document.effect().layers[0].modules[0].type == "motion");
    }
    SECTION("remove keeps the module's position and ID for undo") {
        const int position = moduleIndex(document.effect().layers[0], motion.id);
        RemoveModuleCommand remove(layer, motion.id);
        REQUIRE(remove.apply(document).ok());
        remove.revert(document);
        CHECK(moduleIndex(document.effect().layers[0], motion.id) == position);
    }
    SECTION("reorder") {
        MoveModuleCommand move(layer, motion.id, 0);
        checkRoundTrip(document, move);
    }
    SECTION("refusals") {
        AddModuleCommand second(layer, makeModule(*registry.findModule("motion"), document.newId()));
        checkRefused(document, second);  // a layer has one of each type

        RemoveModuleCommand prepare(layer, motion.id);
        REQUIRE(prepare.apply(document).ok());

        Module reused = makeModule(*registry.findModule("motion"), document.effect().id);
        AddModuleCommand duplicateId(layer, reused);
        checkRefused(document, duplicateId);

        Module wrongTable = makeModule(*registry.findModule("motion"), document.newId());
        wrongTable.type = "sprite";
        AddModuleCommand mismatch(layer, wrongTable);
        checkRefused(document, mismatch);

        Module shortValues = makeModule(*registry.findModule("motion"), document.newId());
        shortValues.values.pop_back();
        AddModuleCommand truncated(layer, shortValues);
        checkRefused(document, truncated);

        Module invalid = makeModule(*registry.findModule("motion"), document.newId());
        *invalid.find("drag") = -5.0;
        AddModuleCommand badValue(layer, invalid);
        checkRefused(document, badValue);

        AddModuleCommand noLayer(Id{0xbeef},
                                 makeModule(*registry.findModule("motion"), document.newId()));
        checkRefused(document, noLayer);
        RemoveModuleCommand noModule(layer, Id{0xbeef});
        checkRefused(document, noModule);
        MoveModuleCommand noMove(layer, Id{0xbeef}, 0);
        checkRefused(document, noMove);
    }
}

TEST_CASE("Modules this version does not know can be moved and removed, and survive undo") {
    Effect effect = sampleEffect();
    Module future;
    future.id = Id{0x4242};
    future.type = "turbulence";
    future.extra.emplace_back("strength", "2.5");
    effect.layers[0].modules.push_back(future);

    Document document(std::move(effect));
    const Id layer = document.effect().layers[0].id;
    CHECK(text(document).find("\"strength\": 2.5") != std::string::npos);

    RemoveModuleCommand remove(layer, future.id);
    checkRoundTrip(document, remove);
    MoveModuleCommand move(layer, future.id, 0);
    checkRoundTrip(document, move);

    // Its contents are opaque here, so they cannot be edited.
    SetPropertyCommand edit(Path::property(layer, future.id, "strength"), Value(1.0));
    checkRefused(document, edit);
}

TEST_CASE("Assets: add and remove") {
    Document document(sampleEffect());
    const Id inUse = document.effect().assets[0].id;

    Asset extra;
    extra.id = document.newId();
    extra.path = "textures/Étoile dorée.png";
    AddAssetCommand add(extra);
    checkRoundTrip(document, add);

    SECTION("an asset in use cannot be removed until nothing refers to it") {
        RemoveAssetCommand remove(inUse);
        checkRefused(document, remove);

        SetPropertyCommand clear(propertyPath(document.effect(), 0, "sprite", "texture"),
                                 Value(AssetRef{}));
        REQUIRE(clear.apply(document).ok());
        checkRoundTrip(document, remove);
    }
    SECTION("refusals") {
        Asset duplicate = extra;
        duplicate.id = inUse;
        AddAssetCommand sameId(duplicate);
        checkRefused(document, sameId);

        for (const char* path : {"", "/abs/x.png", "../x.png", "a/../x.png", "C:/x.png", "a\\x.png",
                                 "a//x.png", "a/./x.png", "a/x.png/"}) {
            INFO(path);
            Asset bad = extra;
            bad.path = path;
            AddAssetCommand badPath(bad);
            checkRefused(document, badPath);
        }
        RemoveAssetCommand missing(Id{0xbeef});
        checkRefused(document, missing);
    }
}

TEST_CASE("Composite: all children apply and revert as one") {
    Document document(sampleEffect());
    const auto& effect = document.effect();

    std::vector<CommandPtr> children;
    children.push_back(set(propertyPath(effect, 0, "initial", "spread"), Value(90.0)));
    children.push_back(set(propertyPath(effect, 1, "initial", "spread"), Value(90.0)));
    children.push_back(std::make_unique<RemoveLayerCommand>(effect.layers[1].id));
    CompositeCommand composite("Make It Wider", std::move(children));
    CHECK(composite.name() == "Make It Wider");
    checkRoundTrip(document, composite);
}

TEST_CASE("Composite: one failing child undoes the ones before it") {
    Document document(sampleEffect());
    const auto& effect = document.effect();

    std::vector<CommandPtr> children;
    children.push_back(set(propertyPath(effect, 0, "initial", "spread"), Value(90.0)));
    children.push_back(std::make_unique<RemoveLayerCommand>(effect.layers[1].id));
    children.push_back(set(propertyPath(effect, 0, "initial", "spread"), Value(9999.0)));
    CompositeCommand composite("Doomed", std::move(children));
    checkRefused(document, composite);
    CHECK(document.effect().layers.size() == 2);
}

TEST_CASE("Listeners hear about every change, including undo") {
    Document document(sampleEffect());
    std::vector<Change> heard;
    const int token = document.subscribe([&heard](const Change& c) { heard.push_back(c); });

    const Path spread = propertyPath(document.effect(), 0, "initial", "spread");
    SetPropertyCommand setSpread(spread, Value(90.0));
    REQUIRE(setSpread.apply(document).ok());
    REQUIRE(heard.size() == 1);
    CHECK(heard[0].kind == Change::Kind::Property);
    CHECK(heard[0].layer == spread.layer);
    CHECK(heard[0].module == spread.module);
    CHECK(heard[0].field == "spread");
    setSpread.revert(document);
    REQUIRE(heard.size() == 2);
    CHECK(heard[1].kind == Change::Kind::Property);

    heard.clear();
    const Id layer = document.effect().layers[1].id;
    RemoveLayerCommand remove(layer);
    REQUIRE(remove.apply(document).ok());
    remove.revert(document);
    REQUIRE(heard.size() == 2);
    CHECK(heard[0].kind == Change::Kind::LayerRemoved);
    CHECK(heard[1].kind == Change::Kind::LayerAdded);
    CHECK(heard[1].layer == layer);

    heard.clear();
    SetPropertyCommand refused(spread, Value(9999.0));
    CHECK_FALSE(refused.apply(document).ok());
    CHECK(heard.empty());  // a refused edit is silent

    document.unsubscribe(token);
    REQUIRE(setSpread.apply(document).ok());
    CHECK(heard.empty());
}

TEST_CASE("A listener may unsubscribe while it is being called") {
    Document document(sampleEffect());
    int calls = 0;
    int token = 0;
    token = document.subscribe([&](const Change&) {
        ++calls;
        document.unsubscribe(token);
    });
    SetPropertyCommand a(Path::effect("duration"), Value(3.0));
    SetPropertyCommand b(Path::effect("duration"), Value(4.0));
    REQUIRE(a.apply(document).ok());
    REQUIRE(b.apply(document).ok());
    CHECK(calls == 1);
}

TEST_CASE("New IDs never collide with ones already in the effect") {
    // Give the document a generator that would repeat the effect's own IDs.
    Document document(sampleEffect(42), IdGenerator(42));
    const std::vector<Id> used = collectIds(document.effect());
    for (int i = 0; i < 200; ++i) {
        const Id fresh = document.newId();
        CHECK(fresh.valid());
        CHECK(std::find(used.begin(), used.end(), fresh) == used.end());
    }
}

TEST_CASE("Reading a path that does not exist is an error, not a crash") {
    Document document(sampleEffect());
    CHECK_FALSE(document.get(Path::effect("nothing")).ok());
    CHECK_FALSE(document.get(Path::layerField(Id{0xbeef}, "name")).ok());
    CHECK(document.describe(Path::effect("nothing")) == nullptr);
    CHECK(document.describe(Path::effect("duration")) != nullptr);
}
