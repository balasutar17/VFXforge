// Reference to VFX: reading a reference, rebuilding it, comparing, refining.
//
// The references here are painted by the tests themselves, so what the
// analysis ought to find is known exactly.
#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <vector>

#include "vfx/FileIO.h"
#include "vfx/Program.h"
#include "vfx/Serialize.h"
#include "vfx/Simulation.h"
#include "vfx/editor/Compare.h"
#include "vfx/editor/Reconstruct.h"
#include "vfx/editor/Reference.h"
#include "vfx/editor/Refine.h"
#include "vfx/editor/Session.h"
#include "vfx/editor/Shapes.h"

using namespace vfx;
using namespace vfx::editor;
using Catch::Approx;

namespace {

constexpr float kPi = 3.14159265f;

// A canvas of light on black, or of paint on any colour, that turns into
// an Image. Coordinates are fractions of the height, from the top-left.
class Canvas {
public:
    Canvas(int width, int height, float r = 0, float g = 0, float b = 0)
        : w_(width), h_(height), pixels_(static_cast<std::size_t>(width * height) * 3) {
        for (std::size_t i = 0; i < pixels_.size(); i += 3) {
            pixels_[i] = r;
            pixels_[i + 1] = g;
            pixels_[i + 2] = b;
        }
    }

    // Adds light: amount(x, y) in 0..1 of the colour given.
    void light(float r, float g, float b, const std::function<float(float, float)>& amount) {
        each([&](float x, float y, float* p) {
            const float a = amount(x, y);
            p[0] += r * a;
            p[1] += g * a;
            p[2] += b * a;
        });
    }

    // Paints over: cover(x, y) in 0..1.
    void paint(float r, float g, float b, const std::function<float(float, float)>& cover) {
        each([&](float x, float y, float* p) {
            const float a = std::clamp(cover(x, y), 0.0f, 1.0f);
            p[0] += (r - p[0]) * a;
            p[1] += (g - p[1]) * a;
            p[2] += (b - p[2]) * a;
        });
    }

    Image image() const {
        Image out;
        out.width = w_;
        out.height = h_;
        out.rgba.resize(static_cast<std::size_t>(w_ * h_) * 4);
        for (int i = 0; i < w_ * h_; ++i) {
            for (int c = 0; c < 3; ++c) {
                out.rgba[static_cast<std::size_t>(i * 4 + c)] = static_cast<std::uint8_t>(
                    std::lround(std::clamp(pixels_[static_cast<std::size_t>(i * 3 + c)], 0.0f, 1.0f) * 255.0f));
            }
            out.rgba[static_cast<std::size_t>(i * 4 + 3)] = 255;
        }
        return out;
    }

private:
    void each(const std::function<void(float, float, float*)>& fn) {
        for (int y = 0; y < h_; ++y) {
            for (int x = 0; x < w_; ++x) {
                fn((static_cast<float>(x) + 0.5f) / static_cast<float>(h_), (static_cast<float>(y) + 0.5f) / static_cast<float>(h_),
                   &pixels_[static_cast<std::size_t>((y * w_ + x) * 3)]);
            }
        }
    }
    int w_, h_;
    std::vector<float> pixels_;
};

std::function<float(float, float)> glow(float cx, float cy, float radius, float level = 1.0f) {
    return [=](float x, float y) {
        const float d2 = ((x - cx) * (x - cx) + (y - cy) * (y - cy)) / (radius * radius);
        const float v = 1.0f - d2;
        return v > 0.0f ? level * v * v : 0.0f;
    };
}

std::function<float(float, float)> disc(float cx, float cy, float radius, float soft = 0.004f) {
    return [=](float x, float y) {
        const float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
        return std::clamp((radius - d) / soft + 0.5f, 0.0f, 1.0f);
    };
}

std::function<float(float, float)> ring(float cx, float cy, float radius, float thickness) {
    return [=](float x, float y) {
        const float d = std::fabs(std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy)) - radius);
        return std::clamp((0.5f * thickness - d) / 0.004f + 0.5f, 0.0f, 1.0f);
    };
}

// Thin rays out of a point, turned by an angle (degrees).
std::function<float(float, float)> rays(float cx, float cy, int count, float length, float width, float turn = 0.0f) {
    return [=](float x, float y) {
        const float dx = x - cx, dy = cy - y;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d > length || d < 1e-5f) {
            return d < 1e-5f ? 1.0f : 0.0f;
        }
        float best = 0.0f;
        for (int k = 0; k < count; ++k) {
            const float a = turn * kPi / 180.0f + 2.0f * kPi * static_cast<float>(k) / static_cast<float>(count);
            const float along = dx * std::cos(a) + dy * std::sin(a);
            const float across = std::fabs(-dx * std::sin(a) + dy * std::cos(a));
            if (along > 0.0f) {
                const float taper = width * (1.0f - d / length);
                best = std::max(best, std::clamp((taper - across) / 0.004f + 0.5f, 0.0f, 1.0f));
            }
        }
        return best;
    };
}

Reference still(const Image& image) {
    Reference reference;
    reference.name = "test";
    reference.frames.push_back(image);
    return reference;
}

// A repeatable scatter of numbers from 0 to 1.
struct Dice {
    std::uint32_t state = 12345;
    float next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 16777216.0f;
    }
};

IdSource counting(IdGenerator& ids) {
    return [&ids]() { return ids.next(); };
}

const Finding* finding(const ReferenceAnalysis& a, const char* key) {
    for (const Finding& f : a.findings) {
        if (f.key == key) {
            return &f;
        }
    }
    return nullptr;
}

// A burst clip: a glow that spreads and fades, with sparks, from frame 3 on.
Reference burstClip(int frames = 30, double fps = 30.0) {
    Reference clip;
    clip.name = "burst";
    clip.framesPerSecond = fps;
    Dice dice;
    std::vector<std::pair<float, float>> directions;
    for (int i = 0; i < 16; ++i) {
        const float a = dice.next() * 2.0f * kPi;
        directions.emplace_back(std::cos(a), std::sin(a));
    }
    for (int f = 0; f < frames; ++f) {
        Canvas canvas(160, 160);
        const float t = static_cast<float>(f - 3) / static_cast<float>(fps);
        if (f >= 3 && f < 24) {
            const float spread = 1.0f - std::exp(-6.0f * t);
            const float fade = std::max(0.0f, 1.0f - t / 0.7f);
            canvas.light(1.0f, 0.5f, 0.1f, glow(0.5f, 0.5f, 0.08f + 0.22f * spread, 0.9f * fade));
            for (const auto& d : directions) {
                canvas.light(1.0f, 0.9f, 0.6f,
                             disc(0.5f + d.first * 0.36f * spread, 0.5f + d.second * 0.36f * spread, 0.012f, 0.008f));
            }
        }
        clip.frames.push_back(canvas.image());
    }
    return clip;
}

}  // namespace

// ----------------------------------------------------------------- import

TEST_CASE("a reference is checked before it is studied", "[reference]") {
    Reference none;
    CHECK_FALSE(validateReference(none).ok());

    Reference tiny = still(Canvas(4, 4).image());
    CHECK_FALSE(validateReference(tiny).ok());

    Reference fine = still(Canvas(64, 48).image());
    CHECK(validateReference(fine).ok());
    CHECK_FALSE(fine.moving());

    Reference mixed = fine;
    mixed.frames.push_back(Canvas(32, 32).image());
    mixed.framesPerSecond = 24;
    CHECK_FALSE(validateReference(mixed).ok());

    Reference clip = fine;
    clip.frames.push_back(Canvas(64, 48).image());
    CHECK_FALSE(validateReference(clip).ok());  // two frames and no frame rate
    clip.framesPerSecond = 24;
    CHECK(validateReference(clip).ok());
    CHECK(clip.moving());
    CHECK(clip.seconds() == Approx(2.0 / 24.0));

    Reference damaged = fine;
    damaged.frames[0].rgba.pop_back();
    CHECK_FALSE(validateReference(damaged).ok());

    Reference many = fine;
    many.framesPerSecond = 30;
    many.frames.resize(static_cast<std::size_t>(kMaxReferenceFrames) + 1, fine.frames[0]);
    const Status tooMany = validateReference(many);
    REQUIRE_FALSE(tooMany.ok());
    CHECK(tooMany.error().detail.find("shorter") != std::string::npos);
}

TEST_CASE("an empty reference is reported, not rebuilt", "[reference]") {
    const auto result = analyzeReference(still(Canvas(96, 96).image()), {});
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().message.find("No effect") != std::string::npos);
}

