// Shapes that can be changed: which, how far, what each setting does, and
// how a changed shape reaches the picture renderer, the graphics card and
// Unity.
#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "vfx/Metadata.h"
#include "vfx/Program.h"
#include "vfx/Serialize.h"
#include "vfx/Simulation.h"
#include "vfx/Templates.h"
#include "vfx/editor/Picture.h"
#include "vfx/editor/ShapeLibrary.h"
#include "vfx/editor/Shapes.h"
#include "vfx/editor/SpriteMesh.h"
#include "vfx/editor/UnityExport.h"

using namespace vfx;
using namespace vfx::editor;
using J = nlohmann::json;

namespace {

int numbered(const char* name) {
    const int number = shapeNumber(name);
    REQUIRE(number >= 0);
    return number;
}

ShapeEdit edit(int count, float thickness = 0, float sharpness = 0, float round = 0) {
    ShapeEdit e;
    e.count = count;
    e.thickness = thickness;
    e.sharpness = sharpness;
    e.round = round;
    return e;
}

float cover(const char* name, const ShapeEdit& e, float x, float y) {
    return sampleShapeEdited(numbered(name), e, x, y, 0.01f, 0.01f).cover;
}

// How much of the square a shape covers, in points of a 101 by 101 grid.
float area(int number, const ShapeEdit& e) {
    float total = 0.0f;
    for (int yi = -50; yi <= 50; ++yi) {
        for (int xi = -50; xi <= 50; ++xi) {
            total += sampleShapeEdited(number, e, static_cast<float>(xi) / 50.0f, static_cast<float>(yi) / 50.0f,
                                       0.02f, 0.02f).cover;
        }
    }
    return total;
}

// How many separate stretches of the circle of this radius the shape covers.
int stretches(const char* name, const ShapeEdit& e, float radius) {
    const int steps = 1440;
    int count = 0;
    bool before = cover(name, e, radius * std::cos(-0.001f), radius * std::sin(-0.001f)) > 0.5f;
    for (int i = 0; i < steps; ++i) {
        const float a = (static_cast<float>(i) + 0.37f) / static_cast<float>(steps) * 6.2831853f;
        const bool now = cover(name, e, radius * std::cos(a), radius * std::sin(a)) > 0.5f;
        count += now && !before ? 1 : 0;
        before = now;
    }
    return count;
}

RenderFrame frameOf(SpriteShape shape, const ShapeEdit& e) {
    RenderFrame frame;
    frame.flat = true;
    SpriteInstance p;
    p.size = 2.0f;
    p.r = 1.0f;
    p.g = 0.0f;
    p.b = 0.0f;
    frame.instances.push_back(p);
    RenderBatch batch;
    batch.shape = shape;
    batch.shapeEdit = e;
    batch.count = 1;
    frame.batches.push_back(batch);
    return frame;
}

View square(int pixels, float units) {
    View v;
    v.width = v.height = static_cast<float>(pixels);
    v.centerX = v.centerY = 0;
    v.unitsHigh = units;
    return v;
}

// An effect of basic emitters, each drawing a shape with settings changed.
struct Drawn {
    const char* shape;
    int count;
    double thickness, sharpness, round;
};

Effect drawing(std::initializer_list<Drawn> layers) {
    IdGenerator ids(31);
    const IdSource newId = [&ids]() { return ids.next(); };
    Effect effect = makeEmptyEffect(newId, "Edits", false);
    effect.duration = 2.0;
    for (const Drawn& d : layers) {
        Layer layer = makeBasicEmitter(newId, d.shape);
        layer.duration = 2.0;
        for (Module& m : layer.modules) {
            if (m.type == "sprite") {
                *m.find("shape") = Value{std::string(d.shape)};
                *m.find("shapeCount") = Value{static_cast<std::int64_t>(d.count)};
                *m.find("shapeThickness") = Value{d.thickness};
                *m.find("shapeSharpness") = Value{d.sharpness};
                *m.find("shapeRound") = Value{d.round};
            }
        }
        effect.layers.push_back(layer);
    }
    return effect;
}

}  // namespace

