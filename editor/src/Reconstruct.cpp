#include "vfx/editor/Reconstruct.h"

#include <algorithm>
#include <cmath>

#include "vfx/Program.h"
#include "vfx/editor/Shapes.h"

#include "EffectTools.h"
#include "LayerMake.h"

namespace vfx::editor {

using namespace make;

// ------------------------------------------------------------------ names

const char* matchModeName(MatchMode mode) {
    switch (mode) {
        case MatchMode::ShapeColour: return "shape";
        case MatchMode::Motion: return "motion";
        case MatchMode::Layered: return "layered";
        case MatchMode::Balanced: return "balanced";
    }
    return "balanced";
}

bool parseMatchMode(std::string_view text, MatchMode& mode) {
    for (MatchMode m : {MatchMode::ShapeColour, MatchMode::Motion, MatchMode::Layered, MatchMode::Balanced}) {
        if (text == matchModeName(m)) {
            mode = m;
            return true;
        }
    }
    return false;
}

const char* variationName(Variation variation) {
    switch (variation) {
        case Variation::Closest: return "closest";
        case Variation::Performance: return "performance";
        case Variation::Enhanced: return "enhanced";
    }
    return "closest";
}

bool parseVariation(std::string_view text, Variation& variation) {
    for (Variation v : {Variation::Closest, Variation::Performance, Variation::Enhanced}) {
        if (text == variationName(v)) {
            variation = v;
            return true;
        }
    }
    return false;
}

const char* targetName(Target target) {
    switch (target) {
        case Target::Mobile: return "mobile";
        case Target::Desktop: return "desktop";
        case Target::VR: return "vr";
    }
    return "desktop";
}

bool parseTarget(std::string_view text, Target& target) {
    for (Target t : {Target::Mobile, Target::Desktop, Target::VR}) {
        if (text == targetName(t)) {
            target = t;
            return true;
        }
    }
    return false;
}

int particleBudget(Target target) {
    switch (target) {
        case Target::Mobile: return 120;
        case Target::Desktop: return 600;
        case Target::VR: return 250;
    }
    return 600;
}

// ----------------------------------------------------------------- curves

namespace {

double valueAt(const std::vector<CurveKey>& keys, double t) {
    if (keys.empty()) {
        return 0.0;
    }
    if (t <= keys.front().t) {
        return keys.front().v;
    }
    for (std::size_t i = 1; i < keys.size(); ++i) {
        if (t <= keys[i].t) {
            const double span = keys[i].t - keys[i - 1].t;
            const double u = span > 1e-12 ? (t - keys[i - 1].t) / span : 1.0;
            return keys[i - 1].v + (keys[i].v - keys[i - 1].v) * u;
        }
    }
    return keys.back().v;
}

}  // namespace

std::vector<CurveKey> simplifyCurve(const std::vector<CurveKey>& samples, int maxKeys, double tolerance) {
    std::vector<CurveKey> keys;
    if (samples.empty()) {
        return keys;
    }
    if (samples.size() == 1) {
        return {CurveKey{0.0, samples.front().v}, CurveKey{1.0, samples.front().v}};
    }
    // Start with the two ends and keep adding the sample the curve misses
    // by most, until it is close enough everywhere or has enough keys.
    std::vector<std::size_t> picked{0, samples.size() - 1};
    const auto build = [&] {
        keys.clear();
        for (std::size_t i : picked) {
            keys.push_back(samples[i]);
        }
    };
    build();
    while (static_cast<int>(picked.size()) < std::max(2, maxKeys)) {
        double worst = tolerance;
        std::size_t worstAt = 0;
        bool found = false;
        for (std::size_t i = 1; i + 1 < samples.size(); ++i) {
            const double miss = std::fabs(valueAt(keys, samples[i].t) - samples[i].v);
            if (miss > worst) {
                worst = miss;
                worstAt = i;
                found = true;
            }
        }
        if (!found) {
            break;
        }
        picked.insert(std::upper_bound(picked.begin(), picked.end(), worstAt), worstAt);
        build();
    }
    return keys;
}

// ------------------------------------------------------------ the builder

namespace {

constexpr double kU = kReferenceUnits;
constexpr double kPi = 3.14159265358979;

Color linear(const Swatch& s, double alpha = 1.0) {
    return Color{srgbToLinear(std::clamp<double>(s.r, 0, 1)), srgbToLinear(std::clamp<double>(s.g, 0, 1)),
                 srgbToLinear(std::clamp<double>(s.b, 0, 1)), alpha};
}

// The same hue at full brightness: what an added light of that colour is.
Swatch bright(Swatch s) {
    const float top = std::max({s.r, s.g, s.b});
    // Black has no hue to brighten; as added light it is simply nothing.
    if (top > 0.04f) {
        s.r /= top;
        s.g /= top;
        s.b /= top;
    }
    return s;
}

Swatch towardWhite(Swatch s, float amount) {
    s.r += (1.0f - s.r) * amount;
    s.g += (1.0f - s.g) * amount;
    s.b += (1.0f - s.b) * amount;
    return s;
}

SpriteShape shapeFrom(const std::string& name, SpriteShape fallback) {
    for (int i = 0; i < kSpriteShapeCount; ++i) {
        if (name == shapeName(static_cast<SpriteShape>(i))) {
            return static_cast<SpriteShape>(i);
        }
    }
    return fallback;
}

// How far a particle has gone after t seconds, per unit of starting speed,
// when drag slows it.
double travel(double drag, double t) { return drag > 1e-6 ? (1.0 - std::exp(-drag * t)) / drag : t; }

struct Plan {
    bool measured = false;  // timing came from a clip
    bool steady = false;    // keeps going, rather than one burst
    double duration = 1.2;  // of the effect
    double life = 0.9;      // of the one-shot layers
    double peak = 0.27;     // when the effect looks like the picture analysed
    double start = 0;       // seconds into the clip that the effect begins
    std::vector<CurveKey> grow, fade, flash;
    std::vector<CurveKey> fadeRing;  // for an outline, which thins less than a filled shape as it widens
    std::vector<std::pair<double, double>> bursts;  // time, share of the main burst
    double drag = 3.5;
    double spin = 0;
    bool spinSeen = false;  // the turning was measured, not assumed
    double driftSpeed = 0, driftHeading = 0;
    Vec3 pull{};
    bool tinted = false;
    std::vector<Tint> tint;
    // For a clip: the frames of it the effect stands for, and its rate.
    int first = 0, last = -1;
    double fps = 0;
};

// How many degrees a ground pattern can turn before it looks the same again.
double groundSymmetry(const std::string& pattern) {
    return pattern == "sigil" ? 60.0 : 360.0;
}

// The timing of something that stays on the ground: it turns steadily, and
// its length is a whole number of turns, so that when the effect comes
// round again the base is exactly where it started.
void groundPlan(const StillAnalysis& s, bool moving, Plan& p) {
    const bool seen = moving && s.groundTurnConfidence >= 0.45f && std::fabs(s.groundTurn) >= 5.0f;
    if (seen) {
        p.spin = std::clamp<double>(s.groundTurn, -360.0, 360.0);
    } else if (s.groundPattern == "vortex") {
        p.spin = 60.0;
    } else if (s.groundPattern == "sigil") {
        p.spin = 30.0;
    } else {
        p.spin = 0.0;
    }
    p.spinSeen = seen;
    double length = 4.0;
    if (p.spin != 0.0) {
        const double once = groundSymmetry(s.groundPattern) / std::fabs(p.spin);
        length = once * std::max(1.0, std::ceil(2.5 / once));
    }
    p.duration = length;
    p.life = length;
    p.peak = std::min(1.0, 0.5 * length);
    p.grow = {{0, 1}, {1, 1}};
    p.fade = {{0, 1}, {1, 1}};
    p.fadeRing = p.fade;
    p.flash = p.fade;
    p.tinted = false;
    p.tint.clear();
    p.driftSpeed = 0;
    p.pull = Vec3{};
}

Plan makePlan(const ReferenceAnalysis& a, const ReconstructOptions& o) {
    Plan p;
    const TimeAnalysis& t = a.time;
    const bool moving = t.available && t.frames >= 2;
    // Something lying on the ground keeps going (a circle, a portal),
    // unless it is only a ring crossing the floor, which is a burst.
    const bool standing = a.still.ground && !(a.still.groundHollow && a.still.beams < 2);
    // A line that the clip draws on is a one-off: drawn, held, gone; even
    // when it is held to the clip's end.
    bool drawsOn = false;
    for (const LineFound& line : a.still.lines) {
        drawsOn = drawsOn || (moving && a.still.linesLead && line.drawn);
    }
    p.steady = o.pace == Pace::Steady ||
               (o.pace == Pace::Auto && !drawsOn &&
                (moving ? t.continuous : (a.still.comet || standing || a.still.linesLead)));
    const int keys = (o.mode == MatchMode::Motion || o.mode == MatchMode::Layered) ? 8 : 6;

    if (moving) {
        p.measured = true;
        const double fps = t.framesPerSecond;
        const int n = t.frames;
        const auto frameAt = [&](double seconds) {
            return std::clamp(static_cast<int>(std::lround(seconds * fps)), 0, n - 1);
        };
        int first = 0, last = n - 1;
        if (!p.steady) {
            p.start = t.start;
            // A line being drawn on can show before the whole effect counts
            // as started: the effect starts with the first of them.
            for (const LineFound& line : a.still.lines) {
                if (a.still.linesLead && line.drawn) {
                    p.start = std::min(p.start, line.drawFrom);
                }
            }
            p.life = std::max(0.12, t.end - p.start);
            p.peak = std::clamp(t.main - p.start, 0.0, p.life);
            p.duration = p.life + (o.mode == MatchMode::Motion ? 0.15 : 0.3);
            // A clip that showed the burst come round again set its own rhythm.
            if (t.loops && t.loopLength >= p.life) {
                p.duration = t.loopLength;
            }
            first = frameAt(p.start);
            last = std::max(first, frameAt(t.end) - 1);
        } else {
            // What the clip shows once it is going. (Any starting-up is left out.)
            first = frameAt(t.steadyFrom);
            p.start = t.steadyFrom;
            p.duration = t.loops && t.loopLength > 0.1 ? t.loopLength : static_cast<double>(n - first) / fps;
            p.duration = std::max(0.2, p.duration);
            p.life = p.duration;
            p.peak = std::fmod(std::max(0.0, t.main - t.steadyFrom), p.duration);
            last = std::min(n - 1, first + frameAt(p.duration) - 1);
            last = std::max(last, first);
        }
        p.first = first;
        p.last = last;
        p.fps = fps;
        // Everything is measured against the frame that was described.
        const int peakFrame = frameAt(t.main);
        const float baseRadius = std::max(1e-4f, t.radius[static_cast<std::size_t>(peakFrame)]);
        const float baseBright = std::max(1e-4f, t.brightness[static_cast<std::size_t>(peakFrame)]);
        const float baseAmount = std::max(1e-4f, t.energy[static_cast<std::size_t>(peakFrame)]);
        std::vector<CurveKey> size, bright, brightRing;
        const Swatch& baseColour = t.colour[static_cast<std::size_t>(peakFrame)];
        std::vector<Tint> tint;
        float colourShift = 0.0f;
        for (int f = first; f <= last; ++f) {
            const auto i = static_cast<std::size_t>(f);
            const double at = last > first ? static_cast<double>(f - first) / (last - first) : 0.0;
            // Before it is there, it has no size of its own: hold the nearest real value.
            float r = t.radius[i];
            if (r <= 0.0f) {
                r = f < peakFrame ? t.radius[static_cast<std::size_t>(std::min(last, f + 1))] : 0.0f;
            }
            size.push_back(CurveKey{at, std::clamp<double>(r / baseRadius, 0.05, 4.0)});
            double level = std::clamp<double>(t.brightness[i] / baseBright, 0.0, 4.0);
            double levelRing = level;
            if (!p.steady) {
                // The brightest points alone cannot tell a fading shape from
                // one that is still at full strength: a few sparks stay
                // bright long after a glow or ring has thinned. How much
                // there is, for how far it has spread, can. A filled shape's
                // amount goes with its area, an outline's with its length.
                const double amount = t.energy[i] / baseAmount;
                const double wide = std::max(0.25, static_cast<double>(r) / baseRadius);
                level = std::min(level, amount / (wide * wide));
                levelRing = std::min(levelRing, amount / wide);
            }
            bright.push_back(CurveKey{at, level});
            brightRing.push_back(CurveKey{at, levelRing});
            const Swatch& c = t.colour[i];
            const Color now = linear(c), base = linear(baseColour);
            if (t.energy[i] > 0.1f) {
                tint.push_back(Tint{at, std::clamp(now.r / std::max(0.02, base.r), 0.0, 3.0),
                                    std::clamp(now.g / std::max(0.02, base.g), 0.0, 3.0),
                                    std::clamp(now.b / std::max(0.02, base.b), 0.0, 3.0), 1.0});
                colourShift = std::max(colourShift, std::max({std::fabs(c.r - baseColour.r), std::fabs(c.g - baseColour.g),
                                                              std::fabs(c.b - baseColour.b)}));
            }
        }
        if (!p.steady) {
            // A burst ends with nothing: the last key is zero.
            if (!bright.empty() && last < n - 1) {
                bright.back().v = 0.0;
                brightRing.back().v = 0.0;
            }
        } else {
            // A loop is steadier described about its average than about one frame.
            double meanSize = 0, meanBright = 0;
            for (std::size_t i = 0; i < size.size(); ++i) {
                meanSize += size[i].v;
                meanBright += bright[i].v;
            }
            meanSize /= std::max<std::size_t>(1, size.size());
            meanBright /= std::max<std::size_t>(1, bright.size());
            for (std::size_t i = 0; i < size.size(); ++i) {
                size[i].v = meanSize > 1e-6 ? size[i].v / meanSize : 1.0;
                bright[i].v = meanBright > 1e-6 ? std::min(1.0, bright[i].v / meanBright) : 1.0;
            }
            // The curve must end where it began, or the loop jumps.
            if (size.size() > 1) {
                size.back().v = size.front().v;
                bright.back().v = bright.front().v;
            }
            brightRing = bright;
        }
        // Opacity runs from 0 to 1, so the curve is measured against its
        // own brightest moment rather than the frame that was described.
        for (std::vector<CurveKey>* curve : {&bright, &brightRing}) {
            double top = 0.0;
            for (const CurveKey& k : *curve) {
                top = std::max(top, k.v);
            }
            if (top > 1.0) {
                for (CurveKey& k : *curve) {
                    k.v /= top;
                }
            }
        }
        p.grow = simplifyCurve(size, keys, 0.04);
        p.fade = simplifyCurve(bright, keys, 0.05);
        p.fadeRing = simplifyCurve(brightRing, keys, 0.05);
        p.flash = p.fade;
        if (colourShift > 0.12f && tint.size() >= 3) {
            // Keep a handful of evenly spread colour keys.
            const std::size_t want = std::min<std::size_t>(5, tint.size());
            for (std::size_t k = 0; k < want; ++k) {
                p.tint.push_back(tint[k * (tint.size() - 1) / (want - 1)]);
            }
            p.tint.front().t = 0.0;
            p.tint.back().t = 1.0;
            p.tinted = true;
        }
        if (!p.steady) {
            for (std::size_t i = 0; i < t.bursts.size(); ++i) {
                const double at = t.bursts[i] - p.start;
                if (at >= -0.02 && at < p.life) {
                    p.bursts.emplace_back(std::max(0.0, at), p.bursts.empty() ? 1.0 : 0.6);
                }
            }
        }
        if (p.bursts.empty()) {
            p.bursts.emplace_back(0.0, 1.0);
        }
        p.bursts.front().first = 0.0;
        p.drag = t.slowing > 0 ? std::clamp<double>(t.slowing, 0.5, 12.0) : 3.5;
        if (t.turningConfidence >= 0.4f) {
            p.spin = t.turning;
        }
        p.driftSpeed = std::hypot(t.driftX, t.driftY) * kU;
        p.driftHeading = std::atan2(t.driftY, t.driftX) * 180.0 / kPi;
        p.pull = Vec3{t.pullX * kU, t.pullY * kU, 0.0};
        if (a.still.ground && p.steady) {
            groundPlan(a.still, true, p);
        }
        return p;
    }

    // A still picture: the timing is assumed.
    if (p.steady) {
        p.duration = 2.0;
        p.life = 2.0;
        p.peak = 1.0;
        p.grow = {{0, 1}, {1, 1}};
        p.fade = {{0, 1}, {1, 1}};
        p.flash = p.fade;
        if (a.still.ground) {
            groundPlan(a.still, false, p);
        }
    } else if (o.mode == MatchMode::ShapeColour) {
        // Reach the picture quickly and hold it, so it can be compared.
        p.duration = 1.3;
        p.life = 1.0;
        p.peak = 0.22;
        p.grow = {{0, 0.7}, {0.22, 1.0}, {1, 1.04}};
        p.fade = {{0, 0}, {0.06, 1}, {0.7, 1}, {1, 0}};
        p.flash = {{0, 0}, {0.06, 1}, {0.5, 1}, {0.8, 0}};
    } else {
        p.duration = 1.2;
        p.life = 0.9;
        p.peak = 0.27;
        p.grow = {{0, 0.4}, {0.3, 1.0}, {1, 1.1}};
        p.fade = {{0, 0}, {0.07, 1}, {0.45, 1}, {1, 0}};
        p.flash = {{0, 0}, {0.07, 1}, {0.34, 1}, {0.65, 0}};
    }
    p.bursts.emplace_back(0.0, 1.0);
    return p;
}

struct Built {
    Layer layer;
    LayerNote note;
    int rank = 0;       // lower is kept first when layers must be dropped
    int particles = 1;  // alive at its busiest, roughly
    bool polish = false;
};

class Builder {
public:
    Builder(const ReferenceAnalysis& a, const ReconstructOptions& o, const IdSource& id,
            const std::vector<Cutout>* cutouts)
        : a_(a), s_(a.still), o_(o), id_(id), cutouts_(o.cutouts ? cutouts : nullptr), plan_(makePlan(a, o)) {}

