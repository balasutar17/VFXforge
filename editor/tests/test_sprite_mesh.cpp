// Turning particles into triangles: camera, colour and blending rules.
#include <catch2/catch_amalgamated.hpp>

#include <cmath>
#include <cstdlib>
#include <memory>

#include "vfx/editor/Image.h"
#include "vfx/editor/Picture.h"
#include "vfx/editor/Session.h"
#include "vfx/editor/SpriteMesh.h"

using namespace vfx;
using namespace vfx::editor;

namespace {

RenderFrame oneParticle(SpriteInstance p, BlendMode blend = BlendMode::Alpha, bool flat = true) {
    RenderFrame frame;
    frame.flat = flat;
    frame.instances.push_back(p);
    RenderBatch batch;
    batch.blend = blend;
    batch.first = 0;
    batch.count = 1;
    frame.batches.push_back(batch);
    return frame;
}

View flatView() {
    View v;
    v.width = 800;
    v.height = 600;
    v.centerX = 0;
    v.centerY = 0;
    v.unitsHigh = 6;  // 100 pixels per unit
    return v;
}

}  // namespace

TEST_CASE("a 2D particle becomes a square of the right size in the right place", "[mesh]") {
    SpriteInstance p;
    p.x = 1.0f;
    p.y = 2.0f;
    p.size = 0.5f;
    SpriteMesh mesh;
    buildSpriteMesh(oneParticle(p), flatView(), mesh);

    REQUIRE(mesh.drawn == 1);
    REQUIRE(mesh.vertices.size() == 4);
    REQUIRE(mesh.indices == std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3});

    // Centre at (400 + 100, 300 - 200); the square is 50 pixels across.
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (const auto& v : mesh.vertices) {
        minX = std::min(minX, v.x);
        maxX = std::max(maxX, v.x);
        minY = std::min(minY, v.y);
        maxY = std::max(maxY, v.y);
    }
    CHECK(minX == Catch::Approx(475.0f));
    CHECK(maxX == Catch::Approx(525.0f));
    CHECK(minY == Catch::Approx(75.0f));
    CHECK(maxY == Catch::Approx(125.0f));

    // World "up" is screen "up": the corner with v = 0 is the top of the image.
    for (const auto& v : mesh.vertices) {
        if (v.v == 0.0f) {
            CHECK(v.y == Catch::Approx(75.0f));
        } else {
            CHECK(v.y == Catch::Approx(125.0f));
        }
        CHECK(v.x == Catch::Approx(v.u == 0.0f ? 475.0f : 525.0f));
    }
}

TEST_CASE("rotation turns the square counter-clockwise on screen", "[mesh]") {
    SpriteInstance p;
    p.size = 2.0f;
    p.rotation = 3.14159265f / 2.0f;  // a quarter turn
    SpriteMesh mesh;
    buildSpriteMesh(oneParticle(p), flatView(), mesh);
    REQUIRE(mesh.vertices.size() == 4);
    // The corner that started bottom-left (u 0, v 1) ends bottom-right.
    const SpriteVertex& c = mesh.vertices[0];
    CHECK(c.u == 0.0f);
    CHECK(c.v == 1.0f);
    CHECK(c.x == Catch::Approx(500.0f).margin(1e-3));
    CHECK(c.y == Catch::Approx(400.0f).margin(1e-3));
}

TEST_CASE("alpha particles are premultiplied; additive ones have no alpha", "[mesh]") {
    SpriteInstance p;
    p.size = 1.0f;
    p.r = 1.0f;
    p.g = 0.5f;
    p.b = 0.0f;
    p.a = 0.5f;

    SpriteMesh mesh;
    RenderFrame frame = oneParticle(p, BlendMode::Alpha);
    buildSpriteMesh(frame, flatView(), mesh);
    REQUIRE(mesh.vertices.size() == 4);
    CHECK(mesh.vertices[0].r == Catch::Approx(0.5f));
    CHECK(mesh.vertices[0].g == Catch::Approx(0.5f * static_cast<float>(linearToSrgb(0.5))));
    CHECK(mesh.vertices[0].b == 0.0f);
    CHECK(mesh.vertices[0].a == Catch::Approx(0.5f));

    frame.batches[0].blend = BlendMode::Additive;
    frame.batches[0].glow = 3.0f;
    buildSpriteMesh(frame, flatView(), mesh);
    REQUIRE(mesh.vertices.size() == 4);
    CHECK(mesh.vertices[0].r == Catch::Approx(1.5f));  // glow brightens past white
    CHECK(mesh.vertices[0].a == 0.0f);
    for (const auto& v : mesh.vertices) {
        CHECK(v.r == mesh.vertices[0].r);
        CHECK(v.a == mesh.vertices[0].a);
    }
}