TEST_CASE("the background is worked out, or taken as told", "[reference][matte]") {
    SECTION("light on black") {
        Canvas canvas(128, 128);
        canvas.light(0.2f, 0.6f, 1.0f, glow(0.5f, 0.5f, 0.3f));
        StillAnalysis seen;
        const auto matte = makeMatte(canvas.image(), {}, &seen);
        REQUIRE(matte.ok());
        CHECK(seen.backdrop == Backdrop::Dark);
        CHECK(seen.additive);
        CHECK(seen.backdropEvenness > 0.95f);
        // The middle is fully there and the colour is the light's own hue.
        const Matte& m = matte.value();
        const std::size_t centre = m.at(m.width / 2, m.height / 2);
        CHECK(m.cover[centre] > 0.9f);
        CHECK(m.b[centre] == Approx(1.0f).margin(0.03f));
        CHECK(m.r[centre] == Approx(0.2f).margin(0.05f));
        CHECK(m.cover[m.at(2, 2)] == 0.0f);
    }
    SECTION("paint on white") {
        Canvas canvas(128, 128, 1, 1, 1);
        canvas.paint(0.9f, 0.1f, 0.1f, disc(0.5f, 0.5f, 0.25f));
        StillAnalysis seen;
        const auto matte = makeMatte(canvas.image(), {}, &seen);
        REQUIRE(matte.ok());
        CHECK(seen.backdrop == Backdrop::Light);
        CHECK_FALSE(seen.additive);
        const Matte& m = matte.value();
        const std::size_t centre = m.at(m.width / 2, m.height / 2);
        CHECK(m.cover[centre] > 0.85f);
        CHECK(m.r[centre] > 0.8f);
        CHECK(m.g[centre] < 0.2f);
    }
    SECTION("a picture with its own cut-out") {
        Image image = Canvas(96, 96, 1, 0, 1).image();
        for (int y = 0; y < 96; ++y) {
            for (int x = 0; x < 96; ++x) {
                const float d = std::hypot(static_cast<float>(x) - 48.0f, static_cast<float>(y) - 48.0f);
                image.rgba[static_cast<std::size_t>((y * 96 + x) * 4 + 3)] = d < 24.0f ? 255 : 0;
            }
        }
        StillAnalysis seen;
        const auto matte = makeMatte(image, {}, &seen);
        REQUIRE(matte.ok());
        CHECK(seen.backdrop == Backdrop::Transparent);
        CHECK(matte.value().cover[matte.value().at(48, 48)] > 0.95f);
        CHECK(matte.value().cover[matte.value().at(4, 4)] == 0.0f);
    }
    SECTION("a colour the artist picked") {
        Canvas canvas(96, 96, 0.0f, 0.5f, 0.0f);
        canvas.paint(1.0f, 1.0f, 0.0f, disc(0.5f, 0.5f, 0.2f));
        ReferenceOptions options;
        options.backdrop = Backdrop::Colour;
        options.keyG = 0.5f;
        StillAnalysis seen;
        const auto matte = makeMatte(canvas.image(), options, &seen);
        REQUIRE(matte.ok());
        CHECK(seen.backdrop == Backdrop::Colour);
        CHECK(matte.value().cover[matte.value().at(48, 48)] > 0.9f);
        CHECK(matte.value().cover[matte.value().at(3, 3)] == 0.0f);
    }
    SECTION("a busy background lowers the confidence and says so") {
        Canvas canvas(128, 128);
        Dice dice;
        canvas.light(1, 1, 1, [&](float, float) { return dice.next() * 0.5f; });
        canvas.light(1.0f, 0.4f, 0.1f, glow(0.5f, 0.5f, 0.3f));
        const auto analysed = analyzeReference(still(canvas.image()), {});
        REQUIRE(analysed.ok());
        const Finding* background = finding(analysed.value(), "background");
        REQUIRE(background);
        CHECK(background->confidence < 0.7f);
        CHECK_FALSE(background->note.empty());
    }
}

// ------------------------------------------------------------ still analysis

TEST_CASE("a background that is nearly black, or nearly white, does not turn into a shape", "[reference][matte]") {
    // A wide picture, as saved from the web: the background is the faintest
    // blue rather than true black, except for a band down the left side that
    // is true black. Nobody can see the difference, and it is not an effect.
    {
        Canvas canvas(280, 178, 0.0f, 0.0f, 2.0f / 255.0f);
        canvas.paint(0.0f, 0.0f, 0.0f, [](float x, float) { return x < 0.45f ? 1.0f : 0.0f; });
        canvas.light(0.2f, 0.8f, 1.0f, glow(0.9f, 0.6f, 0.25f));
        const auto analysed = analyzeReference(still(canvas.image()), {});
        REQUIRE(analysed.ok());
        const StillAnalysis& s = analysed.value().still;
        CHECK(s.backdrop == Backdrop::Dark);
        // The effect is the glow, where the glow is.
        CHECK(s.centreX == Approx(0.9f * 178.0f / 280.0f).margin(0.03f));
        CHECK(s.centreY == Approx(0.6f).margin(0.03f));
        CHECK(s.hasGlow);
        CHECK_FALSE(s.hasBody);
        CHECK(s.sparks.count == 0);
        CHECK(s.streaks.count == 0);
        REQUIRE_FALSE(s.palette.empty());
        for (const Swatch& colour : s.palette) {
            CHECK(std::max({colour.r, colour.g, colour.b}) > 0.5f);  // no "black" in a picture of light
        }
        // And nothing is counted in the black band.
        const auto matte = makeMatte(canvas.image(), {});
        REQUIRE(matte.ok());
        double band = 0;
        for (int y = 0; y < matte.value().height; ++y) {
            for (int x = 0; x < matte.value().width / 5; ++x) {
                band += matte.value().cover[matte.value().at(x, y)];
            }
        }
        CHECK(band == Approx(0.0).margin(1e-6));
    }
    // The same the other way up: off-white paper with a true-white band.
    {
        Canvas canvas(280, 178, 250.0f / 255.0f, 250.0f / 255.0f, 248.0f / 255.0f);
        canvas.paint(1.0f, 1.0f, 1.0f, [](float x, float) { return x < 0.45f ? 1.0f : 0.0f; });
        canvas.paint(0.8f, 0.1f, 0.1f, disc(0.9f, 0.6f, 0.2f));
        const auto analysed = analyzeReference(still(canvas.image()), {});
        REQUIRE(analysed.ok());
        const StillAnalysis& s = analysed.value().still;
        CHECK(s.backdrop == Backdrop::Light);
        CHECK(s.centreX == Approx(0.9f * 178.0f / 280.0f).margin(0.03f));
        CHECK(s.centreY == Approx(0.6f).margin(0.03f));
        REQUIRE_FALSE(s.palette.empty());
        CHECK(s.palette[0].r > 0.6f);
        CHECK(s.palette[0].g < 0.3f);
    }
    // A real shape darker than a mid-grey background still counts in full.
    {
        Canvas canvas(200, 200, 0.5f, 0.5f, 0.5f);
        canvas.paint(0.0f, 0.0f, 0.0f, disc(0.5f, 0.5f, 0.2f));
        const auto matte = makeMatte(canvas.image(), {});
        REQUIRE(matte.ok());
        const Matte& m = matte.value();
        CHECK(m.cover[m.at(m.width / 2, m.height / 2)] > 0.9f);
        CHECK(m.cover[m.at(2, 2)] == Approx(0.0f).margin(1e-6f));
    }
}

TEST_CASE("a glow is found where it is, as large as it is, in its colour", "[reference][still]") {
    Canvas canvas(240, 180);
    canvas.light(0.3f, 0.5f, 1.0f, glow(0.8f, 0.4f, 0.3f, 0.8f));  // x is in heights: 0.8 of 1.333
    const auto analysed = analyzeReference(still(canvas.image()), {});
    REQUIRE(analysed.ok());
    const StillAnalysis& s = analysed.value().still;
    CHECK(s.centreX == Approx(0.8f / (240.0f / 180.0f)).margin(0.02f));
    CHECK(s.centreY == Approx(0.4f).margin(0.02f));
    CHECK(s.hasGlow);
    CHECK_FALSE(s.hasBody);
    CHECK(s.glowOuter == Approx(0.3f).margin(0.05f));
    CHECK(s.glowOuterColour.b > s.glowOuterColour.r + 0.3f);
    CHECK(s.elongation < 1.2f);
    CHECK(s.hardness < 0.3f);
    CHECK(s.rings.empty());
    CHECK(s.rays < 2);
    CHECK(s.sparks.count == 0);
    CHECK(s.separate == 1);

    // The report says what was seen and what was assumed.
    const ReferenceAnalysis& a = analysed.value();
    REQUIRE(finding(a, "glow"));
    CHECK(finding(a, "glow")->basis == Basis::Observed);
    REQUIRE(finding(a, "timing"));
    CHECK(finding(a, "timing")->basis == Basis::Inferred);
    CHECK(finding(a, "timing")->confidence < 0.5f);
    REQUIRE(finding(a, "blend"));
    CHECK(finding(a, "blend")->basis == Basis::Inferred);
    CHECK_FALSE(a.uncertain.empty());
    CHECK_FALSE(a.methods.empty());
    for (const Finding& f : a.findings) {
        CHECK(f.confidence >= 0.0f);
        CHECK(f.confidence <= 1.0f);
        CHECK_FALSE(f.label.empty());
        CHECK_FALSE(f.value.empty());
    }
}

TEST_CASE("a ring is found at its radius", "[reference][still]") {
    Canvas canvas(200, 200);
    canvas.light(0.2f, 0.9f, 1.0f, ring(0.5f, 0.5f, 0.3f, 0.03f));
    canvas.light(0.6f, 0.2f, 1.0f, glow(0.5f, 0.5f, 0.15f, 0.7f));
    const auto analysed = analyzeReference(still(canvas.image()), {});
    REQUIRE(analysed.ok());
    const StillAnalysis& s = analysed.value().still;
    REQUIRE(s.rings.size() == 1);
    CHECK(s.rings[0].radius == Approx(0.3f).margin(0.025f));
    CHECK(s.rings[0].around > 0.9f);
    CHECK(s.rings[0].colour.b > 0.8f);
    CHECK(s.rings[0].shape == "ring");
    CHECK(finding(analysed.value(), "ring") != nullptr);
}

TEST_CASE("small loose pieces are counted", "[reference][still]") {
    Canvas canvas(220, 220);
    canvas.light(1.0f, 0.4f, 0.2f, glow(0.5f, 0.5f, 0.2f, 0.8f));
    Dice dice;
    const int placed = 24;
    for (int i = 0; i < placed; ++i) {
        const float angle = dice.next() * 2.0f * kPi, distance = 0.24f + 0.16f * dice.next();
        canvas.light(1.0f, 1.0f, 0.5f, disc(0.5f + std::cos(angle) * distance, 0.5f + std::sin(angle) * distance, 0.009f));
    }
    const auto analysed = analyzeReference(still(canvas.image()), {});
    REQUIRE(analysed.ok());
    const StillAnalysis& s = analysed.value().still;
    CHECK(std::abs(s.sparks.count - placed) <= 4);  // a few overlap
    CHECK(s.sparks.nearest > 0.18f);
    CHECK(s.sparks.farthest < 0.45f);
    CHECK(s.sparks.spread > 150.0f);  // all round
    CHECK(s.sparks.colour.r > 0.8f);
    CHECK(s.sparks.colour.b < 0.75f);
    CHECK(s.streaks.count <= 1);
}

TEST_CASE("rays out of a centre are found", "[reference][still]") {
    Canvas canvas(220, 220);
    canvas.light(1.0f, 0.7f, 0.2f, glow(0.5f, 0.5f, 0.3f, 0.6f));
    canvas.light(1.0f, 1.0f, 1.0f, rays(0.5f, 0.5f, 6, 0.4f, 0.03f, 15.0f));
    const auto analysed = analyzeReference(still(canvas.image()), {});
    REQUIRE(analysed.ok());
    const StillAnalysis& s = analysed.value().still;
    CHECK(s.rays >= 5);
    CHECK(s.rays <= 7);
    CHECK(s.raysEven);
    CHECK_FALSE(s.rayShape.empty());
    CHECK(s.hasGlow);
    CHECK(finding(analysed.value(), "rays") != nullptr);
}

