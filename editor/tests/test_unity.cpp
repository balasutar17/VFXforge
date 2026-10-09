// Exporting to Unity: the translation into Particle System terms, and the
// files that go into a Unity project.
#include <catch2/catch_amalgamated.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "vfx/FileIO.h"
#include "vfx/Templates.h"
#include "vfx/editor/Presets.h"
#include "vfx/editor/Session.h"
#include "vfx/editor/UnityExport.h"
#include "vfx/editor/UnityFiles.h"

using namespace vfx;
using namespace vfx::editor;
using J = nlohmann::json;

namespace {

Effect preset(const char* id) {
    IdGenerator ids(5);
    const IdSource newId = [&ids]() { return ids.next(); };
    auto made = makePreset(id, newId);
    REQUIRE(made.ok());
    return made.value();
}

J describe(const Effect& effect) { return J::parse(unityDescription(effect)); }

const J& layerNamed(const J& root, const std::string& name) {
    for (const J& layer : root["layers"]) {
        if (layer["name"] == name) {
            return layer;
        }
    }
    FAIL("no layer called " << name);
    return root;
}

// One basic emitter, with a few settings changed through the property table.
Effect single(const std::function<void(Layer&)>& change) {
    IdGenerator ids(9);
    const IdSource newId = [&ids]() { return ids.next(); };
    Effect effect = makeEmptyEffect(newId, "Test", false);
    effect.duration = 2.0;
    Layer layer = makeBasicEmitter(newId, "Layer");
    layer.duration = 2.0;
    change(layer);
    effect.layers.push_back(layer);
    return effect;
}

void put(Layer& layer, const char* type, const char* key, Value value) {
    for (Module& m : layer.modules) {
        if (m.type == type) {
            *m.find(key) = std::move(value);
        }
    }
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

}  // namespace

TEST_CASE("the Unity files built into the program are the ones in unity/", "[unity]") {
    const auto& files = unityHelperSources();
    std::set<std::string> paths;
    for (const UnityFile& f : files) {
        INFO(f.path);
        paths.insert(f.path);
        std::string disk = readText(std::filesystem::path(VFX_UNITY_DIR) / f.path);
        std::string::size_type at;
        while ((at = disk.find("\r\n")) != std::string::npos) {
            disk.erase(at, 1);
        }
        CHECK(f.text == disk);
    }
    CHECK(paths.count("Editor/VFXForgeImporter.cs") == 1);
    CHECK(paths.count("Editor/VFXForgeJson.cs") == 1);
    CHECK(paths.count("Shaders/VFXForgeParticle.shader") == 1);
    CHECK(paths.count("README.txt") == 1);
}

TEST_CASE("every preset translates into a complete Unity description", "[unity]") {
    for (const PresetInfo& info : presets()) {
        INFO(info.id);
        const Effect effect = preset(info.id.c_str());
        const J root = describe(effect);
        CHECK(root["format"] == "vfxforge.unity");
        CHECK(root["formatVersion"] == kUnityFormatVersion);
        CHECK(root["name"] == effect.name);
        CHECK(root["duration"] == effect.duration);
        CHECK(root["loop"] == true);
        CHECK(root["atlas"]["columns"] == kAtlasColumns);
        REQUIRE(root["layers"].size() == effect.layers.size());
        int order = 0;
        for (const J& layer : root["layers"]) {
            INFO(layer["name"]);
            for (const char* key : {"startLifetime", "startSpeed", "startSize", "startRotation", "startColor",
                                    "rate", "bursts", "shape", "render", "maxParticles", "seed"}) {
                CHECK(layer.contains(key));
            }
            CHECK(layer["render"]["order"] == order++);
            CHECK(layer["render"]["shape"].get<int>() >= 0);
            CHECK(layer["render"]["shape"].get<int>() < kAtlasColumns * kAtlasRows);
            CHECK(layer["render"]["glow"].get<double>() > 0.0);
            // Unity gradients cannot go above 1, or hold more than 8 keys.
            if (!layer["colorOverLife"].is_null()) {
                CHECK(layer["colorOverLife"]["colors"].size() <= 8);
                CHECK(layer["colorOverLife"]["alphas"].size() <= 8);
                for (const J& k : layer["colorOverLife"]["colors"]) {
                    for (int c = 1; c <= 3; ++c) {
                        CHECK(k[c].get<double>() >= 0.0);
                        CHECK(k[c].get<double>() <= 1.0);
                    }
                }
            }
            for (const J& burst : layer["bursts"]) {
                CHECK(burst[0].get<double>() >= 0.0);
                CHECK(burst[0].get<double>() < effect.duration);
            }
            const J& shape = layer["shape"];
            CHECK((shape["type"] == "circle" || shape["type"] == "sphere" || shape["type"] == "cone" ||
                   shape["type"] == "box"));
        }
    }
}

TEST_CASE("start values: colours on screen, rotation clockwise in radians", "[unity]") {
    const Effect effect = single([](Layer& l) {
        put(l, "initial", "color", Color{srgbToLinear(0.5), 1.0, 0.0, 0.75});
        put(l, "initial", "rotation", Value(Scalar::random(0.0, 90.0)));
        put(l, "initial", "lifetime", Value(Scalar::random(0.5, 1.5)));
        put(l, "initial", "size", Value(Scalar::constant(0.4)));
    });
    const J layer = describe(effect)["layers"][0];
    CHECK(layer["startColor"][0].get<double>() == Catch::Approx(0.5).margin(1e-5));
    CHECK(layer["startColor"][1] == 1.0);
    CHECK(layer["startColor"][3] == 0.75);
    CHECK(layer["startLifetime"][0] == 0.5);
    CHECK(layer["startLifetime"][1] == 1.5);
    // 0..90 degrees counter-clockwise is 0..-pi/2 clockwise.
    CHECK(layer["startRotation"][0].get<double>() == Catch::Approx(-1.570796).margin(1e-5));
    CHECK(layer["startRotation"][1].get<double>() == 0.0);
    // The template's size over life is a curve, so start size stays as set.
    CHECK(layer["startSize"].get<double>() == Catch::Approx(0.4));
}

TEST_CASE("a fixed size or opacity multiplier moves into the start values", "[unity]") {
    const Effect effect = single([](Layer& l) {
        put(l, "initial", "size", Value(Scalar::constant(0.5)));
        put(l, "overLife", "size", Value(Scalar::random(1.0, 3.0)));
        put(l, "overLife", "opacity", Value(Scalar::constant(0.5)));
        put(l, "initial", "color", Color{1, 1, 1, 0.8});
    });
    const J layer = describe(effect)["layers"][0];
    CHECK(layer["startSize"][0] == 0.5);
    CHECK(layer["startSize"][1] == 1.5);
    CHECK(layer["sizeOverLife"].is_null());
    CHECK(layer["startColor"][3].get<double>() == Catch::Approx(0.4));
}

TEST_CASE("2D spread from a point becomes an exact arc", "[unity][shape]") {
    // Straight up with 30 degrees either side: an arc of 60 starting at 60.
    J shape = describe(single([](Layer& l) { put(l, "initial", "spread", Value(30.0)); }))["layers"][0]["shape"];
    CHECK(shape["type"] == "circle");
    CHECK(shape["arc"].get<double>() == Catch::Approx(60.0));
    CHECK(shape["rotation"][2].get<double>() == Catch::Approx(60.0));
    CHECK(shape["exact"] == true);

    // Every direction.
    shape = describe(single([](Layer& l) { put(l, "initial", "spread", Value(180.0)); }))["layers"][0]["shape"];
    CHECK(shape["type"] == "circle");
    CHECK(shape["arc"] == 360.0);

    // To the right with none: a sliver of arc centred on 0 degrees.
    shape = describe(single([](Layer& l) {
        put(l, "initial", "direction", Value(Vec3{1, 0, 0}));
        put(l, "initial", "spread", Value(0.0));
    }))["layers"][0]["shape"];
    CHECK(shape["arc"].get<double>() < 0.01);
    CHECK(shape["rotation"][2].get<double>() == Catch::Approx(0.0).margin(1e-6));
}

TEST_CASE("a rectangle sending particles up is a box facing up", "[unity][shape]") {
    const J shape = describe(single([](Layer& l) {
                        put(l, "shape", "shape", Value(std::string("rectangle")));
                        put(l, "shape", "size", Value(Vec3{4, 0.5, 1}));
                        put(l, "initial", "spread", Value(18.0));
                    }))["layers"][0]["shape"];
    CHECK(shape["type"] == "box");
    // Unity boxes emit along their own Z: turned -90 degrees about X, Z points
    // up the screen and the rectangle's height runs along it.
    CHECK(shape["rotation"][0].get<double>() == Catch::Approx(-90.0));
    CHECK(shape["rotation"][1].get<double>() == Catch::Approx(0.0));
    CHECK(shape["scale"][0].get<double>() == Catch::Approx(4.0));
    CHECK(shape["scale"][1].get<double>() == Catch::Approx(0.0).margin(1e-6));
    CHECK(shape["scale"][2].get<double>() == Catch::Approx(0.5));
    CHECK(shape["randomDirection"].get<double>() == Catch::Approx(0.1));
    CHECK(shape["edge"] == false);
}

TEST_CASE("a frame outline is a flat box emitting from its edges", "[unity][shape]") {
    const J root = describe(preset("frame-sparkle"));
    const J shape = layerNamed(root, "Outline")["shape"];
    CHECK(shape["type"] == "box");
    CHECK(shape["edge"] == true);
    // The box is turned to send particles up, so the outline's height runs
    // along the box's own Z; it is still the same rectangle in the world.
    CHECK(shape["rotation"][0].get<double>() == Catch::Approx(-90.0));
    CHECK(shape["scale"][0].get<double>() == Catch::Approx(5.2));
    CHECK(shape["scale"][1].get<double>() == Catch::Approx(0.0).margin(1e-6));
    CHECK(shape["scale"][2].get<double>() == Catch::Approx(3.2));
}

TEST_CASE("3D points become cones or spheres", "[unity][shape]") {
    auto threeD = [](double spread) {
        IdGenerator ids(3);
        const IdSource newId = [&ids]() { return ids.next(); };
        Effect effect = makeEmptyEffect(newId, "Test", true);
        Layer layer = makeBasicEmitter(newId, "Layer");
        put(layer, "initial", "spread", Value(spread));
        put(layer, "initial", "direction", Value(Vec3{1, 0, 0}));
        effect.layers.push_back(layer);
        return describe(effect)["layers"][0]["shape"];
    };
    J shape = threeD(40);
    CHECK(shape["type"] == "cone");
    CHECK(shape["angle"].get<double>() == Catch::Approx(40.0));
    CHECK(shape["rotation"][1].get<double>() == Catch::Approx(90.0));  // +Z turned to +X
    shape = threeD(180);
    CHECK(shape["type"] == "sphere");
}

TEST_CASE("streaks become stretched billboards", "[unity][render]") {
    const J root = describe(preset("fire-blast"));
    const J sparks = layerNamed(root, "Sparks")["render"];
    CHECK(sparks["mode"] == "stretched");
    CHECK(sparks["velocityScale"].get<double>() == Catch::Approx(0.045));
    CHECK(sparks["lengthScale"] == 1.0);
    CHECK(sparks["shapeName"] == "streak");
    CHECK(sparks["additive"] == true);
    const J smoke = layerNamed(root, "Smoke")["render"];
    CHECK(smoke["mode"] == "billboard");
    CHECK(smoke["additive"] == false);
    CHECK(smoke["shapeName"] == "smoke");
}

TEST_CASE("a white-hot gradient keeps its brightness in the glow", "[unity][render]") {
    const J root = describe(preset("campfire"));
    const J flames = layerNamed(root, "Flames");
    // The hot end of the gradient is 2.6 times the colour, and the layer's
    // glow is 0.7: the gradient is divided by 2.6 and the glow multiplied.
    CHECK(flames["render"]["glow"].get<double>() == Catch::Approx(0.7 * 2.6).epsilon(1e-4));
    const J& first = flames["colorOverLife"]["colors"][0];
    CHECK(first[1].get<double>() == Catch::Approx(1.0));
    CHECK(flames["sizeOverLife"]["keys"].size() == 3);
}

TEST_CASE("a layer that starts late emits only in its own time", "[unity][timing]") {
    // Arcane Burst's outer ring starts 0.06 s into a 2 s effect.
    J root = describe(preset("arcane-burst"));
    const J ring = layerNamed(root, "Outer ring");
    REQUIRE(ring["bursts"].size() == 1);
    CHECK(ring["bursts"][0][0].get<double>() == Catch::Approx(0.06));

    // Level Up's column glow runs for the first 1.2 s of 2.4 at 10 a second.
    root = describe(preset("level-up"));
    const J rate = layerNamed(root, "Column glow")["rate"];
    REQUIRE(rate.is_object());
    const double multiplier = rate["multiplier"];
    CHECK(multiplier == 10.0);
    // Evaluate the straight-line curve at a few moments.
    auto at = [&](double t) {
        const J& keys = rate["keys"];
        for (std::size_t i = 1; i < keys.size(); ++i) {
            const double t0 = keys[i - 1][0], t1 = keys[i][0];
            if (t <= t1) {
                const double v0 = keys[i - 1][1], v1 = keys[i][1];
                return multiplier * (t1 > t0 ? v0 + (v1 - v0) * (t - t0) / (t1 - t0) : v1);
            }
        }
        return multiplier * keys.back()[1].get<double>();
    };
    CHECK(at(0.1) == Catch::Approx(10.0));
    CHECK(at(0.45) == Catch::Approx(10.0));
    CHECK(at(0.6) == Catch::Approx(0.0).margin(1e-6));
    CHECK(at(0.9) == Catch::Approx(0.0).margin(1e-6));
}

TEST_CASE("the shape atlas holds every shape in a grid", "[unity]") {
    const std::string png = unityShapeAtlasPng();
    REQUIRE(png.size() > 1000);
    auto be32 = [&](std::size_t at) {
        return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at])) << 24) |
               (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 1])) << 16) |
               (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 2])) << 8) |
               static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 3]));
    };
    CHECK(be32(16) == static_cast<std::uint32_t>(kAtlasColumns * kAtlasTile));
    CHECK(be32(20) == static_cast<std::uint32_t>(kAtlasRows * kAtlasTile));
    CHECK(png[25] == 4);  // grey and alpha
    CHECK(kAtlasColumns * kAtlasRows >= kSpriteShapeCount);
    CHECK(png.size() < 2'000'000u);  // compressed: mostly empty space
}