TEST_CASE("some shapes can be changed, and say how", "[shapes][edits]") {
    int editable = 0;
    for (int i = 0; i < shapeCount(); ++i) {
        const ShapeEditInfo& info = shapeEditInfo(i);
        INFO(shapeInfo(i).name);
        if (!info.any()) {
            CHECK_FALSE(usableEdit(i, edit(7, 1, 1, 1)).any());
            CHECK_FALSE(drawnFromPicture(i, edit(7, 1, 1, 1)) != isLibraryShape(i));
            continue;
        }
        ++editable;
        if (info.count()) {
            CHECK(info.countLowest >= 3);
            CHECK(info.countHighest <= kShapeEditMaxCount);
            CHECK(info.countOwn >= info.countLowest);
            CHECK(info.countOwn <= info.countHighest);
            CHECK(std::string(info.countLabel).size() >= 4);
        } else {
            CHECK(info.countLowest == 0);
        }
        if (info.sharperToo) {
            CHECK(info.round);
        }
    }
    CHECK(editable >= 20);
    // Three of the first thirty-five among them.
    CHECK(shapeEditInfo(numbered("star")).count());
    CHECK(std::string(shapeEditInfo(numbered("star")).countLabel) == "Points");
    CHECK(shapeEditInfo(numbered("ring")).thickness);
    CHECK(shapeEditInfo(numbered("square")).round);
    CHECK(std::string(shapeEditInfo(numbered("gear")).countLabel) == "Teeth");
    CHECK_FALSE(shapeEditInfo(numbered("leaf")).any());
    CHECK_FALSE(shapeEditInfo(-1).any());
    CHECK_FALSE(shapeEditInfo(100000).any());
}

TEST_CASE("a change is held to what the shape can use", "[shapes][edits]") {
    const int star = numbered("star"), ring = numbered("ring"), triangle = numbered("triangle");
    // Settings a shape does not have are dropped.
    CHECK(usableEdit(ring, edit(9, 0.5f, 0.5f, 0.5f)) == edit(0, usableEdit(ring, edit(0, 0.5f)).thickness));
    CHECK(usableEdit(star, edit(0, 0.5f)).thickness == 0.0f);
    // The count is held to the shape's range, and its own count is "as drawn".
    CHECK(usableEdit(star, edit(7)).count == 7);
    CHECK(usableEdit(star, edit(5)).count == 0);
    CHECK(usableEdit(star, edit(1)).count == 3);
    CHECK(usableEdit(star, edit(99)).count == 16);
    CHECK(usableEdit(star, edit(-4)).count == 0);
    // Amounts are held to -1..1 and to whole steps.
    CHECK(usableEdit(star, edit(0, 0, 5.0f)).sharpness == 1.0f);
    CHECK(usableEdit(star, edit(0, 0, -5.0f)).sharpness == -1.0f);
    const float step = usableEdit(star, edit(0, 0, 0.5f)).sharpness;
    CHECK(step == Catch::Approx(0.5f).margin(1.0f / kShapeEditSteps));
    CHECK(std::round(step * kShapeEditSteps) == Catch::Approx(step * kShapeEditSteps).margin(1e-4));
    CHECK(usableEdit(star, edit(0, 0, 0.003f)).sharpness == 0.0f);
    CHECK(usableEdit(star, edit(0, 0, std::nanf(""))).sharpness == 0.0f);
    // A figure with sharp corners cannot be made sharper.
    CHECK(usableEdit(triangle, edit(0, 0, 0, -0.5f)).round == 0.0f);
    CHECK(usableEdit(triangle, edit(0, 0, 0, 0.5f)).round > 0.4f);
    CHECK(usableEdit(star, edit(0, 0, 0, -0.5f)).round < -0.4f);
    // Asking twice changes nothing more.
    for (int i = 0; i < shapeCount(); ++i) {
        const ShapeEdit once = usableEdit(i, edit(7, 0.31f, -0.77f, 0.5f));
        REQUIRE(usableEdit(i, once) == once);
    }
}