TEST_CASE("a crisp shape is matched to the built-in one it is", "[reference][still]") {
    const auto draw = [](SpriteShape shape, float turnDegrees) {
        Canvas canvas(200, 200);
        const float c = std::cos(turnDegrees * kPi / 180.0f), s = std::sin(turnDegrees * kPi / 180.0f);
        canvas.light(1.0f, 0.5f, 0.1f, [&](float x, float y) {
            // The shape, 0.6 heights across, turned counter-clockwise.
            const float dx = (x - 0.5f) / 0.3f, dy = (0.5f - y) / 0.3f;
            return shapeCoverage(shape, dx * c + dy * s, -dx * s + dy * c, 0.02f, 0.02f);
        });
        return canvas.image();
    };
    SECTION("a star") {
        const auto analysed = analyzeReference(still(draw(SpriteShape::Star, 0)), {});
        REQUIRE(analysed.ok());
        const StillAnalysis& s = analysed.value().still;
        REQUIRE(s.hasBody);
        CHECK(s.bodyShape == "star");
        CHECK(s.bodyMatch > 0.85f);
        CHECK(s.hardness > 0.5f);
    }
    SECTION("a burst, turned") {
        const auto analysed = analyzeReference(still(draw(SpriteShape::Burst, 0)), {});
        REQUIRE(analysed.ok());
        REQUIRE(analysed.value().still.hasBody);
        CHECK(analysed.value().still.bodyShape == "burst");
    }
    SECTION("a heart") {
        const auto analysed = analyzeReference(still(draw(SpriteShape::Heart, 0)), {});
        REQUIRE(analysed.ok());
        REQUIRE(analysed.value().still.hasBody);
        CHECK(analysed.value().still.bodyShape == "heart");
    }
    SECTION("a plain disc") {
        const auto analysed = analyzeReference(still(draw(SpriteShape::Disc, 0)), {});
        REQUIRE(analysed.ok());
        REQUIRE(analysed.value().still.hasBody);
        CHECK(analysed.value().still.bodyShape == "disc");
    }
}

TEST_CASE("a head with a tail is read as one", "[reference][still]") {
    Canvas canvas(240, 240);
    // Head at the lower left, tail fading up and to the right.
    for (int k = 0; k < 40; ++k) {
        const float t = static_cast<float>(k) / 39.0f;
        canvas.light(0.3f, 0.6f, 1.0f, glow(0.25f + 0.5f * t, 0.75f - 0.5f * t, 0.09f * (1.0f - 0.8f * t), 0.25f * (1.0f - t)));
    }
    canvas.light(1.0f, 1.0f, 1.0f, glow(0.25f, 0.75f, 0.07f));
    const auto analysed = analyzeReference(still(canvas.image()), {});
    REQUIRE(analysed.ok());
    const StillAnalysis& s = analysed.value().still;
    REQUIRE(s.comet);
    CHECK(s.headX == Approx(0.25f).margin(0.05f));
    CHECK(s.headY == Approx(0.75f).margin(0.05f));
    CHECK(s.tailHeading == Approx(45.0f).margin(12.0f));  // up and to the right
    CHECK(s.tailLength > 0.35f);
    CHECK(s.tailWidthEnd < s.tailWidthStart);
    CHECK(s.tailColours.size() >= 2);

    IdGenerator ids(3);
    const Reconstruction built = reconstruct(analysed.value(), {}, counting(ids));
    bool tail = false;
    for (const Layer& layer : built.effect.layers) {
        tail = tail || layer.role == "trail";
    }
    CHECK(tail);
}

TEST_CASE("cropping looks at one part of the picture", "[reference]") {
    Canvas canvas(320, 160);
    canvas.light(1.0f, 0.5f, 0.1f, glow(0.5f, 0.5f, 0.3f));
    canvas.light(0.1f, 0.5f, 1.0f, glow(1.5f, 0.5f, 0.3f));
    const Reference reference = still(canvas.image());

    const auto whole = analyzeReference(reference, {});
    REQUIRE(whole.ok());
    CHECK(whole.value().still.separate == 2);
    CHECK(finding(whole.value(), "several") != nullptr);

    ReferenceOptions right;
    right.cropLeft = 0.5f;
    const auto half = analyzeReference(reference, right);
    REQUIRE(half.ok());
    CHECK(half.value().still.separate == 1);
    CHECK(half.value().still.centreX == Approx(0.5f).margin(0.03f));
    CHECK(half.value().still.glowOuterColour.b > half.value().still.glowOuterColour.r);

    const Image shown = referencePicture(reference, right, 0, 100);
    CHECK(shown.width == 100);
    CHECK(shown.height == 100);
}

TEST_CASE("the same picture always reads the same", "[reference][determinism]") {
    Canvas canvas(200, 200);
    canvas.light(1.0f, 0.3f, 0.6f, glow(0.5f, 0.5f, 0.3f, 0.8f));
    canvas.light(1.0f, 1.0f, 1.0f, rays(0.5f, 0.5f, 8, 0.35f, 0.02f));
    const Reference reference = still(canvas.image());
    const auto a = analyzeReference(reference, {});
    const auto b = analyzeReference(reference, {});
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    IdGenerator one(77), two(77);
    const Reconstruction first = reconstruct(a.value(), {}, counting(one));
    const Reconstruction second = reconstruct(b.value(), {}, counting(two));
    CHECK(writeEffect(first.effect) == writeEffect(second.effect));
    CHECK(first.peakTime == second.peakTime);
}

// ------------------------------------------------------------- clip analysis

TEST_CASE("a burst clip gives its start, peak and end", "[reference][time]") {
    const Reference clip = burstClip();
    const auto analysed = analyzeReference(clip, {});
    REQUIRE(analysed.ok());
    const TimeAnalysis& t = analysed.value().time;
    REQUIRE(t.available);
    CHECK(t.frames == 30);
    CHECK(t.framesPerSecond == Approx(30.0));
    CHECK_FALSE(t.continuous);
    CHECK(t.start == Approx(3.0 / 30.0).margin(1.5 / 30.0));
    CHECK(t.end == Approx(24.0 / 30.0).margin(3.0 / 30.0));
    CHECK(t.peak > t.start - 1e-9);
    CHECK(t.peak < t.end);
    CHECK(t.main >= t.peak);
    CHECK(t.growth > 0.0f);   // it spreads
    CHECK(t.slowing > 1.0f);  // and slows
    CHECK(t.energy.size() == 30);
    CHECK(t.radius.size() == 30);
    CHECK(t.energy[0] < 0.06f);
    CHECK(t.energy[29] < 0.06f);
    REQUIRE_FALSE(t.bursts.empty());
    CHECK(t.bursts.front() == Approx(3.0 / 30.0).margin(2.5 / 30.0));

    bool appear = false, gone = false, peak = false;
    for (std::size_t i = 0; i < t.markers.size(); ++i) {
        appear = appear || t.markers[i].key == "appear";
        gone = gone || t.markers[i].key == "gone";
        peak = peak || t.markers[i].key == "peak";
        if (i > 0) {
            CHECK(t.markers[i].time >= t.markers[i - 1].time);
        }
    }
    CHECK(appear);
    CHECK(peak);
    CHECK(gone);

    REQUIRE(finding(analysed.value(), "timing"));
    CHECK(finding(analysed.value(), "timing")->basis == Basis::Observed);
    // The sparks' outward speed was measured from the clip.
    CHECK(analysed.value().still.sparks.count >= 10);
    CHECK(analysed.value().still.sparks.speed > 0.0f);
}

TEST_CASE("trimming and speed change a clip's timing", "[reference][time]") {
    const Reference clip = burstClip();
    ReferenceOptions options;
    options.firstFrame = 3;
    options.lastFrame = 26;
    options.speed = 2.0;
    int first = 0, last = 0;
    frameRange(clip, options, first, last);
    CHECK(first == 3);
    CHECK(last == 26);
    const auto analysed = analyzeReference(clip, options);
    REQUIRE(analysed.ok());
    const TimeAnalysis& t = analysed.value().time;
    CHECK(t.frames == 24);
    CHECK(t.framesPerSecond == Approx(60.0));       // twice as fast
    CHECK(t.start == Approx(0.0).margin(1.0 / 60.0));  // it is there from the first frame used
    CHECK(t.end == Approx(21.0 / 60.0).margin(3.0 / 60.0));
}

TEST_CASE("a pulsing clip is steady and loops", "[reference][time]") {
    Reference clip;
    clip.framesPerSecond = 30;
    for (int f = 0; f < 40; ++f) {
        Canvas canvas(128, 128);
        const float pulse = 0.5f + 0.5f * std::sin(2.0f * kPi * static_cast<float>(f) / 10.0f);
        canvas.light(0.4f, 1.0f, 0.5f, glow(0.5f, 0.5f, 0.22f + 0.08f * pulse, 0.6f + 0.3f * pulse));
        clip.frames.push_back(canvas.image());
    }
    const auto analysed = analyzeReference(clip, {});
    REQUIRE(analysed.ok());
    const TimeAnalysis& t = analysed.value().time;
    CHECK(t.continuous);
    CHECK(t.loops);
    CHECK(t.loopLength == Approx(10.0 / 30.0).margin(1.5 / 30.0));
    CHECK(t.bursts.empty());

    IdGenerator ids(5);
    const Reconstruction built = reconstruct(analysed.value(), {}, counting(ids));
    CHECK(built.effect.duration == Approx(t.loopLength).margin(0.02));
    CHECK(built.effect.loop == "loop");
}

