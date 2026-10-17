// The target: a symbol's picture, its silhouette, and effects that follow
// it. Follows the acceptance list of the slot-game brief as far as the
// library goes (the window's part is in the app's self-test).
#include <catch2/catch_amalgamated.hpp>

#include <cmath>
#include <filesystem>
#include <functional>

#include "vfx/FileIO.h"
#include "vfx/Serialize.h"
#include "vfx/editor/Frames.h"
#include "vfx/editor/Image.h"
#include "vfx/editor/Session.h"
#include "vfx/editor/Silhouette.h"

using namespace vfx;
using namespace vfx::editor;
using Catch::Approx;

namespace {

// A picture, solid where inside() says, see-through elsewhere.
Image paint(int w, int h, const std::function<bool(double, double)>& inside) {
    Image image;
    image.width = w;
    image.height = h;
    image.rgba.assign(static_cast<std::size_t>(w) * h * 4, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (inside(x + 0.5, y + 0.5)) {
                std::uint8_t* p = &image.rgba[(static_cast<std::size_t>(y) * w + x) * 4];
                p[0] = 240;
                p[1] = 190;
                p[2] = 40;
                p[3] = 255;
            }
        }
    }
    return image;
}

Image disc(int size = 200, double radius = 80) {
    return paint(size, size, [=](double x, double y) { return std::hypot(x - size / 2.0, y - size / 2.0) < radius; });
}

// A crown: a band with three spikes on top.
Image crown(int w = 240, int h = 200) {
    return paint(w, h, [=](double x, double y) {
        if (y > 120 && y < 180 && x > 30 && x < 210) {
            return true;
        }
        for (const double cx : {50.0, 120.0, 190.0}) {
            const double top = 30, half = 28;
            if (y >= top && y <= 120 && std::fabs(x - cx) <= half * (y - top) / 90.0) {
                return true;
            }
        }
        return false;
    });
}

Value valueOf(const Layer& l, const char* type, const char* key) {
    for (const Module& m : l.modules) {
        if (m.type == type) {
            return *m.find(key);
        }
    }
    return Value(false);
}