TEST_CASE("with nothing changed a shape is the shape as drawn, and a little change is a little change",
          "[shapes][edits]") {
    const float one = 1.0f / kShapeEditSteps;
    for (int i = 0; i < shapeCount(); ++i) {
        const ShapeEditInfo& info = shapeEditInfo(i);
        if (!info.any()) {
            continue;
        }
        INFO(shapeInfo(i).name);
        // Exactly the same, point for point.
        for (int k = 0; k < 400; ++k) {
            const float x = static_cast<float>((k * 37) % 101) / 50.0f - 1.0f;
            const float y = static_cast<float>((k * 61) % 101) / 50.0f - 1.0f;
            const ShapeSample a = sampleShapeEdited(i, ShapeEdit{}, x, y, 0.02f, 0.02f);
            const ShapeSample b = sampleShape(static_cast<SpriteShape>(i), x, y, 0.02f, 0.02f);
            REQUIRE(a.cover == b.cover);
            REQUIRE(a.tone == b.tone);
            REQUIRE(a.shine == b.shine);
        }
        // One step of a slider away from "as drawn" is nearly the same
        // shape: the changed drawing starts from the original's own
        // measurements, with no jump.
        const float whole = area(i, ShapeEdit{});
        for (const ShapeEdit& e : {edit(0, one), edit(0, -one), edit(0, 0, one), edit(0, 0, -one), edit(0, 0, 0, one),
                                   edit(0, 0, 0, -one)}) {
            if (!usableEdit(i, e).any()) {
                continue;
            }
            CAPTURE(e.thickness, e.sharpness, e.round);
            CHECK(area(i, e) == Catch::Approx(whole).epsilon(0.12));
        }
    }
}

TEST_CASE("a changed shape stays inside its particle, however far it is changed", "[shapes][edits]") {
    for (int i = 0; i < shapeCount(); ++i) {
        const ShapeEditInfo& info = shapeEditInfo(i);
        if (!info.any()) {
            continue;
        }
        INFO(shapeInfo(i).name);
        std::vector<int> counts{0};
        if (info.count()) {
            counts = {info.countLowest, info.countOwn, (info.countLowest + info.countHighest) / 2, info.countHighest};
        }
        const std::vector<float> ends{-1.0f, 0.0f, 1.0f}, none{0.0f};
        for (const int count : counts) {
            for (const float thickness : info.thickness ? ends : none) {
                for (const float sharpness : info.sharpness ? ends : none) {
                    for (const float round : info.round ? ends : none) {
                        const ShapeEdit e = edit(count, thickness, sharpness, round);
                        CAPTURE(count, thickness, sharpness, round);
                        float most = 0.0f, atEdge = 0.0f;
                        for (int yi = -25; yi <= 25; ++yi) {
                            for (int xi = -25; xi <= 25; ++xi) {
                                const ShapeSample s = sampleShapeEdited(i, e, static_cast<float>(xi) / 25.0f,
                                                                        static_cast<float>(yi) / 25.0f, 0.02f, 0.02f);
                                REQUIRE(std::isfinite(s.cover));
                                REQUIRE(std::isfinite(s.tone));
                                REQUIRE(std::isfinite(s.shine));
                                REQUIRE(s.cover >= 0.0f);
                                REQUIRE(s.cover <= 1.0f);
                                REQUIRE(s.tone >= -1.0f);
                                REQUIRE(s.tone <= 1.0f);
                                most = std::max(most, s.cover);
                                if (std::abs(xi) == 25 || std::abs(yi) == 25) {
                                    atEdge = std::max(atEdge, s.cover);
                                }
                            }
                        }
                        CHECK(atEdge < 0.01f);
                        CHECK(most > 0.8f);
                    }
                }
            }
        }
    }
}

