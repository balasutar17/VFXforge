// Photoshop's blend modes: the sums for each, and a layer in each mode
// drawn over another through the mesh and the picture renderer.
#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "vfx/editor/Blend.h"
#include "vfx/editor/Picture.h"
#include "vfx/editor/Session.h"
#include "vfx/editor/SpriteMesh.h"
#include "vfx/editor/UnityExport.h"

using namespace vfx;
using namespace vfx::editor;
using Catch::Approx;

namespace {

// One pixel, a colour of the particle's own (straight) at alpha a.
struct Out {
    float c[3];
};
Out lay(BlendMode mode, float dr, float dg, float db, float sr, float sg, float sb, float a = 1.0f,
        float noise = 0.5f) {
    Out o{{dr, dg, db}};
    blendOver(mode, sr * a, sg * a, sb * a, a, o.c, noise);
    return o;
}

float lum(float r, float g, float b) { return 0.3f * r + 0.59f * g + 0.11f * b; }

}  // namespace

TEST_CASE("Normal and Linear Dodge (Add) are source plus pixel times what the source lets through", "[blend]") {
    Out o = lay(BlendMode::Alpha, 0.4f, 0.4f, 0.4f, 1.0f, 0.0f, 0.0f, 0.25f);
    CHECK(o.c[0] == Approx(0.25f + 0.4f * 0.75f));
    CHECK(o.c[1] == Approx(0.4f * 0.75f));
    // Light that is only added: alpha 0.
    float pixel[3] = {0.2f, 0.2f, 0.2f};
    blendOver(BlendMode::Additive, 0.3f, 0.1f, 0.0f, 0.0f, pixel, 0.5f);
    CHECK(pixel[0] == Approx(0.5f));
    CHECK(pixel[1] == Approx(0.3f));
    CHECK(pixel[2] == Approx(0.2f));
}

TEST_CASE("the modes the card does by itself, worked out on one pixel", "[blend]") {
    // Multiply: the pixel times the colour; a fainter particle lets more through.
    CHECK(lay(BlendMode::Multiply, 0.8f, 0.5f, 1.0f, 0.5f, 0.5f, 0.5f).c[0] == Approx(0.4f));
    CHECK(lay(BlendMode::Multiply, 0.8f, 0.5f, 1.0f, 0.5f, 0.5f, 0.5f, 0.5f).c[0] == Approx(0.8f * 0.75f));
    CHECK(lay(BlendMode::Multiply, 0.8f, 0.5f, 1.0f, 0.5f, 0.5f, 0.5f, 0.0f).c[0] == Approx(0.8f));
    // Screen: 1 - (1 - a)(1 - b).
    CHECK(lay(BlendMode::Screen, 0.5f, 0.0f, 1.0f, 0.5f, 0.5f, 0.5f).c[0] == Approx(0.75f));
    CHECK(lay(BlendMode::Screen, 0.5f, 0.0f, 1.0f, 0.5f, 0.5f, 0.5f).c[2] == Approx(1.0f));
    // Exclusion: a + b - 2ab.
    CHECK(lay(BlendMode::Exclusion, 0.5f, 0.2f, 1.0f, 0.5f, 0.5f, 0.5f).c[0] == Approx(0.5f));
    CHECK(lay(BlendMode::Exclusion, 0.2f, 0.2f, 1.0f, 1.0f, 1.0f, 1.0f).c[0] == Approx(0.8f));
}