    // For describing a different frame of the same clip on its own timing.
    Builder(const ReferenceAnalysis& a, const StillAnalysis& still, const ReconstructOptions& o, const IdSource& id,
            Plan plan)
        : a_(a), s_(still), o_(o), id_(id), plan_(std::move(plan)) {}

    // The opening flash of a burst: only its own form (a shape, or rays),
    // living for the instant the clip shows it.
    std::vector<Built> flashOnly() {
        if (s_.hasBody) {
            body();
        } else if (s_.rays >= 2 && (s_.rays >= 8 || s_.rayMatch >= 0.3f)) {
            rays();
        }
        return std::move(built_);
    }

    Reconstruction run() {
        // How many particles the loose pieces would take, to fit the budget.
        const int budget = particleBudget(o_.target) / (o_.variation == Variation::Performance ? 2 : 1);
        int wanted = 12;
        for (const PieceGroup* g : {&s_.sparks, &s_.streaks, &s_.bits}) {
            wanted += pieceCount(*g);
        }
        if (s_.comet) {
            wanted += 90;
        }
        if (s_.linesLead) {
            for (const LineFound& line : s_.lines) {
                wanted += lineDots(line);
            }
        }
        scale_ = wanted > budget ? static_cast<double>(budget) / wanted : 1.0;
        if (o_.variation == Variation::Performance) {
            scale_ *= 0.5;
        }
        if (o_.variation == Variation::Enhanced) {
            scale_ = std::min(1.3, scale_ * 1.3);
        }

        if (s_.linesLead) {
            lines();
        } else if (s_.ground) {
            ground();
        } else if (s_.comet) {
            comet();
        } else {
            if (s_.hasSmoke) {
                smoke();
            }
            glows();
            if (s_.hasBody) {
                body();
            }
            rings();
            if (s_.rays >= 2 && !s_.hasBody) {
                // A few faint rays that no built-in flash resembles are
                // better left out than drawn wrongly.
                if (cutout("rays") || s_.rays >= 8 || s_.rayMatch >= 0.3f) {
                    rays();
                } else {
                    skipped_.push_back("A few faint rays were seen, but no built-in flash shape is like them, so they "
                                       "were left out. Allowing cut-outs from the reference keeps them.");
                }
            }
        }
        if (!s_.ground) {
            pieces(s_.streaks, "streaks", "Streaks", 6);
            pieces(s_.bits, "bits", "Pieces", 7);
            pieces(s_.sparks, "sparks", "Sparks", 5);
        }
        if (a_.hasEarly && !plan_.steady && plan_.measured && o_.variation != Variation::Performance) {
            // The clip opens with a flash that looks nothing like what
            // follows: it gets layers of its own, for as long as it showed.
            Plan flash = plan_;
            flash.life = std::max(0.06, a_.earlyEnd - a_.time.start);
            flash.peak = std::clamp(a_.earlyTime - a_.time.start, 0.0, flash.life);
            const double at = flash.life > 0 ? flash.peak / flash.life : 0.3;
            flash.grow = {{0, 0.5}, {std::max(0.05, at), 1.0}, {1, 1.15}};
            flash.fade = {{0, 1}, {std::max(0.05, at), 1}, {1, 0}};
            flash.flash = flash.fade;
            flash.fadeRing = flash.fade;
            flash.tinted = false;
            flash.bursts = {{0.0, 1.0}};
            Builder opening(a_, a_.early, o_, id_, flash);
            for (Built& b : opening.flashOnly()) {
                b.layer.name = "Opening " + std::string(b.note.role == "body" ? "shape" : "flash");
                b.layer.duration = plan_.duration;
                b.note.name = b.layer.name;
                b.note.from = "early";
                b.note.note = "The clip opens with this for an instant before the main part takes over. " + b.note.note;
                b.rank = 2;
                built_.push_back(std::move(b));
            }
        }
        if (o_.variation == Variation::Enhanced) {
            polish();
        }
        if (built_.empty()) {
            // Something was found but nothing above claimed it: a plain glow
            // of the right size and colour is the honest minimum.
            fallback();
        }
        return finish(budget);
    }

private:
    int pieceCount(const PieceGroup& g) const {
        if (g.count <= 0) {
            return 0;
        }
        // A clip may show more pieces in other frames than in the fullest one.
        float most = static_cast<float>(g.count);
        if (a_.time.available && &g == &s_.sparks) {
            for (float c : a_.time.pieces) {
                most = std::max(most, 0.9f * c);
            }
        }
        return static_cast<int>(std::lround(most));
    }

    int scaled(int count, int least = 3) const {
        return std::max(std::min(count, least), static_cast<int>(std::lround(count * scale_)));
    }

    double sizeFor(SpriteShape shape, double radius, float ShapeMeasure::* which) const {
        const float m = std::max(0.15f, measureShape(shape).*which);
        return std::max(0.02, 2.0 * radius * kU / m);
    }