TEST_CASE("invisible and off-screen particles are left out", "[mesh]") {
    SpriteMesh mesh;
    SpriteInstance p;
    p.size = 1.0f;

    p.a = 0.0f;
    buildSpriteMesh(oneParticle(p), flatView(), mesh);
    CHECK(mesh.drawn == 0);

    p.a = 1.0f;
    p.size = 0.0f;
    buildSpriteMesh(oneParticle(p), flatView(), mesh);
    CHECK(mesh.drawn == 0);

    p.size = 1.0f;
    p.x = 50.0f;  // far off to the right
    buildSpriteMesh(oneParticle(p), flatView(), mesh);
    CHECK(mesh.drawn == 0);
    CHECK(mesh.vertices.empty());
    CHECK(mesh.indices.empty());

    p.x = 4.4f;  // centre just outside, edge still showing
    buildSpriteMesh(oneParticle(p), flatView(), mesh);
    CHECK(mesh.drawn == 1);

    View none = flatView();
    none.width = 0;
    buildSpriteMesh(oneParticle(p), none, mesh);
    CHECK(mesh.drawn == 0);
}

TEST_CASE("a 2D effect ignores depth", "[mesh]") {
    SpriteInstance p;
    p.size = 1.0f;
    p.z = 500.0f;
    SpriteMesh a, b;
    buildSpriteMesh(oneParticle(p), flatView(), a);
    p.z = 0.0f;
    buildSpriteMesh(oneParticle(p), flatView(), b);
    REQUIRE(a.vertices.size() == 4);
    CHECK(a.vertices[2].x == b.vertices[2].x);
    CHECK(a.vertices[2].y == b.vertices[2].y);
}

TEST_CASE("the 3D camera looks at its target and things shrink with distance", "[mesh][camera]") {
    View v;
    v.width = 800;
    v.height = 600;
    v.targetX = 0;
    v.targetY = 1;
    v.targetZ = 0;
    v.yaw = 0;
    v.pitch = 0;
    v.distance = 10;
    v.fieldOfView = 40;

    float px = 0, py = 0, scale = 0;
    REQUIRE(projectPoint(v, false, 0, 1, 0, px, py, scale));
    CHECK(px == Catch::Approx(400.0f));
    CHECK(py == Catch::Approx(300.0f));
    const float atTarget = scale;
    // At the target's distance, the field of view spans the viewport height.
    CHECK(atTarget * 2.0f * 10.0f * std::tan(20.0f * 3.14159265f / 180.0f) ==
          Catch::Approx(600.0f).epsilon(1e-4));

    // Right is right and up is up.
    REQUIRE(projectPoint(v, false, 1, 2, 0, px, py, scale));
    CHECK(px > 400.0f);
    CHECK(py < 300.0f);

    // Twice as far from the camera, half the size.
    REQUIRE(projectPoint(v, false, 0, 1, -10, px, py, scale));
    CHECK(scale == Catch::Approx(atTarget * 0.5f));

    // Behind the camera: not drawn.
    CHECK_FALSE(projectPoint(v, false, 0, 1, 11, px, py, scale));

    // Turn the camera a quarter of the way round: it now sits on +X, so a
    // point further along +Z appears to the left.
    v.yaw = 90;
    REQUIRE(projectPoint(v, false, 0, 1, 1, px, py, scale));
    CHECK(px < 400.0f);
    CHECK(py == Catch::Approx(300.0f));

    // From above, the target stays in the middle.
    v.pitch = 60;
    REQUIRE(projectPoint(v, false, 0, 1, 0, px, py, scale));
    CHECK(px == Catch::Approx(400.0f).margin(1e-3));
    CHECK(py == Catch::Approx(300.0f).margin(1e-3));
}