TEST_CASE("the darkening, lightening and contrast modes", "[blend]") {
    const auto r = [](BlendMode m, float d, float s) { return lay(m, d, d, d, s, s, s).c[0]; };
    CHECK(r(BlendMode::Darken, 0.6f, 0.4f) == Approx(0.4f));
    CHECK(r(BlendMode::Lighten, 0.6f, 0.4f) == Approx(0.6f));
    CHECK(r(BlendMode::LinearBurn, 0.6f, 0.7f) == Approx(0.3f));
    CHECK(r(BlendMode::LinearBurn, 0.2f, 0.3f) == Approx(0.0f));
    CHECK(r(BlendMode::ColorBurn, 0.75f, 0.5f) == Approx(0.5f));
    CHECK(r(BlendMode::ColorBurn, 0.5f, 0.0f) == Approx(0.0f));
    CHECK(r(BlendMode::ColorDodge, 0.25f, 0.5f) == Approx(0.5f));
    CHECK(r(BlendMode::ColorDodge, 0.5f, 1.0f) == Approx(1.0f));
    CHECK(r(BlendMode::Overlay, 0.25f, 0.5f) == Approx(0.25f));
    CHECK(r(BlendMode::Overlay, 0.75f, 0.5f) == Approx(0.75f));
    // Hard Light is Overlay with the layer and what is behind it swapped.
    CHECK(r(BlendMode::HardLight, 0.5f, 0.25f) == Approx(0.25f));
    CHECK(r(BlendMode::HardLight, 0.5f, 0.75f) == Approx(0.75f));
    CHECK(r(BlendMode::SoftLight, 0.4f, 0.5f) == Approx(0.4f));   // 50% grey changes nothing
    CHECK(r(BlendMode::SoftLight, 0.4f, 1.0f) > 0.4f);
    CHECK(r(BlendMode::SoftLight, 0.4f, 0.0f) < 0.4f);
    CHECK(r(BlendMode::VividLight, 0.5f, 0.5f) == Approx(0.5f));
    CHECK(r(BlendMode::VividLight, 0.5f, 0.75f) == Approx(1.0f));   // dodge by 0.5
    CHECK(r(BlendMode::VividLight, 0.5f, 0.25f) == Approx(0.0f));   // burn by 0.5
    CHECK(r(BlendMode::LinearLight, 0.5f, 0.75f) == Approx(1.0f));
    CHECK(r(BlendMode::LinearLight, 0.5f, 0.5f) == Approx(0.5f));
    CHECK(r(BlendMode::PinLight, 0.5f, 0.25f) == Approx(0.5f));
    CHECK(r(BlendMode::PinLight, 0.8f, 0.25f) == Approx(0.5f));
    CHECK(r(BlendMode::PinLight, 0.2f, 0.9f) == Approx(0.8f));
    CHECK(r(BlendMode::HardMix, 0.6f, 0.5f) == Approx(1.0f));
    CHECK(r(BlendMode::HardMix, 0.4f, 0.5f) == Approx(0.0f));
}

TEST_CASE("the comparison modes", "[blend]") {
    const auto r = [](BlendMode m, float d, float s) { return lay(m, d, d, d, s, s, s).c[0]; };
    CHECK(r(BlendMode::Difference, 0.7f, 0.2f) == Approx(0.5f));
    CHECK(r(BlendMode::Difference, 0.2f, 0.7f) == Approx(0.5f));
    CHECK(r(BlendMode::Subtract, 0.7f, 0.2f) == Approx(0.5f));
    CHECK(r(BlendMode::Subtract, 0.2f, 0.7f) == Approx(0.0f));
    CHECK(r(BlendMode::Divide, 0.25f, 0.5f) == Approx(0.5f));
    CHECK(r(BlendMode::Divide, 0.6f, 0.3f) == Approx(1.0f));
    CHECK(r(BlendMode::Divide, 0.5f, 0.0f) == Approx(1.0f));
    CHECK(r(BlendMode::Divide, 0.0f, 0.0f) == Approx(0.0f));
}