TEST_CASE("turning and travelling are measured", "[reference][time]") {
    SECTION("a pattern turning counter-clockwise") {
        Reference clip;
        clip.framesPerSecond = 30;
        for (int f = 0; f < 24; ++f) {
            Canvas canvas(160, 160);
            canvas.light(1.0f, 0.8f, 0.3f, glow(0.5f, 0.5f, 0.15f, 0.8f));
            canvas.light(1.0f, 1.0f, 1.0f, rays(0.5f, 0.5f, 3, 0.4f, 0.05f, 2.0f * static_cast<float>(f)));  // 60 degrees a second
            clip.frames.push_back(canvas.image());
        }
        const auto analysed = analyzeReference(clip, {});
        REQUIRE(analysed.ok());
        const TimeAnalysis& t = analysed.value().time;
        CHECK(t.turning == Approx(60.0f).margin(12.0f));
        CHECK(t.turningConfidence > 0.5f);
        CHECK(finding(analysed.value(), "turning") != nullptr);
    }
    SECTION("an effect travelling to the right and up") {
        Reference clip;
        clip.framesPerSecond = 30;
        for (int f = 0; f < 24; ++f) {
            Canvas canvas(240, 160);
            const float t = static_cast<float>(f) / 30.0f;
            canvas.light(0.3f, 0.7f, 1.0f, glow(0.3f + 0.8f * t, 0.7f - 0.4f * t, 0.12f));
            clip.frames.push_back(canvas.image());
        }
        const auto analysed = analyzeReference(clip, {});
        REQUIRE(analysed.ok());
        const TimeAnalysis& t = analysed.value().time;
        CHECK(t.driftX == Approx(0.8f).margin(0.1f));
        CHECK(t.driftY == Approx(0.4f).margin(0.1f));  // up is positive
        CHECK(finding(analysed.value(), "travel") != nullptr);
    }
}

TEST_CASE("analysis reports progress and can be cancelled", "[reference]") {
    const Reference clip = burstClip();
    float last = -1.0f;
    int calls = 0;
    const auto done = analyzeReference(clip, {}, [&](float fraction, const char* stage) {
        CHECK(fraction >= last - 1e-6f);
        CHECK(stage != nullptr);
        last = fraction;
        ++calls;
        return true;
    });
    REQUIRE(done.ok());
    CHECK(calls > 10);
    CHECK(last == Approx(1.0f));
    CHECK(done.value().cost >= 0.0);

    int allowed = 5;
    const auto stopped = analyzeReference(clip, {}, [&](float, const char*) { return --allowed > 0; });
    REQUIRE_FALSE(stopped.ok());
    CHECK(stopped.error().message.find("cancelled") != std::string::npos);
}

// --------------------------------------------------------------- curves

TEST_CASE("measurements become curves with few keys", "[reference][curves]") {
    // A rise, a hold and a fall, sampled thirty times.
    std::vector<CurveKey> samples;
    for (int i = 0; i <= 30; ++i) {
        const double t = i / 30.0;
        const double v = t < 0.2 ? t / 0.2 : (t < 0.6 ? 1.0 : (1.0 - t) / 0.4);
        samples.push_back(CurveKey{t, v});
    }
    const auto keys = simplifyCurve(samples, 6, 0.03);
    REQUIRE(keys.size() >= 4);
    CHECK(keys.size() <= 6);
    CHECK(keys.front().t == Approx(0.0));
    CHECK(keys.back().t == Approx(1.0));
    for (std::size_t i = 1; i < keys.size(); ++i) {
        CHECK(keys[i].t > keys[i - 1].t);
    }
    // Reading the curve back stays close to every sample.
    for (const CurveKey& s : samples) {
        double v = keys.back().v;
        for (std::size_t i = 1; i < keys.size(); ++i) {
            if (s.t <= keys[i].t) {
                const double u = (s.t - keys[i - 1].t) / (keys[i].t - keys[i - 1].t);
                v = keys[i - 1].v + (keys[i].v - keys[i - 1].v) * u;
                break;
            }
        }
        CHECK(v == Approx(s.v).margin(0.05));
    }
    CHECK(simplifyCurve({}, 6, 0.03).empty());
    CHECK(simplifyCurve({CurveKey{0.3, 2.0}}, 6, 0.03).size() == 2);
    CHECK(simplifyCurve(samples, 2, 0.0).size() == 2);  // never more than asked for
}

TEST_CASE("a clip's growth and fade become the layers' curves", "[reference][curves]") {
    const auto analysed = analyzeReference(burstClip(), {});
    REQUIRE(analysed.ok());
    IdGenerator ids(11);
    const Reconstruction built = reconstruct(analysed.value(), {}, counting(ids));
    const TimeAnalysis& t = analysed.value().time;
    // The effect lasts as long as the burst did (plus a short pause before it repeats).
    CHECK(built.effect.duration == Approx((t.end - t.start) + 0.3).margin(0.02));
    CHECK(built.referenceStart == Approx(t.start));
    CHECK(built.peakTime >= 0.0);
    CHECK(built.peakTime <= t.end - t.start);

    const Layer* glowLayer = nullptr;
    for (const Layer& layer : built.effect.layers) {
        if (layer.role == "glow") {
            glowLayer = &layer;
        }
    }
    REQUIRE(glowLayer);
    for (const Module& m : glowLayer->modules) {
        if (m.type != "overLife") {
            continue;
        }
        const Scalar size = std::get<Scalar>(*m.find("size"));
        const Scalar opacity = std::get<Scalar>(*m.find("opacity"));
        REQUIRE(size.kind == Scalar::Kind::Curve);
        REQUIRE(opacity.kind == Scalar::Kind::Curve);
        CHECK(size.keys.size() >= 2);
        CHECK(size.keys.size() <= 8);
        // It grows, and it fades to nothing.
        CHECK(size.keys.back().v > size.keys.front().v);
        CHECK(opacity.keys.back().v == Approx(0.0).margin(0.05));
    }
    for (const LayerNote& note : built.layers) {
        if (note.role == "glow") {
            CHECK(note.motion == Basis::Observed);
        }
    }
}

// ----------------------------------------------------------- reconstruction

namespace {

ReferenceAnalysis busyAnalysis() {
    Canvas canvas(240, 240);
    canvas.light(1.0f, 0.4f, 0.1f, glow(0.5f, 0.5f, 0.3f, 0.7f));
    canvas.light(1.0f, 1.0f, 1.0f, glow(0.5f, 0.5f, 0.1f, 1.0f));
    canvas.light(1.0f, 0.8f, 0.4f, ring(0.5f, 0.5f, 0.36f, 0.02f));
    canvas.light(1.0f, 1.0f, 1.0f, rays(0.5f, 0.5f, 8, 0.33f, 0.02f));
    Dice dice;
    for (int i = 0; i < 30; ++i) {
        const float angle = dice.next() * 2.0f * kPi, distance = 0.4f + 0.07f * dice.next();
        canvas.light(1.0f, 0.9f, 0.5f, disc(0.5f + std::cos(angle) * distance, 0.5f + std::sin(angle) * distance, 0.008f));
    }
    return analyzeReference(still(canvas.image()), {}).value();
}

int particlesIn(const Reconstruction& built) { return built.particlesAtBusiest; }

}  // namespace

TEST_CASE("the rebuilt effect is made of ordinary, labelled layers", "[reference][reconstruct]") {
    const ReferenceAnalysis analysis = busyAnalysis();
    IdGenerator ids(21);
    const Reconstruction built = reconstruct(analysis, {}, counting(ids));
    REQUIRE(built.effect.layers.size() >= 3);
    CHECK(built.effect.layers.size() <= 7);  // Balanced keeps it manageable
    REQUIRE(built.layers.size() == built.effect.layers.size());

    bool glowFound = false, sparks = false;
    for (std::size_t i = 0; i < built.effect.layers.size(); ++i) {
        const Layer& layer = built.effect.layers[i];
        const LayerNote& note = built.layers[i];
        CHECK(note.layer == layer.id);
        CHECK(note.role == layer.role);
        CHECK_FALSE(layer.role.empty());
        CHECK_FALSE(note.technique.empty());
        CHECK_FALSE(layer.locked);
        // Every layer is a full, standard emitter: all its modules, and its Simple controls.
        CHECK(layer.modules.size() >= 6);
        CHECK(layer.controls.size() >= 10);
        for (const Module& m : layer.modules) {
            CHECK(m.known());
        }
        glowFound = glowFound || layer.role == "glow";
        sparks = sparks || layer.role == "sparks";
        // A still gives the look, never the motion.
        CHECK(note.motion == Basis::Inferred);
    }
    CHECK(glowFound);
    CHECK(sparks);
    CHECK_FALSE(built.notes.empty());  // what cannot be built is said
    CHECK(built.view.unitsHigh == Approx(kReferenceUnits));
    CHECK(built.budget == particleBudget(Target::Desktop));

    // It is a valid effect: it survives saving and loading unchanged.
    const std::string text = writeEffect(built.effect);
    const auto loaded = readEffect(text);
    REQUIRE(loaded.ok());
    CHECK(loaded.value().diagnostics.empty());
    CHECK(writeEffect(loaded.value().effect) == text);
    for (std::size_t i = 0; i < loaded.value().effect.layers.size(); ++i) {
        CHECK(loaded.value().effect.layers[i].role == built.effect.layers[i].role);
    }
}