TEST_CASE("3D particles face the camera or lie on the plane", "[mesh][camera]") {
    View v;
    v.width = 800;
    v.height = 600;
    v.targetY = 0;
    v.yaw = 0;
    v.pitch = 60;
    v.distance = 10;

    SpriteInstance p;
    p.size = 1.0f;
    RenderFrame frame = oneParticle(p, BlendMode::Alpha, false);

    auto extent = [](const SpriteMesh& m, float& w, float& h) {
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
        for (const auto& vert : m.vertices) {
            minX = std::min(minX, vert.x);
            maxX = std::max(maxX, vert.x);
            minY = std::min(minY, vert.y);
            maxY = std::max(maxY, vert.y);
        }
        w = maxX - minX;
        h = maxY - minY;
    };

    SpriteMesh facing, lying;
    float w = 0, h = 0;
    buildSpriteMesh(frame, v, facing);
    REQUIRE(facing.drawn == 1);
    extent(facing, w, h);
    CHECK(w == Catch::Approx(h).epsilon(1e-4));  // a square, whatever the angle

    frame.batches[0].facing = Facing::Plane;
    buildSpriteMesh(frame, v, lying);
    REQUIRE(lying.drawn == 1);
    float lw = 0, lh = 0;
    extent(lying, lw, lh);
    // The XY plane seen from 60 degrees up is squashed to about half height.
    CHECK(lh < lw * 0.6f);
    CHECK(lh > lw * 0.4f);
}

TEST_CASE("a whole frame from a session turns into a mesh", "[mesh][session]") {
    Session s;
    for (int i = 0; i < 90; ++i) {
        s.tick(1.0 / 60.0);
    }
    SpriteMesh mesh;
    View view;
    buildSpriteMesh(s.frame(), view, mesh);
    CHECK(mesh.drawn > 20);
    CHECK(mesh.drawn <= s.particleCount());
    CHECK(mesh.vertices.size() == mesh.drawn * 4u);
    CHECK(mesh.indices.size() == mesh.drawn * 6u);
    for (std::uint32_t index : mesh.indices) {
        REQUIRE(index < mesh.vertices.size());
    }
    for (const auto& vert : mesh.vertices) {
        REQUIRE(std::isfinite(vert.x));
        REQUIRE(std::isfinite(vert.y));
        REQUIRE(vert.a == 0.0f);  // the starter effect is additive
        REQUIRE(vert.r >= 0.0f);
    }
}

TEST_CASE("a picture particle shows its sprite-sheet cell, drawn by the picture renderer",
          "[mesh][image]") {
    // A 4 x 2 sheet of 8-pixel cells, each one solid colour.
    auto sheet = std::make_shared<Image>();
    sheet->width = 32;
    sheet->height = 16;
    sheet->rgba.resize(32 * 16 * 4);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 32; ++x) {
            const int cell = (y / 8) * 4 + x / 8;
            std::uint8_t* px = &sheet->rgba[static_cast<std::size_t>(y * 32 + x) * 4];
            px[0] = static_cast<std::uint8_t>(cell * 30);
            px[1] = static_cast<std::uint8_t>(255 - cell * 30);
            px[2] = 128;
            px[3] = 255;
        }
    }
    ImageSet images;
    images.put(Id{7}, sheet, "images/sheet.png");

    SpriteInstance p;
    p.size = 2.0f;
    p.frame = 5;  // second row, second column
    RenderFrame frame = oneParticle(p);
    frame.batches[0].texture = Id{7};
    frame.batches[0].columns = 4;
    frame.batches[0].rows = 2;

    SpriteMesh mesh;
    buildSpriteMesh(frame, flatView(), mesh, &images);
    REQUIRE(mesh.drawn == 1);
    REQUIRE(mesh.runs.size() == 1);
    CHECK(mesh.runs[0].texture == Id{7});
    float minU = 9, maxU = -9, minV = 9, maxV = -9;
    for (const auto& v : mesh.vertices) {
        CHECK(v.shape == kPictureShape);
        minU = std::min(minU, v.u);
        maxU = std::max(maxU, v.u);
        minV = std::min(minV, v.v);
        maxV = std::max(maxV, v.v);
    }
    // Half a pixel inside the cell, so smoothing never reaches the next one.
    CHECK(minU == Catch::Approx(0.25f + 0.5f / 32.0f));
    CHECK(maxU == Catch::Approx(0.5f - 0.5f / 32.0f));
    CHECK(minV == Catch::Approx(0.5f + 0.5f / 16.0f));
    CHECK(maxV == Catch::Approx(1.0f - 0.5f / 16.0f));

    // The picture renderer paints the cell's colour, tinted by white.
    const Picture picture = drawPicture(mesh, 800, 600, ScreenColor{0, 0, 0}, &images);
    const std::uint8_t* centre = picture.pixel(400, 300);
    CHECK(std::abs(centre[0] - 150) <= 1);
    CHECK(std::abs(centre[1] - 105) <= 1);
    CHECK(std::abs(centre[2] - 128) <= 1);

    // Frames past the end of the sheet hold on the last cell.
    frame.instances[0].frame = 99;
    buildSpriteMesh(frame, flatView(), mesh, &images);
    for (const auto& v : mesh.vertices) {
        CHECK(v.u > 0.75f);
        CHECK(v.v > 0.5f);
    }

    // Drawn far smaller than painted, it reads a smaller copy and stays
    // smooth: a striped sheet averages to grey instead of shimmering.
    auto stripes = std::make_shared<Image>();
    stripes->width = stripes->height = 256;
    stripes->rgba.resize(256 * 256 * 4);
    for (int i = 0; i < 256 * 256; ++i) {
        const std::uint8_t on = ((i % 256) % 2) ? 255 : 0;
        stripes->rgba[static_cast<std::size_t>(i) * 4] = on;
        stripes->rgba[static_cast<std::size_t>(i) * 4 + 1] = on;
        stripes->rgba[static_cast<std::size_t>(i) * 4 + 2] = on;
        stripes->rgba[static_cast<std::size_t>(i) * 4 + 3] = 255;
    }
    images.put(Id{8}, stripes, "images/stripes.png");
    frame.batches[0].texture = Id{8};
    frame.batches[0].columns = frame.batches[0].rows = 1;
    frame.instances[0].frame = 0;
    frame.instances[0].size = 0.2f;  // 20 pixels for 256
    buildSpriteMesh(frame, flatView(), mesh, &images);
    const Picture small = drawPicture(mesh, 800, 600, ScreenColor{0, 0, 0}, &images);
    for (int x = 395; x <= 405; ++x) {
        CHECK(std::abs(small.pixel(x, 300)[0] - 128) < 12);
    }
}