TEST_CASE("Darker Color and Lighter Color take the whole colour of one or the other", "[blend]") {
    // Behind: a dark red. The particle: a bright green, which is lighter.
    Out lighter = lay(BlendMode::LighterColor, 0.4f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    CHECK(lighter.c[0] == Approx(0.0f));
    CHECK(lighter.c[1] == Approx(1.0f));
    Out darker = lay(BlendMode::DarkerColor, 0.4f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    CHECK(darker.c[0] == Approx(0.4f));
    CHECK(darker.c[1] == Approx(0.0f));
}

TEST_CASE("the colour modes keep what Photoshop says they keep", "[blend]") {
    const float d[3] = {0.8f, 0.3f, 0.2f}, s[3] = {0.1f, 0.4f, 0.9f};
    // Luminosity: the luminosity of the particle, the colour of what is behind.
    Out l = lay(BlendMode::Luminosity, d[0], d[1], d[2], s[0], s[1], s[2]);
    CHECK(lum(l.c[0], l.c[1], l.c[2]) == Approx(lum(s[0], s[1], s[2])).margin(1e-4));
    // Color: the luminosity of what is behind, the colour of the particle.
    Out c = lay(BlendMode::Color, d[0], d[1], d[2], s[0], s[1], s[2]);
    CHECK(lum(c.c[0], c.c[1], c.c[2]) == Approx(lum(d[0], d[1], d[2])).margin(1e-4));
    CHECK(c.c[2] > c.c[0]);  // it is blue-ish, as the particle is
    // Hue keeps the luminosity and saturation of what is behind.
    Out h = lay(BlendMode::Hue, d[0], d[1], d[2], s[0], s[1], s[2]);
    CHECK(lum(h.c[0], h.c[1], h.c[2]) == Approx(lum(d[0], d[1], d[2])).margin(1e-4));
    CHECK(h.c[2] > h.c[0]);
    // Saturation: the hue and luminosity of what is behind.
    Out sa = lay(BlendMode::Saturation, d[0], d[1], d[2], s[0], s[1], s[2]);
    CHECK(lum(sa.c[0], sa.c[1], sa.c[2]) == Approx(lum(d[0], d[1], d[2])).margin(1e-4));
    CHECK(sa.c[0] > sa.c[2]);  // still red-ish, as what is behind it is
    // A grey particle in Color leaves a grey with what is behind's luminosity.
    Out grey = lay(BlendMode::Color, d[0], d[1], d[2], 0.5f, 0.5f, 0.5f);
    CHECK(grey.c[0] == Approx(grey.c[1]).margin(1e-4));
    CHECK(grey.c[1] == Approx(grey.c[2]).margin(1e-4));
    CHECK(grey.c[0] == Approx(lum(d[0], d[1], d[2])).margin(1e-4));
}

TEST_CASE("Dissolve shows the particle where chance is below its alpha, and leaves the rest", "[blend]") {
    CHECK(lay(BlendMode::Dissolve, 0.2f, 0.2f, 0.2f, 1.0f, 0.5f, 0.0f, 0.5f, 0.3f).c[0] == Approx(1.0f));
    CHECK(lay(BlendMode::Dissolve, 0.2f, 0.2f, 0.2f, 1.0f, 0.5f, 0.0f, 0.5f, 0.7f).c[0] == Approx(0.2f));
    // Whole, never half-mixed.
    CHECK(lay(BlendMode::Dissolve, 0.2f, 0.2f, 0.2f, 1.0f, 0.5f, 0.0f, 0.5f, 0.3f).c[1] == Approx(0.5f));
    // Across a patch, about half the pixels at alpha 0.5.
    int shown = 0;
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 40; ++x) {
            float pixel[3] = {0, 0, 0};
            blendOver(BlendMode::Dissolve, 0.5f, 0.5f, 0.5f, 0.5f, pixel, blendNoise(x, y));
            shown += pixel[0] > 0.1f ? 1 : 0;
        }
    }
    CHECK(shown > 600);
    CHECK(shown < 1000);
}

