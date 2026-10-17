// Reference to VFX: lines of light. Frames round symbols, bolts down the
// sides of light columns: read as lines, followed along their middles, and
// rebuilt on path emitters. The references are painted here, so what should
// be found is known.
#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "vfx/Templates.h"
#include "vfx/editor/Compare.h"
#include "vfx/editor/Reconstruct.h"
#include "vfx/editor/Reference.h"

using namespace vfx;
using namespace vfx::editor;
using Catch::Approx;

namespace {

constexpr float kPi = 3.14159265f;

// Light on black. Coordinates are fractions of the height, from the top-left.
class Canvas {
public:
    Canvas(int width, int height) : w_(width), h_(height), pixels_(static_cast<std::size_t>(width * height) * 3, 0.0f) {}

    void light(float r, float g, float b, const std::function<float(float, float)>& amount) {
        for (int y = 0; y < h_; ++y) {
            for (int x = 0; x < w_; ++x) {
                const float a = amount((static_cast<float>(x) + 0.5f) / static_cast<float>(h_),
                                       (static_cast<float>(y) + 0.5f) / static_cast<float>(h_));
                float* p = &pixels_[static_cast<std::size_t>((y * w_ + x) * 3)];
                p[0] += r * a;
                p[1] += g * a;
                p[2] += b * a;
            }
        }
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
    int w_, h_;
    std::vector<float> pixels_;
};

// How far a point is from the edge of a box (positive outside and inside alike).
float fromBoxEdge(float x, float y, float l, float t, float r, float b) {
    const float cx = 0.5f * (l + r), cy = 0.5f * (t + b), hx = 0.5f * (r - l), hy = 0.5f * (b - t);
    const float qx = std::fabs(x - cx) - hx, qy = std::fabs(y - cy) - hy;
    const float outside = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f));
    const float inside = std::min(std::max(qx, qy), 0.0f);
    return std::fabs(outside + inside);
}

// A line of light thickness wide along the edge of a box, with a soft halo.
std::function<float(float, float)> frameLine(float l, float t, float r, float b, float thickness,
                                             const std::function<bool(float, float)>& lit = {}) {
    return [=](float x, float y) {
        if (lit && !lit(x, y)) {
            return 0.0f;
        }
        const float d = fromBoxEdge(x, y, l, t, r, b);
        const float core = std::clamp((0.5f * thickness - d) / 0.003f + 0.5f, 0.0f, 1.0f);
        return std::max(core, 0.35f * std::exp(-d / 0.015f));
    };
}

std::function<float(float, float)> dot(float cx, float cy, float radius) {
    return [=](float x, float y) {
        const float d = std::hypot(x - cx, y - cy);
        return std::clamp((radius - d) / 0.003f + 0.5f, 0.0f, 1.0f);
    };
}