    // The settings every one-shot layer shares: when it lives, how it
    // grows and fades, how the whole effect travels and turns.
    Make& oneShot(Make& m, bool turns) {
        m.burst(1).life(plan_.life, plan_.life).gravity(plan_.pull.x, plan_.pull.y);
        if (plan_.driftSpeed > 0.05) {
            m.speed(plan_.driftSpeed, plan_.driftSpeed).aim(plan_.driftHeading, 0.0);
        } else {
            m.speed(0, 0);
        }
        if (turns && plan_.spin != 0) {
            m.spin(plan_.spin, plan_.spin);
        }
        return m;
    }

    void blend(Make& m, bool additive, double level) {
        if (additive) {
            m.additive();
            m.glow(std::max(1.0, level));
        }
    }

    void push(Make& m, const char* role, std::string technique, std::string from, Basis look, int rank, int particles,
              std::string note = {}, bool isPolish = false) {
        m.role(role);
        Built b;
        b.layer = m.done(plan_.duration);
        // Readings of brightness run from "nothing added" to "as bright as
        // the picture can show". Over a background that is not black there
        // is less room between the two, so the same reading is less light.
        if (s_.additive && tools::isAdditive(b.layer)) {
            const float taken = std::max({s_.backdropColour.r, s_.backdropColour.g, s_.backdropColour.b});
            if (s_.backdrop != Backdrop::Transparent && taken > 0.02f) {
                tools::scaleBrightness(b.layer, std::clamp(1.0 - static_cast<double>(taken), 0.4, 1.0));
            }
        }
        b.note.layer = b.layer.id;
        b.note.name = b.layer.name;
        b.note.role = role;
        b.note.technique = std::move(technique);
        b.note.from = std::move(from);
        b.note.look = look;
        b.note.motion = plan_.measured ? Basis::Observed : Basis::Inferred;
        b.note.note = std::move(note);
        b.rank = rank;
        b.particles = particles;
        b.polish = isPolish;
        built_.push_back(std::move(b));
    }

    const Cutout* cutout(const char* part) const {
        if (cutouts_) {
            for (const Cutout& c : *cutouts_) {
                if (c.part == part && c.image.width > 0) {
                    return &c;
                }
            }
        }
        return nullptr;
    }

    // Makes a layer draw a cut-out: registers the picture and points the
    // layer's sprite at it.
    void usePicture(Make& m, const Cutout& cut, const char* name) {
        PictureUse use;
        use.asset = id_();
        use.png = encodePng(cut.image);
        use.path = imageAssetPath(std::string("reference-") + name, use.png);
        use.image = std::make_shared<Image>(cut.image);
        m.put("sprite", "texture", AssetRef{use.asset});
        const double size = 2.0 * cut.halfSize * kU;
        m.size(size, size).turn(0, 0);
        pictures_.push_back(std::move(use));
    }

    static const char* cutoutNote() {
        return "This layer draws pixels cut out of your reference, so it matches closely. Only keep it if the "
               "reference is yours to use; otherwise repaint it or switch the layer to a built-in shape.";
    }

    std::string timingNote() const {
        return plan_.measured ? std::string("Its size and brightness over time follow the clip.")
                              : std::string("Its timing is assumed; a still picture does not show it.");
    }

    void glows() {
        if (!s_.hasGlow) {
            return;
        }
        const bool additive = s_.additive;
        const bool two = s_.glowInnerLevel > 0.0f && o_.variation != Variation::Performance &&
                         o_.mode != MatchMode::Motion;
        if (s_.glowOuterLevel >= 0.03f) {
            Make m(id_, "Outer glow");
            oneShot(m, false);
            const double level = s_.glowOuterLevel;
            m.colorLinear(linear(additive ? bright(s_.glowOuterColour) : s_.glowOuterColour,
                                    std::clamp(level, 0.05, 1.0)))
                .sizeOver(plan_.grow)
                .fade(plan_.fade)
                .look("soft");
            const double one = 2.0 * s_.glowOuter * kU;  // the soft shape fades to nothing at its edge
            m.size(one, one);
            blend(m, additive, level);
            if (plan_.tinted) {
                m.tint(plan_.tint);
            }
            push(m, "glow", "One large soft sprite", "glow", Basis::Observed, 1, 1, timingNote());
        }
        if (s_.hasCore && s_.coreLevel > 0.0f) {
            // White light at the centre that the glow's colour cannot make.
            Make m(id_, "White core");
            oneShot(m, false);
            const double level = s_.coreLevel;
            const double one = 2.0 * s_.coreRadius * kU;
            m.size(one, one).colorLinear(linear(s_.coreColour, std::clamp(level, 0.05, 1.0))).sizeOver(plan_.grow)
                .fade(plan_.fade).look("soft");
            blend(m, additive, level);
            push(m, "core", "One small white soft sprite", "core", Basis::Observed, 3, 1, timingNote());
        }
        if (two) {
            Make m(id_, "Inner glow");
            oneShot(m, false);
            const double level = s_.glowInnerLevel;
            const double one = 2.0 * s_.glowInner * kU;
            m.size(one, one)
                .colorLinear(linear(additive ? bright(s_.glowInnerColour) : s_.glowInnerColour,
                                    std::clamp(level, 0.05, 1.0)))
                .sizeOver(plan_.grow)
                .fade(plan_.fade)
                .look("soft");
            blend(m, additive, level);
            push(m, "glow", "One small bright soft sprite", "glow", Basis::Observed, 3, 1, timingNote());
        }
    }

    void body() {
        const SpriteShape shape = shapeFrom(s_.bodyShape, SpriteShape::Disc);
        const bool additive = s_.additive && s_.hardness < 0.5f;
        if (const Cutout* cut = s_.bodyMatch < 0.85f ? cutout("body") : nullptr) {
            Make m(id_, "Shape");
            oneShot(m, true);
            m.colorLinear(Color{1, 1, 1, 1}).sizeOver(plan_.grow);
            if (s_.hardness > 0.5f && !plan_.measured) {
                m.fade({{0, 1}, {0.75, 1}, {1, 0}});
            } else {
                m.fade(plan_.fade);
            }
            usePicture(m, *cut, "shape");
            blend(m, additive, 1.0);
            push(m, "body", "One sprite, drawing the main shape cut from the reference", "body", Basis::Observed, 0, 1,
                 cutoutNote());
            return;
        }
        // Pointed, regular shapes are trusted on a fair match. A blobby
        // outline that only roughly fits a blobby shape is more likely a
        // cloud of several overlapping pieces.
        const bool pointed = shape == SpriteShape::Burst || shape == SpriteShape::Star || shape == SpriteShape::Starflash ||
                             shape == SpriteShape::Glint || shape == SpriteShape::Heart || shape == SpriteShape::Crescent ||
                             shape == SpriteShape::Bolt || shape == SpriteShape::Sliver || shape == SpriteShape::Diamond ||
                             shape == SpriteShape::Square;
        const bool cluster = s_.hardness > 0.4f && s_.elongation < 1.9f &&
                             (s_.bodyMatch < 0.72f || (!pointed && s_.bodyMatch < 0.88f));
        if (cluster) {
            // No one shape fits a lumpy outline: a cluster of puffs stands in
            // for it, in the body's main colour, with a second, smaller
            // cluster in its other colour when it has one.
            const double r = s_.bodyRadius * kU;
            const auto puffs = [&](const char* name, int count, double spread, double low, double high, const Swatch& colour,
                                   int rank) {
                Make m(id_, name);
                m.burst(count).circle(spread * r).life(plan_.life * 0.8, plan_.life).speed(0.05 * r, 0.35 * r).aim(90, 180)
                    .size(low * r, high * r).turn(-40, 40).colorLinear(linear(colour)).drag(3.0).gravity(0, 0)
                    .sizeOver(plan_.grow).fade({{0, 1}, {0.75, 1}, {1, 0}}).look("puff");
                push(m, "body", "A cluster of puff shapes", "body", Basis::Observed, rank, count,
                     "No single built-in shape matched the outline, so it is approximated with a cluster. For an "
                     "exact outline, allow cut-outs from the reference, or paint it and use it as this layer's picture.");
            };
            puffs("Shape", scaled(8, 5), 0.5, 0.7, 1.1, s_.bodyColour, 0);
            // The second colour: the palette entry least like the first.
            const Swatch* second = nullptr;
            float gap = 0.25f;
            for (const Swatch& c : s_.palette) {
                const float d = std::fabs(c.r - s_.bodyColour.r) + std::fabs(c.g - s_.bodyColour.g) + std::fabs(c.b - s_.bodyColour.b);
                if (c.share >= 0.12f && d > gap) {
                    gap = d;
                    second = &c;
                }
            }
            if (second && o_.variation != Variation::Performance && o_.mode != MatchMode::Motion) {
                puffs("Shape, inner", scaled(5, 3), 0.3, 0.45, 0.75, *second, 3);
            }
            return;
        }
        Make m(id_, "Shape");
        oneShot(m, true);
        const double size = sizeFor(shape, s_.bodyRadius, &ShapeMeasure::extent);
        m.size(size, size).turn(s_.bodyTurn, s_.bodyTurn)
            .colorLinear(linear(additive ? bright(s_.bodyColour) : s_.bodyColour))
            .sizeOver(plan_.grow)
            .look(shapeName(shape));
        if (s_.hardness > 0.5f && !plan_.measured) {
            m.fade({{0, 1}, {0.75, 1}, {1, 0}});
        } else {
            m.fade(plan_.fade);
        }
        blend(m, additive, 1.0);
        if (plan_.tinted) {
            m.tint(plan_.tint);
        }
        std::string note = timingNote();
        if (s_.bodyMatch < 0.6f) {
            note = "The built-in \"" + s_.bodyShape + "\" shape is only a rough match for the outline. " + note;
        }
        push(m, "body", std::string("One sprite, the \"") + shapeName(shape) + "\" shape", "body", Basis::Observed, 0, 1,
             note);
    }

    void rings() {
        const std::size_t most = o_.mode == MatchMode::Layered || o_.mode == MatchMode::ShapeColour ? 3u : 1u;
        for (std::size_t i = 0; i < s_.rings.size() && i < most; ++i) {
            const RingFound& ring = s_.rings[i];
            const SpriteShape shape = shapeFrom(
                ring.shape, ring.thickness < 0.22f * ring.radius ? SpriteShape::Ring : SpriteShape::Shockwave);
            Make m(id_, s_.rings.size() > 1 && most > 1 ? ("Ring " + std::to_string(i + 1)).c_str() : "Ring");
            oneShot(m, false);
            const double size = sizeFor(shape, ring.radius, &ShapeMeasure::ringRadius);
            m.size(size, size)
                .colorLinear(linear(s_.additive ? bright(ring.colour) : ring.colour, std::clamp<double>(ring.strength * 1.6, 0.2, 1.0)))
                .look(shapeName(shape));
            if (plan_.measured) {
                m.sizeOver(plan_.grow).fade(plan_.fadeRing.empty() ? plan_.fade : plan_.fadeRing);
            } else if (plan_.steady) {
                m.sizeOver({{0, 1}, {1, 1}}).solid();
            } else {
                // A ring is assumed to open outward and thin away.
                const double at = plan_.peak / plan_.life;
                m.sizeOver({{0, 0.25}, {at, 1.0}, {1, 1.3}}).fade({{0, 0}, {0.5 * at, 1}, {at, 1}, {1, 0}});
            }
            blend(m, s_.additive, 1.0);
            push(m, "ring", std::string("One sprite, the \"") + shapeName(shape) + "\" shape",
                 "ring", Basis::Observed, 4, 1,
                 plan_.measured ? timingNote() : std::string("It is assumed to open outward; a still picture does not show that."));
        }
    }

