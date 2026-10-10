#include "vfx/editor/Compare.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "vfx/Program.h"
#include "vfx/Simulation.h"

#include "EffectTools.h"
#include "ReferenceTools.h"

namespace vfx::editor {

using namespace ref;

// ----------------------------------------------------- lining up the clocks

int referenceFrameAt(const Reference& reference, const ReferenceOptions& options, const Placement& placement,
                     double effectTime) {
    int first = 0, last = 0;
    frameRange(reference, options, first, last);
    if (!reference.moving() || last <= first) {
        return first;
    }
    const double fps = reference.framesPerSecond * (options.speed > 0.01 ? options.speed : 1.0);
    const auto frame = static_cast<int>(std::lround((effectTime + placement.referenceStart) * fps));
    return std::clamp(first + frame, first, last);
}

double effectTimeAt(const Reference& reference, const ReferenceOptions& options, const Placement& placement,
                    int frame) {
    int first = 0, last = 0;
    frameRange(reference, options, first, last);
    if (!reference.moving() || last <= first) {
        return placement.peakTime;
    }
    const double fps = reference.framesPerSecond * (options.speed > 0.01 ? options.speed : 1.0);
    return static_cast<double>(frame - first) / fps - placement.referenceStart;
}

// ------------------------------------------------------------------ drawing

namespace {

std::uint8_t byteOf(float v) { return static_cast<std::uint8_t>(std::lround(clamp01(v) * 255.0f)); }

Picture blank(const StillAnalysis& still, int width, int height) {
    Picture p;
    p.width = width;
    p.height = height;
    p.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0);
    if (still.backdrop != Backdrop::Transparent) {
        for (std::size_t i = 0; i < p.rgba.size(); i += 4) {
            p.rgba[i + 0] = byteOf(still.backdropColour.r);
            p.rgba[i + 1] = byteOf(still.backdropColour.g);
            p.rgba[i + 2] = byteOf(still.backdropColour.b);
            p.rgba[i + 3] = 255;
        }
    }
    return p;
}

}  // namespace

Picture drawLikeReference(const Effect& effect, const Placement& placement, const StillAnalysis& still, int width,
                          int height, double effectTime, const ImageSet* images) {
    width = std::clamp(width, 8, 4096);
    height = std::clamp(height, 8, 4096);
    const bool loops = effect.loop == "loop";
    if (!(effect.duration > 0.0) || effectTime < 0.0 || (!loops && effectTime > effect.duration)) {
        return blank(still, width, height);
    }
    // A looping effect is shown on its second time round, when anything
    // that carries over from one pass to the next is already in the air.
    double time = effectTime;
    if (loops) {
        time = effect.duration + std::fmod(effectTime, effect.duration);
    }
    Simulation simulation(compileEffect(effect));
    simulation.seek(static_cast<std::int64_t>(std::llround(time / kSimulationStep)) + 1);
    RenderFrame frame;
    simulation.extract(frame);
    View view = placement.view;
    view.width = static_cast<float>(width);
    view.height = static_cast<float>(height);
    SpriteMesh mesh;
    buildSpriteMesh(frame, view, mesh, images);
    if (still.backdrop == Backdrop::Transparent) {
        return drawPictureClear(mesh, width, height, images);
    }
    return drawPicture(mesh, width, height,
                       ScreenColor{still.backdropColour.r, still.backdropColour.g, still.backdropColour.b}, images);
}

Picture drawReference(const Reference& reference, const ReferenceOptions& options, const StillAnalysis& still,
                      int frame, int width, int height) {
    width = std::clamp(width, 8, 4096);
    height = std::clamp(height, 8, 4096);
    Picture out = blank(still, width, height);
    if (reference.frames.empty()) {
        return out;
    }
    const Image source = referencePicture(reference, options, frame, std::max(width, height));
    if (source.width <= 0 || source.height <= 0) {
        return out;
    }
    // The crop keeps its proportions; if the size asked for differs by a
    // rounding, the nearest pixel is read.
    for (int y = 0; y < height; ++y) {
        const int sy = std::min(source.height - 1, y * source.height / height);
        for (int x = 0; x < width; ++x) {
            const int sx = std::min(source.width - 1, x * source.width / width);
            const std::uint8_t* s = &source.rgba[(static_cast<std::size_t>(sy) * static_cast<std::size_t>(source.width) +
                                                  static_cast<std::size_t>(sx)) * 4u];
            std::uint8_t* d = &out.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                         static_cast<std::size_t>(x)) * 4u];
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = still.backdrop == Backdrop::Transparent ? s[3] : 255;
        }
    }
    return out;
}

