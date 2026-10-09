// The built-in particle pictures, and the renderer that draws them.
#include <catch2/catch_amalgamated.hpp>

#include <cmath>
#include <cstring>
#include <string>

#include "vfx/editor/Picture.h"
#include "vfx/editor/Shapes.h"
#include "vfx/editor/SpriteMesh.h"

using namespace vfx;
using namespace vfx::editor;

namespace {

RenderFrame one(SpriteInstance p, SpriteShape shape, BlendMode blend = BlendMode::Alpha) {
    RenderFrame frame;
    frame.flat = true;
    frame.instances.push_back(p);
    RenderBatch batch;
    batch.shape = shape;
    batch.blend = blend;
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

}  // namespace

TEST_CASE("every shape stays inside its particle and has a solid part", "[shapes]") {
    const float pixel = 0.02f;
    for (int i = 0; i < kSpriteShapeCount; ++i) {
        const auto shape = static_cast<SpriteShape>(i);
        INFO(shapeName(shape));
        float most = 0.0f, total = 0.0f;
        for (int yi = -50; yi <= 50; ++yi) {
            for (int xi = -50; xi <= 50; ++xi) {
                const float x = static_cast<float>(xi) / 50.0f, y = static_cast<float>(yi) / 50.0f;
                const float c = shapeCoverage(shape, x, y, pixel, pixel);
                REQUIRE(std::isfinite(c));
                REQUIRE(c >= 0.0f);
                REQUIRE(c <= 1.0f);
                most = std::max(most, c);
                total += c;
                // Nothing reaches the edge of the square, so no shape is cut off.
                if (std::abs(xi) == 50 || std::abs(yi) == 50) {
                    REQUIRE(c < 0.01f);
                }
            }
        }
        CHECK(most > 0.8f);
        CHECK(total > 200.0f);  // not just a few stray points
    }
    CHECK(std::string(shapeName(SpriteShape::Soft)) == "soft");
    CHECK(std::string(shapeName(SpriteShape::Flame)) == "flame");
}

TEST_CASE("the shapes are the shapes they claim to be", "[shapes]") {
    const float px = 0.01f;
    auto at = [px](SpriteShape s, float x, float y) { return shapeCoverage(s, x, y, px, px); };

    CHECK(at(SpriteShape::Soft, 0, 0) == 1.0f);
    CHECK(at(SpriteShape::Soft, 0.5f, 0) == Catch::Approx(0.5625f));

    CHECK(at(SpriteShape::Disc, 0, 0) == 1.0f);
    CHECK(at(SpriteShape::Disc, 0.8f, 0) == 1.0f);
    CHECK(at(SpriteShape::Disc, 0.95f, 0) == 0.0f);

    CHECK(at(SpriteShape::Ring, 0, 0) == 0.0f);      // hollow
    CHECK(at(SpriteShape::Ring, 0.85f, 0) == 1.0f);
    CHECK(at(SpriteShape::Ring, 0, -0.85f) == 1.0f);

    CHECK(at(SpriteShape::Bubble, 0.86f, 0) == 1.0f);         // the rim
    CHECK(at(SpriteShape::Bubble, 0, 0) < 0.3f);              // see-through middle
    CHECK(at(SpriteShape::Bubble, -0.36f, 0.40f) > 0.8f);     // the shine, upper left

    CHECK(at(SpriteShape::Sparkle, 0, 0) == 1.0f);
    CHECK(at(SpriteShape::Sparkle, 0.6f, 0) > at(SpriteShape::Sparkle, 0.42f, 0.42f));  // rays, not a blob

    CHECK(at(SpriteShape::Star, 0, 0) == 1.0f);
    CHECK(at(SpriteShape::Star, 0, 0.85f) == 1.0f);   // a point straight up
    CHECK(at(SpriteShape::Star, 0, -0.7f) == 0.0f);   // and a notch straight down

    CHECK(at(SpriteShape::Square, 0.7f, 0.7f) == 1.0f);
    CHECK(at(SpriteShape::Diamond, 0.6f, 0.6f) == 0.0f);
    CHECK(at(SpriteShape::Diamond, 0, 0.85f) == 1.0f);

    CHECK(at(SpriteShape::Heart, 0, -0.5f) == 1.0f);
    CHECK(at(SpriteShape::Heart, 0, 0.8f) == 0.0f);    // the dip between the lobes
    CHECK(at(SpriteShape::Heart, 0.45f, 0.6f) == 1.0f);
    CHECK(at(SpriteShape::Heart, -0.45f, 0.6f) == 1.0f);

    // The streak's bright head is at +x, where it is going.
    CHECK(at(SpriteShape::Streak, 0.6f, 0) > 0.7f);
    CHECK(at(SpriteShape::Streak, -0.6f, 0) < 0.25f);
    // The flame is fat at the bottom and pointed at the top.
    CHECK(at(SpriteShape::Flame, 0.4f, -0.5f) > 0.3f);
    CHECK(at(SpriteShape::Flame, 0.4f, 0.5f) == 0.0f);
}

TEST_CASE("the mesh carries the shape and the pixel size", "[shapes][mesh]") {
    SpriteInstance p;
    p.size = 2.0f;
    SpriteMesh mesh;
    buildSpriteMesh(one(p, SpriteShape::Heart), square(200, 4), mesh);  // 50 pixels a unit
    REQUIRE(mesh.vertices.size() == 4);
    for (const SpriteVertex& v : mesh.vertices) {
        CHECK(v.shape == static_cast<float>(SpriteShape::Heart));
        CHECK(v.aaX == Catch::Approx(1.0f / 50.0f));  // the particle is 50 pixels from centre to edge
        CHECK(v.aaY == Catch::Approx(1.0f / 50.0f));
    }
}

TEST_CASE("a particle that follows its movement points where it is going", "[shapes][mesh]") {
    SpriteMesh mesh;
    SpriteInstance p;
    p.size = 0.4f;   // 20 pixels across at 50 pixels a unit
    p.rotation = 1.0f;  // ignored once it follows its movement

    auto corners = [&](float vx, float vy, float stretch, float& headX, float& headY, float& tailX,
                       float& tailY, float& length, float& width) {
        p.vx = vx;
        p.vy = vy;
        RenderFrame frame = one(p, SpriteShape::Streak);
        frame.batches[0].alongMotion = true;
        frame.batches[0].stretch = stretch;
        buildSpriteMesh(frame, square(200, 4), mesh);
        REQUIRE(mesh.vertices.size() == 4);
        headX = headY = tailX = tailY = 0;
        for (const SpriteVertex& v : mesh.vertices) {
            (v.u > 0.5f ? headX : tailX) += v.x * 0.5f;
            (v.u > 0.5f ? headY : tailY) += v.y * 0.5f;
        }
        length = std::hypot(headX - tailX, headY - tailY);
        width = std::hypot(mesh.vertices[1].x - mesh.vertices[2].x, mesh.vertices[1].y - mesh.vertices[2].y);
    };

    float hx, hy, tx, ty, length, width;

    // Moving right at 2 units a second, no stretch: still 20 pixels square.
    corners(2, 0, 0, hx, hy, tx, ty, length, width);
    CHECK(hx > tx);
    CHECK(hy == Catch::Approx(ty));
    CHECK(length == Catch::Approx(20.0f));
    CHECK(width == Catch::Approx(20.0f));

    // Moving up: the head is higher on screen, which is a smaller y.
    corners(0, 2, 0, hx, hy, tx, ty, length, width);
    CHECK(hy < ty);
    CHECK(hx == Catch::Approx(tx).margin(1e-3));

    // A tenth of a second of stretch at 2 units a second adds 0.2 units,
    // which is 10 pixels, to its length and nothing to its width.
    corners(2, 0, 0.1f, hx, hy, tx, ty, length, width);
    CHECK(length == Catch::Approx(30.0f).epsilon(1e-3));
    CHECK(width == Catch::Approx(20.0f));
    CHECK(mesh.vertices[0].aaX == Catch::Approx(1.0f / 15.0f).epsilon(1e-3));
    CHECK(mesh.vertices[0].aaY == Catch::Approx(1.0f / 10.0f));

    // Diagonally down-left.
    corners(-3, -3, 0, hx, hy, tx, ty, length, width);
    CHECK(hx < tx);
    CHECK(hy > ty);

    // Standing still, it falls back to its own rotation and stays square.
    corners(0, 0, 0.5f, hx, hy, tx, ty, length, width);
    CHECK(length == Catch::Approx(20.0f));
    CHECK(width == Catch::Approx(20.0f));
}

TEST_CASE("the picture renderer draws what the mesh says", "[picture]") {
    SpriteInstance p;
    p.size = 2.0f;
    p.r = 1.0f;
    p.g = 0.0f;
    p.b = 0.0f;
    SpriteMesh mesh;
    buildSpriteMesh(one(p, SpriteShape::Disc), square(100, 4), mesh);
    const Picture disc = drawPicture(mesh, 100, 100, ScreenColor{0, 0, 1});
    REQUIRE(disc.width == 100);
    REQUIRE(disc.rgba.size() == 100u * 100u * 4u);

    // Red in the middle, the blue background in the corner, opaque everywhere.
    CHECK(disc.pixel(50, 50)[0] == 255);
    CHECK(disc.pixel(50, 50)[2] == 0);
    CHECK(disc.pixel(2, 2)[0] == 0);
    CHECK(disc.pixel(2, 2)[2] == 255);
    CHECK(disc.pixel(50, 50)[3] == 255);
    // The disc's radius is 0.9 of 25 pixels: inside at 20, outside at 24.
    CHECK(disc.pixel(70, 50)[0] == 255);
    CHECK(disc.pixel(74, 50)[0] == 0);
    // The particle is two triangles; the seam between them must not show.
    for (int i = 35; i < 65; ++i) {
        REQUIRE(disc.pixel(i, i)[0] == 255);
        REQUIRE(disc.pixel(i, 99 - i)[0] == 255);
    }

    // Half-transparent: an even mix with the background.
    p.a = 0.5f;
    buildSpriteMesh(one(p, SpriteShape::Disc), square(100, 4), mesh);
    const Picture half = drawPicture(mesh, 100, 100, ScreenColor{0, 0, 1});
    CHECK(half.pixel(50, 50)[0] == Catch::Approx(128).margin(1));
    CHECK(half.pixel(50, 50)[2] == Catch::Approx(128).margin(1));

    // Additive: light is added, and the background still shows through.
    p.a = 1.0f;
    buildSpriteMesh(one(p, SpriteShape::Disc, BlendMode::Additive), square(100, 4), mesh);
    const Picture added = drawPicture(mesh, 100, 100, ScreenColor{0, 0, 1});
    CHECK(added.pixel(50, 50)[0] == 255);
    CHECK(added.pixel(50, 50)[2] == 255);

    // An empty mesh is just the background, and no size is no picture.
    mesh = SpriteMesh{};
    const Picture empty = drawPicture(mesh, 8, 4, ScreenColor{1, 1, 1});
    CHECK(empty.pixel(7, 3)[1] == 255);
    CHECK(drawPicture(mesh, 0, 10).rgba.empty());
}

TEST_CASE("a PNG is written in the right form", "[picture]") {
    Picture picture;
    picture.width = 3;
    picture.height = 2;
    picture.rgba.assign(3 * 2 * 4, 200);
    const std::string png = encodePng(picture);
    REQUIRE(png.size() > 8 + 25 + 12);
    CHECK(std::memcmp(png.data(), "\x89PNG\r\n\x1a\n", 8) == 0);
    CHECK(png.substr(12, 4) == "IHDR");
    CHECK(static_cast<unsigned char>(png[19]) == 3);  // width, big-endian
    CHECK(static_cast<unsigned char>(png[23]) == 2);  // height
    CHECK(png.find("IDAT") != std::string::npos);
    CHECK(png.substr(png.size() - 8, 4) == "IEND");
    // The well-known checksum of an empty IEND chunk proves the CRC is right.
    CHECK(std::memcmp(png.data() + png.size() - 4, "\xae\x42\x60\x82", 4) == 0);

    Picture pasted;
    pasted.width = pasted.height = 4;
    pasted.rgba.assign(4 * 4 * 4, 0);
    paste(pasted, picture, 2, 3);  // hangs off the right and the bottom
    CHECK(pasted.pixel(2, 3)[0] == 200);
    CHECK(pasted.pixel(3, 3)[0] == 200);
    CHECK(pasted.pixel(1, 3)[0] == 0);
    CHECK(pasted.pixel(3, 2)[0] == 0);

    CHECK_FALSE(writePng("unused.png", Picture{}).ok());
}

TEST_CASE("a see-through picture keeps colours straight and counts glow", "[picture]") {
    SpriteInstance p;
    p.size = 2.0f;
    p.r = 1.0f;
    p.g = 0.0f;
    p.b = 0.0f;
    p.a = 0.5f;
    SpriteMesh mesh;
    buildSpriteMesh(one(p, SpriteShape::Disc), square(100, 4), mesh);
    const Picture half = drawPictureClear(mesh, 100, 100);
    // Outside the particle: nothing at all.
    CHECK(half.pixel(2, 2)[3] == 0);
    CHECK(half.pixel(2, 2)[0] == 0);
    // Inside: the particle's own colour, at its own opacity.
    CHECK(half.pixel(50, 50)[0] == 255);
    CHECK(half.pixel(50, 50)[1] == 0);
    CHECK(half.pixel(50, 50)[3] == Catch::Approx(128).margin(1));

    // Glow (added light) still shows, by its brightness.
    p.a = 1.0f;
    p.r = p.g = p.b = 1.0f;
    buildSpriteMesh(one(p, SpriteShape::Disc, BlendMode::Additive), square(100, 4), mesh);
    const Picture glow = drawPictureClear(mesh, 100, 100);
    CHECK(glow.pixel(50, 50)[3] == 255);
    CHECK(glow.pixel(50, 50)[0] == 255);
}