    void rays() {
        const bool additive = s_.additive;
        Swatch colour = additive ? bright(s_.rayColour) : s_.rayColour;
        const double reach = std::max(0.02f, s_.rayReach) * kU;
        const SpriteShape flash = shapeFrom(s_.rayShape, SpriteShape::Starflash);
        const bool whiteRays = std::max({colour.r, colour.g, colour.b}) - std::min({colour.r, colour.g, colour.b}) < 0.25f;
        if (whiteRays && measureShape(flash).shine > 0.2f) {
            // The shape turns its own middle white-hot, so it wants the
            // colour of the light round it, not the white of the rays.
            Swatch tint = s_.hasGlow ? s_.glowOuterColour : (s_.palette.empty() ? colour : s_.palette.front());
            for (const Swatch& c : s_.palette) {
                const float sat = std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b});
                const float have = std::max({tint.r, tint.g, tint.b}) - std::min({tint.r, tint.g, tint.b});
                if (sat > have + 0.15f) {
                    tint = c;
                }
            }
            colour = additive ? bright(tint) : tint;
        }
        if (const Cutout* cut = cutout("rays")) {
            Make m(id_, "Flash");
            oneShot(m, true);
            m.colorLinear(Color{1, 1, 1, 1}).sizeOver(plan_.grow).fade(plan_.flash);
            usePicture(m, *cut, "flash");
            blend(m, additive, 1.0);
            push(m, "rays", "One sprite, drawing the rays cut from the reference", "rays", Basis::Observed, 2, 1,
                 cutoutNote());
            return;
        }
        // A built-in flash is used whenever one is anything like the rays
        // seen. Only many rays that match nothing become loose slivers.
        if (!s_.rayShape.empty() && (s_.rayMatch >= 0.2f || s_.rays < 8)) {
            const SpriteShape shape = flash;
            Make m(id_, "Flash");
            oneShot(m, true);
            const double size = 2.0 * reach / std::max(0.5f, measureShape(shape).reach);
            m.size(size, size).turn(s_.rayTurn, s_.rayTurn).colorLinear(linear(colour)).sizeOver(plan_.grow)
                .fade(plan_.flash).look(shapeName(shape));
            blend(m, additive, 1.0);
            push(m, "rays", std::string("One sprite, the \"") + shapeName(shape) + "\" shape", "rays", Basis::Observed, 2,
                 1, std::to_string(s_.rays) + " rays were counted; the built-in shape has its own number of points.");
            return;
        }
        // Many or uneven rays: thin slivers thrown out from the centre that
        // stop at once, one per ray.
        const int count = scaled(std::clamp(s_.rays, 3, 40), 3);
        Make m(id_, "Rays");
        const double drag = 22.0;
        const double speedLow = 0.25 * reach * drag, speedHigh = 0.55 * reach * drag;
        m.burst(count).life(plan_.life * 0.55, plan_.life * 0.75).speed(speedLow, speedHigh).aim(90, 180)
            .size(0.7 * reach, 1.2 * reach).colorLinear(linear(colour)).drag(drag).gravity(0, 0)
            .sizeOver({{0, 0.3}, {0.2, 1.0}, {1, 0.7}}).fade(plan_.flash).look("sliver").streak(0.0);
        blend(m, additive, 1.2);
        push(m, "rays", "Thin slivers pointing outward, one per ray", "rays", Basis::Observed, 2, count,
             "Their angles are random, so they will not sit exactly where the reference's rays do.");
    }

    void pieces(const PieceGroup& all, const char* role, const char* name, int rank) {
        int count = pieceCount(all);
        if (count <= 0) {
            return;
        }
        // A couple of specks are more likely dirt in the picture than effect.
        if (count < 3 && o_.mode != MatchMode::Layered) {
            return;
        }
        if (o_.variation == Variation::Performance && std::string(role) == "bits") {
            return;
        }
        count = scaled(count);
        const bool streaks = std::string(role) == "streaks";
        const bool additive = s_.additive;
        const bool split = o_.mode == MatchMode::Layered && !streaks && count >= 10 && all.sizeHigh > 2.5f * all.sizeLow;

        SpriteShape shape = SpriteShape::Disc;
        if (streaks) {
            // Thin dashes. (The "streak" shape has a bright head and a
            // fading tail, which a still dash does not show.)
            shape = all.softness > 0.6f ? SpriteShape::Streak : SpriteShape::Sliver;
        } else if (!all.shape.empty()) {
            shape = shapeFrom(all.shape, SpriteShape::Disc);
            // Crisp-edged pieces are discs, however round and plain they look.
            if (shape == SpriteShape::Soft && all.softness < 0.4f) {
                shape = SpriteShape::Disc;
            }
        } else if (all.softness > 0.5f || (additive && all.sizeHigh < 0.03f)) {
            shape = SpriteShape::Soft;
        } else if (std::string(role) == "bits") {
            shape = SpriteShape::Shard;
        }

        const auto buildFor = [&](const PieceGroup& g, const char* layerName, int n, float sizeLow, float sizeHigh) {
            Make m(id_, layerName);
            const double drag = plan_.drag;
            double life1 = plan_.life * 0.55, life2 = plan_.life * 0.95;
            if (g.lifetime > 0) {
                life1 = std::max(0.08, 0.7 * g.lifetime);
                life2 = std::max(life1 + 0.02, 1.05 * g.lifetime);
            }
            // Where the pieces are at the moment of the picture fixes how
            // fast they must have left the centre.
            const double at = plan_.steady ? 0.6 * life2 : std::max(0.05, plan_.peak);
            const double reachFactor = std::max(1e-3, travel(drag, at));
            double speed1 = g.nearest * kU / reachFactor, speed2 = g.farthest * kU / reachFactor;
            Basis motion = Basis::Inferred;
            if (g.speed > 0) {
                // The clip showed how fast the group spreads.
                const double mid = g.speed * kU;
                const double ratio = g.farthest > 1e-4f ? g.nearest / g.farthest : 0.5;
                speed2 = mid * 2.0 / (1.0 + ratio);
                speed1 = speed2 * ratio;
                motion = Basis::Observed;
            }
            speed2 = std::max(speed2, speed1 + 0.05);
            const float measure = shape == SpriteShape::Soft ? measureShape(shape).outer : measureShape(shape).extent;
            double size1 = std::max(0.03, sizeLow * kU / std::max(0.3f, measure));
            double size2 = std::max(size1, sizeHigh * kU / std::max(0.3f, measure));
            if (streaks) {
                size1 = std::max(0.04, g.sizeLow * kU * 1.6);
                size2 = std::max(size1, g.sizeHigh * kU * 1.6);
            }
            if (plan_.steady) {
                m.rate(std::max(1.0, n / std::max(0.1, 0.5 * (life1 + life2))));
                m.circle(std::max(0.02, 0.5 * g.nearest * kU));
            } else {
                for (const auto& [when, share] : plan_.bursts) {
                    m.burst(std::max<std::int64_t>(1, std::llround(n * share)), when);
                }
            }
            m.life(life1, life2).speed(speed1, speed2).size(size1, size2).drag(drag).gravity(0, 0)
                .colorLinear(linear(additive ? bright(g.colour) : g.colour))
                .sizeOver({{0, 1}, {0.6, 0.85}, {1, 0}}).fade({{0, 1}, {0.7, 1}, {1, 0}}).look(shapeName(shape));
            if (streaks && !g.radial) {
                // All leaning one way: rain, speed lines.
                m.aim(g.heading, g.spread).rect(2.4 * s_.extent * kU, 2.4 * s_.extent * kU);
            } else {
                m.aim(g.heading, g.spread);
            }
            if (streaks && shape == SpriteShape::Sliver) {
                // A sliver is as long as the particle is large, and points
                // the way it moves.
                m.size(std::max(0.05, g.lengthLow * kU), std::max(0.06, g.lengthHigh * kU));
                m.sizeOver({{0, 0.5}, {0.25, 1.0}, {1, 0.6}});
                m.streak(0.0);
            } else if (streaks) {
                // Stretched along its movement to the length seen.
                const double speedNow = 0.5 * (speed1 + speed2) * std::exp(-drag * at);
                const double length = 0.5 * (g.lengthLow + g.lengthHigh) * kU;
                m.streak(std::clamp(length / std::max(0.05, speedNow), 0.0, 5.0));
            } else if (shape != SpriteShape::Soft && shape != SpriteShape::Disc) {
                m.turn(-180, 180);
            }
            blend(m, additive, 1.0);
            std::string note = motion == Basis::Observed
                                   ? "Their speed follows the clip, measured for the group as a whole."
                                   : "They are assumed to fly outward and slow; their speed is set so they are where "
                                     "the reference shows them at its fullest moment.";
            note += " Each one's exact place is random.";
            push(m, role, plan_.steady ? "Particles sent out steadily" : "A burst of particles", role, Basis::Observed,
                 rank, n, note);
            built_.back().note.motion = motion;
        };
        const auto build = [&](const char* layerName, int n, float sizeLow, float sizeHigh) {
            buildFor(all, layerName, n, sizeLow, sizeHigh);
        };
        const auto buildColour = [&](const char* layerName, int n, const PieceGroup& one) {
            buildFor(one, layerName, n, one.sizeLow, one.sizeHigh);
        };

        // Clearly different colours get a layer each, as far as the mode allows.
        std::size_t colourLayers = 1;
        if (!all.colours.empty() && o_.variation != Variation::Performance) {
            colourLayers = std::min<std::size_t>(all.colours.size(),
                                                 o_.mode == MatchMode::Layered ? 3u : (o_.mode == MatchMode::Motion ? 1u : 2u));
        }
        if (colourLayers >= 2) {
            float shares = 0.0f;
            for (std::size_t i = 0; i < colourLayers; ++i) {
                shares += all.colours[i].share;
            }
            for (std::size_t i = 0; i < colourLayers; ++i) {
                PieceGroup one = all;
                one.colour = all.colours[i];
                // Each colour's pieces at their own size: small white glints
                // among large gold coins stay small.
                if (i < all.colourSizes.size() && all.colourSizes[i] > 0.0f && !streaks) {
                    one.sizeLow = 0.75f * all.colourSizes[i];
                    one.sizeHigh = 1.25f * all.colourSizes[i];
                }
                const std::string layerName = std::string(name) + ", " + colourName(all.colours[i]);
                const int n = std::max(2, static_cast<int>(std::lround(count * all.colours[i].share / std::max(0.01f, shares))));
                buildColour(layerName.c_str(), n, one);
            }
            return;
        }
        if (split) {
            const float middle = std::sqrt(all.sizeLow * all.sizeHigh);
            build((std::string("Small ") + name).c_str(), std::max(2, count * 2 / 3), all.sizeLow, middle);
            build((std::string("Large ") + name).c_str(), std::max(2, count / 3), middle, all.sizeHigh);
        } else {
            build(name, count, all.sizeLow, all.sizeHigh);
        }
    }

    void smoke() {
        const int count = scaled(6, 3);
        Make m(id_, "Smoke");
        const double r = s_.smokeRadius * kU;
        m.burst(count).circle(0.4 * r).life(plan_.life * 0.8, plan_.life).speed(0.1 * r, 0.4 * r).aim(90, 180)
            .size(0.9 * r, 1.4 * r).turn(-180, 180).colorLinear(linear(s_.smokeColour, 0.6)).drag(2.0).gravity(0, 0.3)
            .sizeOver({{0, 0.6}, {1, 1.2}}).fade({{0, 0}, {0.15, 1}, {0.6, 0.8}, {1, 0.3}}).look("smoke")
            .turbulence(0.2 * r, std::max(0.3, 0.8 * r), 0.5).dissolve(0.55, 0.0, 0.6);
        push(m, "smoke", "A few soft smoke puffs", "smoke", Basis::Inferred, 8, count,
             "Grey, soft areas are read as smoke. That it drifts on moving air and thins away in patches is how "
             "smoke goes, not something measured.");
    }