TEST_CASE("modes and variations change what is built", "[reference][reconstruct]") {
    const ReferenceAnalysis analysis = busyAnalysis();
    const auto build = [&](MatchMode mode, Variation variation, Target target = Target::Desktop) {
        IdGenerator ids(31);
        ReconstructOptions options;
        options.mode = mode;
        options.variation = variation;
        options.target = target;
        return reconstruct(analysis, options, counting(ids));
    };
    const Reconstruction balanced = build(MatchMode::Balanced, Variation::Closest);
    const Reconstruction layered = build(MatchMode::Layered, Variation::Closest);
    const Reconstruction motion = build(MatchMode::Motion, Variation::Closest);
    const Reconstruction lean = build(MatchMode::Balanced, Variation::Performance);
    const Reconstruction rich = build(MatchMode::Balanced, Variation::Enhanced);
    const Reconstruction phone = build(MatchMode::Balanced, Variation::Closest, Target::Mobile);

    CHECK(layered.effect.layers.size() >= balanced.effect.layers.size());
    CHECK(motion.effect.layers.size() <= 4);
    CHECK(lean.effect.layers.size() <= 4);
    CHECK(particlesIn(lean) < particlesIn(balanced));
    CHECK(rich.effect.layers.size() > balanced.effect.layers.size());

    // What the enhanced take adds is marked as not being in the reference.
    int added = 0;
    for (const LayerNote& note : rich.layers) {
        if (note.from.empty()) {
            ++added;
            CHECK(note.note.find("Not in the reference") != std::string::npos);
        }
    }
    CHECK(added >= 1);
    for (const LayerNote& note : balanced.layers) {
        CHECK_FALSE(note.from.empty());
    }
    CHECK(phone.budget == particleBudget(Target::Mobile));
    CHECK(particlesIn(phone) <= particleBudget(Target::Mobile) + 12);
    CHECK(particleBudget(Target::Mobile) < particleBudget(Target::VR));
    CHECK(particleBudget(Target::VR) < particleBudget(Target::Desktop));

    // Names for the options round-trip.
    for (MatchMode m : {MatchMode::ShapeColour, MatchMode::Motion, MatchMode::Layered, MatchMode::Balanced}) {
        MatchMode parsed = MatchMode::Balanced;
        REQUIRE(parseMatchMode(matchModeName(m), parsed));
        CHECK(parsed == m);
    }
    for (Variation v : {Variation::Closest, Variation::Performance, Variation::Enhanced}) {
        Variation parsed = Variation::Closest;
        REQUIRE(parseVariation(variationName(v), parsed));
        CHECK(parsed == v);
    }
    Target target = Target::Desktop;
    CHECK(parseTarget("vr", target));
    CHECK(target == Target::VR);
    CHECK_FALSE(parseTarget("toaster", target));
}

TEST_CASE("a still can be rebuilt as one burst or as something steady", "[reference][reconstruct]") {
    const ReferenceAnalysis analysis = busyAnalysis();
    IdGenerator a(41), b(41);
    ReconstructOptions burst, steady;
    burst.pace = Pace::Burst;
    steady.pace = Pace::Steady;
    const Reconstruction once = reconstruct(analysis, burst, counting(a));
    const Reconstruction going = reconstruct(analysis, steady, counting(b));
    const auto emitsSteadily = [](const Reconstruction& built) {
        for (const Layer& layer : built.effect.layers) {
            if (layer.role != "sparks") {
                continue;
            }
            for (const Module& m : layer.modules) {
                if (m.type == "emission") {
                    return std::get<Scalar>(*m.find("rate")).a > 0.0;
                }
            }
        }
        return false;
    };
    CHECK_FALSE(emitsSteadily(once));
    CHECK(emitsSteadily(going));
}

// ------------------------------------------------------- comparing and fitting

TEST_CASE("an effect compared with a picture of itself measures alike", "[reference][compare]") {
    const ReferenceAnalysis analysis = busyAnalysis();
    IdGenerator ids(51);
    const Reconstruction built = reconstruct(analysis, {}, counting(ids));
    const Placement placement = placementOf(built);

    // Photograph the effect, and use the photograph as the reference.
    const Picture photo = drawLikeReference(built.effect, placement, analysis.still, 240, 240, placement.peakTime);
    Image image;
    image.width = photo.width;
    image.height = photo.height;
    image.rgba = photo.rgba;
    const Reference reference = still(image);
    const auto again = analyzeReference(reference, {});
    REQUIRE(again.ok());

    Placement same = placement;  // the same framing: the photo was taken with it
    const Similarity alike = compareToReference(reference, {}, again.value(), built.effect, same);
    CHECK(alike.silhouette > 0.95f);
    CHECK(alike.colour > 0.95f);
    CHECK(alike.brightness > 0.95f);
    CHECK(alike.detail > 0.9f);
    CHECK(alike.overall > 0.93f);
    CHECK(alike.motion < 0.0f);  // a still has no motion to measure
    CHECK(alike.timing < 0.0f);

    // Twice the size is measurably less alike, and the report says how.
    Effect large = built.effect;
    for (Layer& layer : large.layers) {
        for (Module& m : layer.modules) {
            if (m.type == "initial") {
                Scalar& size = std::get<Scalar>(*m.find("size"));
                size.a *= 2.0;
                size.b *= 2.0;
            }
        }
    }
    const Similarity off = compareToReference(reference, {}, again.value(), large, same);
    CHECK(off.silhouette < alike.silhouette - 0.1f);
    CHECK(off.overall < alike.overall);
    REQUIRE_FALSE(off.differences.empty());
    bool saidLarger = false;
    for (const std::string& d : off.differences) {
        saidLarger = saidLarger || d.find("larger") != std::string::npos || d.find("reaches too far") != std::string::npos;
    }
    CHECK(saidLarger);

    // Priorities change the overall figure, not the parts.
    Priorities onlyColour{0, 1, 0, 0, 0, 0, 0};
    const Similarity weighted = compareToReference(reference, {}, again.value(), large, same, onlyColour);
    CHECK(weighted.overall == Approx(weighted.colour));
    CHECK(weighted.silhouette == Approx(off.silhouette));
}

TEST_CASE("the comparison pictures line up", "[reference][compare]") {
    const ReferenceAnalysis analysis = busyAnalysis();
    IdGenerator ids(61);
    const Reconstruction built = reconstruct(analysis, {}, counting(ids));
    Canvas canvas(240, 240);
    canvas.light(1.0f, 0.4f, 0.1f, glow(0.5f, 0.5f, 0.3f, 0.7f));
    const Reference reference = still(canvas.image());

    const Picture a = drawReference(reference, {}, analysis.still, 0, 120, 120);
    const Picture b = drawLikeReference(built.effect, placementOf(built), analysis.still, 120, 120, built.peakTime);
    REQUIRE(a.width == 120);
    REQUIRE(b.width == 120);
    REQUIRE(a.rgba.size() == b.rgba.size());

    const Picture none = overlayPictures(a, b, 0.0f);
    const Picture all = overlayPictures(a, b, 1.0f);
    CHECK(none.rgba == a.rgba);
    CHECK(all.rgba == b.rgba);

    const Picture same = differencePicture(a, a);
    for (std::size_t i = 0; i < same.rgba.size(); i += 4) {
        REQUIRE(same.rgba[i] == 0);
        REQUIRE(same.rgba[i + 1] == 0);
        REQUIRE(same.rgba[i + 2] == 0);
    }
    const Picture different = differencePicture(a, b);
    long total = 0;
    for (std::size_t i = 0; i < different.rgba.size(); i += 4) {
        total += different.rgba[i] + different.rgba[i + 1] + different.rgba[i + 2];
    }
    CHECK(total > 0);

    // Before the effect begins there is only background.
    const Picture before = drawLikeReference(built.effect, placementOf(built), analysis.still, 64, 64, -0.5);
    for (std::size_t i = 0; i < before.rgba.size(); i += 4) {
        REQUIRE(before.rgba[i] == before.rgba[0]);
    }
}

TEST_CASE("the two clocks line up", "[reference][compare]") {
    const Reference clip = burstClip();
    const auto analysed = analyzeReference(clip, {});
    REQUIRE(analysed.ok());
    IdGenerator ids(71);
    const Reconstruction built = reconstruct(analysed.value(), {}, counting(ids));
    const Placement placement = placementOf(built);
    ReferenceOptions options;

    // Effect time 0 is where the clip's effect begins.
    const int startFrame = referenceFrameAt(clip, options, placement, 0.0);
    CHECK(startFrame == static_cast<int>(std::lround(analysed.value().time.start * 30.0)));
    for (int frame : {4, 10, 20}) {
        const double time = effectTimeAt(clip, options, placement, frame);
        CHECK(referenceFrameAt(clip, options, placement, time) == frame);
    }
    // Past either end, the nearest frame.
    CHECK(referenceFrameAt(clip, options, placement, -10.0) == 0);
    CHECK(referenceFrameAt(clip, options, placement, 100.0) == 29);

    // A trimmed, sped-up clip keeps the two in step too.
    options.firstFrame = 3;
    options.speed = 2.0;
    const auto fast = analyzeReference(clip, options);
    REQUIRE(fast.ok());
    IdGenerator more(72);
    const Placement quick = placementOf(reconstruct(fast.value(), {}, counting(more)));
    CHECK(effectTimeAt(clip, options, quick, 3 + 12) == Approx(12.0 / 60.0 - quick.referenceStart));

    // Motion and timing are measured for a clip.
    const Similarity s = compareToReference(clip, {}, analysed.value(), built.effect, placement);
    CHECK(s.motion >= 0.0f);
    CHECK(s.timing >= 0.0f);
    CHECK(s.timing > 0.5f);
    CHECK(s.motion > 0.5f);
}

TEST_CASE("a burst is shown once through; one the clip repeats comes round again", "[reference][compare]") {
    const Reference clip = burstClip();
    const auto analysed = analyzeReference(clip, {});
    REQUIRE(analysed.ok());
    IdGenerator ids(73);
    const Reconstruction built = reconstruct(analysed.value(), {}, counting(ids));
    const Placement placement = placementOf(built);
    const double length = built.effect.duration;
    REQUIRE(built.effect.loop == "loop");  // it repeats for watching...

    // ...but beside the reference it plays once, and then there is nothing.
    CHECK(comparedTime(analysed.value(), built.effect, 0.1) == Approx(0.1));
    CHECK(comparedTime(analysed.value(), built.effect, length + 0.1) < 0.0);

    const auto amount = [&](double time) {
        const Picture p = drawLikeReference(built.effect, placement, analysed.value().still, 96, 96, time);
        double sum = 0;
        for (std::size_t i = 0; i < p.rgba.size(); i += 4) {
            sum += p.rgba[i] + p.rgba[i + 1] + p.rgba[i + 2];
        }
        return sum;
    };
    // The last instants of the pass never show the next pass beginning.
    const double peak = amount(built.peakTime);
    REQUIRE(peak > 1000.0);
    for (double before : {0.03, 0.017, 0.009, 0.004, 0.0005}) {
        CAPTURE(before);
        CHECK(amount(length - before) < 0.05 * peak);
    }

    // The same burst shown twice in one clip is a rhythm: the second time
    // round is compared with the effect's second time round.
    Reference twice = clip;
    twice.frames.insert(twice.frames.end(), clip.frames.begin(), clip.frames.end());
    const auto again = analyzeReference(twice, {});
    REQUIRE(again.ok());
    REQUIRE(again.value().time.loops);
    REQUIRE_FALSE(again.value().time.continuous);
    IdGenerator more(74);
    const Reconstruction rhythm = reconstruct(again.value(), {}, counting(more));
    CHECK(rhythm.effect.duration == Approx(1.0).margin(0.04));
    CHECK(comparedTime(again.value(), rhythm.effect, rhythm.effect.duration + 0.1) == Approx(0.1).margin(1e-9));
    const Similarity s = compareToReference(twice, {}, again.value(), rhythm.effect, placementOf(rhythm));
    CHECK(s.timing > 0.7f);
    for (const std::string& said : s.differences) {
        CAPTURE(said);
        CHECK(said.find("sooner") == std::string::npos);
    }
}