Picture overlayPictures(const Picture& reference, const Picture& made, float opacity) {
    Picture out = reference;
    if (made.width != reference.width || made.height != reference.height) {
        return out;
    }
    const float t = clamp01(opacity);
    for (std::size_t i = 0; i < out.rgba.size(); ++i) {
        out.rgba[i] = static_cast<std::uint8_t>(
            std::lround(static_cast<float>(reference.rgba[i]) * (1.0f - t) + static_cast<float>(made.rgba[i]) * t));
    }
    return out;
}

Picture differencePicture(const Picture& reference, const Picture& made) {
    Picture out;
    out.width = reference.width;
    out.height = reference.height;
    out.rgba.assign(reference.rgba.size(), 0);
    if (made.width != reference.width || made.height != reference.height) {
        return out;
    }
    for (std::size_t i = 0; i + 3 < out.rgba.size(); i += 4) {
        // Pictures with a see-through background are compared as they would
        // look over black.
        const float ra = static_cast<float>(reference.rgba[i + 3]) / 255.0f;
        const float ma = static_cast<float>(made.rgba[i + 3]) / 255.0f;
        float size = 0.0f, sign = 0.0f;
        for (std::size_t c = 0; c < 3; ++c) {
            const float d = static_cast<float>(made.rgba[i + c]) * ma - static_cast<float>(reference.rgba[i + c]) * ra;
            size = std::max(size, std::fabs(d));
            sign += d;
        }
        const float v = clamp01(size / 255.0f * 1.6f);
        // Warm where the effect is brighter than the reference, cool where dimmer.
        if (sign >= 0.0f) {
            out.rgba[i + 0] = byteOf(v);
            out.rgba[i + 1] = byteOf(v * 0.55f);
            out.rgba[i + 2] = byteOf(v * 0.15f);
        } else {
            out.rgba[i + 0] = byteOf(v * 0.15f);
            out.rgba[i + 1] = byteOf(v * 0.6f);
            out.rgba[i + 2] = byteOf(v);
        }
        out.rgba[i + 3] = 255;
    }
    return out;
}

// ---------------------------------------------------------------- measuring

namespace {

constexpr int kCompareSide = 112;  // the longest side pictures are compared at
constexpr int kMotionSide = 64;
constexpr int kProfile = 32;

BackdropRead backdropOf(const StillAnalysis& still) { return backdropFrom(still); }

Grid gridOf(const Picture& picture) {
    Grid grid;
    grid.width = picture.width;
    grid.height = picture.height;
    const std::size_t n = static_cast<std::size_t>(picture.width) * static_cast<std::size_t>(picture.height);
    grid.r.resize(n);
    grid.g.resize(n);
    grid.b.resize(n);
    grid.a.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        grid.r[i] = static_cast<float>(picture.rgba[i * 4 + 0]) / 255.0f;
        grid.g[i] = static_cast<float>(picture.rgba[i * 4 + 1]) / 255.0f;
        grid.b[i] = static_cast<float>(picture.rgba[i * 4 + 2]) / 255.0f;
        grid.a[i] = static_cast<float>(picture.rgba[i * 4 + 3]) / 255.0f;
    }
    return grid;
}

Matte matteOfEffect(const Effect& effect, const Placement& placement, const StillAnalysis& still, int width,
                    int height, double time, const ImageSet* images) {
    return matteFromGrid(gridOf(drawLikeReference(effect, placement, still, width, height, time, images)),
                         backdropOf(still));
}

std::array<float, kProfile> profile(const Matte& m, float cx, float cy, float limit) {
    std::array<float, kProfile> sum{};
    std::array<int, kProfile> count{};
    const float step = std::max(0.5f, limit / kProfile);
    for (int y = 0; y < m.height; ++y) {
        for (int x = 0; x < m.width; ++x) {
            const float dx = static_cast<float>(x) - cx, dy = static_cast<float>(y) - cy;
            const int bin = static_cast<int>(std::sqrt(dx * dx + dy * dy) / step);
            if (bin < kProfile) {
                sum[static_cast<std::size_t>(bin)] += m.cover[m.at(x, y)];
                count[static_cast<std::size_t>(bin)] += 1;
            }
        }
    }
    for (std::size_t i = 0; i < kProfile; ++i) {
        if (count[i] > 0) {
            sum[i] /= static_cast<float>(count[i]);
        }
    }
    return sum;
}