    void comet() {
        const bool additive = s_.additive;
        const std::vector<Swatch>& colours = s_.tailColours;
        const Swatch headColour = colours.empty() ? Swatch{1, 1, 1, 0} : colours.front();
        const double headRadius = std::max(0.01f, s_.headRadius) * kU;
        const double length = std::max(0.05f, s_.tailLength) * kU;
        // The tail: particles streaming from the head, shrinking and
        // changing colour as they go.
        {
            const double life = 0.6;
            // The last part of each particle's flight is too faint to see,
            // so they are sent a little further than the tail is long.
            const double speed = 1.25 * length / life;
            // A soft blob looks about three quarters as wide as its particle.
            const double width = std::max(0.02f, s_.tailWidthStart) * kU / 0.75;
            // Close enough together to read as one streak, not a row of dots.
            const double rate = std::clamp(8.0 * speed / width, 30.0, 240.0) * scale_;
            const double overlap = std::max(1.0, 0.6 * width * std::max(8.0, rate) / speed);
            const double level = std::clamp(2.2 / overlap, 0.08, 1.0);
            Make m(id_, "Tail");
            const double taper = s_.tailWidthStart > 1e-4f ? std::clamp<double>(s_.tailWidthEnd / s_.tailWidthStart, 0.05, 1.5) : 0.3;
            // Every particle alike and evenly spaced: any randomness here
            // shows as lumps travelling down the tail.
            m.rate(std::max(8.0, rate)).life(life, life).speed(speed, speed)
                .aim(s_.tailHeading, 0.0).size(width, width).drag(0.0).gravity(0, 0)
                .colorLinear(linear(additive ? bright(headColour) : headColour, level))
                .sizeOver({{0, 1}, {1, taper}}).fade({{0, 1}, {0.5, 0.75}, {1, 0}}).look("soft");
            if (colours.size() >= 2) {
                std::vector<Tint> tint;
                const Color base = linear(headColour);
                for (std::size_t i = 0; i < colours.size(); ++i) {
                    const Color c = linear(colours[i]);
                    tint.push_back(Tint{static_cast<double>(i) / static_cast<double>(colours.size() - 1),
                                        std::clamp(c.r / std::max(0.02, base.r), 0.0, 3.0),
                                        std::clamp(c.g / std::max(0.02, base.g), 0.0, 3.0),
                                        std::clamp(c.b / std::max(0.02, base.b), 0.0, 3.0), 1.0});
                }
                m.tint(tint);
            }
            blend(m, additive, 1.0);
            push(m, "trail", "Soft particles streaming back from the head", "comet", Basis::Observed, 0,
                 static_cast<int>(std::lround(std::max(8.0, rate) * life)),
                 "The head stays where it is and the tail streams behind it, as in the picture. To make it fly, move "
                 "the effect in your game.");
        }
        {
            Make m(id_, "Head");
            m.burst(1).life(plan_.duration, plan_.duration).speed(0, 0).gravity(0, 0);
            const double size = 2.0 * headRadius * 1.6;
            m.size(size, size).colorLinear(linear(additive ? bright(towardWhite(headColour, 0.5f)) : headColour))
                .sizeOver({{0, 1}, {0.5, 1.08}, {1, 1}}).solid().look("soft");
            blend(m, additive, 1.4);
            push(m, "core", "One bright soft sprite", "comet", Basis::Observed, 1, 1);
        }
    }

    // Something lying on the ground, seen from an angle: a flat base that
    // turns in its own plane, with shafts of light and motes rising from it.
    void ground() {
        const bool additive = s_.additive;
        const double tilt = std::clamp<double>(s_.groundTilt, 30.0, 82.0);
        const double radius = std::max(0.02f, s_.groundRadius);
        const bool stays = plan_.steady;
        const std::string& pattern = s_.groundPattern;
        const Basis turning = plan_.spinSeen ? Basis::Observed : Basis::Inferred;
        const std::string flatNote = "Lies flat on the ground, leaning back about " +
                                     std::to_string(static_cast<int>(std::lround(tilt))) +
                                     " degrees, as the oval in the reference shows. The Lie Flat control changes it.";
        const auto turnNote = [&]() -> std::string {
            if (plan_.spin == 0.0) {
                return {};
            }
            return plan_.spinSeen ? " Its turning was measured from the clip."
                                  : " Its turning is assumed: a still picture does not show any.";
        };
        // One sprite that stays (or, for a burst, grows and fades as the clip does).
        const auto base = [&](Make& m, bool turns) {
            if (stays) {
                m.burst(1).life(plan_.duration, plan_.duration).speed(0, 0).gravity(0, 0);
                if (turns && plan_.spin != 0.0) {
                    m.spin(plan_.spin, plan_.spin);
                }
            } else {
                oneShot(m, false);
                m.sizeOver(plan_.grow);
            }
            m.lieFlat(tilt);
        };
        const auto hold = [&](Make& m, bool ring) {
            if (stays) {
                m.solid();
            } else {
                m.fade(ring && !plan_.fadeRing.empty() ? plan_.fadeRing : plan_.fade);
            }
        };
        const Cutout* cut = cutout("ground");

        // The soft light round the base.
        if (!cut && s_.groundGlowRadius > 1.05f * s_.groundRadius) {
            Make m(id_, "Ground glow");
            base(m, false);
            const double size = 2.0 * s_.groundGlowRadius * kU / 0.8;
            const Swatch colour = additive ? bright(s_.groundGlowColour) : s_.groundGlowColour;
            m.size(size, size).colorLinear(linear(colour, 0.5)).look("soft");
            hold(m, false);
            blend(m, additive, 1.0);
            push(m, "glow", "One large soft sprite, lying flat", "ground", Basis::Observed, 1, 1, flatNote);
            built_.back().note.motion = stays ? Basis::Inferred : built_.back().note.motion;
        }

        if (cut) {
            // The base as painted, laid back down flat so that it can turn.
            Make m(id_, "Base");
            base(m, true);
            usePicture(m, *cut, "ground");
            m.colorLinear(Color{1, 1, 1, 1});
            hold(m, false);
            if (additive) {
                m.additive();
            }
            push(m, "body", "One sprite, drawing the base cut from the reference and laid flat", "ground",
                 Basis::Observed, 0, 1, flatNote + turnNote() + " " + cutoutNote());
            built_.back().note.motion = turning;
        } else if (pattern == "vortex") {
            const Swatch colour = additive ? bright(s_.groundColour) : s_.groundColour;
            {
                Make m(id_, "Vortex");
                base(m, true);
                const double size = 2.0 * radius * kU / 0.76;
                m.size(size, size).colorLinear(linear(colour)).look("vortex");
                hold(m, false);
                blend(m, additive, 1.2);
                push(m, "body", "One sprite, the \"vortex\" shape, lying flat and turning", "ground", Basis::Observed, 0,
                     1,
                     flatNote + turnNote() +
                         " The arms are the built-in whirl, not the reference's own; allow cut-outs to keep those.");
                built_.back().note.motion = turning;
            }
            if (o_.variation != Variation::Performance && o_.mode != MatchMode::Motion) {
                // A second, smaller whirl turning faster gives the depth a
                // real one has. Its colour is the reference's second colour.
                const Swatch second = additive ? bright(s_.groundInnerColour) : s_.groundInnerColour;
                Make m(id_, "Inner vortex");
                base(m, false);
                if (stays && plan_.spin != 0.0) {
                    m.spin(2.0 * plan_.spin, 2.0 * plan_.spin);
                }
                const double size = 2.0 * 0.62 * radius * kU / 0.76;
                m.size(size, size).turn(50, 50).colorLinear(linear(second)).look("vortex");
                hold(m, false);
                blend(m, additive, 1.2);
                push(m, "body", "A smaller \"vortex\" shape inside the first, turning twice as fast", "ground",
                     Basis::Observed, 3, 1,
                     "Its colour is the colour of the reference's inner part. That it turns faster than the outer "
                     "whirl is assumed.");
                built_.back().note.motion = Basis::Inferred;
            }
        } else if (pattern == "sigil") {
            Make m(id_, "Circle");
            base(m, true);
            const double size = 2.0 * radius * kU / 0.93;
            const Swatch colour = additive ? bright(s_.groundColour) : s_.groundColour;
            m.size(size, size).colorLinear(linear(colour)).look("sigil");
            hold(m, false);
            blend(m, additive, 1.0);
            push(m, "body", "One sprite, the \"sigil\" shape (a circle of lines), lying flat", "ground", Basis::Observed,
                 0, 1,
                 flatNote + turnNote() +
                     " The lines are the built-in circle, not the reference's own drawing; allow cut-outs to keep that, "
                     "or paint your own and use it as this layer's picture.");
            built_.back().note.motion = turning;
        } else if (pattern == "ring") {
            const SpriteShape shape = s_.hardness > 0.5f ? SpriteShape::Shockwave : SpriteShape::Ring;
            Make m(id_, "Ring");
            base(m, false);
            const double size = sizeFor(shape, radius * 0.92, &ShapeMeasure::ringRadius);
            const Swatch colour = additive ? bright(s_.groundColour) : s_.groundColour;
            m.size(size, size).colorLinear(linear(colour)).look(shapeName(shape));
            hold(m, true);
            blend(m, additive, 1.1);
            push(m, "ring", std::string("One sprite, the \"") + shapeName(shape) + "\" shape, lying flat", "ground",
                 Basis::Observed, 0, 1, flatNote);
        } else {
            // Plain light: a soft disc, with a ring at its edge when one shows.
            const Swatch colour = additive ? bright(s_.groundColour) : s_.groundColour;
            {
                Make m(id_, "Base");
                base(m, false);
                const double size = 2.0 * radius * kU / 0.56;
                m.size(size, size).colorLinear(linear(colour, std::clamp<double>(s_.groundLevel, 0.3, 1.0))).look("soft");
                hold(m, false);
                blend(m, additive, 1.3);
                push(m, "body", "One soft sprite, lying flat", "ground", Basis::Observed, 0, 1,
                     flatNote + " No pattern could be made out on it, so it is plain light; allow cut-outs to keep "
                                "what is drawn there.");
                if (stays && !plan_.measured) {
                    built_.back().note.motion = Basis::Inferred;
                }
            }
            {
                Make m(id_, "Rim");
                base(m, false);
                const double size = sizeFor(SpriteShape::Ring, radius, &ShapeMeasure::ringRadius);
                m.size(size, size).colorLinear(linear(towardWhite(colour, 0.3f), 0.8)).look("ring");
                hold(m, true);
                blend(m, additive, 1.0);
                push(m, "ring", "One sprite, the \"ring\" shape, lying flat at the base's edge", "ground",
                     s_.groundRings >= 1 ? Basis::Observed : Basis::Inferred, 4, 1);
            }
        }

        // A white light of its own in the middle.
        if (!cut && s_.groundCore && s_.groundCoreRadius > 0.0f) {
            Make m(id_, "Core");
            base(m, false);
            // Driven hard, a soft sprite burns white out to seven tenths of
            // its radius and has only a narrow skirt of colour beyond: a
            // tight white light, as wide as the white seen, rather than a
            // wash over everything round it.
            const double size = 2.0 * std::max(s_.groundCoreRadius, 0.12f * s_.groundRadius) * kU / 0.85;
            m.size(size, size).colorLinear(linear(towardWhite(s_.groundCoreColour, 0.2f))).look("soft");
            if (stays) {
                m.sizeOver({{0, 1.0}, {0.25, 1.1}, {0.5, 1.0}, {0.75, 1.1}, {1, 1.0}});
            }
            hold(m, false);
            blend(m, additive, 4.0);
            push(m, "core", "One small bright soft sprite, lying flat", "ground", Basis::Observed, 2, 1,
                 stays ? "Its slow pulse is assumed." : "");
            if (stays) {
                built_.back().note.motion = Basis::Inferred;
            }
        }

        // Shafts of light standing on the base.
        if (s_.beams >= 2) {
            const double height = std::max(0.08f, s_.beamHeight) * kU;
            const double width = 2.0 * std::max(0.02f, s_.beamSpread) * kU;
            const double thickness = std::clamp<double>(s_.beamWidth * kU * 1.6, 0.07, 0.6);
            // Each lives a little over a second, so this many a second keeps
            // about as many standing as the reference shows.
            const double life = 1.3;
            const int showing = scaled(s_.beams, 2);
            // A streak is as long as its particle travels in `stretch`
            // seconds, so the speeds set the lengths: from a good deal
            // shorter than the typical shaft seen, to the tallest.
            const double stretch = 7.5;
            const double shortest = 0.6 * height;
            const double tallest = std::max(1.05 * height, static_cast<double>(s_.beamTallest) * kU);
            Make m(id_, "Beams");
            const Swatch colour = additive ? bright(s_.beamColour) : s_.beamColour;
            if (stays) {
                m.rate(showing / (0.8 * life));
            } else {
                m.burst(showing, 0.0);
            }
            m.rect(width, 0.25 * width).at(0.0, 0.45 * height)
                .life(0.7 * life, 1.3 * life).speed(shortest / stretch, tallest / stretch).aim(90, 0)
                .size(0.7 * thickness, 1.3 * thickness).colorLinear(linear(colour)).gravity(0, 0)
                .sizeOver({{0, 0.6}, {0.3, 1.0}, {1, 0.7}}).fade({{0, 0}, {0.25, 1}, {0.6, 0.8}, {1, 0}})
                .look("beam").streak(stretch);
            blend(m, additive, 1.3);
            push(m, "rays", "Thin shafts of light: particles drawn as long upright streaks", "ground", Basis::Observed,
                 2, showing,
                 "How many, how tall and how wide are as seen. Where each one stands, and how they come and go, is "
                 "random.");
            built_.back().note.motion = Basis::Inferred;
        }

        // Sparks in the air above it.
        const int motes = pieceCount(s_.sparks);
        if (motes >= 3 || (motes >= 1 && o_.mode == MatchMode::Layered)) {
            const int showing = scaled(motes, 3);
            const double life = 1.35;
            const double reach = std::max<double>(s_.beams >= 2 ? s_.beamTallest : 0.0f, s_.sparks.farthest) * kU;
            const double width = 2.0 * std::max<double>(s_.groundRadius * 0.7, s_.beamSpread) * kU;
            // Motes are specks: at this size a square and a dot look alike
            // in the reference, so anything but a glint is drawn as a dot.
            const double low = std::clamp(static_cast<double>(s_.sparks.sizeLow) * kU, 0.03, 0.14);
            const double high = std::clamp(static_cast<double>(s_.sparks.sizeHigh) * kU, low, 0.2);
            SpriteShape shape = shapeFrom(s_.sparks.shape, SpriteShape::Disc);
            if (shape != SpriteShape::Glint && shape != SpriteShape::Twinkle && shape != SpriteShape::Sparkle) {
                shape = s_.sparks.softness > 0.6f ? SpriteShape::Soft : SpriteShape::Disc;
            }
            Make m(id_, "Motes");
            const Swatch colour = additive ? bright(s_.sparks.colour) : s_.sparks.colour;
            if (stays) {
                m.rate(showing / life);
            } else {
                m.burst(showing, 0.0);
            }
            const double rise = std::max(0.4, reach) / life;
            m.rect(width, 0.3 * width).at(0.0, 0.1 * std::max(0.4, reach))
                .life(0.7 * life, 1.3 * life).speed(0.5 * rise, 1.1 * rise).aim(90, 12)
                .size(low, high).colorLinear(linear(colour)).gravity(0, 0.4)
                .sizeOver({{0, 0}, {0.25, 1.0}, {1, 0}}).solid().look(shapeName(shape));
            blend(m, additive, 1.4);
            push(m, "sparks", stays ? "Small particles sent up steadily" : "A burst of small particles", "ground",
                 Basis::Observed, 5, showing,
                 "Their number, size and colour are as seen. That they rise is assumed.");
            built_.back().note.motion = Basis::Inferred;
        }
        if (!cut && (pattern == "vortex" || pattern == "sigil")) {
            skipped_.push_back("The pattern on the ground is a built-in one of the same kind (a whirl, or a circle of "
                               "lines), not a copy of the reference's. Allowing cut-outs from the reference lifts the "
                               "real one out and lays it flat.");
        }
        skipped_.push_back("The base is one flat shape leaned back, not true depth: the far side is not smaller than "
                           "the near side as it would be through a camera.");
    }