TEST_CASE("an export writes the helper, the atlas and the effect", "[unity][files]") {
    Effect effect = preset("toon-hit");
    effect.name = "Toon: Hit / big?";
    CHECK(unityFileStem(effect) == "Toon_ Hit _ big_");
    effect.name = "  Spark  ";
    CHECK(unityFileStem(effect) == "Spark");

    const auto files = unityExportFiles(effect);
    std::set<std::string> paths;
    for (const TarEntry& f : files) {
        CHECK(f.path.rfind("Assets/VFXForge/", 0) == 0);
        paths.insert(f.path);
    }
    CHECK(paths.count("Assets/VFXForge/Editor/VFXForgeImporter.cs") == 1);
    CHECK(paths.count("Assets/VFXForge/Shaders/VFXForgeParticle.shader") == 1);
    CHECK(paths.count("Assets/VFXForge/Textures/VFXForgeShapes.png") == 1);
    CHECK(paths.count("Assets/VFXForge/Effects/Spark.vfxforge") == 1);

    // Into a folder that is not a Unity project: refused, nothing written.
    const auto base = std::filesystem::temp_directory_path() / "vfxforge_unity_test";
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(base);
    const Status refused = exportToUnityProject(effect, base);
    CHECK_FALSE(refused.ok());
    CHECK_FALSE(std::filesystem::exists(base / "Assets"));

    // Into a Unity project.
    std::filesystem::create_directories(base / "Assets");
    std::filesystem::create_directories(base / "ProjectSettings");
    CHECK(isUnityProject(base));
    std::filesystem::path written;
    REQUIRE(exportToUnityProject(effect, base, &written).ok());
    CHECK(written == base / "Assets/VFXForge/Effects/Spark.vfxforge");
    CHECK(readText(written) == unityDescription(effect));
    CHECK(std::filesystem::exists(base / "Assets/VFXForge/Editor/VFXForgeJson.cs"));
    // Again, for the same effect: replaced in place, still one file.
    REQUIRE(exportToUnityProject(effect, base).ok());
    int effects = 0;
    for (const auto& entry : std::filesystem::directory_iterator(base / "Assets/VFXForge/Effects")) {
        effects += entry.path().extension() == ".vfxforge" ? 1 : 0;
    }
    CHECK(effects == 1);
    std::filesystem::remove_all(base);
}

TEST_CASE("a .unitypackage is a gzip of one folder per asset", "[unity][files]") {
    const auto files = unityExportFiles(preset("hearts"));
    const std::string package = makeUnityPackage(files);
    REQUIRE(package.size() > 100);
    CHECK(static_cast<std::uint8_t>(package[0]) == 0x1f);
    CHECK(static_cast<std::uint8_t>(package[1]) == 0x8b);
    // The same files give the same package, byte for byte.
    CHECK(makeUnityPackage(files) == package);
}
