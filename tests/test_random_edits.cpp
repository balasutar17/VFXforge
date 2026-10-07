// Property test: long random sequences of edits, undo and redo.
//
// After every single step the document must still save to a file that loads
// cleanly and saves back byte-identical, and every undo or redo must land on
// exactly the state that was there before.
#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "helpers.h"
#include "random_values.h"

using namespace vfx;
using testing::Rng;
using testing::randomValue;
using testing::sampleEffect;
using testing::set;
using testing::text;

namespace {

// Picks any editable property anywhere in the effect.
bool randomPath(const Effect& effect, Rng& rng, Path& path, const PropertyDesc*& desc) {
    const auto& registry = Registry::builtin();
    const std::size_t where = rng.below(10);
    if (where == 0 || effect.layers.empty()) {
        const auto& fields = registry.effectFields().properties;
        desc = &fields[rng.below(fields.size())];
        path = Path::effect(desc->key);
        return true;
    }
    const Layer& layer = effect.layers[rng.below(effect.layers.size())];
    if (where <= 2 || layer.modules.empty()) {
        const auto& fields = registry.layerFields().properties;
        desc = &fields[rng.below(fields.size())];
        path = Path::layerField(layer.id, desc->key);
        return true;
    }
    const Module& module = layer.modules[rng.below(layer.modules.size())];
    if (!module.desc) {
        return false;
    }
    desc = &module.desc->properties[rng.below(module.desc->properties.size())];
    path = Path::property(layer.id, module.id, desc->key);
    return true;
}

void checkInvariants(const Document& document, const std::string& now) {
    auto loaded = readEffect(now);
    REQUIRE(loaded.ok());
    REQUIRE_FALSE(loaded.value().repaired);
    REQUIRE_FALSE(loaded.value().readOnly);
    REQUIRE(writeEffect(loaded.value().effect) == now);

    const std::vector<Id> ids = collectIds(document.effect());
    const std::set<Id> unique(ids.begin(), ids.end());
    REQUIRE(unique.size() == ids.size());
    REQUIRE(unique.count(Id{}) == 0);
}

void runSequence(std::uint64_t seed, int steps) {
    // A cap far above the step count, so no history falls away mid-test.
    INFO("seed " << seed);
    Rng rng(seed);
    Document document(sampleEffect(seed), IdGenerator(seed * 7919 + 1));
    CommandStack stack(document, 1000000);
    const auto& effect = document.effect();
    const IdSource newId = [&document]() { return document.newId(); };
    const auto& registry = Registry::builtin();

    // snapshots[n] is the saved text when n steps can be undone.
    std::vector<std::string> snapshots{text(document)};
    int accepted = 0, refused = 0, undone = 0, redone = 0;

    auto push = [&](CommandPtr command) {
        const std::string before = text(document);
        const Status status = stack.push(std::move(command));
        if (status.ok()) {
            ++accepted;
        } else {
            ++refused;
            REQUIRE(text(document) == before);  // a refused edit changes nothing
        }
    };

    for (int step = 0; step < steps; ++step) {
        INFO("step " << step);
        const std::size_t roll = rng.below(100);
        bool history = false;

        if (roll < 40) {
            Path path;
            const PropertyDesc* desc = nullptr;
            if (randomPath(effect, rng, path, desc)) {
                push(set(path, randomValue(*desc, rng, effect)));
            }
        } else if (roll < 44) {
            // An edit that must be refused: a number far outside any range.
            Path path;
            const PropertyDesc* desc = nullptr;
            if (randomPath(effect, rng, path, desc) && desc->kind == ValueKind::Float) {
                const std::string before = text(document);
                REQUIRE_FALSE(stack.push(set(path, Value(1e30))).ok());
                REQUIRE(text(document) == before);
            }
        } else if (roll < 52) {
            if (effect.layers.size() < 6) {
                const int index = static_cast<int>(rng.below(effect.layers.size() + 2)) - 1;
                push(std::make_unique<AddLayerCommand>(makeBasicEmitter(newId, "New"), index));
            }
        } else if (roll < 57) {
            if (!effect.layers.empty()) {
                push(std::make_unique<RemoveLayerCommand>(
                    effect.layers[rng.below(effect.layers.size())].id));
            }
        } else if (roll < 62) {
            if (!effect.layers.empty()) {
                // Two random draws are taken on separate lines: the order in
                // which function arguments are evaluated is up to the compiler,
                // and the sequence must be the same on every platform.
                const Id which = effect.layers[rng.below(effect.layers.size())].id;
                const int to = static_cast<int>(rng.below(effect.layers.size() + 1));
                push(std::make_unique<MoveLayerCommand>(which, to));
            }
        } else if (roll < 67) {
            if (!effect.layers.empty()) {
                const Layer& layer = effect.layers[rng.below(effect.layers.size())];
                if (!layer.modules.empty()) {
                    push(std::make_unique<RemoveModuleCommand>(
                        layer.id, layer.modules[rng.below(layer.modules.size())].id));
                }
            }
        } else if (roll < 73) {
            if (!effect.layers.empty()) {
                // Refused when the layer already has this type; both outcomes are valid.
                const Layer& layer = effect.layers[rng.below(effect.layers.size())];
                const auto& desc = registry.modules()[rng.below(registry.modules().size())];
                const int at = static_cast<int>(rng.below(layer.modules.size() + 2)) - 1;
                push(std::make_unique<AddModuleCommand>(layer.id, makeModule(desc, newId()), at));
            }
        } else if (roll < 77) {
            if (!effect.layers.empty()) {
                const Layer& layer = effect.layers[rng.below(effect.layers.size())];
                if (!layer.modules.empty()) {
                    const Id which = layer.modules[rng.below(layer.modules.size())].id;
                    const int to = static_cast<int>(rng.below(layer.modules.size()));
                    push(std::make_unique<MoveModuleCommand>(layer.id, which, to));
                }
            }
        } else if (roll < 80) {
            Asset asset;
            asset.id = newId();
            asset.path = "textures/tex" + std::to_string(rng.below(1000)) + ".png";
            push(std::make_unique<AddAssetCommand>(asset));
        } else if (roll < 83) {
            if (!effect.assets.empty()) {
                // Refused while a sprite still uses it.
                push(std::make_unique<RemoveAssetCommand>(
                    effect.assets[rng.below(effect.assets.size())].id));
            }
        } else if (roll < 88) {
            // A drag: many values on one property, committed or cancelled.
            Path path;
            const PropertyDesc* desc = nullptr;
            if (randomPath(effect, rng, path, desc)) {
                const std::string before = text(document);
                const std::size_t undoBefore = stack.undoCount();
                stack.beginTransaction("Drag");
                const std::size_t moves = 2 + rng.below(8);
                for (std::size_t i = 0; i < moves; ++i) {
                    push(set(path, randomValue(*desc, rng, effect)));
                }
                if (rng.chance(0.3)) {
                    stack.cancelTransaction();
                    REQUIRE(text(document) == before);
                    REQUIRE(stack.undoCount() == undoBefore);
                } else {
                    stack.endTransaction();
                    REQUIRE(stack.undoCount() <= undoBefore + 1);  // one step at most
                }
            }
        } else if (roll < 95) {
            history = true;
            if (stack.canUndo()) {
                REQUIRE(stack.undo().ok());
                ++undone;
            } else {
                REQUIRE_FALSE(stack.undo().ok());
            }
        } else {
            history = true;
            if (stack.canRedo()) {
                REQUIRE(stack.redo().ok());
                ++redone;
            } else {
                REQUIRE_FALSE(stack.redo().ok());
            }
        }

        const std::string now = text(document);
        checkInvariants(document, now);

        const std::size_t n = stack.undoCount();
        if (history) {
            REQUIRE(n < snapshots.size());
            REQUIRE(now == snapshots[n]);  // undo and redo land exactly where they should
        } else if (!(n < snapshots.size() && snapshots[n] == now)) {
            snapshots.resize(n + 1);
            snapshots[n] = now;
        }
        REQUIRE(stack.isDirty() == (n != 0));
    }

    // Walk all the way back and all the way forward again.
    const std::string last = text(document);
    const std::size_t top = stack.undoCount();
    std::size_t redoable = 0;
    while (stack.canUndo()) {
        REQUIRE(stack.undo().ok());
        ++redoable;
        REQUIRE(text(document) == snapshots[stack.undoCount()]);
    }
    REQUIRE(text(document) == snapshots[0]);
    REQUIRE_FALSE(stack.isDirty());
    for (std::size_t i = 0; i < redoable; ++i) {
        REQUIRE(stack.redo().ok());
    }
    REQUIRE(stack.undoCount() == top);
    REQUIRE(text(document) == last);

    // The run must have actually exercised something.
    CHECK(accepted > steps / 4);
    CHECK(undone + redone + refused > 0);
}

// Set VFX_RANDOM_SCALE to multiply the amount of work, for example 10 for a
// long soak run.
int scale() {
    if (const char* env = std::getenv("VFX_RANDOM_SCALE")) {
        const int n = std::atoi(env);
        if (n > 0) {
            return n;
        }
    }
    return 1;
}

}  // namespace

TEST_CASE("Random edit sequences never corrupt the document or the history") {
    const int sequences = 40 * scale();
    for (int seed = 1; seed <= sequences; ++seed) {
        runSequence(static_cast<std::uint64_t>(seed), 80);
    }
}

TEST_CASE("One very long random edit sequence") {
    runSequence(0xC0FFEE, 1000 * scale());
}
