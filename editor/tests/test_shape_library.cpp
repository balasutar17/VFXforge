// The shape library: the list, the shapes themselves, the pictures the
// graphics card draws them from, and how they travel to Unity.
#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "vfx/Program.h"
#include "vfx/Metadata.h"
#include "vfx/Serialize.h"
#include "vfx/Templates.h"
#include "vfx/editor/Image.h"
#include "vfx/editor/Picture.h"
#include "vfx/editor/Presets.h"
#include "vfx/editor/Session.h"
#include "vfx/editor/ShapeLibrary.h"
#include "vfx/editor/Shapes.h"
#include "vfx/editor/SpriteMesh.h"
#include "vfx/editor/UnityExport.h"

using namespace vfx;
using namespace vfx::editor;
using J = nlohmann::json;

namespace {

SpriteShape numbered(const char* name) {
    const int number = shapeNumber(name);
    REQUIRE(number >= 0);
    return static_cast<SpriteShape>(number);
}

float cover(const char* name, float x, float y) { return shapeCoverage(numbered(name), x, y, 0.01f, 0.01f); }

RenderFrame frameOf(std::initializer_list<SpriteShape> shapes) {
    RenderFrame frame;
    frame.flat = true;
    for (const SpriteShape shape : shapes) {
        SpriteInstance p;
        p.size = 2.0f;
        p.r = 1.0f;
        p.g = 0.0f;
        p.b = 0.0f;
        frame.instances.push_back(p);
        RenderBatch batch;
        batch.shape = shape;
        batch.first = static_cast<std::uint32_t>(frame.instances.size() - 1);
        batch.count = 1;
        frame.batches.push_back(batch);
    }
    return frame;
}

View square(int pixels, float units) {
    View v;
    v.width = v.height = static_cast<float>(pixels);
    v.centerX = v.centerY = 0;
    v.unitsHigh = units;
    return v;
}

// One basic emitter that draws the named shape.
Effect drawing(const char* shape, const char* second = nullptr) {
    IdGenerator ids(21);
    const IdSource newId = [&ids]() { return ids.next(); };
    Effect effect = makeEmptyEffect(newId, "Shapes", false);
    effect.duration = 2.0;
    for (const char* name : {shape, second}) {
        if (!name) {
            continue;
        }
        Layer layer = makeBasicEmitter(newId, name);
        layer.duration = 2.0;
        for (Module& m : layer.modules) {
            if (m.type == "sprite") {
                *m.find("shape") = Value{std::string(name)};
            }
        }
        effect.layers.push_back(layer);
    }
    return effect;
}

}  // namespace

TEST_CASE("the library lists every shape the Shape setting offers, in its order", "[shapes][library]") {
    const auto& options = Registry::builtin().findModule("sprite")->find("shape")->options;
    REQUIRE(static_cast<std::size_t>(shapeCount()) == options.size());
    REQUIRE(shapeCount() <= kMaxSpriteShapes);
    CHECK(shapeCount() - kSpriteShapeCount >= 100);

    std::set<std::string> labels;
    std::map<std::string, int> perCategory;
    const auto& categories = shapeCategories();
    for (int i = 0; i < shapeCount(); ++i) {
        const ShapeInfo& info = shapeInfo(i);
        INFO(info.name);
        CHECK(options[static_cast<std::size_t>(i)] == info.name);
        CHECK(shapeNumber(info.name) == i);
        CHECK(std::string(shapeName(static_cast<SpriteShape>(i))) == info.name);
        CHECK(isLibraryShape(i) == (i >= kSpriteShapeCount));
        // A label an artist can read, and no two the same.
        const std::string label = info.label;
        REQUIRE(label.size() >= 3);
        CHECK(label[0] >= 'A');
        CHECK(label[0] <= 'Z');
        CHECK(labels.insert(label).second);
        CHECK(std::find(categories.begin(), categories.end(), info.category) != categories.end());
        ++perCategory[info.category];
    }
    // Every category is worth opening.
    CHECK(categories.size() >= 8);
    for (const std::string& category : categories) {
        INFO(category);
        CHECK(perCategory[category] >= 8);
    }
    CHECK(std::set<std::string>(categories.begin(), categories.end()).size() == categories.size());

    // The formula shapes keep the numbers they have always had.
    CHECK(shapeNumber("soft") == 0);
    CHECK(shapeNumber("beam") == kSpriteShapeCount - 1);
    CHECK(shapeNumber("triangle") == kSpriteShapeCount);
    CHECK(shapeNumber("no such shape") == -1);
    CHECK(shapeNumber("") == -1);
    // Asking past either end answers with the nearest, never out of bounds.
    CHECK(std::string(shapeInfo(-5).name) == "soft");
    CHECK(std::string(shapeInfo(100000).name) == shapeInfo(shapeCount() - 1).name);
    CHECK_FALSE(isLibraryShape(-1));
    CHECK_FALSE(isLibraryShape(shapeCount()));
}