    // ---- lines of light (see ReferenceLines.cpp)

    // About how many dots a line's layers keep alive at full density.
    int lineDots(const LineFound& line) const {
        const double lengthW = std::max(0.05, static_cast<double>(line.length) * kU);
        const double core = sizeFor(SpriteShape::Soft, 0.5 * line.thickness, &ShapeMeasure::halfLevel);
        double dots = lengthW / (0.25 * core);
        if (line.halo > 0.5f * line.thickness && line.haloLevel > 0.05f) {
            dots += lengthW / (0.15 * sizeFor(SpriteShape::Soft, 0.5 * line.thickness + line.halo, &ShapeMeasure::outer));
        }
        if (line.jagged >= 0.35f) {
            dots += 2.5 * lengthW / std::clamp(2.5 * static_cast<double>(line.band) * kU, 0.25, 1.5);
        }
        return static_cast<int>(dots) + line.sparkles;
    }

    // A place in the reference, in the effect's units.
    Point2 world(const Point2& p) const {
        const double aspect = static_cast<double>(std::max(1, s_.width)) / std::max(1, s_.height);
        return Point2{(p.x - s_.centreX) * aspect * kU, (s_.centreY - p.y) * kU};
    }

    // A line's brightness at a moment of the effect (seconds), from what the
    // clip showed; 1 when it showed nothing.
    double lineLevelAt(const LineFound& line, double effectTime) const {
        if (!plan_.measured || line.levelOverTime.empty() || plan_.fps <= 0) {
            return 1.0;
        }
        double t = effectTime;
        if (plan_.steady && plan_.duration > 0) {
            t = std::fmod(std::max(0.0, t), plan_.duration);
        }
        const double frame = (plan_.start + t) * plan_.fps;
        const int n = static_cast<int>(line.levelOverTime.size());
        const int a = std::clamp(static_cast<int>(std::floor(frame)), 0, n - 1), b = std::min(n - 1, a + 1);
        const double f = std::clamp(frame - std::floor(frame), 0.0, 1.0);
        return line.levelOverTime[static_cast<std::size_t>(a)] * (1.0 - f) +
               line.levelOverTime[static_cast<std::size_t>(b)] * f;
    }

    // How many dots a second, over the layer's own time (start, length in
    // seconds of the effect), as a curve: the base rate times the line's
    // brightness then, times how much of it has been drawn (drawTime > 0).
    void lineRate(Make& m, const LineFound& line, double rate, double start, double length, double drawTime = 0.0) {
        if (!plan_.measured || line.levelOverTime.empty()) {
            if (drawTime > 0.0) {
                m.put("emission", "rate", Scalar::curve({{0, 0.15 * rate}, {std::min(1.0, drawTime / length), rate},
                                                         {1, rate}}));
            } else {
                m.rate(rate);
            }
            return;
        }
        std::vector<CurveKey> keys;
        constexpr int kSteps = 40;
        for (int k = 0; k <= kSteps; ++k) {
            const double u = static_cast<double>(k) / kSteps;
            const double at = start + u * length;
            double v = lineLevelAt(line, at);
            if (drawTime > 0.0) {
                v *= std::clamp((at - start) / drawTime, 0.1, 1.0);
            }
            keys.push_back(CurveKey{u, v * rate});
        }
        if (plan_.steady && keys.size() > 1) {
            keys.back().v = keys.front().v;  // a loop ends where it begins
        }
        std::vector<CurveKey> curve = simplifyCurve(keys, 10, 0.06 * rate);
        bool flat = true;
        for (const CurveKey& k : curve) {
            flat = flat && std::fabs(k.v - rate) < 0.03 * rate;
        }
        if (flat) {
            m.rate(rate);
        } else {
            m.put("emission", "rate", Scalar::curve(std::move(curve)));
        }
    }

    // Where and when a line is: its path (turned so that it starts where the
    // clip starts drawing it), and the window of the effect it is in.
    struct LinePlan {
        PointList path;
        bool closed = false;
        double start = 0, length = 0;
        double drawTime = 0;  // 0: there all at once
        int way = 1;          // drawn forward, backward
        bool split = false;   // drawn both ways from its start: two halves
    };

    std::vector<LinePlan> linePlans(const LineFound& line) const {
        LinePlan p;
        p.closed = line.closed;
        std::vector<Point2> points;
        for (const Point2& q : line.points) {
            points.push_back(world(q));
        }
        p.start = 0.0;
        p.length = plan_.steady ? plan_.duration : plan_.life;
        const bool drawn = plan_.measured && line.drawn && line.drawTime > 0.0;
        if (!drawn) {
            p.path.points = std::move(points);
            return {p};
        }
        p.start = std::clamp(line.drawFrom - plan_.start, 0.0, std::max(0.0, p.length - 0.05));
        p.length = std::max(0.05, p.length - p.start);
        p.drawTime = std::min(line.drawTime, p.length);
        const int n = static_cast<int>(points.size());
        const int from = std::clamp(static_cast<int>(std::lround(line.drawStart * n)), 0, std::max(0, n - 1));
        if (line.closed) {
            std::rotate(points.begin(), points.begin() + from, points.end());
            points.push_back(points.front());  // drawn all the way round, back to where it began
        }
        if (line.drawWay != 0 || !line.closed) {
            p.way = line.closed ? (line.drawWay < 0 ? -1 : 1) : (line.drawStart > 0.5f ? -1 : 1);
            p.closed = false;
            p.path.points = std::move(points);
            return {p};
        }
        // Both ways from the start: the first half forward, the second backward.
        LinePlan a = p, b = p;
        a.closed = b.closed = false;
        a.split = b.split = true;
        const std::size_t half = points.size() / 2;
        a.path.points.assign(points.begin(), points.begin() + static_cast<std::ptrdiff_t>(half) + 1);
        b.path.points.assign(points.rbegin(), points.rbegin() + static_cast<std::ptrdiff_t>(points.size() - half));
        return {a, b};
    }

    void placeLine(Make& m, const LinePlan& p) {
        m.path(p.path, p.closed);
        m.from(p.start, p.length);
        if (p.drawTime > 0.0) {
            m.drawOn(static_cast<double>(p.way) / p.drawTime);
        }
    }