struct Moment {
    float silhouette = 0, colour = 0, brightness = 0, detail = 0, density = 0;
    // Signed readings, the effect minus the reference, for saying what differs.
    float extentRatio = 1;        // effect / reference
    float inner = 0, outer = 0;   // brightness near the centre and at the edge
    Swatch referenceColour, madeColour;
    float colourGap = 0;
    float referencePieces = 0, madePieces = 0;
};

// Measures one picture of the effect against one of the reference. Both
// mattes are the same size; (cx, cy) and extent are the reference's.
Moment measureMoment(const Matte& a, const Matte& b, float cx, float cy, float extent, bool pieces) {
    Moment out;
    const int w = a.width, h = a.height;

    // Outline: how much the two soft-edged shapes overlap.
    {
        std::vector<float> sa = a.cover, sb = b.cover;
        const int radius = std::max(1, h / 48);
        blur(sa, w, h, radius);
        blur(sb, w, h, radius);
        double both = 0, either = 0;
        for (std::size_t i = 0; i < sa.size(); ++i) {
            const float x = clamp01((sa[i] - 0.08f) / 0.25f), y = clamp01((sb[i] - 0.08f) / 0.25f);
            both += std::min(x, y);
            either += std::max(x, y);
        }
        out.silhouette = either > 1e-6 ? static_cast<float>(both / either) : 1.0f;

        // Fine detail: what a blur takes away.
        std::vector<float> wa = a.cover, wb = b.cover;
        const int wide = std::max(2, h / 28);
        blur(wa, w, h, wide);
        blur(wb, w, h, wide);
        double da = 0, db = 0;
        for (std::size_t i = 0; i < wa.size(); ++i) {
            da += std::fabs(a.cover[i] - wa[i]);
            db += std::fabs(b.cover[i] - wb[i]);
        }
        da /= static_cast<double>(wa.size());
        db /= static_cast<double>(wb.size());
        out.detail = std::max(da, db) < 1e-4 ? 1.0f : static_cast<float>(std::min(da, db) / std::max(da, db));
    }

    // Colour: block by block, where both have something.
    {
        constexpr int kBlocks = 12;
        std::array<double, kBlocks * kBlocks * 4> ca{}, cb{};
        for (int y = 0; y < h; ++y) {
            const int by = std::min(kBlocks - 1, y * kBlocks / h);
            for (int x = 0; x < w; ++x) {
                const int bx = std::min(kBlocks - 1, x * kBlocks / w);
                const std::size_t i = a.at(x, y);
                const auto k = static_cast<std::size_t>((by * kBlocks + bx) * 4);
                ca[k + 0] += a.cover[i] * a.r[i];
                ca[k + 1] += a.cover[i] * a.g[i];
                ca[k + 2] += a.cover[i] * a.b[i];
                ca[k + 3] += a.cover[i];
                cb[k + 0] += b.cover[i] * b.r[i];
                cb[k + 1] += b.cover[i] * b.g[i];
                cb[k + 2] += b.cover[i] * b.b[i];
                cb[k + 3] += b.cover[i];
            }
        }
        double score = 0, weight = 0;
        double ra = 0, ga = 0, ba = 0, wa = 0, rb = 0, gb = 0, bb = 0, wb = 0;
        for (std::size_t k = 0; k < ca.size(); k += 4) {
            ra += ca[k];
            ga += ca[k + 1];
            ba += ca[k + 2];
            wa += ca[k + 3];
            rb += cb[k];
            gb += cb[k + 1];
            bb += cb[k + 2];
            wb += cb[k + 3];
            const double m = std::min(ca[k + 3], cb[k + 3]);
            if (m < 0.5) {
                continue;
            }
            const double gap = (std::fabs(ca[k] / ca[k + 3] - cb[k] / cb[k + 3]) +
                                std::fabs(ca[k + 1] / ca[k + 3] - cb[k + 1] / cb[k + 3]) +
                                std::fabs(ca[k + 2] / ca[k + 3] - cb[k + 2] / cb[k + 3])) / 3.0;
            score += m * (1.0 - std::min(1.0, gap / 0.5));
            weight += m;
        }
        out.colour = weight > 1e-6 ? static_cast<float>(score / weight) : 0.0f;
        if (wa > 1e-6) {
            out.referenceColour = Swatch{static_cast<float>(ra / wa), static_cast<float>(ga / wa),
                                         static_cast<float>(ba / wa), 0};
        }
        if (wb > 1e-6) {
            out.madeColour = Swatch{static_cast<float>(rb / wb), static_cast<float>(gb / wb), static_cast<float>(bb / wb), 0};
        }
        out.colourGap = wa > 1e-6 && wb > 1e-6
                            ? (std::fabs(out.referenceColour.r - out.madeColour.r) +
                               std::fabs(out.referenceColour.g - out.madeColour.g) +
                               std::fabs(out.referenceColour.b - out.madeColour.b)) / 3.0f
                            : 0.0f;
    }

    // Brightness from the centre outward.
    {
        const float limit = 1.5f * std::max(2.0f, extent);
        const auto pa = profile(a, cx, cy, limit), pb = profile(b, cx, cy, limit);
        double gap = 0, scale = 0, inner = 0, outer = 0;
        int innerCount = 0, outerCount = 0;
        for (int k = 0; k < kProfile; ++k) {
            const auto i = static_cast<std::size_t>(k);
            const double weight = static_cast<double>(k) + 1.0;
            gap += weight * std::fabs(pa[i] - pb[i]);
            scale += weight * std::max({pa[i], pb[i], 0.02f});
            const float radius = (static_cast<float>(k) + 0.5f) / kProfile * 1.5f;  // in extents
            if (radius < 0.35f) {
                inner += pb[i] - pa[i];
                ++innerCount;
            } else if (radius > 0.7f && radius < 1.3f) {
                outer += pb[i] - pa[i];
                ++outerCount;
            }
        }
        out.brightness = clamp01(static_cast<float>(1.0 - gap / std::max(1e-6, scale)));
        out.inner = innerCount ? static_cast<float>(inner / innerCount) : 0.0f;
        out.outer = outerCount ? static_cast<float>(outer / outerCount) : 0.0f;
    }

    const Basics ba = measureBasics(a), bb = measureBasics(b);
    out.extentRatio = ba.total > 1.0f && bb.total > 1.0f ? bb.extent / std::max(1.0f, ba.extent) : (bb.total > 1.0f ? 2.0f : 0.0f);

    out.density = 1.0f;
    if (pieces) {
        float reach = 0;
        countPieces(a, cx, cy, std::max(2.0f, extent), out.referencePieces, reach);
        countPieces(b, cx, cy, std::max(2.0f, extent), out.madePieces, reach);
        const float lo = std::min(out.referencePieces, out.madePieces), hi = std::max(out.referencePieces, out.madePieces);
        out.density = hi < 2.0f ? 1.0f : (lo + 1.0f) / (hi + 1.0f);
    }
    return out;
}