TEST_CASE("every library shape stays inside its particle and has a solid part", "[shapes][library]") {
    const float pixel = 0.02f;
    for (int i = kSpriteShapeCount; i < shapeCount(); ++i) {
        INFO(shapeInfo(i).name);
        float most = 0.0f, total = 0.0f, atEdge = 0.0f;
        for (int yi = -50; yi <= 50; ++yi) {
            for (int xi = -50; xi <= 50; ++xi) {
                const float x = static_cast<float>(xi) / 50.0f, y = static_cast<float>(yi) / 50.0f;
                const ShapeSample s = sampleLibraryShape(i, x, y, pixel, pixel);
                REQUIRE(std::isfinite(s.cover));
                REQUIRE(std::isfinite(s.tone));
                REQUIRE(std::isfinite(s.shine));
                REQUIRE(s.cover >= 0.0f);
                REQUIRE(s.cover <= 1.0f);
                REQUIRE(s.tone >= -1.0f);
                REQUIRE(s.tone <= 1.0f);
                REQUIRE(s.shine >= 0.0f);
                REQUIRE(s.shine <= 1.0f);
                most = std::max(most, s.cover);
                total += s.cover;
                // Nothing reaches the edge of the square, so no shape is cut off.
                if (std::abs(xi) == 50 || std::abs(yi) == 50) {
                    atEdge = std::max(atEdge, s.cover);
                }
                // sampleShape answers for both kinds.
                if ((xi + yi) % 10 == 0) {
                    const ShapeSample same = sampleShape(static_cast<SpriteShape>(i), x, y, pixel, pixel);
                    REQUIRE(same.cover == s.cover);
                    REQUIRE(same.tone == s.tone);
                    REQUIRE(same.shine == s.shine);
                }
            }
        }
        CHECK(atEdge < 0.01f);
        CHECK(most > 0.8f);
        CHECK(total > 100.0f);   // not just a few stray points
        CHECK(total < 9000.0f);  // and not the whole square: it is a shape
    }
}