TEST_CASE("a trail follows the exact path the particle took", "[mesh][trail]") {
    // Fly a particle forward with the simulation's own step rule.
    const float h = 1.0f / 60.0f, gx = 0.5f, gy = -9.0f, drag = 1.3f;
    float x = -1.0f, y = 0.5f, vx = 6.0f, vy = 4.0f;
    const float startX = x, startY = y;
    for (int i = 0; i < 30; ++i) {
        const float damp = 1.0f / (1.0f + drag * h);
        vx = (vx + gx * h) * damp;
        vy = (vy + gy * h) * damp;
        x += vx * h;
        y += vy * h;
    }
    SpriteInstance p;
    p.x = x;
    p.y = y;
    p.vx = vx;
    p.vy = vy;
    p.size = 0.4f;
    p.age = 30 * h;
    RenderFrame frame = oneParticle(p);
    RenderBatch& b = frame.batches[0];
    b.trail = 5.0f;  // longer than the particle has lived: stops at its birth
    b.trailWidth = 1.0f;
    b.gravityX = gx;
    b.gravityY = gy;
    b.drag = drag;
    b.step = h;

    SpriteMesh mesh;
    buildSpriteMesh(frame, flatView(), mesh);
    REQUIRE(mesh.vertices.size() > 8);
    // Trail quads come first, then the particle itself.
    CHECK(mesh.vertices.front().shape == kRibbonShape);
    CHECK(mesh.vertices.back().shape == static_cast<float>(SpriteShape::Soft));
    // The far end of the ribbon is where the particle was born.
    const std::size_t trailVertices = mesh.vertices.size() - 4;
    const SpriteVertex& endA = mesh.vertices[trailVertices - 2];
    const SpriteVertex& endB = mesh.vertices[trailVertices - 1];
    float sx = 0, sy = 0, scale = 0;
    REQUIRE(projectPoint(flatView(), true, startX, startY, 0, sx, sy, scale));
    CHECK(0.5f * (endA.x + endB.x) == Catch::Approx(sx).margin(0.05));
    CHECK(0.5f * (endA.y + endB.y) == Catch::Approx(sy).margin(0.05));
    CHECK(endA.u == Catch::Approx(1.0f));
    // It narrows to nothing and fades out.
    CHECK(std::abs(endA.x - endB.x) + std::abs(endA.y - endB.y) < 0.01f);
    CHECK(endA.a == Catch::Approx(0.0f).margin(1e-5));

    // No trail, or a particle just born, draws only itself.
    b.trail = 0.0f;
    buildSpriteMesh(frame, flatView(), mesh);
    CHECK(mesh.vertices.size() == 4);
    b.trail = 1.0f;
    frame.instances[0].age = 0.0f;
    buildSpriteMesh(frame, flatView(), mesh);
    CHECK(mesh.vertices.size() == 4);
}