TEST_CASE("a ring that thins as it widens fades, though the middle stays bright", "[reference][curves]") {
    Reference clip;
    clip.name = "ring";
    clip.framesPerSecond = 30;
    for (int f = 0; f < 30; ++f) {
        Canvas canvas(160, 160);
        if (f >= 2 && f < 22) {
            const float u = static_cast<float>(f - 2) / 19.0f;  // 0 to 1 over its life
            const float strength = 1.0f - 0.9f * u;
            const std::function<float(float, float)> band = ring(0.5f, 0.5f, 0.17f + 0.23f * u, 0.03f);
            canvas.light(0.4f, 0.5f, 1.0f, [&](float x, float y) { return strength * band(x, y); });
            // A white middle, as bright at the end as at the start.
            // (Large enough that the brightest twentieth of what is lit is all middle.)
            canvas.light(1.0f, 1.0f, 1.0f, disc(0.5f, 0.5f, 0.05f));
        }
        clip.frames.push_back(canvas.image());
    }
    const auto analysed = analyzeReference(clip, {});
    REQUIRE(analysed.ok());
    IdGenerator ids(75);
    const Reconstruction built = reconstruct(analysed.value(), {}, counting(ids));
    const Layer* ringLayer = nullptr;
    for (const Layer& layer : built.effect.layers) {
        if (layer.role == "ring") {
            ringLayer = &layer;
        }
    }
    REQUIRE(ringLayer);
    bool checked = false;
    for (const Module& m : ringLayer->modules) {
        if (m.type != "overLife") {
            continue;
        }
        const Scalar opacity = std::get<Scalar>(*m.find("opacity"));
        REQUIRE(opacity.kind == Scalar::Kind::Curve);
        double top = 0.0;
        for (const CurveKey& k : opacity.keys) {
            top = std::max(top, k.v);
        }
        // Read between the keys, three quarters of the way through. (The
        // last key is always zero, so it would prove nothing.)
        const auto at = [&](double t) {
            for (std::size_t i = 1; i < opacity.keys.size(); ++i) {
                const CurveKey& a = opacity.keys[i - 1];
                const CurveKey& b = opacity.keys[i];
                if (t <= b.t) {
                    return b.t > a.t ? a.v + (b.v - a.v) * (t - a.t) / (b.t - a.t) : b.v;
                }
            }
            return opacity.keys.back().v;
        };
        CHECK(top <= 1.0);
        // The painted ring is at a third of its strength by then. The middle
        // that does not fade is measured along with it, so the curve is not
        // that low, but it is well down.
        CHECK(at(0.75) < 0.6 * top);
        checked = true;
    }
    CHECK(checked);
}

TEST_CASE("what is meant to stay never blinks out between one pass and the next", "[reference][reconstruct]") {
    // Counts the moments, over three passes, when a layer that had something
    // on screen a frame before and a frame after has nothing; and, for a
    // layer that is one sprite, the moments when there are two.
    const auto blinks = [](const Effect& effect) {
        int found = 0;
        const int pass = static_cast<int>(std::lround(effect.duration / kSimulationStep));
        for (const Layer& layer : effect.layers) {
            Effect one = effect;
            one.layers = {layer};
            Simulation simulation(compileEffect(one));
            std::vector<int> alive;
            for (int step = pass; step <= 4 * pass + 2; ++step) {
                simulation.seek(step);
                alive.push_back(static_cast<int>(simulation.aliveCount()));
            }
            const bool single = std::count(alive.begin(), alive.end(), 1) * 2 > static_cast<std::ptrdiff_t>(alive.size());
            for (std::size_t i = 1; i + 1 < alive.size(); ++i) {
                if (alive[i] == 0 && alive[i - 1] > 0 && alive[i + 1] > 0) {
                    ++found;
                }
                if (single && alive[i] > 1) {
                    ++found;
                }
            }
        }
        return found;
    };

    // A steady glow, at loop lengths that land on either side of rounding.
    for (int frames : {6, 8, 10, 12, 15}) {
        CAPTURE(frames);
        Reference clip;
        clip.framesPerSecond = 30;
        for (int f = 0; f < 4 * frames; ++f) {
            Canvas canvas(128, 128);
            const float pulse = 0.5f + 0.5f * std::sin(2.0f * kPi * static_cast<float>(f) / static_cast<float>(frames));
            canvas.light(0.4f, 1.0f, 0.5f, glow(0.5f, 0.5f, 0.22f + 0.08f * pulse, 0.6f + 0.3f * pulse));
            clip.frames.push_back(canvas.image());
        }
        const auto analysed = analyzeReference(clip, {});
        REQUIRE(analysed.ok());
        REQUIRE(analysed.value().time.continuous);
        IdGenerator ids(81);
        Reconstruction built = reconstruct(analysed.value(), {}, counting(ids));
        REQUIRE(built.effect.loop == "loop");
        CHECK(blinks(built.effect) == 0);

        // Still so after the pace is changed.
        for (Refine change : {Refine::Faster, Refine::Slower, Refine::Slower, Refine::Slower}) {
            const auto next = refine(built.effect, change, &analysed.value(), counting(ids), nullptr);
            REQUIRE(next.ok());
            built.effect = next.value();
            CHECK(blinks(built.effect) == 0);
        }
    }

    // A head with a tail: the head is one sprite that stays.
    Canvas canvas(240, 240);
    for (int k = 0; k < 40; ++k) {
        const float t = static_cast<float>(k) / 39.0f;
        canvas.light(0.3f, 0.6f, 1.0f, glow(0.25f + 0.5f * t, 0.75f - 0.5f * t, 0.09f * (1.0f - 0.8f * t), 0.25f * (1.0f - t)));
    }
    canvas.light(1.0f, 1.0f, 1.0f, glow(0.25f, 0.75f, 0.07f));
    const auto comet = analyzeReference(still(canvas.image()), {});
    REQUIRE(comet.ok());
    REQUIRE(comet.value().still.comet);
    IdGenerator ids(82);
    const Reconstruction built = reconstruct(comet.value(), {}, counting(ids));
    CHECK(blinks(built.effect) == 0);
}

TEST_CASE("fitting brings a mis-sized effect back toward the reference", "[reference][fit]") {
    Canvas canvas(200, 200);
    canvas.light(0.3f, 0.6f, 1.0f, glow(0.5f, 0.5f, 0.32f, 0.8f));
    const Reference reference = still(canvas.image());
    const auto analysed = analyzeReference(reference, {});
    REQUIRE(analysed.ok());
    IdGenerator ids(81);
    Reconstruction built = reconstruct(analysed.value(), {}, counting(ids));
    const Placement placement = placementOf(built);

    // Spoil it: every size down to 60%.
    for (Layer& layer : built.effect.layers) {
        for (Module& m : layer.modules) {
            if (m.type == "initial") {
                Scalar& size = std::get<Scalar>(*m.find("size"));
                size.a *= 0.6;
                size.b *= 0.6;
            }
        }
    }
    Effect spoiled = built.effect;
    int steps = 0;
    const FitResult fitted = fitToReference(built.effect, reference, {}, analysed.value(), placement, {},
                                            [&](float, const char*) {
                                                ++steps;
                                                return true;
                                            });
    CHECK(fitted.after.overall > fitted.before.overall + 0.03f);
    CHECK(fitted.after.silhouette > fitted.before.silhouette);
    CHECK(fitted.drawings > 3);
    CHECK(steps > 0);
    CHECK_FALSE(fitted.cancelled);
    REQUIRE_FALSE(fitted.changes.empty());
    CHECK(fitted.changes.front().find("size up") != std::string::npos);

    // A locked layer is never adjusted.
    for (Layer& layer : spoiled.layers) {
        layer.locked = true;
    }
    const std::string before = writeEffect(spoiled);
    const FitResult held = fitToReference(spoiled, reference, {}, analysed.value(), placement);
    CHECK(writeEffect(spoiled) == before);
    CHECK(held.changes.empty());

    // Cancelling keeps whatever had been gained and says it stopped.
    Effect again = built.effect;
    int allowed = 2;
    const FitResult stopped = fitToReference(again, reference, {}, analysed.value(), placement, {},
                                             [&](float, const char*) { return --allowed > 0; });
    CHECK(stopped.cancelled);
    CHECK(stopped.after.overall >= stopped.before.overall - 1e-4f);
}

// ------------------------------------------------------------------ cut-outs

