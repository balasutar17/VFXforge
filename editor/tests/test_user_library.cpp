// The artist's own library: saving an effect, listing, opening and adding it.
#include <catch2/catch_amalgamated.hpp>

#include <filesystem>
#include <set>

#include "vfx/FileIO.h"
#include "vfx/Id.h"
#include "vfx/Serialize.h"
#include "vfx/editor/Presets.h"
#include "vfx/editor/Session.h"
#include "vfx/editor/UserLibrary.h"

using namespace vfx;
using namespace vfx::editor;

namespace {

std::filesystem::path freshFolder(const char* name) {
    const auto folder = std::filesystem::temp_directory_path() / "vfxforge-userlib-test" / name;
    std::error_code error;
    std::filesystem::remove_all(folder, error);
    std::filesystem::create_directories(folder, error);
    return folder;
}

Effect aPreset() {
    IdGenerator ids(11);
    auto made = makePreset("coin-burst", [&ids]() { return ids.next(); });
    REQUIRE(made);
    return std::move(made.value());
}

}  // namespace

TEST_CASE("an effect saved to the library comes back as it was", "[userlib]") {
    const auto root = freshFolder("roundtrip") / "Library";
    Effect effect = aPreset();
    effect.name = "My Coin Pop!";
    auto saved = saveUserEffect(root, effect, {}, 1.0f, 2.0f, 7.0f, 0.4);
    REQUIRE(saved);
    CHECK(saved.value().id == "my-coin-pop");
    CHECK_FALSE(saved.value().replaced);

    const auto list = listUserEffects(root);
    REQUIRE(list.size() == 1);
    CHECK(list[0].name == "My Coin Pop!");
    CHECK(list[0].viewHeight == Catch::Approx(7.0f));
    CHECK(list[0].previewTime == Catch::Approx(0.4));
    CHECK(list[0].layers == static_cast<int>(effect.layers.size()));

    auto back = loadUserEffect(root, "my-coin-pop");
    REQUIRE(back);
    CHECK(back.value().layers.size() == effect.layers.size());
    CHECK(back.value().duration == Catch::Approx(effect.duration));

    // The same name replaces; another name with the same folder name is kept beside it.
    auto again = saveUserEffect(root, effect, {}, 0, 0, 6, 0.5);
    REQUIRE(again);
    CHECK(again.value().replaced);
    CHECK(again.value().id == "my-coin-pop");
    // Another name that makes the same folder name is kept beside it, not over it.
    effect.name = "my coin pop?";
    auto beside = saveUserEffect(root, effect, {}, 0, 0, 6, 0.5);
    REQUIRE(beside);
    CHECK_FALSE(beside.value().replaced);
    CHECK(beside.value().id != "my-coin-pop");
    CHECK(listUserEffects(root).size() == 2);
    // ...and the same name again, in other letters, replaces that one.
    effect.name = "MY COIN POP?";
    auto over = saveUserEffect(root, effect, {}, 0, 0, 6, 0.5);
    REQUIRE(over);
    CHECK(over.value().replaced);
    CHECK(listUserEffects(root).size() == 2);

    REQUIRE(removeUserEffect(root, "my-coin-pop"));
    CHECK(listUserEffects(root).size() == 1);
    CHECK_FALSE(loadUserEffect(root, "my-coin-pop"));
    CHECK_FALSE(removeUserEffect(root, "../escape"));
}

TEST_CASE("pictures travel with a saved effect", "[userlib]") {
    const auto base = freshFolder("pictures");
    const auto project = base / "project";
    std::filesystem::create_directories(project / "images");
    REQUIRE(writeFileAtomic(project / "images" / "spark.png", "not really a png"));
    Effect effect = aPreset();
    Asset asset;
    asset.id = Id{987654321};
    asset.kind = "texture";
    asset.path = "images/spark.png";
    effect.assets.push_back(asset);
    auto saved = saveUserEffect(base / "Library", effect, project, 0, 0, 6, 0.5);
    REQUIRE(saved);
    CHECK(std::filesystem::exists(userEffectFolder(base / "Library", saved.value().id) / "images" / "spark.png"));
}

TEST_CASE("an effect added from the library gets IDs of its own", "[userlib]") {
    const auto root = freshFolder("add") / "Library";
    Session session;
    // Make an effect with a picture reference and keep it.
    IdGenerator ids(5);
    Effect effect = aPreset();
    REQUIRE(saveUserEffect(root, effect, {}, 0, 0, 6, 0.5));

    auto loaded = loadUserEffect(root, userEffectSlug(effect.name));
    REQUIRE(loaded);
    IdGenerator fresh(777);
    Effect made = withFreshIds(std::move(loaded.value()), [&fresh]() { return fresh.next(); });

    // None of the old IDs is left, and every layer and module has one.
    std::set<std::uint64_t> before;
    for (const Layer& layer : effect.layers) {
        before.insert(layer.id.value);
        for (const Module& m : layer.modules) before.insert(m.id.value);
    }
    std::set<std::uint64_t> after;
    for (const Layer& layer : made.layers) {
        CHECK(layer.id.valid());
        CHECK(before.count(layer.id.value) == 0);
        CHECK(after.insert(layer.id.value).second);
        for (const Module& m : layer.modules) {
            CHECK(before.count(m.id.value) == 0);
            CHECK(after.insert(m.id.value).second);
        }
    }
    CHECK(made.layers.size() == effect.layers.size());

    // Added to the open effect, as one group of its own.
    const std::size_t had = session.effect().layers.size();
    int count = 0;
    REQUIRE(session.addEffectAsGroup(std::move(made), "My Coin Burst", {}, &count));
    CHECK(count == static_cast<int>(effect.layers.size()));
    CHECK(session.effect().layers.size() == had + effect.layers.size());
    CHECK(session.effect().layers.back().group == "My Coin Burst");
    // Undo takes the group out again.
    REQUIRE(session.commands().undo());
    CHECK(session.effect().layers.size() == had);
}

TEST_CASE("a saved effect opens as a new unsaved effect", "[userlib]") {
    const auto root = freshFolder("open") / "Library";
    Effect effect = aPreset();
    REQUIRE(saveUserEffect(root, effect, {}, 0, 0, 6, 0.5));
    auto loaded = loadUserEffect(root, userEffectSlug(effect.name));
    REQUIRE(loaded);
    Session session;
    REQUIRE(session.openEffectCopy(std::move(loaded.value()), userEffectFolder(root, userEffectSlug(effect.name))));
    CHECK_FALSE(session.hasFile());
    CHECK(session.effect().layers.size() == effect.layers.size());
}