TEST_CASE("every mode: nothing from a particle with no alpha, and always a colour on the screen", "[blend]") {
    std::srand(7);
    const auto unit = [] { return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX); };
    for (const BlendMode mode : blendModes()) {
        for (int i = 0; i < 300; ++i) {
            const float d[3] = {unit(), unit(), unit()}, s[3] = {unit(), unit(), unit()}, a = unit();
            float pixel[3] = {d[0], d[1], d[2]};
            blendOver(mode, s[0] * a, s[1] * a, s[2] * a, a, pixel, unit());
            for (int c = 0; c < 3; ++c) {
                CHECK(std::isfinite(pixel[c]));
                CHECK(pixel[c] >= -1e-5f);
                CHECK(pixel[c] <= 1.0001f);
            }
            // No alpha, no colour: nothing changes (light that is added has
            // colour but is only for Linear Dodge (Add), which keeps its own rule).
            if (mode != BlendMode::Additive) {
                float untouched[3] = {d[0], d[1], d[2]};
                blendOver(mode, 0.0f, 0.0f, 0.0f, 0.0f, untouched, unit());
                for (int c = 0; c < 3; ++c) {
                    CHECK(untouched[c] == Approx(d[c]));
                }
            }
        }
    }
}

// ---- through the mesh and the picture renderer

namespace {

// Two layers in the middle of the view: a grey square behind (Normal), and
// a coloured square on top in the mode being tried.
Picture twoLayers(BlendMode top, float topAlpha, float behindGrey, float topR, float topG, float topB) {
    RenderFrame frame;
    frame.flat = true;
    auto add = [&](float srgbR, float srgbG, float srgbB, float alpha, BlendMode mode) {
        SpriteInstance p;
        p.x = 0;
        p.y = 0;
        p.size = 3.0f;
        p.r = static_cast<float>(srgbToLinear(srgbR));
        p.g = static_cast<float>(srgbToLinear(srgbG));
        p.b = static_cast<float>(srgbToLinear(srgbB));
        p.a = alpha;
        frame.instances.push_back(p);
        RenderBatch batch;
        batch.blend = mode;
        batch.shape = SpriteShape::Square;
        batch.first = static_cast<std::uint32_t>(frame.instances.size() - 1);
        batch.count = 1;
        frame.batches.push_back(batch);
    };
    add(behindGrey, behindGrey, behindGrey, 1.0f, BlendMode::Alpha);
    add(topR, topG, topB, topAlpha, top);
    View view;
    view.width = 200;
    view.height = 200;
    view.centerX = 0;
    view.centerY = 0;
    view.unitsHigh = 6;
    SpriteMesh mesh;
    buildSpriteMesh(frame, view, mesh);
    // The layers are two runs, never one, when their modes differ.
    if (top != BlendMode::Alpha) {
        REQUIRE(mesh.runs.size() == 2);
        CHECK(mesh.runs[0].blend == BlendMode::Alpha);
        CHECK(mesh.runs[1].blend == top);
    }
    return drawPicture(mesh, 200, 200, ScreenColor{0.0f, 0.0f, 0.0f});
}

float at(const Picture& p, int channel) { return static_cast<float>(p.pixel(100, 100)[channel]) / 255.0f; }

}  // namespace

TEST_CASE("a layer in Multiply, Screen and Difference over another, drawn as a picture", "[blend][picture]") {
    const Picture multiplied = twoLayers(BlendMode::Multiply, 1.0f, 0.8f, 0.5f, 0.5f, 1.0f);
    CHECK(at(multiplied, 0) == Approx(0.4f).margin(0.01));
    CHECK(at(multiplied, 2) == Approx(0.8f).margin(0.01));
    const Picture screened = twoLayers(BlendMode::Screen, 1.0f, 0.5f, 0.5f, 0.5f, 0.5f);
    CHECK(at(screened, 1) == Approx(0.75f).margin(0.01));
    const Picture different = twoLayers(BlendMode::Difference, 1.0f, 0.75f, 0.25f, 0.75f, 1.0f);
    CHECK(at(different, 0) == Approx(0.5f).margin(0.01));
    CHECK(at(different, 1) == Approx(0.0f).margin(0.01));
    CHECK(at(different, 2) == Approx(0.25f).margin(0.01));
}