std::filesystem::path scratch(const char* name) {
    const auto dir = std::filesystem::temp_directory_path() / (std::string("vfxforge_target_") + name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace

TEST_CASE("a disc's silhouette is a circle of the right size, counter-clockwise", "[target]") {
    auto traced = traceSilhouette(disc(), 2.0);
    REQUIRE(traced.ok());
    const Silhouette& s = traced.value();
    CHECK(s.width == 2.0);
    CHECK(s.height == Approx(2.0));
    CHECK(s.parts == 1);
    CHECK(s.coverage == Approx(3.14159 * 80 * 80 / 40000).epsilon(0.02));
    REQUIRE(s.outline.points.size() == 200);
    double area = 0;
    for (std::size_t i = 0; i < s.outline.points.size(); ++i) {
        const Point2& a = s.outline.points[i];
        const Point2& b = s.outline.points[(i + 1) % s.outline.points.size()];
        area += a.x * b.y - b.x * a.y;
        // 80 pixels of 200 across 2 units: 0.8, within about a pixel.
        REQUIRE(std::hypot(a.x, a.y) == Approx(0.8).margin(0.015));
    }
    CHECK(area > 0);
}

TEST_CASE("a crown's silhouette keeps its spikes; the largest part is traced; nothing solid is refused", "[target]") {
    auto traced = traceSilhouette(crown(), 2.4);
    REQUIRE(traced.ok());
    // The spike tips (30 px from the top of 200 at 2.4/240 units a pixel: y = 0.7) are reached.
    double top = -1e9;
    for (const Point2& p : traced.value().outline.points) {
        top = std::max(top, p.y);
    }
    CHECK(top == Approx(0.7).margin(0.06));
    // Two discs: the larger is traced, both are counted.
    Image two = paint(300, 200, [](double x, double y) {
        return std::hypot(x - 80, y - 100) < 60 || std::hypot(x - 230, y - 100) < 30;
    });
    auto both = traceSilhouette(two, 3.0);
    REQUIRE(both.ok());
    CHECK(both.value().parts == 2);
    for (const Point2& p : both.value().outline.points) {
        REQUIRE(p.x < 0.0);  // all on the left disc
    }
    Image empty = paint(50, 50, [](double, double) { return false; });
    auto none = traceSilhouette(empty, 1.0);
    REQUIRE_FALSE(none.ok());
    CHECK(none.error().message.find("see-through") != std::string::npos);
}

TEST_CASE("the brief's workflow: a symbol, a golden outline fitted to it, changed, swapped, saved, exported",
          "[target][acceptance]") {
    const auto dir = scratch("workflow");
    Session s;
    REQUIRE(s.saveAs(dir / "symbol.vfx"));
    // 1. A transparent PNG of a slot symbol.
    REQUIRE(s.setTarget("coin.png", encodePng(disc()), 2.0));
    REQUIRE(s.effect().target.id.valid());
    // 2. A golden outline: a layer fitted to the symbol.
    const Id outline = s.effect().layers.front().id;
    REQUIRE(s.fitToTarget({outline}));
    const Layer* l = findLayer(s.effect(), outline);
    CHECK(std::get<std::string>(valueOf(*l, "shape", "shape")) == "path");
    CHECK(std::get<bool>(valueOf(*l, "shape", "fitTarget")));
    CHECK(std::get<PointList>(valueOf(*l, "shape", "path")).points.size() == 200);
    const Id shapeModule = [&] {
        for (const Module& m : l->modules) {
            if (m.type == "shape") return m.id;
        }
        return Id{};
    }();
    const Id initial = [&] {
        for (const Module& m : l->modules) {
            if (m.type == "initial") return m.id;
        }
        return Id{};
    }();
    REQUIRE(s.set(Path::property(outline, shapeModule, "emitFrom"), Value(std::string("edge"))));
    REQUIRE(s.set(Path::property(outline, initial, "speed"), Value(Scalar::constant(0.0))));
    for (const Module& m : l->modules) {
        if (m.type == "motion") {
            REQUIRE(s.set(Path::property(outline, m.id, "gravity"), Value(Vec3{0, 0, 0})));
        }
    }
    REQUIRE(s.set(Path::property(outline, initial, "color"), Value(Color{1.0, 0.75, 0.2, 1.0})));
    // 3. Thickness and colour.
    REQUIRE(s.set(Path::property(outline, shapeModule, "thickness"), Value(0.05)));
    // 4. Travelling round the symbol.
    REQUIRE(s.set(Path::property(outline, shapeModule, "travel"), Value(1.0)));
    s.tick(0.5);
    // Particles are on the circle (within the thickness and their drift).
    int on = 0, all = 0;
    for (const auto& p : s.frame().instances) {
        ++all;
        on += std::fabs(std::hypot(p.x, p.y) - 0.8) < 0.1 ? 1 : 0;
    }
    REQUIRE(all > 0);
    CHECK(on > all * 0.9);

    // 5. Another symbol, a different shape: the same layer follows it, its
    // own settings untouched.
    const std::string before = writeEffect(s.effect());
    REQUIRE(s.setTarget("crown.png", encodePng(crown()), 2.4));
    l = findLayer(s.effect(), outline);
    double top = -1e9;
    for (const Point2& p : std::get<PointList>(valueOf(*l, "shape", "path")).points) {
        top = std::max(top, p.y);
    }
    CHECK(top == Approx(0.7).margin(0.06));
    CHECK(std::get<double>(valueOf(*l, "shape", "travel")) == 1.0);
    CHECK(std::get<double>(valueOf(*l, "shape", "thickness")) == 0.05);
    REQUIRE(s.undo());
    CHECK(writeEffect(s.effect()) == before);  // one undo: the coin again
    REQUIRE(s.redo());

    // 6. A glow layer of its own, moved without moving the outline.
    Id glow;
    REQUIRE(s.addEmitter("Glow", &glow));
    LayerTransform t;
    t.move = Vec3{0.5, 0, 0};
    const auto pathBefore = std::get<PointList>(valueOf(*findLayer(s.effect(), outline), "shape", "path"));
    REQUIRE(s.transformLayers({glow}, t, "Move Glow"));
    CHECK(std::get<PointList>(valueOf(*findLayer(s.effect(), outline), "shape", "path")) == pathBefore);
    CHECK(std::get<Vec3>(valueOf(*findLayer(s.effect(), glow), "shape", "offset")).x == Approx(0.5));

    // 7. Moving the target takes the fitted layer with it, not the glow.
    REQUIRE(s.placeTarget(2.4, Vec3{0, 1, 0}));
    CHECK(std::get<Vec3>(valueOf(*findLayer(s.effect(), outline), "shape", "offset")).y == Approx(1.0));
    CHECK(std::get<Vec3>(valueOf(*findLayer(s.effect(), glow), "shape", "offset")).y == Approx(0.0));

    // 8. Saved, closed, reopened: still the same, still editable.
    REQUIRE(s.save());
    const std::string saved = writeEffect(s.effect());
    Session again;
    REQUIRE(again.open(dir / "symbol.vfx"));
    CHECK(writeEffect(again.effect()) == saved);
    REQUIRE(again.targetImage() != nullptr);
    REQUIRE(again.set(Path::property(outline, shapeModule, "thickness"), Value(0.1)));

    // 9. A transparent PNG sequence, read back.
    FrameOptions options;
    options.width = 128;
    options.height = 128;
    options.framesPerSecond = 10;
    options.view.centerX = 0;
    options.view.centerY = 1;
    options.view.unitsHigh = 3;
    auto written = exportFrames(again.effect(), options, dir, "symbol");
    REQUIRE(written.ok());
    CHECK(written.value().frames == frameCount(again.effect(), 10));
    int frames = 0, lit = 0, clear = 0;
    for (int i = 0; i < written.value().frames; ++i) {
        char name[64];
        std::snprintf(name, sizeof name, "symbol_%04d.png", i);
        auto bytes = readFile(written.value().folder / name);
        REQUIRE(bytes.ok());
        auto image = decodePng(bytes.value());
        REQUIRE(image.ok());
        REQUIRE(image.value().width == 128);
        ++frames;
        for (std::size_t p = 3; p < image.value().rgba.size(); p += 4) {
            (image.value().rgba[p] > 0 ? lit : clear) += 1;
        }
    }
    CHECK(frames == written.value().frames);
    CHECK(lit > 0);    // something drawn
    CHECK(clear > 0);  // and see-through around it
    std::filesystem::remove_all(dir);
}

TEST_CASE("fitting needs a target, and says so", "[target]") {
    Session s;
    const auto r = s.fitToTarget({s.effect().layers.front().id});
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().message == "There is no target.");
}

TEST_CASE("a rounded rectangle goes round counter-clockwise with rounded corners", "[target]") {
    const PointList r = roundedRect(4, 2, 0.5, 4);
    REQUIRE(r.points.size() == 20);
    double area = 0, maxX = 0;
    for (std::size_t i = 0; i < r.points.size(); ++i) {
        const Point2& a = r.points[i];
        const Point2& b = r.points[(i + 1) % r.points.size()];
        area += a.x * b.y - b.x * a.y;
        maxX = std::max(maxX, a.x);
    }
    CHECK(area / 2 == Approx(4 * 2 - (4 - 3.14159) * 0.25).margin(0.05));
    CHECK(maxX == Approx(2.0));
}