TEST_CASE("parts no shape matches can be cut from the reference, when asked", "[reference][cutout]") {
    Canvas canvas(240, 240);
    canvas.light(1.0f, 0.4f, 0.7f, glow(0.5f, 0.5f, 0.3f, 0.7f));
    canvas.light(1.0f, 1.0f, 1.0f, rays(0.5f, 0.5f, 7, 0.4f, 0.025f, 10.0f));
    const Reference reference = still(canvas.image());
    const auto analysed = analyzeReference(reference, {});
    REQUIRE(analysed.ok());
    REQUIRE(analysed.value().still.rays >= 5);

    const std::vector<Cutout> cutouts = makeCutouts(reference, {}, analysed.value());
    REQUIRE(cutouts.size() == 1);
    const Cutout& cut = cutouts.front();
    CHECK(cut.part == "rays");
    CHECK(cut.image.width == cut.image.height);
    CHECK(cut.image.width >= 16);
    CHECK(cut.centreX == Approx(0.5f).margin(0.02f));
    // See-through at the corners, solid somewhere along a ray, and the
    // round glow itself has been taken away.
    const auto alpha = [&](int x, int y) {
        return cut.image.rgba[static_cast<std::size_t>((y * cut.image.width + x) * 4 + 3)];
    };
    CHECK(alpha(0, 0) == 0);
    CHECK(alpha(cut.image.width - 1, cut.image.height - 1) == 0);
    int solid = 0, covered = 0;
    for (int y = 0; y < cut.image.height; ++y) {
        for (int x = 0; x < cut.image.width; ++x) {
            solid += alpha(x, y) > 128 ? 1 : 0;
            covered += alpha(x, y) > 8 ? 1 : 0;
        }
    }
    CHECK(solid > 20);
    CHECK(covered < cut.image.width * cut.image.height / 2);

    // Not used unless the options allow it.
    IdGenerator a(91), b(91);
    const Reconstruction plain = reconstruct(analysed.value(), {}, counting(a), &cutouts);
    CHECK(plain.pictures.empty());
    CHECK(plain.effect.assets.empty());

    ReconstructOptions allow;
    allow.cutouts = true;
    const Reconstruction withPicture = reconstruct(analysed.value(), allow, counting(b), &cutouts);
    REQUIRE(withPicture.pictures.size() == 1);
    REQUIRE(withPicture.effect.assets.size() == 1);
    CHECK(withPicture.effect.assets[0].kind == "texture");
    CHECK(withPicture.effect.assets[0].id == withPicture.pictures[0].asset);
    CHECK(withPicture.effect.assets[0].path == withPicture.pictures[0].path);
    CHECK(withPicture.pictures[0].path.rfind("images/reference-", 0) == 0);
    CHECK(decodePng(withPicture.pictures[0].png).ok());
    bool drawn = false;
    for (std::size_t i = 0; i < withPicture.effect.layers.size(); ++i) {
        if (isAssetReferenced(withPicture.effect, withPicture.pictures[0].asset) &&
            withPicture.layers[i].note.find("cut out of your reference") != std::string::npos) {
            drawn = true;
        }
    }
    CHECK(drawn);
    const ImageSet pictures = picturesOf(withPicture);
    CHECK(pictures.has(withPicture.pictures[0].asset));

    // With the rays' own pixels, it measures closer than with a built-in flash.
    const Similarity built = compareToReference(reference, {}, analysed.value(), plain.effect, placementOf(plain));
    const Similarity cutOut = compareToReference(reference, {}, analysed.value(), withPicture.effect,
                                                 placementOf(withPicture), {}, {}, &pictures);
    CHECK(cutOut.overall > built.overall - 0.02f);
    CHECK(cutOut.detail > 0.5f);
}

// ------------------------------------------------------------------ refining

TEST_CASE("each refinement is a real change to real properties", "[reference][refine]") {
    const ReferenceAnalysis analysis = busyAnalysis();
    IdGenerator ids(101);
    const IdSource newId = counting(ids);
    const Effect start = reconstruct(analysis, {}, newId).effect;

    const auto burstCount = [](const Effect& effect, const char* role) {
        std::int64_t total = 0;
        for (const Layer& layer : effect.layers) {
            if (layer.role != role) {
                continue;
            }
            for (const Module& m : layer.modules) {
                if (m.type == "emission") {
                    for (const Burst& b : std::get<BurstList>(*m.find("bursts")).items) {
                        total += b.count;
                    }
                }
            }
        }
        return total;
    };
    const auto sizeOf = [](const Effect& effect, const char* role) {
        for (const Layer& layer : effect.layers) {
            if (layer.role == role) {
                for (const Module& m : layer.modules) {
                    if (m.type == "initial") {
                        return std::get<Scalar>(*m.find("size")).a;
                    }
                }
            }
        }
        return 0.0;
    };
    const auto opacityOf = [](const Effect& effect, const char* role) {
        for (const Layer& layer : effect.layers) {
            if (layer.role == role) {
                double glowBy = 1.0, alpha = 1.0;
                for (const Module& m : layer.modules) {
                    if (m.type == "initial") {
                        alpha = std::get<Color>(*m.find("color")).a;
                    }
                    if (m.type == "sprite") {
                        glowBy = std::get<double>(*m.find("glow"));
                    }
                }
                return alpha * glowBy;
            }
        }
        return 0.0;
    };

    // Every refinement in the list runs and says what it did.
    for (const RefineInfo& info : refinements()) {
        CHECK(findRefinement(info.key) == &info);
        CHECK_FALSE(std::string(info.label).empty());
        CHECK_FALSE(std::string(info.help).empty());
        std::string said;
        const auto changed = refine(start, info.id, &analysis, newId, &said);
        if (info.needsClip) {
            CHECK_FALSE(changed.ok());  // the reference here is a still
            continue;
        }
        REQUIRE(changed.ok());
        CHECK_FALSE(said.empty());
        // And the result is still a valid effect.
        const auto loaded = readEffect(writeEffect(changed.value()));
        REQUIRE(loaded.ok());
        CHECK(loaded.value().diagnostics.empty());
    }
    CHECK(findRefinement("nonsense") == nullptr);

    const Effect more = refine(start, Refine::MoreSparks, nullptr, newId).value();
    CHECK(burstCount(more, "sparks") == Approx(static_cast<double>(burstCount(start, "sparks")) * 1.5).margin(1.0));
    CHECK(sizeOf(more, "glow") == sizeOf(start, "glow"));  // nothing else moved

    const Effect fewer = refine(start, Refine::FewerSparks, nullptr, newId).value();
    CHECK(burstCount(fewer, "sparks") < burstCount(start, "sparks"));

    const Effect bigger = refine(start, Refine::Bigger, nullptr, newId).value();
    CHECK(sizeOf(bigger, "glow") == Approx(sizeOf(start, "glow") * 1.15));
    const Effect back = refine(bigger, Refine::Smaller, nullptr, newId).value();
    CHECK(sizeOf(back, "glow") == Approx(sizeOf(start, "glow")));

    const Effect dimmer = refine(start, Refine::LessGlow, nullptr, newId).value();
    CHECK(opacityOf(dimmer, "glow") < opacityOf(start, "glow"));
    const Effect brighter = refine(start, Refine::MoreGlow, nullptr, newId).value();
    CHECK(opacityOf(brighter, "glow") > opacityOf(start, "glow"));

    const Effect faster = refine(start, Refine::Faster, nullptr, newId).value();
    CHECK(faster.duration == Approx(start.duration / 1.25));
    CHECK(faster.layers[0].duration == Approx(start.layers[0].duration / 1.25));
    const Effect slower = refine(start, Refine::Slower, nullptr, newId).value();
    CHECK(slower.duration == Approx(start.duration / 0.8));

    const Effect extra = refine(start, Refine::AddSecondary, nullptr, newId).value();
    REQUIRE(extra.layers.size() == start.layers.size() + 1);
    CHECK(extra.layers.back().role == "sparks");

    const Effect light = refine(more, Refine::ForMobile, nullptr, newId).value();
    CHECK(burstCount(light, "sparks") < burstCount(more, "sparks"));

    // Colours changed by hand come back to the reference's.
    Effect recoloured = start;
    for (Layer& layer : recoloured.layers) {
        for (Module& m : layer.modules) {
            if (m.type == "initial") {
                *m.find("color") = Color{0.0, 1.0, 0.0, std::get<Color>(*m.find("color")).a};
            }
        }
    }
    const Effect restored = refine(recoloured, Refine::MatchColours, &analysis, newId).value();
    for (std::size_t i = 0; i < restored.layers.size(); ++i) {
        if (restored.layers[i].role != "glow") {
            continue;
        }
        for (const Module& m : restored.layers[i].modules) {
            if (m.type == "initial") {
                const Color c = std::get<Color>(*m.find("color"));
                CHECK(c.r > c.g);  // orange again, not green
            }
        }
    }
    CHECK_FALSE(refine(start, Refine::MatchColours, nullptr, newId).ok());

    // Locked layers are left exactly as they were, and that is said.
    Effect held = start;
    for (Layer& layer : held.layers) {
        layer.locked = true;
    }
    std::string said;
    const Effect same = refine(held, Refine::Bigger, nullptr, newId, &said).value();
    CHECK(writeEffect(same) == writeEffect(held));
    CHECK(said.find("locked") != std::string::npos);
}

TEST_CASE("a clip's length can be matched again", "[reference][refine]") {
    const auto analysed = analyzeReference(burstClip(), {});
    REQUIRE(analysed.ok());
    IdGenerator ids(111);
    const IdSource newId = counting(ids);
    const Effect start = reconstruct(analysed.value(), {}, newId).effect;
    const Effect slow = refine(start, Refine::Slower, nullptr, newId).value();
    REQUIRE(slow.duration > start.duration * 1.1);
    std::string said;
    const Effect again = refine(slow, Refine::MatchDuration, &analysed.value(), newId, &said).value();
    CHECK(again.duration == Approx(start.duration).margin(0.01));
    CHECK_FALSE(said.empty());
}

// ------------------------------------------------------------------ session

TEST_CASE("a rebuilt effect goes into the session as one undo step", "[reference][session]") {
    Canvas canvas(240, 240);
    canvas.light(1.0f, 0.4f, 0.7f, glow(0.5f, 0.5f, 0.3f, 0.7f));
    canvas.light(1.0f, 1.0f, 1.0f, rays(0.5f, 0.5f, 7, 0.4f, 0.025f, 10.0f));
    const Reference reference = still(canvas.image());
    const auto analysed = analyzeReference(reference, {});
    REQUIRE(analysed.ok());
    const std::vector<Cutout> cutouts = makeCutouts(reference, {}, analysed.value());

    Session session;
    const std::string before = writeEffect(session.effect());
    const std::size_t layersBefore = session.effect().layers.size();
    Document& doc = session.document();
    const IdSource newId = [&doc]() { return doc.newId(); };
    ReconstructOptions allow;
    allow.cutouts = true;
    const Reconstruction built = reconstruct(analysed.value(), allow, newId, &cutouts);
    REQUIRE(built.pictures.size() == 1);

    REQUIRE(session.applyEffect(built.effect, "Rebuild from reference", &built.pictures).ok());
    CHECK(session.effect().layers.size() == built.effect.layers.size());
    CHECK(session.effect().duration == Approx(built.effect.duration));
    CHECK(session.commands().undoName() == "Rebuild from reference");
    // The picture was written where the effect will look for it, and loads.
    CHECK(std::filesystem::exists(session.projectFolder() / pathFromUtf8(built.pictures[0].path)));
    session.tick(0.0);
    CHECK(session.images().has(built.pictures[0].asset));
    CHECK(session.imageProblems().empty());

    // One undo brings the old effect back whole; redo, the new one.
    REQUIRE(session.undo().ok());
    CHECK(writeEffect(session.effect()) == before);
    CHECK(session.effect().layers.size() == layersBefore);
    REQUIRE(session.redo().ok());
    CHECK(session.effect().layers.size() == built.effect.layers.size());

    // Rebuilding without the cut-out drops the picture that nothing draws.
    const Reconstruction plain = reconstruct(analysed.value(), {}, newId);
    REQUIRE(session.applyEffect(plain.effect, "Rebuild from reference").ok());
    CHECK(session.effect().assets.empty());

    // The effect in the session plays.
    session.clock().seek(built.peakTime);
    session.tick(0.0);
    CHECK(session.particleCount() > 0);
}

