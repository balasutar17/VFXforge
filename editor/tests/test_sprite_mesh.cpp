// Turning particles into triangles: camera, colour and blending rules.
#include <catch2/catch_amalgamated.hpp>

#include <cmath>

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