    void lines() {
        const bool additive = s_.additive;
        int closedCount = 0, openCount = 0;
        for (const LineFound& line : s_.lines) {
            (line.closed ? closedCount : openCount) += 1;
        }
        // The soft light between the lines goes behind them.
        lineFill();
        int closedSeen = 0, openSeen = 0;
        for (const LineFound& line : s_.lines) {
            const bool bolt = line.jagged >= 0.5f;
            std::string name = line.closed ? "Outline" : (bolt ? "Bolt" : "Line");
            const int number = line.closed ? ++closedSeen : ++openSeen;
            if ((line.closed ? closedCount : openCount) > 1) {
                name += " " + std::to_string(number);
            }
            const double lengthW = std::max(0.05, static_cast<double>(line.length) * kU);
            const Swatch colour = additive ? bright(line.colour) : line.colour;
            const std::vector<LinePlan> plans = linePlans(line);
            const bool drawn = plans.front().drawTime > 0.0;
            std::string timing = plan_.measured
                                     ? (drawn ? "It is drawn on as the clip shows, and its brightness follows the clip."
                                              : "Its brightness over time follows the clip.")
                                     : "Its timing is assumed; a still picture does not show it.";
            for (std::size_t part = 0; part < plans.size(); ++part) {
                const LinePlan& p = plans[part];
                const std::string suffix = plans.size() > 1 ? (part == 0 ? " (one way)" : " (other way)") : "";
                const double share = plans.size() > 1 ? 0.5 : 1.0;
                // The core: soft dots so close together they read as one line.
                {
                    const double size = sizeFor(SpriteShape::Soft, 0.5 * line.thickness, &ShapeMeasure::halfLevel);
                    // A quarter of a dot apart: close enough that the line
                    // reads as one stroke, not a row of beads.
                    const double spacing = 0.25 * size / std::clamp(scale_, 0.35, 1.0);
                    const double life = 0.12;
                    const double alive = share * lengthW / spacing;
                    // Dots `spacing` apart add up along the line: a soft dot
                    // gives about a third of its middle's light, on average,
                    // across its width.
                    const double overlap = size / spacing;
                    const double alpha = std::clamp(static_cast<double>(line.level) / (0.35 * overlap), 0.05, 1.0);
                    Make m(id_, (name + suffix).c_str());
                    placeLine(m, p);
                    lineRate(m, line, alive / life, p.start, p.length, p.drawTime);
                    m.life(0.8 * life, 1.2 * life).speed(0, 0).gravity(0, 0).size(0.95 * size, 1.05 * size)
                        .colorLinear(linear(colour, alpha)).look("soft");
                    blend(m, additive, 1.4);
                    push(m, "body", "Soft dots close together along the line's own path, read from the reference",
                         "line", Basis::Observed, 1, static_cast<int>(alive),
                         "Its path is the middle of the line as seen, " + std::string(line.closed ? "all the way round. " : "end to end. ") +
                             timing);
                }
                // Its halo.
                if (line.halo > 0.5f * line.thickness && line.haloLevel > 0.05f) {
                    const double size =
                        sizeFor(SpriteShape::Soft, 0.5 * line.thickness + line.halo, &ShapeMeasure::outer);
                    const double spacing = 0.15 * size / std::clamp(scale_, 0.35, 1.0);
                    const double life = 0.3;
                    const double alive = share * lengthW / spacing;
                    const double alpha = std::clamp(1.3 * static_cast<double>(line.haloLevel * line.level) * spacing / size,
                                                    0.02, 1.0);
                    const Swatch glow = additive ? bright(line.haloColour) : line.haloColour;
                    Make m(id_, (name + " glow" + suffix).c_str());
                    placeLine(m, p);
                    lineRate(m, line, alive / life, p.start, p.length, p.drawTime);
                    // Each dot fades in and out, so none pops: twice the level at its middle.
                    m.life(0.8 * life, 1.2 * life).speed(0, 0).gravity(0, 0).size(0.95 * size, 1.05 * size)
                        .colorLinear(linear(glow, std::min(1.0, 2.0 * alpha))).fade({{0, 0}, {0.5, 1}, {1, 0}}).look("soft");
                    blend(m, additive, 1.0);
                    push(m, "glow", "Large faint soft dots along the line: its halo", "line", Basis::Observed, 3,
                         static_cast<int>(alive), "As wide and as bright as the halo seen round the line.");
                }
                // Crackle: the line is lightning.
                if (line.jagged >= 0.35f) {
                    // Thin branching strands (the built-in bolt), each a stretch of
                    // the line long, overlapping two or three deep so that they braid.
                    const double size = std::clamp(2.5 * static_cast<double>(line.band) * kU, 0.25, 1.5);
                    const double life = 0.08;
                    const double alive = share * 2.5 * lengthW / size * std::clamp(scale_, 0.4, 1.0);
                    const bool flickers = !plan_.measured || line.flicker >= 0.3f;
                    Make m(id_, (name + " crackle" + suffix).c_str());
                    placeLine(m, p);
                    m.band(std::max(0.0, (static_cast<double>(line.band) - line.thickness) * kU));
                    lineRate(m, line, alive / life, p.start, p.length, p.drawTime);
                    m.life(0.6 * life, 1.2 * life).speed(0, 0).gravity(0, 0).size(0.7 * size, 1.1 * size).turn(-8, 8)
                        .colorLinear(linear(towardWhite(colour, 0.25f), 1.0)).alongPath().look("bolt");
                    if (flickers) {
                        m.hold(15);
                    }
                    blend(m, additive, 2.0);
                    push(m, "rays",
                         "Thin branching bolt strands laid along the line" + std::string(flickers ? ", redrawn 15 times a second" : ""),
                         "line", Basis::Observed, 2, static_cast<int>(alive),
                         "The line is crackling: thin strands braid along it, " +
                             std::to_string(static_cast<int>(std::lround(line.band / std::max(1e-4f, line.thickness)))) +
                             " times as wide as one strand. The strands are the built-in bolt, not the reference's own.");
                    built_.back().note.look = Basis::Inferred;
                }
                // Sparkles on it. (Round a crackling line, the small bright
                // bits are pieces of its crackle, which that layer draws.)
                if (line.sparkles >= 3 && part == 0 && !bolt) {
                    const int count = scaled(line.sparkles, 3);
                    const double life = 0.45;
                    const double size = std::clamp(2.0 * static_cast<double>(line.sparkleSize) * kU, 0.04, 0.4);
                    const char* look = bolt ? "soft" : shapeName(shapeFrom(s_.sparks.shape, SpriteShape::Twinkle));
                    const Swatch c = additive ? bright(line.sparkleColour) : line.sparkleColour;
                    Make m(id_, (name + " sparkles").c_str());
                    m.path(PointList{[&] {
                               std::vector<Point2> all;
                               for (const Point2& q : line.points) {
                                   all.push_back(world(q));
                               }
                               return all;
                           }()},
                           line.closed);
                    m.band(2.0 * (static_cast<double>(line.sparkleReach) + 0.5 * line.thickness) * kU);
                    lineRate(m, line, count / life, 0.0, plan_.steady ? plan_.duration : plan_.life);
                    m.life(0.6 * life, 1.4 * life).speed(0, 0.1).gravity(0, 0).size(0.7 * size, 1.3 * size).turn(0, 45)
                        .colorLinear(linear(c)).sizeOver({{0, 0}, {0.3, 1, true}, {1, 0}}).look(look);
                    blend(m, additive, 1.8);
                    push(m, "sparks", "Small sparkles on and just off the line", "line", Basis::Observed, 5, count,
                         "How many, how big and how far off the line are as seen; where each one is, and that they "
                         "twinkle, is not followed one by one.");
                    built_.back().note.motion = Basis::Inferred;
                }
                // Light running round it.
                if (line.closed && line.run != 0.0f && !drawn) {
                    const double size = std::clamp(2.5 * line.thickness * kU, 0.08, 1.0);
                    Make m(id_, (name + " runner").c_str());
                    placeLine(m, p);
                    m.travel(line.run, 0.06, 1).rate(160).life(0.12, 0.18).speed(0, 0).gravity(0, 0)
                        .size(0.8 * size, size).colorLinear(linear(towardWhite(colour, 0.5f), 0.35)).look("soft");
                    blend(m, additive, 1.8);
                    push(m, "core", "A bright patch running round the line", "line", Basis::Observed, 4, 30,
                         "It runs round at the speed the clip shows.");
                }
            }
        }
    }

    // The soft light that fills the space between or inside the lines.
    void lineFill() {
        const LineFill& f = s_.lineFill;
        if (!f.present || f.level < 0.02f) {
            return;
        }
        const double aspect = static_cast<double>(std::max(1, s_.width)) / std::max(1, s_.height);
        const double width = (f.right - f.left) * aspect * kU, height = (f.bottom - f.top) * kU;
        const Point2 middle = world(Point2{0.5 * (f.left + f.right), 0.5 * (f.top + f.bottom)});
        const double along = f.upright ? height : width, across = f.upright ? width : height;
        const Swatch colour = s_.additive ? bright(f.colour) : f.colour;
        const float first = f.profile.empty() ? 1.0f : f.profile.front();
        const float last = f.profile.empty() ? 1.0f : f.profile.back();
        const bool graded = first >= 1.8f * std::max(0.02f, last);
        // Dots as wide as the fill, close together down its length, so that
        // it reads as one smooth wash rather than a cloud of blobs.
        const double size = std::clamp(0.75 * across, 0.1, 5.0);
        Make m(id_, "Fill");
        if (graded) {
            // Brightest at one end, fading toward the other: light rising
            // from that end (falling, or running across, as it lies).
            const double life = 1.0;
            double heading = 0;
            Point2 from = middle;
            if (f.upright) {
                heading = f.brightFirst ? 270.0 : 90.0;
                from.y += (f.brightFirst ? 0.5 : -0.5) * height;
            } else {
                heading = f.brightFirst ? 0.0 : 180.0;
                from.x += (f.brightFirst ? -0.5 : 0.5) * width;
            }
            std::vector<CurveKey> fade;
            for (std::size_t k = 0; k < f.profile.size(); ++k) {
                fade.push_back(CurveKey{static_cast<double>(k) / std::max<std::size_t>(1, f.profile.size() - 1),
                                        static_cast<double>(f.profile[k])});
            }
            // Soft streaks rising from the bright edge, evenly spaced across
            // it (a path along that edge spreads them evenly), each a quarter
            // of the way up long, so the light washes smoothly up the fill.
            const double streakShare = 0.25;
            const double bar = 0.5 * across;
            const double side = std::max(1.0, across / (0.25 * bar));
            const double alive = side / streakShare;
            // A point of the fill is under about two bars side by side and
            // four end to end; each soft bar gives about half its colour.
            const double alpha = std::clamp(static_cast<double>(f.level) / (2.0 * 4.0 * 0.5) * 1.4, 0.01, 1.0);
            if (f.upright) {
                m.line(from.x - 0.5 * across + 0.3 * bar, from.y, from.x + 0.5 * across - 0.3 * bar, from.y);
            } else {
                m.line(from.x, from.y - 0.5 * across + 0.3 * bar, from.x, from.y + 0.5 * across - 0.3 * bar);
            }
            m.streak(streakShare * life);
            m.rate(std::clamp(alive / life * scale_, 10.0, 600.0)).life(life, life).speed(along / life, along / life)
                .aim(heading, 0).size(0.95 * bar, 1.05 * bar).gravity(0, 0).drag(0.0)
                .colorLinear(linear(colour, alpha)).fade(simplifyCurve(fade, 6, 0.05)).look("soft");
        } else {
            const double life = 0.6;
            const double spacing = 0.12 * size;
            const double alive = std::max(1.0, along / spacing);
            const double alpha = std::clamp(1.6 * static_cast<double>(f.level) * spacing / (0.5 * size), 0.01, 1.0);
            if (f.upright) {
                m.at(middle.x, middle.y).rect(0.15 * across, std::max(0.05, along - size));
            } else {
                m.at(middle.x, middle.y).rect(std::max(0.05, along - size), 0.15 * across);
            }
            m.rate(std::clamp(alive / life * scale_, 10.0, 400.0)).life(life, life).speed(0, 0).gravity(0, 0)
                .size(0.95 * size, 1.05 * size).colorLinear(linear(colour, std::min(1.0, 2.0 * alpha)))
                .fade({{0, 0}, {0.5, 1}, {1, 0}}).look("soft");
        }
        blend(m, s_.additive, 1.0);
        push(m, "glow", graded ? "Soft light rising from its bright end, fading as it goes" : "Soft light filling the space",
             "lineFill", Basis::Observed, 2, 60,
             graded ? "How bright it is from one end to the other is as seen. That it moves is how the gradient is made, "
                      "not something measured."
                    : "Its size, colour and brightness are as seen.");
        built_.back().note.motion = Basis::Inferred;
    }