TEST_CASE("each setting does what it says", "[shapes][edits]") {
    // The count is the number of points, sides, dashes, rays, petals, teeth.
    for (const int n : {3, 4, 7, 12}) {
        CAPTURE(n);
        CHECK(stretches("star", edit(n), 0.72f) == n);
        CHECK(stretches("starfat", edit(n), 0.8f - 0.02f * static_cast<float>(12 - n)) == n);
        CHECK(stretches("starfour", edit(n), 0.72f) == n);
    }
    CHECK(stretches("star", edit(16), 0.72f) == 16);
    CHECK(stretches("star", ShapeEdit{}, 0.72f) == 5);
    for (const int n : {3, 4, 5, 9, 12}) {
        CAPTURE(n);
        // A circle that passes inside every corner and outside every side
        // is covered once at each corner.
        const float between = 0.5f * (1.0f + std::cos(3.14159265f / static_cast<float>(n)));
        CHECK(stretches("hexagon", edit(n), 0.9062f * between) == n);
        CHECK(stretches("triangle", edit(n), 0.98f * between) == n);
        CHECK(stretches("hexframe", edit(n), 0.9324f * between) == n);
    }
    for (const int n : {3, 10, 24}) {
        CAPTURE(n);
        CHECK(stretches("dashring", edit(n), 0.8f) == n);
    }
    for (const int n : {5, 12, 24}) {
        CAPTURE(n);
        CHECK(stretches("sunburst", edit(n), 0.8f) == n);
    }
    for (const int n : {3, 5, 12}) {
        CAPTURE(n);
        CHECK(stretches("flower", edit(n), 0.8f) == n);
    }
    for (const int n : {5, 8, 16}) {
        CAPTURE(n);
        CHECK(stretches("gear", edit(n), 0.85f) == n);
    }

    // Thickness: a ring thinned is still a ring; thickened all the way it
    // is a disc. Its outside stays where it was.
    CHECK(cover("ring", ShapeEdit{}, 0, 0) == 0.0f);
    CHECK(cover("ring", edit(0, 1), 0, 0) == 1.0f);
    CHECK(cover("ring", edit(0, -1), 0, 0) == 0.0f);
    CHECK(cover("ring", edit(0, 1), 0.95f, 0) == 0.0f);
    CHECK(cover("ring", edit(0, 0.5f), 0.89f, 0) == 1.0f);
    CHECK(area(numbered("ring"), edit(0, -1)) < 0.5f * area(numbered("ring"), ShapeEdit{}));
    for (const char* name : {"ring", "frame", "hexframe", "arc", "dashring", "doublering", "starline", "plus", "cross"}) {
        INFO(name);
        const int number = numbered(name);
        float before = 0.0f;
        for (const float t : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
            const float now = area(number, edit(0, t));
            CHECK(now > before);  // thicker covers more, all the way along
            before = now;
        }
    }
    CHECK(cover("frame", edit(0, 1), 0, 0) == 1.0f);
    CHECK(cover("plus", edit(0, -1), 0.3f, 0.2f) == 0.0f);  // a thin arm misses what the drawn one covers
    CHECK(cover("plus", ShapeEdit{}, 0.3f, 0.2f) == 1.0f);

    // Pointiness: the notches move in, the points stay where they are.
    for (const char* name : {"star", "starfour", "starsix", "stareight", "starfat"}) {
        INFO(name);
        const int number = numbered(name);
        float before = 1e9f;
        for (const float t : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
            const float now = area(number, edit(0, 0, t));
            CHECK(now < before);  // more pointed covers less
            before = now;
        }
    }
    CHECK(cover("star", edit(0, 0, 1), 0, 0.8f) == 1.0f);   // the point is still there
    CHECK(cover("star", edit(0, 0, -1), 0, 0.8f) == 1.0f);
    CHECK(cover("star", edit(0, 0, 1), 0.3f, -0.1f) == 0.0f);
    CHECK(cover("star", edit(0, 0, -1), 0.3f, -0.1f) == 1.0f);

    // Roundness: a square with its corners sharp, as drawn, and all the way
    // to a circle. Its sides stay where they were.
    CHECK(cover("square", edit(0, 0, 0, -1), 0.76f, 0.76f) == 1.0f);
    CHECK(cover("square", ShapeEdit{}, 0.76f, 0.76f) == 0.0f);
    CHECK(cover("square", ShapeEdit{}, 0.7f, 0.7f) == 1.0f);
    CHECK(cover("square", edit(0, 0, 0, 1), 0.6f, 0.6f) == 0.0f);
    for (const float r : {-1.0f, 0.0f, 1.0f}) {
        CHECK(cover("square", edit(0, 0, 0, r), 0.74f, 0) == 1.0f);
        CHECK(cover("square", edit(0, 0, 0, r), 0.82f, 0) == 0.0f);
    }
    // A triangle rounded all the way is the circle that fits inside it.
    CHECK(cover("triangle", edit(0, 0, 0, 1), 0, 0.5f) == 0.0f);
    CHECK(cover("triangle", ShapeEdit{}, 0, 0.5f) == 1.0f);
    CHECK(cover("triangle", edit(0, 0, 0, 1), 0, -0.6f) == 1.0f);  // the base has not moved
    CHECK(cover("triangle", edit(0, 0, 0, 1), 0.6f, -0.6f) == 0.0f);
    for (const char* name : {"triangle", "pentagon", "hexagon", "octagon", "square", "plus"}) {
        INFO(name);
        const int number = numbered(name);
        CHECK(area(number, edit(0, 0, 0, 1)) < area(number, edit(0, 0, 0, 0.5f)));
        CHECK(area(number, edit(0, 0, 0, 0.5f)) < area(number, ShapeEdit{}));
    }
    // A star is rounded the other way about: its points stay where they
    // are and it fills out, so a rounder star covers more.
    for (const char* name : {"star", "starfour", "starfat"}) {
        INFO(name);
        const int number = numbered(name);
        CHECK(area(number, edit(0, 0, 0, 1)) > area(number, ShapeEdit{}));
        CHECK(area(number, edit(0, 0, 0, -1)) <= area(number, ShapeEdit{}));
        CHECK(cover(name, edit(0, 0, 0, 1), 0, 0.8f) == 1.0f);
    }
}