// The reference frames an effect is compared at, with how much each counts.
struct Checkpoint {
    int frame = 0;
    double effectTime = 0;
    float weight = 1;
    Matte matte;
};

struct Yardstick {
    int width = 0, height = 0;
    float cx = 0, cy = 0, extent = 1;
    std::vector<Checkpoint> checkpoints;
    bool moving = false;
    bool steady = false;
};

Yardstick makeYardstick(const Reference& reference, const ReferenceOptions& options, const ReferenceAnalysis& analysis,
                        const Placement& placement) {
    Yardstick y;
    int first = 0, last = 0;
    frameRange(reference, options, first, last);
    y.moving = analysis.time.available && reference.moving() && last > first;
    y.steady = y.moving && analysis.time.continuous;
    const BackdropRead backdrop = backdropOf(analysis.still);
    const auto add = [&](int frame, float weight) {
        frame = std::clamp(frame, first, last);
        for (Checkpoint& c : y.checkpoints) {
            if (c.frame == frame) {
                c.weight += weight;
                return;
            }
        }
        Checkpoint c;
        c.frame = frame;
        c.effectTime = y.moving ? effectTimeAt(reference, options, placement, frame) : placement.peakTime;
        c.weight = weight;
        c.matte = matteFromGrid(makeGrid(reference.frames[static_cast<std::size_t>(frame)], options, kCompareSide), backdrop);
        y.checkpoints.push_back(std::move(c));
    };
    if (!y.moving) {
        add(std::clamp(analysis.stillFrame, first, last), 1.0f);
    } else {
        const double fps = analysis.time.framesPerSecond;
        const auto frameAt = [&](double seconds) { return first + static_cast<int>(std::lround(seconds * fps)); };
        // The frame that was described counts most, and goes first.
        add(std::clamp(analysis.stillFrame, first, last), 0.5f);
        if (y.steady) {
            const int from = frameAt(analysis.time.steadyFrom);
            add(from + (last - from) / 4, 0.25f);
            add(from + (last - from) * 3 / 4, 0.25f);
        } else {
            add(frameAt(analysis.time.peak), 0.25f);
            add(frameAt(analysis.time.main + 0.4 * (analysis.time.end - analysis.time.main)), 0.25f);
        }
    }
    y.width = y.checkpoints.front().matte.width;
    y.height = y.checkpoints.front().matte.height;
    y.cx = analysis.still.centreX * static_cast<float>(y.width);
    y.cy = analysis.still.centreY * static_cast<float>(y.height);
    y.extent = std::max(2.0f, analysis.still.extent * static_cast<float>(y.height));
    return y;
}