TEST_CASE("library shapes are the shapes they claim to be", "[shapes][library]") {
    // A triangle stands on its base with a point straight up.
    CHECK(cover("triangle", 0, 0.5f) == 1.0f);
    CHECK(cover("triangle", 0.5f, 0.6f) == 0.0f);
    CHECK(cover("triangle", 0.5f, -0.4f) == 1.0f);
    CHECK(cover("triangle", -0.5f, -0.4f) == 1.0f);
    // A plus has arms along the axes; a cross has them along the diagonals.
    CHECK(cover("plus", 0.6f, 0) == 1.0f);
    CHECK(cover("plus", 0, -0.6f) == 1.0f);
    CHECK(cover("plus", 0.6f, 0.6f) == 0.0f);
    CHECK(cover("cross", 0.5f, 0.5f) == 1.0f);
    CHECK(cover("cross", -0.5f, 0.5f) == 1.0f);
    CHECK(cover("cross", 0.6f, 0) == 0.0f);
    // Outlines and a doughnut are hollow.
    CHECK(cover("frame", 0, 0) == 0.0f);
    CHECK(cover("frame", 0.8f, 0) == 1.0f);
    CHECK(cover("hexframe", 0, 0) == 0.0f);
    CHECK(cover("donut", 0, 0) == 0.0f);
    CHECK(cover("donut", 0.5f, 0) == 1.0f);
    CHECK(cover("coin", 0, 0) == 1.0f);
    // Flat on one side, and wider than tall.
    CHECK(cover("halfdisc", 0, 0.2f) == 1.0f);
    CHECK(cover("halfdisc", 0, -0.6f) == 0.0f);
    CHECK(cover("capsule", 0.7f, 0) == 1.0f);
    CHECK(cover("capsule", 0, 0.5f) == 0.0f);
    CHECK(cover("oval", 0.7f, 0) == 1.0f);
    CHECK(cover("oval", 0, 0.7f) == 0.0f);
    // An arc is the top of a ring.
    CHECK(cover("arc", 0, 0.45f) == 1.0f);
    CHECK(cover("arc", 0, -0.45f) == 0.0f);
    // Arrows and chevrons point along +x, the way a particle that follows
    // its movement travels.
    CHECK(cover("arrow", 0.7f, 0) == 1.0f);
    CHECK(cover("arrow", 0.3f, 0.35f) == 1.0f);   // the head is wide
    CHECK(cover("arrow", -0.5f, 0.35f) == 0.0f);  // the shaft is not
    CHECK(cover("chevron", 0.5f, 0) == 1.0f);
    CHECK(cover("chevron", 0, 0) == 0.0f);        // open behind
    CHECK(cover("chevron", -0.2f, 0.6f) == 1.0f);
    // A drop is pointed above and round below.
    CHECK(cover("droplet", 0.3f, -0.4f) == 1.0f);
    CHECK(cover("droplet", 0.3f, 0.5f) == 0.0f);
    CHECK(cover("droplet", 0, 0.7f) == 1.0f);
    // An exclamation mark is a stroke, a gap and a dot.
    CHECK(cover("exclaim", 0, 0.3f) == 1.0f);
    CHECK(cover("exclaim", 0, -0.45f) == 0.0f);
    CHECK(cover("exclaim", 0, -0.7f) == 1.0f);
    // A crescent moon is open on the right.
    CHECK(cover("moon", -0.6f, 0) == 1.0f);
    CHECK(cover("moon", 0.3f, 0.1f) == 0.0f);
    // A tick: short stroke down on the left, long stroke up to the right.
    CHECK(cover("check", -0.3f, -0.4f) == 1.0f);
    CHECK(cover("check", 0.6f, 0.5f) == 1.0f);
    CHECK(cover("check", -0.5f, 0.5f) == 0.0f);

    // The plain figures are plain: one flat colour, no shading or shine,
    // so they take a layer's colour exactly.
    for (const char* name : {"triangle", "pentagon", "hexagon", "octagon", "oval", "capsule", "plus", "frame"}) {
        INFO(name);
        const SpriteShape shape = numbered(name);
        for (int yi = -9; yi <= 9; ++yi) {
            for (int xi = -9; xi <= 9; ++xi) {
                const ShapeSample s = sampleShape(shape, static_cast<float>(xi) / 10.0f,
                                                  static_cast<float>(yi) / 10.0f, 0.01f, 0.01f);
                REQUIRE(s.tone == 0.0f);
                REQUIRE(s.shine == 0.0f);
            }
        }
    }
    // The treasures are not: a coin and a gem are lit and shine somewhere.
    for (const char* name : {"coin", "gem", "trophy", "crown"}) {
        INFO(name);
        const SpriteShape shape = numbered(name);
        float shine = 0.0f, light = 0.0f, dark = 0.0f;
        for (int yi = -45; yi <= 45; ++yi) {
            for (int xi = -45; xi <= 45; ++xi) {
                const ShapeSample s = sampleShape(shape, static_cast<float>(xi) / 50.0f,
                                                  static_cast<float>(yi) / 50.0f, 0.01f, 0.01f);
                if (s.cover > 0.5f) {
                    shine = std::max(shine, s.shine);
                    light = std::max(light, s.tone);
                    dark = std::min(dark, s.tone);
                }
            }
        }
        CHECK(shine + light > 0.3f);
        CHECK(dark < -0.1f);
    }
}