TEST_CASE("a changed shape goes to a picture of its own", "[shapes][edits][mesh]") {
    const int star = numbered("star"), leaf = numbered("leaf"), triangle = numbered("triangle");
    // IDs say which shape and which change, and give both back.
    std::set<Id> ids;
    for (const ShapeEdit& e : {edit(7), edit(7, 0, 0.5f), edit(0, 0, -1), edit(16, 0, 1, 1), edit(0, 0, 0, -1)}) {
        const Id id = shapeTextureId(star, e);
        ShapeEdit back;
        CHECK(shapeFromTextureId(id, &back) == star);
        CHECK(back == usableEdit(star, e));
        CHECK(ids.insert(id).second);
    }
    // One of the first thirty-five, unchanged, has no picture.
    CHECK(shapeFromTextureId(shapeTextureId(star)) == -1);
    CHECK(shapeTextureId(star, edit(5)) == shapeTextureId(star));  // five points is the star as drawn
    CHECK_FALSE(drawnFromPicture(star, ShapeEdit{}));
    CHECK_FALSE(drawnFromPicture(star, edit(5)));
    CHECK(drawnFromPicture(star, edit(6)));
    CHECK_FALSE(drawnFromPicture(static_cast<int>(SpriteShape::Soft), edit(6, 1, 1, 1)));
    // A library shape always has one; a change it cannot use is no change.
    CHECK(shapeTextureId(leaf, edit(7, 1, 1, 1)) == shapeTextureId(leaf));
    ShapeEdit back = edit(3);
    CHECK(shapeFromTextureId(shapeTextureId(triangle, edit(6)), &back) == triangle);
    CHECK(back.count == 6);
    CHECK(shapeFromTextureId(shapeTextureId(triangle), &back) == triangle);
    CHECK_FALSE(back.any());

    SpriteMesh mesh;
    buildSpriteMesh(frameOf(SpriteShape::Star, ShapeEdit{}), square(100, 2), mesh);
    REQUIRE(mesh.runs.size() == 1);
    CHECK_FALSE(mesh.runs[0].texture.valid());
    buildSpriteMesh(frameOf(SpriteShape::Star, edit(7)), square(100, 2), mesh);
    REQUIRE(mesh.runs.size() == 1);
    CHECK(mesh.runs[0].texture == shapeTextureId(star, edit(7)));
    CHECK(mesh.vertices[0].shape == static_cast<float>(SpriteShape::Star));
    buildSpriteMesh(frameOf(SpriteShape::Soft, edit(7, 1, 1, 1)), square(100, 2), mesh);
    CHECK_FALSE(mesh.runs[0].texture.valid());

    // The picture renderer draws the change: a red ring, thickened into a
    // disc, covers the middle of the picture.
    buildSpriteMesh(frameOf(SpriteShape::Ring, ShapeEdit{}), square(100, 2), mesh);
    CHECK(drawPicture(mesh, 100, 100, ScreenColor{0, 0, 1}).pixel(50, 50)[0] == 0);
    buildSpriteMesh(frameOf(SpriteShape::Ring, edit(0, 1)), square(100, 2), mesh);
    const Picture disc = drawPicture(mesh, 100, 100, ScreenColor{0, 0, 1});
    CHECK(disc.pixel(50, 50)[0] == 255);
    CHECK(disc.pixel(50, 50)[2] == 0);
    CHECK(disc.pixel(2, 2)[2] == 255);
    // And a triangle given four sides is a square: its upper corners are filled.
    buildSpriteMesh(frameOf(static_cast<SpriteShape>(triangle), ShapeEdit{}), square(100, 2), mesh);
    CHECK(drawPicture(mesh, 100, 100, ScreenColor{0, 0, 1}).pixel(22, 22)[0] == 0);
    buildSpriteMesh(frameOf(static_cast<SpriteShape>(triangle), edit(4)), square(100, 2), mesh);
    CHECK(drawPicture(mesh, 100, 100, ScreenColor{0, 0, 1}).pixel(22, 22)[0] == 255);
}