double timeFor(const Yardstick& y, const Effect& effect, double effectTime) {
    // A steady effect has no beginning to line up with: any moment of the
    // loop will do, so the loop is simply wrapped.
    if (y.steady && effect.duration > 0) {
        return std::fmod(std::max(0.0, effectTime), effect.duration);
    }
    // A burst is compared once through: after it has played, there is
    // nothing, even though the effect is set to repeat for watching.
    if (y.moving && effectTime >= effect.duration) {
        return -1.0;
    }
    return effectTime;
}

Moment measureAll(const Yardstick& y, const Effect& effect, const Placement& placement, const StillAnalysis& still,
                  bool pieces, const ImageSet* images = nullptr) {
    Moment total;
    total.silhouette = total.colour = total.brightness = total.detail = total.density = 0.0f;
    float weight = 0.0f;
    bool firstDone = false;
    for (const Checkpoint& c : y.checkpoints) {
        const Matte made = matteOfEffect(effect, placement, still, y.width, y.height, timeFor(y, effect, c.effectTime), images);
        const Moment m = measureMoment(c.matte, made, y.cx, y.cy, y.extent, pieces);
        total.silhouette += c.weight * m.silhouette;
        total.colour += c.weight * m.colour;
        total.brightness += c.weight * m.brightness;
        total.detail += c.weight * m.detail;
        total.density += c.weight * m.density;
        weight += c.weight;
        if (!firstDone) {
            // The first checkpoint is the fullest frame: its signed readings
            // are the ones put into words.
            total.extentRatio = m.extentRatio;
            total.inner = m.inner;
            total.outer = m.outer;
            total.referenceColour = m.referenceColour;
            total.madeColour = m.madeColour;
            total.colourGap = m.colourGap;
            total.referencePieces = m.referencePieces;
            total.madePieces = m.madePieces;
            firstDone = true;
        }
    }
    if (weight > 0) {
        total.silhouette /= weight;
        total.colour /= weight;
        total.brightness /= weight;
        total.detail /= weight;
        total.density /= weight;
    }
    return total;
}

std::string secondsText(double value) {
    char text[32];
    std::snprintf(text, sizeof text, "%.2f s", value);
    return text;
}

std::string percentText(float value) {
    char text[32];
    std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::lround(value * 100.0f)));
    return text;
}

float weighted(const Similarity& s, const Priorities& p) {
    double sum = 0, weight = 0;
    const auto add = [&](float value, float priority) {
        if (value >= 0.0f && priority > 0.0f) {
            sum += static_cast<double>(value) * priority;
            weight += priority;
        }
    };
    add(s.silhouette, p.silhouette);
    add(s.colour, p.colour);
    add(s.brightness, p.brightness);
    add(s.density, p.density);
    add(s.motion, p.motion);
    add(s.timing, p.timing);
    add(s.detail, p.detail);
    return weight > 0 ? static_cast<float>(sum / weight) : 0.0f;
}

}  // namespace