    // Extras the reference does not have. Each is marked as added.
    void polish() {
        const Swatch accent = s_.palette.size() > 1 ? s_.palette[1] : (s_.palette.empty() ? Swatch{1, 1, 1, 0} : s_.palette[0]);
        const double r = std::max(0.05f, s_.extent) * kU;
        {
            Make m(id_, "Glints");
            const int count = 8;
            if (plan_.steady) {
                m.rate(6.0);
            } else {
                m.burst(count, std::min(plan_.life * 0.5, plan_.peak * 0.6));
            }
            m.circle(0.7 * r).life(0.3, 0.6).speed(0.1, 0.6).aim(90, 180).size(0.08 * r, 0.2 * r)
                .colorLinear(linear(bright(towardWhite(accent, 0.5f)))).gravity(0, 0)
                .sizeOver({{0, 0.1}, {0.35, 1.0}, {1, 0}}).solid().look("glint");
            if (s_.additive) {
                m.additive();
            }
            push(m, "sparks", "Small four-point glints that twinkle", "", Basis::Inferred, 9, count,
                 "Added for polish. Not in the reference.", true);
        }
        if (!plan_.steady && s_.additive) {
            Make m(id_, "Afterglow");
            m.from(std::min(plan_.peak, plan_.life * 0.5), plan_.duration);
            m.burst(1).life(plan_.life, plan_.life).speed(0, 0).gravity(0, 0).size(1.6 * r, 1.6 * r)
                .colorLinear(linear(bright(s_.palette.empty() ? Swatch{1, 1, 1, 0} : s_.palette[0]), 0.25))
                .sizeOver({{0, 0.8}, {1, 1.3}}).fade({{0, 0}, {0.2, 1}, {1, 0}}).look("soft").additive();
            push(m, "glow", "One faint soft sprite that lingers", "", Basis::Inferred, 10, 1,
                 "Added for polish. Not in the reference.", true);
        }
    }

    void fallback() {
        Make m(id_, "Glow");
        oneShot(m, false);
        const Swatch colour = s_.palette.empty() ? Swatch{1, 1, 1, 0} : s_.palette.front();
        const double one = 2.0 * std::max(0.02f, s_.extent) * kU * 1.2;
        m.size(one, one).colorLinear(linear(s_.additive ? bright(colour) : colour)).sizeOver(plan_.grow).fade(plan_.fade)
            .look("soft");
        blend(m, s_.additive, 1.0);
        push(m, "glow", "One large soft sprite", "place", Basis::Observed, 0, 1,
             "Nothing more specific could be told apart, so only the overall size and colour are matched.");
    }

    Reconstruction finish(int budget) {
        Reconstruction out;
        out.notes = skipped_;
        // Too many layers for the mode: drop the least important.
        std::size_t most = 7;
        switch (o_.mode) {
            case MatchMode::Motion: most = 4; break;
            case MatchMode::Layered: most = 12; break;
            case MatchMode::ShapeColour: most = 9; break;
            case MatchMode::Balanced: most = 7; break;
        }
        if (s_.linesLead && o_.mode != MatchMode::Motion) {
            // Each line is a few layers of its own (core, glow, crackle, sparkles).
            most = std::max<std::size_t>(most, std::min<std::size_t>(14, 4 + 3 * s_.lines.size()));
        }
        if (o_.variation == Variation::Performance) {
            most = std::min<std::size_t>(most, 4 + s_.lines.size());
        }
        std::size_t kept = 0;
        for (const Built& b : built_) {
            kept += b.polish ? 0u : 1u;
        }
        while (kept > most) {
            std::size_t worst = built_.size();
            for (std::size_t i = 0; i < built_.size(); ++i) {
                if (!built_[i].polish && (worst == built_.size() || built_[i].rank >= built_[worst].rank)) {
                    worst = i;
                }
            }
            out.notes.push_back("\"" + built_[worst].layer.name + "\" was found but left out to keep the effect to " +
                                std::to_string(most) + " layers. Layered mode keeps everything.");
            built_.erase(built_.begin() + static_cast<std::ptrdiff_t>(worst));
            --kept;
        }

        Effect effect = makeEmptyEffect(id_, o_.name, false);
        // Pictures whose layer was dropped are dropped with it.
        for (PictureUse& use : pictures_) {
            bool used = false;
            for (const Built& b : built_) {
                for (const Module& mod : b.layer.modules) {
                    if (const Value* v = mod.type == "sprite" ? mod.find("texture") : nullptr) {
                        const auto* ref = std::get_if<AssetRef>(v);
                        used = used || (ref && ref->id == use.asset);
                    }
                }
            }
            if (used) {
                Asset asset;
                asset.id = use.asset;
                asset.kind = "texture";
                asset.path = use.path;
                effect.assets.push_back(asset);
                out.pictures.push_back(std::move(use));
            }
        }
        effect.duration = plan_.duration;
        effect.loop = "loop";
        effect.seed = 7;
        if (a_.time.available) {
            effect.frameRate = std::clamp(std::round(a_.time.framesPerSecond), 12.0, 60.0);
        }
        // Back to front: smoke, glow, shape, ring, flash, core, then loose pieces.
        const auto depth = [](const std::string& role) {
            if (role == "smoke") return 0;
            if (role == "glow") return 1;
            if (role == "trail") return 2;
            if (role == "body") return 3;
            if (role == "ring") return 4;
            if (role == "rays") return 5;
            if (role == "core") return 6;
            if (role == "streaks") return 7;
            if (role == "bits") return 8;
            return 9;
        };
        std::stable_sort(built_.begin(), built_.end(), [&](const Built& x, const Built& y) {
            return depth(x.note.role) < depth(y.note.role);
        });
        int particles = 0;
        for (Built& b : built_) {
            particles += b.particles;
            out.layers.push_back(b.note);
            effect.layers.push_back(std::move(b.layer));
        }
        tools::keepWholePass(effect);
        out.effect = std::move(effect);
        out.particlesAtBusiest = particles;
        out.budget = budget;
        out.peakTime = plan_.peak;
        out.referenceStart = plan_.start;

        const float w = static_cast<float>(std::max(1, s_.width)), h = static_cast<float>(std::max(1, s_.height));
        out.view = View{};
        out.view.width = w;
        out.view.height = h;
        out.view.centerX = (0.5f - s_.centreX) * (w / h) * kReferenceUnits;
        out.view.centerY = (s_.centreY - 0.5f) * kReferenceUnits;
        out.view.unitsHigh = kReferenceUnits;

        // What this version cannot build, said plainly.
        if (plan_.driftSpeed > 0.05) {
            out.notes.push_back("The effect travels in the clip. The one-shot layers follow that path; particle layers "
                                "start from where it began, because an emitter cannot move along a path yet.");
        }
        if (a_.time.available && a_.time.turning != 0 && plan_.spin == 0) {
            out.notes.push_back("Some turning was seen but not clearly enough to copy.");
        }
        out.notes.push_back("Not built in this version: heat haze and distortion, beams and cones as real meshes, and "
                            "true depth. Swirling on moving air is not measured from the reference: if the original "
                            "has it, More swirl adds it. Sparks are placed at random, so they match the reference in "
                            "number, size, colour and spread, not one by one.");
        return out;
    }

    const ReferenceAnalysis& a_;
    const StillAnalysis& s_;
    const ReconstructOptions& o_;
    const IdSource& id_;
    const std::vector<Cutout>* cutouts_ = nullptr;
    std::vector<PictureUse> pictures_;
    std::vector<std::string> skipped_;
    Plan plan_;
    double scale_ = 1.0;
    std::vector<Built> built_;
};

}  // namespace

Reconstruction reconstruct(const ReferenceAnalysis& analysis, const ReconstructOptions& options,
                           const IdSource& newId, const std::vector<Cutout>* cutouts) {
    Builder builder(analysis, options, newId, cutouts);
    return builder.run();
}

void addFlipbook(Reconstruction& built, const Flipbook& book, const StillAnalysis& still, const IdSource& newId,
                 bool alone) {
    if (book.frames <= 0 || book.sheet.width <= 0) {
        return;
    }
    PictureUse use;
    use.asset = newId();
    use.png = encodePng(book.sheet);
    use.path = imageAssetPath("reference-flipbook", use.png);
    use.image = std::make_shared<Image>(book.sheet);

    const double aspect = static_cast<double>(std::max(1, still.width)) / std::max(1, still.height);
    const double x = (book.centreX - still.centreX) * aspect * kU, y = (still.centreY - book.centreY) * kU;
    const double size = 2.0 * book.halfSize * kU;
    const double shown = book.framesPerSecond > 0 ? book.frames / book.framesPerSecond : built.effect.duration;
    Make m(newId, "Flipbook");
    m.burst(1).life(shown, shown).at(x, y).speed(0, 0).gravity(0, 0).size(size, size).turn(0, 0).color(0xffffff)
        .solid();
    m.put("sprite", "texture", AssetRef{use.asset});
    m.put("sprite", "shape", std::string("soft"));
    m.put("sprite", "columns", Value(static_cast<std::int64_t>(book.columns)));
    m.put("sprite", "rows", Value(static_cast<std::int64_t>(book.rows)));
    m.put("sprite", "frames", Value(static_cast<std::int64_t>(book.frames)));
    m.put("sprite", "animate", std::string("life"));
    if (still.additive) {
        m.additive();
    }
    m.role("flipbook");
    Layer layer = m.done(built.effect.duration);
    // Its own window of the clip may start before or after the effect's.
    layer.start = std::max(0.0, book.start - built.referenceStart);
    layer.duration = std::max(0.05, built.effect.duration - layer.start);

    if (alone) {
        for (Layer& other : built.effect.layers) {
            other.enabled = false;
        }
    }
    Asset asset;
    asset.id = use.asset;
    asset.kind = "texture";
    asset.path = use.path;
    built.effect.assets.push_back(asset);
    LayerNote note;
    note.layer = layer.id;
    note.name = layer.name;
    note.role = "flipbook";
    note.technique = "The clip's own frames, cut from their background, played as a sprite sheet (" +
                     std::to_string(book.frames) + " pictures)";
    note.from = "flipbook";
    note.look = Basis::Observed;
    note.motion = Basis::Observed;
    note.note = std::string("These are the reference's own pixels: it matches as closely as anything can, and is "
                            "edited as a whole (size, place, timing, colour), not part by part.") +
                (alone ? " The layers built from the analysis are kept but switched off: switch any on to replace or "
                         "add to part of it."
                       : "");
    built.layers.push_back(note);
    built.effect.layers.insert(built.effect.layers.begin(), std::move(layer));
    built.pictures.push_back(std::move(use));
    built.notes.push_back("A flipbook draws pixels taken from your reference. Only keep it if the reference is yours "
                          "to use.");
}

ImageSet picturesOf(const Reconstruction& built) {
    ImageSet set;
    for (const PictureUse& use : built.pictures) {
        set.put(use.asset, use.image, use.path);
    }
    return set;
}

}  // namespace vfx::editor