TEST_CASE("a library shape's picture is the shape, drawn once and kept", "[shapes][library][picture]") {
    const int leaf = shapeNumber("leaf");
    const std::shared_ptr<const Image> picture = shapePicture(leaf);
    REQUIRE(picture);
    REQUIRE(picture->width == kShapePictureSize);
    REQUIRE(picture->height == kShapePictureSize);
    REQUIRE(picture->rgba.size() == static_cast<std::size_t>(kShapePictureSize) * kShapePictureSize * 4u);
    // Asked for again, it is the very same picture.
    CHECK(shapePicture(leaf).get() == picture.get());
    CHECK(shapePicture(shapeNumber("coin")).get() != picture.get());
    // Small previews keep small pictures of their own.
    const std::shared_ptr<const Image> little = shapePicture(leaf, 64);
    REQUIRE(little->width == 64);
    CHECK(shapePicture(leaf, 64).get() == little.get());
    CHECK(shapePicture(leaf).get() == picture.get());
    CHECK(shapePicture(leaf, 100000)->width == kShapePictureSize);
    CHECK(shapePicture(leaf, 1)->width == 8);

    // Every pixel is the formula at that pixel's centre: tone in red (128
    // for none), shine in green, coverage in blue, and alpha always full.
    const float pixel = 2.0f / static_cast<float>(kShapePictureSize);
    int covered = 0;
    for (int row = 3; row < kShapePictureSize; row += 7) {
        for (int col = 2; col < kShapePictureSize; col += 5) {
            const float x = (static_cast<float>(col) + 0.5f) * pixel - 1.0f;
            const float y = 1.0f - (static_cast<float>(row) + 0.5f) * pixel;  // row 0 is the top
            const ShapeSample s = sampleLibraryShape(leaf, x, y, pixel, pixel);
            const std::uint8_t* p = &picture->rgba[(static_cast<std::size_t>(row) * kShapePictureSize +
                                                    static_cast<std::size_t>(col)) * 4u];
            REQUIRE(p[0] == static_cast<std::uint8_t>(std::lround((s.tone * 0.5f + 0.5f) * 255.0f)));
            REQUIRE(p[1] == static_cast<std::uint8_t>(std::lround(s.shine * 255.0f)));
            REQUIRE(p[2] == static_cast<std::uint8_t>(std::lround(s.cover * 255.0f)));
            REQUIRE(p[3] == 255);
            covered += p[2] > 127 ? 1 : 0;
        }
    }
    CHECK(covered > 300);

    // Any size can be asked for, and the corners of a picture are empty.
    const Image small = drawShapePicture(leaf, 64);
    REQUIRE(small.width == 64);
    REQUIRE(small.rgba.size() == 64u * 64u * 4u);
    CHECK(small.rgba[2] == 0);
    CHECK(small.rgba[0] == 128);
    CHECK(drawShapePicture(leaf, 0).width == 1);
    // A formula shape can be asked for too (the picker shows them all).
    const Image disc = drawShapePicture(static_cast<int>(SpriteShape::Disc), 32);
    CHECK(disc.rgba[(16u * 32u + 16u) * 4u + 2u] == 255);
    CHECK(disc.rgba[2] == 0);

    // The ID a shape's picture goes by is its own, and no asset's.
    std::set<Id> ids;
    for (int i = 0; i < shapeCount(); ++i) {
        const Id id = shapeTextureId(i);
        CHECK(id.valid());
        // Only a library shape is drawn from a picture.
        CHECK(shapeFromTextureId(id) == (isLibraryShape(i) ? i : -1));
        CHECK(ids.insert(id).second);
    }
    CHECK(shapeFromTextureId(Id{}) == -1);
    IdGenerator assets(77);
    for (int i = 0; i < 1000; ++i) {
        REQUIRE(shapeFromTextureId(assets.next()) == -1);
    }
}

TEST_CASE("the mesh sends a library shape to its picture", "[shapes][library][mesh]") {
    const SpriteShape leaf = numbered("leaf"), coin = numbered("coin");
    SpriteMesh mesh;

    // A formula shape has no picture.
    buildSpriteMesh(frameOf({SpriteShape::Heart}), square(200, 4), mesh);
    REQUIRE(mesh.runs.size() == 1);
    CHECK_FALSE(mesh.runs[0].texture.valid());

    // A library shape's run carries the shape's picture, and its corners
    // the shape's number.
    buildSpriteMesh(frameOf({leaf}), square(200, 4), mesh);
    REQUIRE(mesh.runs.size() == 1);
    CHECK(mesh.runs[0].texture == shapeTextureId(static_cast<int>(leaf)));
    CHECK(mesh.runs[0].indexCount == 6);
    REQUIRE(mesh.vertices.size() == 4);
    for (const SpriteVertex& v : mesh.vertices) {
        CHECK(v.shape == static_cast<float>(leaf));
    }

    // Layers are drawn in their order, each run with its own picture; two
    // layers with the same shape next to each other share one run.
    buildSpriteMesh(frameOf({leaf, leaf, SpriteShape::Disc, coin, leaf}), square(200, 4), mesh);
    REQUIRE(mesh.runs.size() == 4);
    CHECK(mesh.runs[0].texture == shapeTextureId(static_cast<int>(leaf)));
    CHECK(mesh.runs[0].indexCount == 12);
    CHECK_FALSE(mesh.runs[1].texture.valid());
    CHECK(mesh.runs[2].texture == shapeTextureId(static_cast<int>(coin)));
    CHECK(mesh.runs[3].texture == shapeTextureId(static_cast<int>(leaf)));
    std::uint32_t next = 0;
    for (const SpriteRun& run : mesh.runs) {
        CHECK(run.firstIndex == next);
        next += run.indexCount;
    }
    CHECK(next == mesh.indices.size());
}