// A crackling upright bolt: two thin strands braiding about x, from top to bottom.
std::function<float(float, float)> bolt(float x0, float top, float bottom, float seed) {
    return [=](float x, float y) {
        if (y < top || y > bottom) {
            return 0.0f;
        }
        float best = 0.0f;
        for (int s = 0; s < 2; ++s) {
            const float phase = seed + 1.7f * static_cast<float>(s);
            const float cx = x0 + 0.012f * std::sin(y * 70.0f + phase) + 0.006f * std::sin(y * 190.0f + 2.0f * phase);
            const float d = std::fabs(x - cx);
            best = std::max(best, std::clamp((0.004f - d) / 0.003f + 0.5f, 0.0f, 1.0f));
            best = std::max(best, 0.3f * std::exp(-d / 0.01f));
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

IdSource counting(IdGenerator& ids) {
    return [&ids]() { return ids.next(); };
}

const Module* moduleOf(const Layer& layer, const char* type) {
    for (const Module& m : layer.modules) {
        if (m.type == type) {
            return &m;
        }
    }
    return nullptr;
}

std::string textOf(const Layer& layer, const char* type, const char* key) {
    const Module* m = moduleOf(layer, type);
    const Value* v = m ? m->find(key) : nullptr;
    const auto* s = v ? std::get_if<std::string>(v) : nullptr;
    return s ? *s : std::string();
}

const Layer* layerNamed(const Effect& effect, const std::string& name) {
    for (const Layer& layer : effect.layers) {
        if (layer.name == name) {
            return &layer;
        }
    }
    return nullptr;
}

// A frame of light, 0.1 to 0.55 across and 0.1 to 0.9 down (fractions of the
// height), on a picture two thirds as wide as it is high, with sparkles.
Image sparkFrame() {
    Canvas canvas(160, 240);
    canvas.light(1.0f, 0.55f, 0.15f, frameLine(0.1f, 0.1f, 0.55f, 0.9f, 0.02f));
    const float at[][2] = {{0.12f, 0.3f}, {0.53f, 0.62f}, {0.3f, 0.885f}, {0.42f, 0.115f}, {0.085f, 0.75f}, {0.565f, 0.2f}};
    for (const auto& p : at) {
        canvas.light(1.0f, 0.9f, 0.5f, dot(p[0], p[1], 0.006f));
    }
    return canvas.image();
}

}  // namespace

TEST_CASE("a hollow frame of light is read as an outline, not a solid shape", "[reference][lines]") {
    const auto analysis = analyzeReference(still(sparkFrame()), {});
    REQUIRE(analysis.ok());
    const StillAnalysis& s = analysis.value().still;
    REQUIRE(s.linesLead);
    REQUIRE(s.lines.size() == 1);
    const LineFound& line = s.lines.front();
    CHECK(line.closed);
    CHECK_FALSE(s.hasBody);
    CHECK_FALSE(s.comet);
    CHECK(line.thickness > 0.01f);
    CHECK(line.thickness < 0.05f);
    CHECK(line.jagged < 0.35f);
    CHECK(line.sparkles >= 3);
    // Its middle runs where the frame is: 0.1 to 0.55 of the height across
    // (a picture two thirds as wide as high), 0.1 to 0.9 down.
    double l = 1, r = 0, t = 1, b = 0;
    for (const Point2& p : line.points) {
        l = std::min(l, p.x);
        r = std::max(r, p.x);
        t = std::min(t, p.y);
        b = std::max(b, p.y);
    }
    const double aspect = 160.0 / 240.0;
    CHECK(l * aspect == Approx(0.1).margin(0.025));
    CHECK(r * aspect == Approx(0.55).margin(0.025));
    CHECK(t == Approx(0.1).margin(0.025));
    CHECK(b == Approx(0.9).margin(0.025));
    CHECK(line.length == Approx(2.0 * (0.45 + 0.8)).epsilon(0.1));

    bool reported = false;
    for (const Finding& f : analysis.value().findings) {
        reported = reported || f.key == "line";
        CHECK(f.key != "body");
    }
    CHECK(reported);
}

TEST_CASE("an outline is rebuilt on a path that follows it", "[reference][lines]") {
    const Reference reference = still(sparkFrame());
    const auto analysis = analyzeReference(reference, {});
    REQUIRE(analysis.ok());
    IdGenerator ids(7);
    const Reconstruction built = reconstruct(analysis.value(), {}, counting(ids));
    const Layer* outline = layerNamed(built.effect, "Outline");
    REQUIRE(outline);
    CHECK(textOf(*outline, "shape", "shape") == "path");
    const Module* shape = moduleOf(*outline, "shape");
    REQUIRE(shape);
    const auto* closed = std::get_if<bool>(shape->find("closed"));
    REQUIRE(closed);
    CHECK(*closed);
    const auto* path = std::get_if<PointList>(shape->find("path"));
    REQUIRE(path);
    double l = 1e9, r = -1e9, t = -1e9, b = 1e9;
    for (const Point2& p : path->points) {
        l = std::min(l, p.x);
        r = std::max(r, p.x);
        t = std::max(t, p.y);
        b = std::min(b, p.y);
    }
    // The reference's height is eight units: the frame is 0.45 x 0.8 of it.
    CHECK(r - l == Approx(0.45 * kReferenceUnits).epsilon(0.08));
    CHECK(t - b == Approx(0.8 * kReferenceUnits).epsilon(0.08));
    CHECK(layerNamed(built.effect, "Outline sparkles"));
    bool solidShape = false;
    for (const LayerNote& note : built.layers) {
        solidShape = solidShape || note.from == "body";
    }
    CHECK_FALSE(solidShape);

    // And it looks like the frame. As built, it is in the right place in
    // the right colour; once fitted (as the app always does), its outline
    // matches closely. Before lines were read, this picture rebuilt as one
    // solid shape at about 40% outline.
    const Placement placement = placementOf(built);
    const Similarity alike = compareToReference(reference, {}, analysis.value(), built.effect, placement);
    CHECK(alike.silhouette > 0.45f);
    CHECK(alike.colour > 0.8f);
    Effect fitted = built.effect;
    const FitResult fit = fitToReference(fitted, reference, {}, analysis.value(), placement);
    CHECK(fit.after.silhouette > 0.8f);
    CHECK(fit.after.overall > 0.85f);
}

TEST_CASE("two crackling bolts with light between them are bolts and a fill, not a comet", "[reference][lines]") {
    Canvas canvas(160, 320);
    // The fill: brighter toward the bottom.
    canvas.light(0.1f, 0.6f, 0.9f, [](float x, float y) {
        if (x < 0.07f || x > 0.43f || y < 0.05f || y > 0.95f) {
            return 0.0f;
        }
        return 0.2f + 0.45f * (y - 0.05f) / 0.9f;
    });
    canvas.light(0.5f, 1.0f, 0.95f, bolt(0.07f, 0.04f, 0.96f, 0.0f));
    canvas.light(0.5f, 1.0f, 0.95f, bolt(0.43f, 0.04f, 0.96f, 2.0f));
    const Reference reference = still(canvas.image());
    const auto analysis = analyzeReference(reference, {});
    REQUIRE(analysis.ok());
    const StillAnalysis& s = analysis.value().still;
    REQUIRE(s.linesLead);
    CHECK_FALSE(s.comet);
    REQUIRE(s.lines.size() == 2);
    for (const LineFound& line : s.lines) {
        CHECK_FALSE(line.closed);
        CHECK(line.length > 0.8f);
        CHECK(line.jagged >= 0.5f);
        CHECK(line.band > 1.5f * line.thickness);  // two strands braiding
    }
    const double first = s.lines[0].points.front().x, second = s.lines[1].points.front().x;
    CHECK(std::min(first, second) * 0.5 == Approx(0.07).margin(0.02));
    CHECK(std::max(first, second) * 0.5 == Approx(0.43).margin(0.02));
    REQUIRE(s.lineFill.present);
    CHECK(s.lineFill.upright);
    CHECK_FALSE(s.lineFill.brightFirst);  // brightest at the bottom
    CHECK(s.lineFill.profile.front() > 1.5f * s.lineFill.profile.back());

    IdGenerator ids(9);
    const Reconstruction built = reconstruct(analysis.value(), {}, counting(ids));
    const Layer* crackle = layerNamed(built.effect, "Bolt 1 crackle");
    REQUIRE(crackle);
    CHECK(textOf(*crackle, "sprite", "shape") == "bolt");
    CHECK(textOf(*crackle, "shape", "shape") == "path");
    CHECK(layerNamed(built.effect, "Bolt 2"));
    CHECK(layerNamed(built.effect, "Fill"));
    for (const LayerNote& note : built.layers) {
        CHECK(note.role != "trail");  // no comet tail
    }
}

TEST_CASE("a frame drawn on from a corner is drawn on in the rebuild, from that corner", "[reference][lines][clip]") {
    // The frame's edge, measured round from the top-left corner, clockwise
    // as seen; the clip shows more of it each frame from frame 2 to 11.
    const float l = 0.1f, t = 0.1f, r = 0.55f, b = 0.9f;
    const float perimeter = 2.0f * ((r - l) + (b - t));
    const auto along = [=](float x, float y) {
        // Which side is nearest, and how far round that puts the point.
        const float dl = std::fabs(x - l), dr = std::fabs(x - r), dt = std::fabs(y - t), db = std::fabs(y - b);
        const float least = std::min({dl, dr, dt, db});
        float s = 0;
        if (least == dt) {
            s = std::clamp(x - l, 0.0f, r - l);
        } else if (least == dr) {
            s = (r - l) + std::clamp(y - t, 0.0f, b - t);
        } else if (least == db) {
            s = (r - l) + (b - t) + std::clamp(r - x, 0.0f, r - l);
        } else {
            s = 2.0f * (r - l) + (b - t) + std::clamp(b - y, 0.0f, b - t);
        }
        return s / perimeter;
    };
    Reference clip;
    clip.name = "trace";
    clip.framesPerSecond = 20;
    for (int f = 0; f < 24; ++f) {
        const float shown = std::clamp((static_cast<float>(f) - 1.0f) / 10.0f, 0.0f, 1.0f);
        Canvas canvas(160, 240);
        if (shown > 0) {
            canvas.light(1.0f, 0.8f, 0.3f, frameLine(l, t, r, b, 0.02f, [&](float x, float y) {
                return along(x, y) <= shown;
            }));
        }
        clip.frames.push_back(canvas.image());
    }
    const auto analysis = analyzeReference(clip, {});
    REQUIRE(analysis.ok());
    const StillAnalysis& s = analysis.value().still;
    REQUIRE(s.linesLead);
    REQUIRE(s.lines.size() == 1);
    const LineFound& line = s.lines.front();
    CHECK(line.closed);
    REQUIRE(line.drawn);
    CHECK(line.drawTime == Approx(0.5).margin(0.15));
    CHECK(line.drawFrom == Approx(0.1).margin(0.1));
    CHECK(line.drawWay != 0);
    // It starts at the top-left corner.
    const std::size_t n = line.points.size();
    const Point2 start = line.points[std::min(n - 1, static_cast<std::size_t>(std::lround(line.drawStart * n)) % n)];
    CHECK(start.x * (160.0 / 240.0) == Approx(0.1).margin(0.08));
    CHECK(start.y == Approx(0.1).margin(0.08));

    IdGenerator ids(11);
    const Reconstruction built = reconstruct(analysis.value(), {}, counting(ids));
    const Layer* outline = layerNamed(built.effect, "Outline");
    REQUIRE(outline);
    CHECK(textOf(*outline, "shape", "travelMode") == "draw");
    // The path starts where the drawing starts.
    const auto* path = std::get_if<PointList>(moduleOf(*outline, "shape")->find("path"));
    REQUIRE(path);
    const Point2 first = path->points.front();
    const double aspect = 160.0 / 240.0;
    const double expectX = (0.1 / aspect - s.centreX) * aspect * kReferenceUnits;
    const double expectY = (s.centreY - 0.1) * kReferenceUnits;
    CHECK(first.x == Approx(expectX).margin(0.6));
    CHECK(first.y == Approx(expectY).margin(0.6));
    CHECK(outline->start == Approx(line.drawFrom - built.referenceStart).margin(0.06));
}

TEST_CASE("light running round a frame is measured and rebuilt running", "[reference][lines][clip]") {
    const float l = 0.1f, t = 0.1f, r = 0.55f, b = 0.9f;
    const float cx = 0.5f * (l + r), cy = 0.5f * (t + b);
    Reference clip;
    clip.name = "run";
    clip.framesPerSecond = 30;
    const float rounds = 0.5f;  // counter-clockwise as seen, half a turn a second
    for (int f = 0; f < 45; ++f) {
        Canvas canvas(160, 240);
        canvas.light(0.9f, 0.5f, 0.1f, [=](float x, float y) { return 0.6f * frameLine(l, t, r, b, 0.02f)(x, y); });
        const float angle = 2.0f * kPi * rounds * static_cast<float>(f) / 30.0f;
        canvas.light(1.0f, 0.95f, 0.8f, [=](float x, float y) {
            const float a = std::atan2(cy - y, x - cx);
            float d = std::fabs(std::remainder(a - angle, 2.0f * kPi));
            return frameLine(l, t, r, b, 0.02f)(x, y) > 0.9f ? 0.8f * std::exp(-d * d / 0.08f) : 0.0f;
        });
        clip.frames.push_back(canvas.image());
    }
    const auto analysis = analyzeReference(clip, {});
    REQUIRE(analysis.ok());
    const StillAnalysis& s = analysis.value().still;
    REQUIRE(s.lines.size() == 1);
    const LineFound& line = s.lines.front();
    CHECK_FALSE(line.drawn);
    CHECK(line.runConfidence >= 0.6f);
    // The frame is not round, so the patch runs faster along the short
    // sides than the long ones; on average it goes round half a turn a second.
    CHECK(std::fabs(line.run) == Approx(rounds).margin(0.15));

    IdGenerator ids(13);
    const Reconstruction built = reconstruct(analysis.value(), {}, counting(ids));
    const Layer* runner = layerNamed(built.effect, "Outline runner");
    REQUIRE(runner);
    const auto* travel = std::get_if<double>(moduleOf(*runner, "shape")->find("travel"));
    REQUIRE(travel);
    CHECK(std::fabs(*travel) == Approx(std::fabs(line.run)).epsilon(0.01));
}

TEST_CASE("a steady frame does not run, draw on or flicker", "[reference][lines][clip]") {
    Reference clip;
    clip.name = "steady";
    clip.framesPerSecond = 24;
    for (int f = 0; f < 24; ++f) {
        Canvas canvas(160, 240);
        canvas.light(0.3f, 0.7f, 1.0f, frameLine(0.1f, 0.1f, 0.55f, 0.9f, 0.02f));
        clip.frames.push_back(canvas.image());
    }
    const auto analysis = analyzeReference(clip, {});
    REQUIRE(analysis.ok());
    REQUIRE(analysis.value().still.lines.size() == 1);
    const LineFound& line = analysis.value().still.lines.front();
    CHECK_FALSE(line.drawn);
    CHECK(line.run == 0.0f);
    CHECK(line.flicker < 0.1f);
    for (float v : line.levelOverTime) {
        CHECK(v == Approx(1.0f).margin(0.05));
    }
}

// ------------------------------------------------------------------ sheets

TEST_CASE("each effect on a sheet of several is found, in reading order", "[reference][sheet]") {
    // Three rows: a frame and a glow above, a pair of bolts with light
    // between them below, on a picture 1.5 times as wide as high. A spark
    // flies off the frame; a faint mark far from everything is noise.
    Canvas canvas(360, 240);
    canvas.light(1.0f, 0.6f, 0.2f, frameLine(0.05f, 0.05f, 0.4f, 0.4f, 0.015f));
    canvas.light(1.0f, 0.9f, 0.5f, dot(0.43f, 0.2f, 0.005f));
    canvas.light(0.3f, 0.6f, 1.0f, [](float x, float y) {
        const float d = std::hypot(x - 1.1f, y - 0.22f) / 0.17f;
        return d < 1.0f ? (1.0f - d * d) * (1.0f - d * d) : 0.0f;
    });
    canvas.light(0.1f, 0.6f, 0.9f, [](float x, float y) {
        return x > 0.62f && x < 0.88f && y > 0.55f && y < 0.95f ? 0.35f : 0.0f;
    });
    canvas.light(0.5f, 1.0f, 0.95f, bolt(0.62f, 0.55f, 0.95f, 0.0f));
    canvas.light(0.5f, 1.0f, 0.95f, bolt(0.88f, 0.55f, 0.95f, 1.0f));
    canvas.light(0.3f, 0.3f, 0.3f, dot(1.4f, 0.9f, 0.002f));
    Reference sheet = still(canvas.image());
    const std::vector<SheetEffect> found = findEffects(sheet);
    REQUIRE(found.size() == 3);
    // Reading order: the frame (top left), the glow (top right), the column.
    const double wide = 1.5;
    CHECK(found[0].left * wide < 0.05);
    CHECK(found[0].right * wide == Approx(0.45).margin(0.06));  // the spark is taken in with the frame
    CHECK(found[0].bottom == Approx(0.4).margin(0.06));
    CHECK(found[1].left * wide == Approx(0.93).margin(0.06));
    CHECK(found[1].right * wide == Approx(1.27).margin(0.06));
    CHECK(found[2].left * wide == Approx(0.6).margin(0.06));
    CHECK(found[2].top == Approx(0.55).margin(0.06));
    for (const SheetEffect& e : found) {
        CHECK(e.share > 0.05f);
    }

    // Each, cropped to its box, is read as what it is.
    ReferenceOptions one;
    one.cropLeft = found[0].left;
    one.cropTop = found[0].top;
    one.cropRight = found[0].right;
    one.cropBottom = found[0].bottom;
    const auto frame = analyzeReference(sheet, one);
    REQUIRE(frame.ok());
    CHECK(frame.value().still.linesLead);
    REQUIRE(frame.value().still.lines.size() == 1);
    CHECK(frame.value().still.lines.front().closed);
    one.cropLeft = found[2].left;
    one.cropTop = found[2].top;
    one.cropRight = found[2].right;
    one.cropBottom = found[2].bottom;
    const auto column = analyzeReference(sheet, one);
    REQUIRE(column.ok());
    CHECK(column.value().still.linesLead);
    CHECK(column.value().still.lines.size() == 2);
    CHECK(column.value().still.lineFill.present);
}

TEST_CASE("one effect alone is not a sheet", "[reference][sheet]") {
    Canvas canvas(200, 200);
    canvas.light(1.0f, 0.6f, 0.2f, frameLine(0.2f, 0.2f, 0.8f, 0.8f, 0.015f));
    const std::vector<SheetEffect> found = findEffects(still(canvas.image()));
    CHECK(found.size() == 1);
}

// ---------------------------------------------------------------- flipbook

TEST_CASE("a flipbook keeps the clip's own frames, in step, with the built layers switched off", "[reference][flipbook]") {
    // A hand-painted-looking blob that wobbles and changes colour: nothing
    // procedural reproduces it exactly.
    Reference clip;
    clip.name = "blob";
    clip.framesPerSecond = 24;
    for (int f = 0; f < 36; ++f) {
        Canvas canvas(160, 160);
        const float t = static_cast<float>(f) / 24.0f;
        canvas.light(1.0f, 0.4f + 0.3f * std::sin(t * 5.0f), 0.2f, [=](float x, float y) {
            const float a = std::atan2(y - 0.5f, x - 0.5f);
            const float r = 0.25f + 0.06f * std::sin(5.0f * a + 4.0f * t);
            const float d = std::hypot(x - 0.5f, y - 0.5f) / r;
            return d < 1.0f ? 1.0f - d * d : 0.0f;
        });
        clip.frames.push_back(canvas.image());
    }
    const auto analysis = analyzeReference(clip, {});
    REQUIRE(analysis.ok());
    IdGenerator ids(17);
    const IdSource newId = counting(ids);
    Reconstruction built = reconstruct(analysis.value(), {}, newId);
    const std::size_t builtLayers = built.effect.layers.size();
    const Flipbook book = makeFlipbook(clip, {}, analysis.value(), built.referenceStart, built.effect.duration);
    CHECK(book.frames >= 2);
    CHECK(book.frames <= 36);
    CHECK(book.columns * book.rows >= book.frames);
    CHECK(book.sheet.width == book.columns * 192);
    // Played at the clip's own pace: as many pictures a second as it had.
    CHECK(book.framesPerSecond == Approx(24.0).epsilon(0.05));
    addFlipbook(built, book, analysis.value().still, newId, true);
    REQUIRE(built.effect.layers.size() == builtLayers + 1);
    const Layer& flip = built.effect.layers.front();
    CHECK(flip.name == "Flipbook");
    CHECK(flip.enabled);
    for (std::size_t i = 1; i < built.effect.layers.size(); ++i) {
        CHECK_FALSE(built.effect.layers[i].enabled);
    }
    REQUIRE(built.pictures.size() == 1);
    CHECK(built.effect.assets.size() == 1);
    CHECK(textOf(flip, "sprite", "animate") == "life");

    // It looks like the reference, at the moment analysed and at others.
    const Placement placement = placementOf(built);
    const ImageSet pictures = picturesOf(built);
    const Similarity alike =
        compareToReference(clip, {}, analysis.value(), built.effect, placement, {}, {}, &pictures);
    CHECK(alike.overall > 0.9f);
    CHECK(alike.silhouette > 0.85f);
    CHECK(alike.colour > 0.9f);
}