TEST_CASE("a layer's opacity mixes its blend mode in, and Normal is as it always was", "[blend][picture]") {
    const Picture half = twoLayers(BlendMode::Multiply, 0.5f, 0.8f, 0.0f, 0.0f, 0.0f);
    CHECK(at(half, 0) == Approx(0.8f * 0.5f).margin(0.01));   // half way to black
    const Picture lighten = twoLayers(BlendMode::Lighten, 0.5f, 0.2f, 1.0f, 1.0f, 1.0f);
    CHECK(at(lighten, 0) == Approx(0.2f + 0.5f * 0.8f).margin(0.01));
    const Picture normal = twoLayers(BlendMode::Alpha, 0.5f, 0.2f, 1.0f, 1.0f, 1.0f);
    CHECK(at(normal, 0) == Approx(0.2f + 0.5f * 0.8f).margin(0.01));
}

TEST_CASE("layers of one mode that follow each other stay one run; a change of mode splits it", "[blend][mesh]") {
    RenderFrame frame;
    frame.flat = true;
    for (int i = 0; i < 4; ++i) {
        SpriteInstance p;
        p.size = 1.0f;
        p.a = 1.0f;
        frame.instances.push_back(p);
        RenderBatch batch;
        batch.blend = i < 2 ? BlendMode::Screen : (i == 2 ? BlendMode::Alpha : BlendMode::Screen);
        batch.first = static_cast<std::uint32_t>(i);
        batch.count = 1;
        frame.batches.push_back(batch);
    }
    View view;
    SpriteMesh mesh;
    buildSpriteMesh(frame, view, mesh);
    REQUIRE(mesh.runs.size() == 3);
    CHECK(mesh.runs[0].blend == BlendMode::Screen);
    CHECK(mesh.runs[0].indexCount == 12);
    CHECK(mesh.runs[1].blend == BlendMode::Alpha);
    CHECK(mesh.runs[2].blend == BlendMode::Screen);
    // A mode other than Add keeps its alpha in the corners; Add has none.
    CHECK(mesh.vertices[0].a == Approx(1.0f));
    frame.batches[0].blend = BlendMode::Additive;
    buildSpriteMesh(frame, view, mesh);
    CHECK(mesh.vertices[0].a == 0.0f);
}

TEST_CASE("the Sprite module's Blend Mode reaches the drawing through a saved effect", "[blend][session]") {
    Session s;
    s.newEffect(false);
    REQUIRE_FALSE(s.effect().layers.empty());
    const Layer& layer = s.effect().layers.front();
    const Module* sprite = nullptr;
    for (const Module& m : layer.modules) {
        if (m.type == "sprite") sprite = &m;
    }
    REQUIRE(sprite != nullptr);
    REQUIRE(s.set(Path::property(layer.id, sprite->id, "blend"), Value(std::string("overlay"))));
    // Unity: Overlay is drawn as Normal, and the effect says so by layer.
    const auto notes = unityBlendNotes(s.effect());
    REQUIRE(notes.size() == 1);
    CHECK(notes[0] == layer.name + ": Overlay is drawn as Normal in Unity");
    REQUIRE(s.set(Path::property(layer.id, sprite->id, "blend"), Value(std::string("screen"))));
    CHECK(unityBlendNotes(s.effect())[0] == layer.name + ": Screen is drawn as Linear Dodge (Add) in Unity");
    REQUIRE(s.set(Path::property(layer.id, sprite->id, "blend"), Value(std::string("additive"))));
    CHECK(unityBlendNotes(s.effect()).empty());
    REQUIRE(s.set(Path::property(layer.id, sprite->id, "blend"), Value(std::string("alpha"))));
    CHECK(unityBlendNotes(s.effect()).empty());
}