Similarity compareToReference(const Reference& reference, const ReferenceOptions& options,
                              const ReferenceAnalysis& analysis, const Effect& effect, const Placement& placement,
                              const Priorities& priorities, const Progress& progress, const ImageSet* images) {
    Similarity out;
    if (reference.frames.empty()) {
        return out;
    }
    const Yardstick y = makeYardstick(reference, options, analysis, placement);
    const Moment m = measureAll(y, effect, placement, analysis.still, true, images);
    out.silhouette = m.silhouette;
    out.colour = m.colour;
    out.brightness = m.brightness;
    out.detail = m.detail;
    out.density = m.density;

    struct Said {
        float size;
        std::string text;
    };
    std::vector<Said> said;
    if (m.extentRatio <= 0.01f) {
        said.push_back({10.0f, "Nothing of the effect is on screen at the reference's fullest moment."});
    } else if (std::fabs(m.extentRatio - 1.0f) > 0.08f) {
        said.push_back({std::fabs(m.extentRatio - 1.0f) * 2.0f,
                        std::string("The effect is ") + percentText(std::fabs(m.extentRatio - 1.0f)) +
                            (m.extentRatio > 1.0f ? " larger" : " smaller") + " overall than the reference."});
    }
    if (std::fabs(m.inner) > 0.06f) {
        said.push_back({std::fabs(m.inner) * 3.0f, std::string("The middle is ") + (m.inner > 0 ? "brighter" : "dimmer") +
                                                       " than in the reference."});
    }
    if (std::fabs(m.outer) > 0.03f) {
        said.push_back({std::fabs(m.outer) * 5.0f,
                        m.outer > 0 ? std::string("The outer edge is brighter than in the reference: the glow reaches too far.")
                                    : std::string("The outer edge is dimmer than in the reference: the glow stops short.")});
    }
    if (m.colourGap > 0.1f) {
        said.push_back({m.colourGap * 3.0f, "Overall the effect is " + colourName(m.madeColour) + " " +
                                                hexColour(m.madeColour) + " where the reference is " +
                                                colourName(m.referenceColour) + " " + hexColour(m.referenceColour) + "."});
    }
    if (std::max(m.referencePieces, m.madePieces) >= 3.0f &&
        std::fabs(m.referencePieces - m.madePieces) > 0.3f * std::max(m.referencePieces, m.madePieces)) {
        said.push_back({1.0f - out.density, "The reference shows about " +
                                                std::to_string(static_cast<int>(std::lround(m.referencePieces))) +
                                                " small pieces; the effect shows about " +
                                                std::to_string(static_cast<int>(std::lround(m.madePieces))) + "."});
    }
    if (out.silhouette < 0.55f && std::fabs(m.extentRatio - 1.0f) <= 0.15f) {
        said.push_back({0.8f - out.silhouette, "The outline differs although the size is about right: the shape itself is off."});
    }

    // ---- over time
    if (y.moving) {
        const TimeAnalysis& t = analysis.time;
        int first = 0, last = 0;
        frameRange(reference, options, first, last);
        const int frames = last - first + 1;
        const int samples = std::min(36, frames);
        const BackdropRead backdrop = backdropOf(analysis.still);
        (void)backdrop;
        std::vector<float> re, rr, me, mr;
        std::vector<double> at;
        // The effect is drawn small for this: only its size and amount matter.
        const int mw = std::max(8, y.width * kMotionSide / std::max(y.width, y.height));
        const int mh = std::max(8, y.height * kMotionSide / std::max(y.width, y.height));
        for (int k = 0; k < samples; ++k) {
            if (progress && !progress(static_cast<float>(k) / static_cast<float>(samples), "Comparing over time")) {
                break;
            }
            const int local = samples > 1 ? k * (frames - 1) / (samples - 1) : 0;
            const auto i = static_cast<std::size_t>(std::min(local, t.frames - 1));
            const double effectTime = effectTimeAt(reference, options, placement, first + local);
            const Matte made = matteOfEffect(effect, placement, analysis.still, mw, mh, timeFor(y, effect, effectTime), images);
            const Basics b = measureBasics(made);
            re.push_back(t.energy[i]);
            rr.push_back(t.radius[i]);
            me.push_back(b.total);
            mr.push_back(b.total > 1.0f ? b.extent / static_cast<float>(mh) : 0.0f);
            at.push_back(static_cast<double>(local) / t.framesPerSecond);
        }
        if (!me.empty()) {
            float most = 0.0f;
            for (float v : me) {
                most = std::max(most, v);
            }
            float widest = 0.0f, madeWidest = 0.0f;
            for (std::size_t i = 0; i < rr.size(); ++i) {
                widest = std::max(widest, rr[i]);
                madeWidest = std::max(madeWidest, mr[i]);
            }
            double energyGap = 0, radiusGap = 0;
            for (std::size_t i = 0; i < me.size(); ++i) {
                me[i] = most > 1e-6f ? me[i] / most : 0.0f;
                if (me[i] < 0.06f) {
                    mr[i] = 0.0f;
                }
                energyGap += std::fabs(me[i] - re[i]);
                radiusGap += std::fabs((madeWidest > 1e-6f ? mr[i] / madeWidest : 0.0f) -
                                       (widest > 1e-6f ? rr[i] / widest : 0.0f));
            }
            energyGap /= static_cast<double>(me.size());
            radiusGap /= static_cast<double>(me.size());
            out.motion = clamp01(static_cast<float>(1.0 - 0.5 * (energyGap + radiusGap) * 1.5));

            if (!y.steady) {
                // When each appears, is fullest and is gone.
                const auto moments = [&](const std::vector<float>& e, double& start, double& peak, double& end) {
                    std::size_t s = 0, p = 0, x = e.size() - 1;
                    while (s + 1 < e.size() && e[s] < 0.06f) {
                        ++s;
                    }
                    while (x > s && e[x] < 0.06f) {
                        --x;
                    }
                    for (std::size_t i = 0; i < e.size(); ++i) {
                        if (e[i] > e[p]) {
                            p = i;
                        }
                    }
                    start = at[s];
                    peak = at[p];
                    end = at[x];
                };
                double rs = 0, rp = 0, rx = 0, ms = 0, mp = 0, mx = 0;
                moments(re, rs, rp, rx);
                moments(me, ms, mp, mx);
                const double life = std::max(0.1, rx - rs);
                out.timing = clamp01(static_cast<float>(1.0 - (std::fabs(ms - rs) + std::fabs(mp - rp) + std::fabs(mx - rx)) / (1.5 * life)));
                const double step = at.size() > 1 ? at[1] - at[0] : 0.05;
                if (std::fabs(mp - rp) > 1.5 * step) {
                    said.push_back({static_cast<float>(std::fabs(mp - rp) / life) * 2.0f,
                                    "The effect is fullest " + secondsText(std::fabs(mp - rp)) +
                                        (mp > rp ? " later" : " earlier") + " than the reference."});
                }
                if (std::fabs(mx - rx) > 1.5 * step) {
                    said.push_back({static_cast<float>(std::fabs(mx - rx) / life) * 2.0f,
                                    "The effect " + std::string(mx > rx ? "lingers " : "is gone ") +
                                        secondsText(std::fabs(mx - rx)) + (mx > rx ? " longer" : " sooner") +
                                        " than the reference."});
                }
            } else {
                out.timing = -1.0f;
            }
            if (out.motion < 0.7f) {
                said.push_back({0.9f - out.motion, "How it grows and fades over time differs from the reference."});
            }
        }
    }

    out.overall = weighted(out, priorities);
    std::stable_sort(said.begin(), said.end(), [](const Said& a, const Said& b) { return a.size > b.size; });
    for (std::size_t i = 0; i < said.size() && i < 5; ++i) {
        out.differences.push_back(said[i].text);
    }
    return out;
}