TEST_CASE("the picture renderer draws a library shape", "[shapes][library][picture]") {
    SpriteMesh mesh;
    // A red plus sign, 50 pixels from centre to edge, on blue.
    buildSpriteMesh(frameOf({numbered("plus")}), square(100, 2), mesh);
    const Picture plus = drawPicture(mesh, 100, 100, ScreenColor{0, 0, 1});
    CHECK(plus.pixel(50, 50)[0] == 255);
    CHECK(plus.pixel(50, 50)[2] == 0);
    CHECK(plus.pixel(80, 50)[0] == 255);  // an arm to the right
    CHECK(plus.pixel(50, 20)[0] == 255);  // and one up
    CHECK(plus.pixel(80, 20)[0] == 0);    // nothing between them
    CHECK(plus.pixel(80, 20)[2] == 255);

    // Up is up: a droplet's point is at the top of the picture.
    buildSpriteMesh(frameOf({numbered("droplet")}), square(100, 2), mesh);
    const Picture drop = drawPicture(mesh, 100, 100, ScreenColor{0, 0, 1});
    CHECK(drop.pixel(65, 70)[2] == 0);    // round below
    CHECK(drop.pixel(65, 25)[2] == 255);  // pointed above

    // A number past the end of the list draws the soft dot, as ever.
    buildSpriteMesh(frameOf({static_cast<SpriteShape>(shapeCount() + 3)}), square(100, 2), mesh);
    const Picture past = drawPicture(mesh, 100, 100, ScreenColor{0, 0, 1});
    CHECK(past.pixel(50, 50)[0] == 255);
    CHECK(past.pixel(2, 2)[0] == 0);
}

TEST_CASE("a layer is given a library shape by name, and keeps it through a save", "[shapes][library][session]") {
    const Effect effect = drawing("trophy");
    const auto program = compileLayer(effect, effect.layers[0]);
    CHECK(static_cast<int>(program->spriteShape) == shapeNumber("trophy"));

    // Every name the list offers compiles to its own number.
    Effect each = drawing("soft");
    for (int i = 0; i < shapeCount(); ++i) {
        for (Module& m : each.layers[0].modules) {
            if (m.type == "sprite") {
                *m.find("shape") = Value{std::string(shapeInfo(i).name)};
            }
        }
        REQUIRE(static_cast<int>(compileLayer(each, each.layers[0])->spriteShape) == i);
    }

    // Through the editor's Shape control, with undo.
    Session session;
    const Id layer = session.effect().layers[0].id;
    Id control;
    for (const ControlView& c : session.controls(layer)) {
        if (c.label == "Shape") {
            control = c.control;
        }
    }
    REQUIRE(control.valid());
    REQUIRE(session.setControl(layer, control, Value{std::string("leaf")}).ok());
    CHECK(static_cast<int>(compileLayer(session.effect(), session.effect().layers[0])->spriteShape) ==
          shapeNumber("leaf"));
    CHECK_FALSE(session.setControl(layer, control, Value{std::string("not a shape")}).ok());
    const std::string saved = writeEffect(session.effect());
    CHECK(saved.find("\"shape\": \"leaf\"") != std::string::npos);
    const auto loaded = readEffect(saved);
    REQUIRE(loaded.ok());
    CHECK_FALSE(loaded.value().repaired);
    CHECK(writeEffect(loaded.value().effect) == saved);
    REQUIRE(session.undo().ok());
    CHECK(compileLayer(session.effect(), session.effect().layers[0])->spriteShape == SpriteShape::Soft);
}