TEST_CASE("pictures of changed shapes are kept only a few at a time", "[shapes][edits][picture]") {
    const int star = numbered("star");
    const std::shared_ptr<const Image> seven = shapePicture(star, 32, edit(7));
    REQUIRE(seven->width == 32);
    CHECK(shapePicture(star, 32, edit(7)).get() == seven.get());
    // It is the changed shape, pixel for pixel.
    const float pixel = 2.0f / 32.0f;
    for (int row = 0; row < 32; ++row) {
        for (int col = 0; col < 32; ++col) {
            const ShapeSample s = sampleShapeEdited(star, edit(7), (static_cast<float>(col) + 0.5f) * pixel - 1.0f,
                                                    1.0f - (static_cast<float>(row) + 0.5f) * pixel, pixel, pixel);
            REQUIRE(seven->rgba[(static_cast<std::size_t>(row) * 32u + static_cast<std::size_t>(col)) * 4u + 2u] ==
                    static_cast<std::uint8_t>(std::lround(s.cover * 255.0f)));
        }
    }
    // The star as drawn is kept for good, whatever else is asked for.
    const std::shared_ptr<const Image> drawn = shapePicture(star, 32);
    const Image* first = seven.get();
    const std::vector<std::uint8_t> pixels = seven->rgba;
    for (int n = 3; n <= 16; ++n) {
        shapePicture(star, 32, edit(n, 0, 0.5f));
    }
    CHECK(shapePicture(star, 32).get() == drawn.get());
    // The seven-pointed one was let go meanwhile (we still hold ours), and
    // comes back the same when asked for again.
    const std::shared_ptr<const Image> again = shapePicture(star, 32, edit(7));
    CHECK(again.get() != first);
    CHECK(again->rgba == pixels);
}

TEST_CASE("a layer keeps its shape's settings, in whole steps", "[shapes][edits][compile]") {
    const auto& sprite = *Registry::builtin().findModule("sprite");
    for (const char* key : {"shapeCount", "shapeThickness", "shapeSharpness", "shapeRound"}) {
        REQUIRE(sprite.find(key) != nullptr);
    }
    Effect effect = drawing({{"star", 7, 0.0, 0.5, -1.0}, {"soft", 0, 0, 0, 0}});
    const auto star = compileLayer(effect, effect.layers[0]);
    CHECK(star->shapeEdit.count == 7);
    CHECK(star->shapeEdit.thickness == 0.0f);
    CHECK(star->shapeEdit.sharpness == Catch::Approx(0.5f).margin(1.0f / kShapeEditSteps));
    CHECK(star->shapeEdit.round == -1.0f);
    CHECK_FALSE(compileLayer(effect, effect.layers[1])->shapeEdit.any());

    // The simulation hands them to whatever draws the frame.
    Simulation simulation(compileEffect(effect));
    simulation.seek(30);
    RenderFrame frame;
    simulation.extract(frame);
    REQUIRE(frame.batches.size() == 2);
    CHECK(frame.batches[0].shapeEdit == star->shapeEdit);
    CHECK_FALSE(frame.batches[1].shapeEdit.any());

    // They are written to the file, and read back.
    const std::string saved = writeEffect(effect);
    CHECK(saved.find("\"shapeCount\": 7") != std::string::npos);
    CHECK(saved.find("\"shapeSharpness\": 0.5") != std::string::npos);
    CHECK(saved.find("\"shapeCount\": 0") != std::string::npos);  // the soft dot's, at its default
    const auto loaded = readEffect(saved);
    REQUIRE(loaded.ok());
    CHECK(writeEffect(loaded.value().effect) == saved);
    // Out of range in a file: repaired on the way in, as any setting is.
    std::string wild = saved;
    wild.replace(wild.find("\"shapeCount\": 7"), 15, "\"shapeCount\": 700");
    const auto repaired = readEffect(wild);
    REQUIRE(repaired.ok());
    CHECK(repaired.value().repaired);
}