TEST_CASE("every kind of rebuilt effect is accepted by the session", "[reference][session]") {
    // The session checks every value against the limits its property
    // allows, which reading a file back does not.
    const auto checkAll = [](const ReferenceAnalysis& analysis) {
        for (MatchMode mode : {MatchMode::Balanced, MatchMode::ShapeColour, MatchMode::Motion, MatchMode::Layered}) {
            for (Variation variation : {Variation::Closest, Variation::Performance, Variation::Enhanced}) {
                for (Pace pace : {Pace::Auto, Pace::Burst, Pace::Steady}) {
                    Session session;
                    Document& doc = session.document();
                    const IdSource newId = [&doc]() { return doc.newId(); };
                    ReconstructOptions options;
                    options.mode = mode;
                    options.variation = variation;
                    options.pace = pace;
                    const Reconstruction built = reconstruct(analysis, options, newId);
                    const Status applied = session.applyEffect(built.effect, "Rebuild");
                    INFO(matchModeName(mode) << " / " << variationName(variation) << ": "
                                             << (applied.ok() ? std::string() : applied.error().message + " " + applied.error().detail));
                    REQUIRE(applied.ok());
                    // Every refinement of it is accepted too.
                    for (const RefineInfo& info : refinements()) {
                        auto changed = refine(session.effect(), info.id, &analysis, newId);
                        if (changed) {
                            const Status again = session.applyEffect(changed.value(), info.label);
                            INFO(info.key << ": " << (again.ok() ? std::string() : again.error().message + " " + again.error().detail));
                            REQUIRE(again.ok());
                        }
                    }
                }
            }
        }
    };
    checkAll(busyAnalysis());
    checkAll(analyzeReference(burstClip(), {}).value());

    // A clip that flares far brighter at its start than at the moment
    // described must still give opacity curves that stay within 0 to 1.
    Reference flare;
    flare.framesPerSecond = 30;
    for (int f = 0; f < 30; ++f) {
        Canvas canvas(128, 128);
        const float t = static_cast<float>(f) / 30.0f;
        if (f >= 2 && f < 26) {
            canvas.light(1.0f, 0.6f, 0.2f, glow(0.5f, 0.5f, 0.1f + 0.25f * t, f < 5 ? 1.0f : 0.25f * (1.0f - t)));
        }
        flare.frames.push_back(canvas.image());
    }
    const auto flared = analyzeReference(flare, {});
    REQUIRE(flared.ok());
    checkAll(flared.value());
}

TEST_CASE("the original reference is kept with the project, untouched", "[reference][session]") {
    const auto folder = std::filesystem::temp_directory_path() / "vfxforge_reference_test";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);

    Canvas canvas(96, 96);
    canvas.light(1.0f, 0.5f, 0.1f, glow(0.5f, 0.5f, 0.3f));
    const std::string original = encodePng(canvas.image());
    const std::string notes = R"({"backdrop":"dark","crop":[0,0,1,1],"mode":"balanced"})";

    Session session;
    CHECK_FALSE(session.keptReference().found);
    REQUIRE(session.keepReference("My Flash Ref.PNG", original, notes).ok());
    Session::KeptReference kept = session.keptReference();
    REQUIRE(kept.found);
    CHECK(kept.name.rfind("my-flash-ref-", 0) == 0);
    CHECK(kept.name.size() > 4);
    CHECK(kept.name.substr(kept.name.size() - 4) == ".png");
    CHECK(kept.notes == notes);
    CHECK(readFile(kept.file).value() == original);  // byte for byte

    // It is not a picture any layer draws, and Unity is not sent it.
    session.tick(0.0);
    CHECK(session.images().size() == 0);
    CHECK(session.imageProblems().empty());

    // Saving somewhere else takes the reference along; opening finds it.
    const auto file = folder / "From Reference.vfx";
    REQUIRE(session.saveAs(file).ok());
    Session reopened;
    REQUIRE(reopened.open(file).ok());
    kept = reopened.keptReference();
    REQUIRE(kept.found);
    CHECK(kept.notes == notes);
    CHECK(kept.file.parent_path() == folder / "reference");
    CHECK(readFile(kept.file).value() == original);
    CHECK(reopened.loadNotes().empty());

    // New settings replace the notes; a new reference replaces the old one.
    REQUIRE(reopened.setReferenceNotes(R"({"backdrop":"light"})").ok());
    CHECK(reopened.keptReference().notes == R"({"backdrop":"light"})");
    REQUIRE(reopened.keepReference("other.png", original + "x", "{}").ok());
    int references = 0;
    for (const Asset& a : reopened.effect().assets) {
        references += a.kind == "reference" ? 1 : 0;
    }
    CHECK(references == 1);
    CHECK(reopened.keptReference().name.rfind("other-", 0) == 0);

    CHECK_FALSE(session.keepReference("empty.png", "", "{}").ok());
    Session bare;
    CHECK_FALSE(bare.setReferenceNotes("{}").ok());
    std::filesystem::remove_all(folder);
}

TEST_CASE("layers can be reordered, copied, renamed, hidden and locked", "[reference][session]") {
    Session session;
    const ReferenceAnalysis analysis = busyAnalysis();
    Document& doc = session.document();
    const IdSource newId = [&doc]() { return doc.newId(); };
    REQUIRE(session.applyEffect(reconstruct(analysis, {}, newId).effect, "Rebuild").ok());
    const std::size_t count = session.effect().layers.size();
    REQUIRE(count >= 3);
    const Id first = session.effect().layers[0].id;
    const Id second = session.effect().layers[1].id;

    REQUIRE(session.moveLayer(first, 1).ok());
    CHECK(session.effect().layers[0].id == second);
    CHECK(session.effect().layers[1].id == first);
    REQUIRE(session.undo().ok());
    CHECK(session.effect().layers[0].id == first);

    Id copy;
    REQUIRE(session.duplicateLayer(first, &copy).ok());
    REQUIRE(session.effect().layers.size() == count + 1);
    CHECK(session.effect().layers[1].id == copy);  // just above the original
    const Layer& original = session.effect().layers[0];
    const Layer& copied = session.effect().layers[1];
    CHECK(copied.name == original.name + " copy");
    CHECK(copied.role == original.role);
    REQUIRE(copied.modules.size() == original.modules.size());
    for (std::size_t i = 0; i < copied.modules.size(); ++i) {
        CHECK(copied.modules[i].id != original.modules[i].id);
        CHECK(copied.modules[i].values == original.modules[i].values);
    }
    // The copy's Simple controls drive the copy, not the original.
    REQUIRE_FALSE(copied.controls.empty());
    for (const SimpleControl& control : copied.controls) {
        for (const ControlTarget& target : control.targets) {
            CHECK(findModule(copied, target.module) != nullptr);
        }
    }
    CHECK_FALSE(session.duplicateLayer(Id{}, nullptr).ok());

    REQUIRE(session.set(Path::layerField(first, "name"), Value(std::string("Halo"))).ok());
    REQUIRE(session.set(Path::layerField(first, "enabled"), Value(false)).ok());
    REQUIRE(session.set(Path::layerField(first, "locked"), Value(true)).ok());
    CHECK(session.effect().layers[0].name == "Halo");
    CHECK_FALSE(session.effect().layers[0].enabled);
    CHECK(session.effect().layers[0].locked);

    // Locked and role survive a save and load; files without them are unchanged.
    const std::string text = writeEffect(session.effect());
    CHECK(text.find("\"locked\"") != std::string::npos);
    const auto loaded = readEffect(text);
    REQUIRE(loaded.ok());
    CHECK(loaded.value().effect.layers[0].locked);
    CHECK(loaded.value().effect.layers[0].role == session.effect().layers[0].role);
    Session plain;
    CHECK(writeEffect(plain.effect()).find("\"locked\"") == std::string::npos);
    CHECK(writeEffect(plain.effect()).find("\"role\"") == std::string::npos);
}

TEST_CASE("the shapes are measured the way layers are sized", "[reference][shapes]") {
    // A disc fills its particle nearly to the edge; a soft blob fades out at it.
    CHECK(measureShape(SpriteShape::Disc).extent > 0.8f);
    CHECK(measureShape(SpriteShape::Soft).outer > 0.7f);
    CHECK(measureShape(SpriteShape::Soft).halfLevel < measureShape(SpriteShape::Disc).halfLevel);
    CHECK(measureShape(SpriteShape::Ring).ringRadius > 0.6f);
    CHECK(measureShape(SpriteShape::Starflash).shine > measureShape(SpriteShape::Disc).shine);
    for (int i = 0; i < kSpriteShapeCount; ++i) {
        const ShapeMeasure& m = measureShape(static_cast<SpriteShape>(i));
        CHECK(m.extent > 0.1f);
        CHECK(m.extent <= 1.4f);
        CHECK(m.reach > 0.3f);
    }
    CHECK(hexColour(Swatch{1.0f, 0.5f, 0.0f, 0}) == "#ff8000");
    CHECK(colourName(Swatch{1.0f, 0.5f, 0.0f, 0}) == "orange");
    CHECK(colourName(Swatch{1.0f, 1.0f, 1.0f, 0}) == "white");
    CHECK(colourName(Swatch{0.1f, 0.2f, 0.9f, 0}) == "blue");
    CHECK(std::string(backdropName(Backdrop::Transparent)) == "transparent");
}