TEST_CASE("Unity is sent a picture for each library shape an effect uses", "[shapes][library][unity]") {
    const Effect effect = drawing("leaf", "heart");
    const J root = J::parse(unityDescription(effect));
    const J& leaf = root["layers"][0]["render"];
    const J& heart = root["layers"][1]["render"];
    // The library shape is drawn from its own picture, as an atlas of one.
    CHECK(leaf["shape"] == 0);
    CHECK(leaf["shapeName"] == "leaf");
    CHECK(leaf["shapePicture"] == "Assets/VFXForge/Shapes/leaf.png");
    CHECK_FALSE(leaf.contains("picture"));
    // A formula shape still comes from the atlas.
    CHECK(heart["shape"] == static_cast<int>(SpriteShape::Heart));
    CHECK_FALSE(heart.contains("shapePicture"));

    const auto numbers = [](const Effect& e) {
        std::vector<int> out;
        for (const UnityShape& u : unityShapePictures(e)) {
            CHECK_FALSE(u.edit.any());
            out.push_back(u.shape);
        }
        return out;
    };
    CHECK(numbers(effect) == std::vector<int>{shapeNumber("leaf")});
    CHECK(numbers(drawing("heart")).empty());
    CHECK(numbers(drawing("trophy", "coin")) == std::vector<int>{shapeNumber("coin"), shapeNumber("trophy")});
    CHECK(numbers(drawing("coin", "coin")).size() == 1);

    int shapeFiles = 0;
    std::string png;
    for (const TarEntry& f : unityExportFiles(effect)) {
        if (f.path.rfind("Assets/VFXForge/Shapes/", 0) == 0) {
            ++shapeFiles;
            CHECK(f.path == "Assets/VFXForge/Shapes/leaf.png");
            png = f.bytes;
        }
    }
    REQUIRE(shapeFiles == 1);
    for (const TarEntry& f : unityExportFiles(drawing("heart"))) {
        CHECK(f.path.rfind("Assets/VFXForge/Shapes/", 0) != 0);
    }

    // The picture is laid out like a tile of the atlas: tone in red and
    // blue, shine in green, coverage in alpha.
    const auto decoded = decodePng(png);
    REQUIRE(decoded.ok());
    const Image& image = decoded.value();
    REQUIRE(image.width == kShapePictureSize);
    REQUIRE(image.height == kShapePictureSize);
    const std::shared_ptr<const Image> ours = shapePicture(shapeNumber("leaf"));
    int solid = 0;
    for (std::size_t i = 0; i < image.rgba.size(); i += 4 * 13) {
        REQUIRE(image.rgba[i] == ours->rgba[i]);
        REQUIRE(image.rgba[i + 1] == ours->rgba[i + 1]);
        REQUIRE(image.rgba[i + 2] == ours->rgba[i]);
        REQUIRE(image.rgba[i + 3] == ours->rgba[i + 2]);
        solid += image.rgba[i + 3] == 255 ? 1 : 0;
    }
    CHECK(solid > 1000);
    CHECK(image.rgba[3] == 0);  // an empty corner is see-through
    CHECK(png.size() < 400'000u);
}

TEST_CASE("the presets made with the library use it", "[shapes][library][presets]") {
    IdGenerator ids(5);
    const IdSource newId = [&ids]() { return ids.next(); };
    const auto shapesOf = [&](const char* id) {
        auto made = makePreset(id, newId);
        REQUIRE(made.ok());
        std::set<std::string> names;
        for (const Layer& layer : made.value().layers) {
            names.insert(shapeName(compileLayer(made.value(), layer)->spriteShape));
        }
        return names;
    };
    CHECK(shapesOf("falling-leaves").count("leaf") == 1);
    CHECK(shapesOf("trophy-win").count("trophy") == 1);
    CHECK(shapesOf("rune-ward").count("runea") == 1);
    CHECK(shapesOf("rune-ward").count("hexshield") == 1);
    CHECK(shapesOf("buff-up").count("chevron") == 1);
    CHECK(shapesOf("sleep").count("zed") == 1);
    CHECK(shapesOf("poisoned").count("skull") == 1);
    CHECK(shapesOf("stunned").count("spiral") == 1);
    CHECK(shapesOf("alert").count("exclaim") == 1);
    CHECK(shapesOf("coin-burst").count("coin") == 1);
    CHECK(shapesOf("snow").count("snowflake") == 1);

    int status = 0;
    for (const PresetInfo& info : presets()) {
        status += info.category == "Status" ? 1 : 0;
    }
    CHECK(status == 5);
    CHECK(presets().size() == 135);
}