TEST_CASE("Unity is sent a picture for each changed shape", "[shapes][edits][unity]") {
    const int star = numbered("star");
    CHECK(unityShapePicturePath(star) == "Assets/VFXForge/Shapes/star.png");
    CHECK(unityShapePicturePath(star, edit(7)) == "Assets/VFXForge/Shapes/star_7_0_0_0.png");
    CHECK(unityShapePicturePath(star, edit(0, 0, 1, -1)) == "Assets/VFXForge/Shapes/star_0_0_63_-63.png");
    CHECK(unityShapePicturePath(star, edit(5, 0.9f)) == "Assets/VFXForge/Shapes/star.png");  // nothing it can use

    const Effect effect = drawing({{"star", 7, 0, 0, 0},      // changed: its own picture
                                   {"star", 0, 0, 0, 0},      // as drawn: from the atlas
                                   {"star", 7, 0, 0, 0},      // the same change again: the same picture
                                   {"triangle", 0, 0, 0, 1},  // a library shape, rounded
                                   {"triangle", 0, 0, 0, 0}});
    const J root = J::parse(unityDescription(effect));
    const auto render = [&](std::size_t i) -> const J& { return root["layers"][i]["render"]; };
    CHECK(render(0)["shape"] == 0);
    CHECK(render(0)["shapeName"] == "star");
    CHECK(render(0)["shapePicture"] == "Assets/VFXForge/Shapes/star_7_0_0_0.png");
    CHECK(render(1)["shape"] == static_cast<int>(SpriteShape::Star));
    CHECK_FALSE(render(1).contains("shapePicture"));
    CHECK(render(2)["shapePicture"] == render(0)["shapePicture"]);
    CHECK(render(3)["shapePicture"] == "Assets/VFXForge/Shapes/triangle_0_0_0_63.png");
    CHECK(render(4)["shapePicture"] == "Assets/VFXForge/Shapes/triangle.png");

    std::vector<std::string> sent;
    std::string sevenPng;
    for (const TarEntry& f : unityExportFiles(effect)) {
        if (f.path.rfind("Assets/VFXForge/Shapes/", 0) == 0) {
            sent.push_back(f.path);
            if (f.path == "Assets/VFXForge/Shapes/star_7_0_0_0.png") {
                sevenPng = f.bytes;
            }
        }
    }
    CHECK(sent == std::vector<std::string>{"Assets/VFXForge/Shapes/star_7_0_0_0.png",
                                           "Assets/VFXForge/Shapes/triangle.png",
                                           "Assets/VFXForge/Shapes/triangle_0_0_0_63.png"});
    // The picture is the seven-pointed star, laid out as the shader expects.
    const auto decoded = decodePng(sevenPng);
    REQUIRE(decoded.ok());
    const std::shared_ptr<const Image> ours = shapePicture(star, kShapePictureSize, edit(7));
    REQUIRE(decoded.value().rgba.size() == ours->rgba.size());
    for (std::size_t i = 0; i < ours->rgba.size(); i += 4 * 17) {
        REQUIRE(decoded.value().rgba[i + 3] == ours->rgba[i + 2]);
        REQUIRE(decoded.value().rgba[i] == ours->rgba[i]);
    }
}