// ------------------------------------------------------------------ fitting

namespace {

enum class KnobKind { Size, Brightness, Reach };

struct Knob {
    std::size_t layer;
    KnobKind kind;
    double total = 1.0;  // how far it has been moved from where it began
};

void apply(Layer& layer, KnobKind kind, double factor) {
    switch (kind) {
        case KnobKind::Size: tools::scaleSize(layer, factor); break;
        case KnobKind::Brightness: tools::scaleBrightness(layer, factor); break;
        case KnobKind::Reach: tools::scaleReach(layer, factor); break;
    }
}

const char* knobWord(KnobKind kind) {
    switch (kind) {
        case KnobKind::Size: return "size";
        case KnobKind::Brightness: return "brightness";
        case KnobKind::Reach: return "spread";
    }
    return "size";
}

}  // namespace

FitResult fitToReference(Effect& effect, const Reference& reference, const ReferenceOptions& options,
                         const ReferenceAnalysis& analysis, const Placement& placement, const FitOptions& fit,
                         const Progress& progress, const ImageSet* images) {
    FitResult result;
    const auto began = std::chrono::steady_clock::now();
    result.before = compareToReference(reference, options, analysis, effect, placement, fit.priorities, {}, images);
    result.after = result.before;
    if (reference.frames.empty()) {
        return result;
    }
    const Yardstick y = makeYardstick(reference, options, analysis, placement);

    Priorities p = fit.priorities;
    if (fit.onlyOutline) {
        p = Priorities{1, 0, 0, 0, 0, 0, 0};
    }
    // What the knobs can change: the outline, colours in place, brightness
    // and fine detail. Counts and timing are left to their own controls.
    const auto cost = [&](const Effect& candidate) {
        const Moment m = measureAll(y, candidate, placement, analysis.still, false, images);
        ++result.drawings;
        const double weight = p.silhouette + p.colour + p.brightness + 0.5 * p.detail;
        if (!(weight > 0)) {
            return 0.0;
        }
        return (p.silhouette * (1.0 - m.silhouette) + p.colour * (1.0 - m.colour) + p.brightness * (1.0 - m.brightness) +
                0.5 * p.detail * (1.0 - m.detail)) / weight;
    };

    std::vector<Knob> knobs;
    for (std::size_t i = 0; i < effect.layers.size(); ++i) {
        const Layer& layer = effect.layers[i];
        if (layer.locked || !layer.enabled) {
            continue;
        }
        knobs.push_back(Knob{i, KnobKind::Size});
        if (tools::isParticles(layer)) {
            knobs.push_back(Knob{i, KnobKind::Reach});
        }
        if (!fit.onlyOutline) {
            knobs.push_back(Knob{i, KnobKind::Brightness});
        }
    }
    if (knobs.empty()) {
        return result;
    }

    static constexpr double kSteps[] = {1.3, 1.15, 1.07, 1.04, 1.02};
    const int rounds = std::clamp(fit.rounds, 1, 5);
    const float planned = static_cast<float>(rounds) * static_cast<float>(knobs.size()) * 2.5f;
    double best = cost(effect);
    bool stop = false;
    for (int round = 0; round < rounds && !stop; ++round) {
        const double step = kSteps[round];
        for (Knob& knob : knobs) {
            if (progress && !progress(std::min(0.99f, static_cast<float>(result.drawings) / planned),
                                      "Adjusting sizes and brightness to match")) {
                result.cancelled = true;
                stop = true;
                break;
            }
            Layer& layer = effect.layers[knob.layer];
            for (const double direction : {step, 1.0 / step}) {
                bool moved = false;
                for (int tries = 0; tries < 4; ++tries) {
                    const double next = knob.total * direction;
                    // Brightness is held closer: pushed far, soft shapes
                    // clip to white and lose their form.
                    double most = knob.kind == KnobKind::Brightness ? 1.5 : 1.8;
                    double least = knob.kind == KnobKind::Brightness ? 0.45 : 0.55;
                    // A ring's radius was measured directly; it may only be trimmed.
                    if (knob.kind == KnobKind::Size && tools::roleOf(layer) == "ring") {
                        most = 1.2;
                        least = 0.85;
                    }
                    if (next > most || next < least) {
                        break;
                    }
                    const Layer saved = layer;
                    apply(layer, knob.kind, direction);
                    const double now = cost(effect);
                    if (now < best - 1e-4) {
                        best = now;
                        knob.total = next;
                        moved = true;
                    } else {
                        layer = saved;
                        break;
                    }
                }
                if (moved) {
                    break;  // it went one way; the other way cannot also help
                }
            }
        }
    }
    for (const Knob& knob : knobs) {
        if (std::fabs(knob.total - 1.0) > 0.015) {
            char text[160];
            std::snprintf(text, sizeof text, "%s: %s %s by %d%%", effect.layers[knob.layer].name.c_str(), knobWord(knob.kind),
                          knob.total > 1.0 ? "up" : "down",
                          static_cast<int>(std::lround(std::fabs(knob.total - 1.0) * 100.0)));
            result.changes.emplace_back(text);
        }
    }
    result.after = compareToReference(reference, options, analysis, effect, placement, fit.priorities, {}, images);
    result.cost = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    return result;
}

}  // namespace vfx::editor
