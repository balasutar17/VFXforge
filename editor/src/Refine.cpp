#include "vfx/editor/Refine.h"

#include <algorithm>
#include <cmath>

#include "vfx/editor/CurveTools.h"
#include "vfx/editor/Reconstruct.h"
#include "vfx/editor/Session.h"

#include "EffectTools.h"
#include "LayerMake.h"

namespace vfx::editor {

using namespace tools;

const std::vector<RefineInfo>& refinements() {
    static const std::vector<RefineInfo> all = {
        {Refine::Bigger, "bigger", "Bigger", "Everything 15% larger: sizes, speeds and spread together."},
        {Refine::Smaller, "smaller", "Smaller", "Everything 15% smaller."},
        {Refine::Wider, "wider", "Wider", "Particles fly 20% further. Their own size stays."},
        {Refine::Tighter, "tighter", "Tighter", "Particles stay 20% closer in."},
        {Refine::MoreSparks, "more-sparks", "More sparks", "Half as many again in every layer of small particles."},
        {Refine::FewerSparks, "fewer-sparks", "Fewer sparks", "A third fewer in every layer of small particles."},
        {Refine::MoreGlow, "more-glow", "More glow", "Glow layers 30% brighter and a little larger."},
        {Refine::LessGlow, "less-glow", "Less glow", "Glow layers dimmer and a little smaller."},
        {Refine::Faster, "faster", "Faster", "The whole effect plays 25% faster."},
        {Refine::Slower, "slower", "Slower", "The whole effect plays 20% slower."},
        {Refine::Sharper, "sharper", "Sharper", "Less halo, and soft small pieces become crisp ones."},
        {Refine::Softer, "softer", "Softer", "More halo, and crisp small pieces become soft ones."},
        {Refine::Stylize, "stylize", "More stylized", "Snappier: shapes pop in past full size and settle. Soft dots become glints."},
        {Refine::AddSecondary, "add-secondary", "Add finer particles", "Adds a layer of small, slower particles in a lighter colour."},
        {Refine::ForMobile, "for-mobile", "Lighter for mobile", "Halves the particles in busy layers and removes trails."},
        {Refine::MoreSwirl, "more-swirl", "More swirl", "Small particles and smoke are carried about by moving air, winding as they go. Again for more."},
        {Refine::Calmer, "calmer", "Calmer", "Less swirling; none when it is already slight."},
        {Refine::DissolveAway, "dissolve-away", "Dissolve away", "Smoke and big shapes are eaten away in ragged patches as they go, instead of only fading."},
        {Refine::OnTwos, "on-twos", "On twos", "Every layer is shown as hand-drawn animation is: twelve drawings a second, each held. Again for \"on threes\" (eight)."},
        {Refine::SmoothTiming, "smooth-timing", "Smooth", "Every layer moves smoothly again: no held drawings."},
        {Refine::SquashStretch, "squash-stretch", "Squash and stretch", "Big shapes land flat, spring up and settle; round flying pieces stretch along their flight."},
        {Refine::Anticipate, "anticipate", "Anticipation", "Flying pieces back away before they go, and big shapes draw in a little before they grow."},
        {Refine::Overshoot, "overshoot", "Overshoot", "Flying pieces go past where they will come to rest, and settle back."},
        {Refine::Curl, "curl", "Curl", "Flying pieces curve round the centre instead of going straight out. Again for more."},
        {Refine::SmoothLoop, "smooth-loop", "Seamless loop", "Rings and circles that last the whole loop turn a whole number of times in it, so nothing jumps when the loop starts again."},
        {Refine::MatchColours, "match-colours", "Reference colours again", "Sets each layer back to the colour measured in the reference.", true, false},
        {Refine::MatchDuration, "match-duration", "Reference length again", "Stretches or squeezes the effect to last as long as the clip.", true, true},
    };
    return all;
}

std::vector<std::string> loopJumps(const Effect& effect) { return tools::loopJumps(effect); }

const RefineInfo* findRefinement(std::string_view key) {
    for (const RefineInfo& r : refinements()) {
        if (key == r.key) {
            return &r;
        }
    }
    return nullptr;
}

namespace {

bool small(const std::string& role) { return role == "sparks" || role == "streaks" || role == "bits"; }
bool glowing(const std::string& role) { return role == "glow" || role == "core"; }

Color toLinear(const Swatch& s, bool additive) {
    float r = s.r, g = s.g, b = s.b;
    if (additive) {
        const float top = std::max({r, g, b});
        if (top > 0.04f) {
            r /= top;
            g /= top;
            b /= top;
        }
    }
    return Color{srgbToLinear(std::clamp<double>(r, 0, 1)), srgbToLinear(std::clamp<double>(g, 0, 1)),
                 srgbToLinear(std::clamp<double>(b, 0, 1)), 1.0};
}

std::string count(int n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

}  // namespace

Result<Effect> refine(const Effect& effect, Refine what, const ReferenceAnalysis* analysis, const IdSource& newId,
                      std::string* said) {
    Effect out = effect;
    int touched = 0, locked = 0;
    std::string note;
    const auto each = [&](const auto& wanted, const auto& change) {
        for (Layer& layer : out.layers) {
            if (!wanted(layer)) {
                continue;
            }
            if (layer.locked) {
                ++locked;
                continue;
            }
            change(layer);
            ++touched;
        }
    };
    const auto any = [](const Layer&) { return true; };
    const auto timeAll = [&](double factor) {
        // Locked layers keep their own timing; the rest, and the effect's
        // length, change together.
        each(any, [&](Layer& layer) { scaleTime(layer, factor); });
        out.duration = std::clamp(out.duration / factor, 0.05, 3600.0);
        // A change of pace must not leave a whole-pass sprite a frame short.
        tools::keepWholePass(out);
    };

    switch (what) {
        case Refine::Bigger:
            each(any, [](Layer& l) { scaleEverything(l, 1.15); });
            note = "Made " + count(touched, "layer", "layers") + " 15% larger.";
            break;
        case Refine::Smaller:
            each(any, [](Layer& l) { scaleEverything(l, 1.0 / 1.15); });
            note = "Made " + count(touched, "layer", "layers") + " 15% smaller.";
            break;
        case Refine::Wider:
            each([](const Layer& l) { return isParticles(l); }, [](Layer& l) { scaleReach(l, 1.2); });
            note = "Particles in " + count(touched, "layer", "layers") + " now fly 20% further.";
            break;
        case Refine::Tighter:
            each([](const Layer& l) { return isParticles(l); }, [](Layer& l) { scaleReach(l, 1.0 / 1.2); });
            note = "Particles in " + count(touched, "layer", "layers") + " now stay 20% closer in.";
            break;
        case Refine::MoreSparks:
            each([](const Layer& l) { return small(roleOf(l)); }, [](Layer& l) { scaleCount(l, 1.5); });
            note = "Half as many particles again in " + count(touched, "layer", "layers") + ".";
            break;
        case Refine::FewerSparks:
            each([](const Layer& l) { return small(roleOf(l)); }, [](Layer& l) { scaleCount(l, 2.0 / 3.0); });
            note = "A third fewer particles in " + count(touched, "layer", "layers") + ".";
            break;
        case Refine::MoreGlow:
            each([](const Layer& l) { return glowing(roleOf(l)); }, [](Layer& l) {
                scaleBrightness(l, 1.3);
                scaleSize(l, 1.06);
            });
            note = "Brightened " + count(touched, "glow layer", "glow layers") + ".";
            break;
        case Refine::LessGlow:
            each([](const Layer& l) { return glowing(roleOf(l)); }, [](Layer& l) {
                scaleBrightness(l, 1.0 / 1.3);
                scaleSize(l, 1.0 / 1.06);
            });
            note = "Dimmed " + count(touched, "glow layer", "glow layers") + ".";
            break;
        case Refine::Faster:
            timeAll(1.25);
            note = "The effect now plays 25% faster.";
            break;
        case Refine::Slower:
            timeAll(0.8);
            note = "The effect now plays 20% slower.";
            break;
        case Refine::Sharper:
            each(any, [](Layer& l) {
                const std::string role = roleOf(l);
                if (role == "glow") {
                    scaleBrightness(l, 0.8);
                    scaleSize(l, 0.9);
                } else if (small(role) && shapeOf(l) == "soft") {
                    setShape(l, "disc");
                } else if (role == "smoke" && shapeOf(l) == "smoke") {
                    setShape(l, "puff");
                }
            });
            note = "Less halo; soft small pieces are now crisp.";
            break;
        case Refine::Softer:
            each(any, [](Layer& l) {
                const std::string role = roleOf(l);
                if (role == "glow") {
                    scaleBrightness(l, 1.2);
                    scaleSize(l, 1.1);
                } else if (small(role) && shapeOf(l) == "disc") {
                    setShape(l, "soft");
                }
            });
            note = "More halo; crisp small pieces are now soft.";
            break;
        case Refine::Stylize:
            each(any, [](Layer& l) {
                const std::string role = roleOf(l);
                if (!isParticles(l)) {
                    // Pop in past full size, then settle where it was going.
                    if (Scalar* size = property<Scalar>(l, "overLife", "size")) {
                        double last = 1.0;
                        if (size->kind == Scalar::Kind::Curve && !size->keys.empty()) {
                            last = size->keys.back().v;
                        } else if (size->kind == Scalar::Kind::Constant) {
                            last = size->a;
                        }
                        *size = Scalar::curve({{0.0, 0.15}, {0.12, 1.18}, {0.28, 0.98}, {1.0, last}});
                    }
                } else if (small(role) && shapeOf(l) == "soft") {
                    setShape(l, "glint");
                    scaleSize(l, 1.6);
                } else if (shapeOf(l) == "smoke") {
                    setShape(l, "puff");
                }
            });
            note = "Shapes now pop in and settle; soft dots became glints.";
            break;
        case Refine::AddSecondary: {
            // Based on the first layer of small particles, when there is one.
            const Layer* from = nullptr;
            for (const Layer& l : out.layers) {
                if (small(roleOf(l)) && isParticles(l)) {
                    from = &l;
                    break;
                }
            }
            make::Make m(newId, "Finer particles");
            m.role("sparks");
            Color colour{1, 1, 1, 1};
            double sizeLow = 0.05, sizeHigh = 0.1, speedLow = 1.0, speedHigh = 3.0, lifeLow = 0.4, lifeHigh = 0.8;
            bool additive = true;
            std::int64_t number = 16;
            double drag = 3.0;
            if (from) {
                if (const Color* c = property<Color>(*from, "initial", "color")) {
                    colour = Color{c->r + (1.0 - c->r) * 0.5, c->g + (1.0 - c->g) * 0.5, c->b + (1.0 - c->b) * 0.5, c->a};
                }
                const auto range = [](const Scalar* s, double& low, double& high, double factor) {
                    if (!s) {
                        return;
                    }
                    const double a = s->kind == Scalar::Kind::Random ? s->a : s->a;
                    const double b = s->kind == Scalar::Kind::Random ? s->b : s->a;
                    low = std::min(a, b) * factor;
                    high = std::max(a, b) * factor;
                };
                range(property<Scalar>(*from, "initial", "size"), sizeLow, sizeHigh, 0.5);
                range(property<Scalar>(*from, "initial", "speed"), speedLow, speedHigh, 0.65);
                range(property<Scalar>(*from, "initial", "lifetime"), lifeLow, lifeHigh, 1.2);
                additive = isAdditive(*from);
                number = std::max<std::int64_t>(6, std::llround(busiest(*from) * 1.5));
                if (const double* d = property<double>(*from, "motion", "drag")) {
                    drag = *d;
                }
            } else {
                for (const Layer& l : out.layers) {
                    if (const Color* c = property<Color>(l, "initial", "color")) {
                        colour = *c;
                        additive = isAdditive(l);
                        break;
                    }
                }
            }
            const bool steady = from && property<Scalar>(*from, "emission", "rate") &&
                                property<Scalar>(*from, "emission", "rate")->a > 0.0;
            if (steady) {
                m.rate(static_cast<double>(number) / std::max(0.1, 0.5 * (lifeLow + lifeHigh)));
            } else {
                m.burst(number, 0.03);
            }
            m.life(lifeLow, std::max(lifeLow + 0.01, lifeHigh)).speed(speedLow, std::max(speedLow + 0.01, speedHigh))
                .aim(90, 180).size(std::max(0.02, sizeLow), std::max(0.03, sizeHigh)).colorLinear(colour).drag(drag)
                .gravity(0, 0).sizeOver({{0, 1}, {0.6, 0.8}, {1, 0}}).fade({{0, 1}, {0.7, 1}, {1, 0}}).look("soft");
            if (additive) {
                m.additive();
            }
            out.layers.push_back(m.done(out.duration));
            touched = 1;
            note = "Added a layer of finer particles.";
            break;
        }
        case Refine::ForMobile: {
            int trails = 0;
            each(any, [&](Layer& l) {
                if (busiest(l) > 8.0) {
                    scaleCount(l, 0.5, 4);
                }
                if (double* trail = property<double>(l, "sprite", "trail"); trail && *trail > 0.0) {
                    *trail = 0.0;
                    ++trails;
                }
            });
            note = "Halved the particles in busy layers" + std::string(trails ? " and removed trails." : ".");
            break;
        }
        case Refine::MoreSwirl: {
            int begun = 0;
            each([](const Layer& l) { return isParticles(l) && (small(roleOf(l)) || roleOf(l) == "smoke"); },
                 [&](Layer& l) {
                     double* turbulence = property<double>(l, "motion", "turbulence");
                     if (!turbulence) {
                         return;
                     }
                     if (*turbulence > 0.0) {
                         *turbulence = std::min(*turbulence * 1.5, 1000.0);
                         return;
                     }
                     // From still air: a push of about a third of the
                     // particles' own speed, in swirls a few particles wide,
                     // so they wind without losing their way.
                     double speed = 0.0;
                     if (const Scalar* s = property<Scalar>(l, "initial", "speed")) {
                         speed = s->kind == Scalar::Kind::Random ? 0.5 * (s->a + s->b) : s->a;
                     }
                     *turbulence = std::clamp(0.35 * std::fabs(speed), 0.3, 6.0);
                     if (double* swirl = property<double>(l, "motion", "turbulenceSize")) {
                         *swirl = 1.2;
                     }
                     if (double* change = property<double>(l, "motion", "turbulenceChange")) {
                         *change = 0.6;
                     }
                     ++begun;
                 });
            note = begun > 0 ? "Moving air now carries the particles in " + count(touched, "layer", "layers") + "."
                             : "Half as much swirling again in " + count(touched, "layer", "layers") + ".";
            break;
        }
        case Refine::Calmer: {
            int stilled = 0;
            each([](const Layer& l) {
                     const double* turbulence = property<double>(l, "motion", "turbulence");
                     return turbulence && *turbulence > 0.0;
                 },
                 [&](Layer& l) {
                     double* turbulence = property<double>(l, "motion", "turbulence");
                     *turbulence *= 2.0 / 3.0;
                     if (*turbulence < 0.15) {
                         *turbulence = 0.0;
                         ++stilled;
                     }
                 });
            note = "A third less swirling in " + count(touched, "layer", "layers") + "." +
                   (stilled > 0 ? " " + count(stilled, "layer is", "layers are") + " now still air." : "");
            break;
        }
        case Refine::DissolveAway: {
            each([](const Layer& l) {
                     const std::string role = roleOf(l);
                     const double* dissolve = property<double>(l, "overLife", "dissolve");
                     return dissolve && *dissolve <= 0.0 && (role == "smoke" || role == "shape");
                 },
                 [](Layer& l) {
                     const bool smoke = roleOf(l) == "smoke";
                     *property<double>(l, "overLife", "dissolve") = smoke ? 0.6 : 0.5;
                     if (double* soft = property<double>(l, "overLife", "dissolveSoft")) {
                         *soft = smoke ? 0.6 : 0.08;
                     }
                 });
            note = count(touched, "layer is", "layers are") + " now eaten away in patches at the end.";
            break;
        }
        case Refine::OnTwos: {
            // Twelve a second; a second press makes it eight.
            bool allTwelve = true, any12 = false;
            for (const Layer& l : out.layers) {
                if (const double* hold = property<double>(l, "sprite", "hold"); hold && !l.locked) {
                    allTwelve = allTwelve && *hold == 12.0;
                    any12 = true;
                }
            }
            const double rate = any12 && allTwelve ? 8.0 : 12.0;
            each([](const Layer& l) { return property<double>(l, "sprite", "hold") != nullptr; },
                 [&](Layer& l) { *property<double>(l, "sprite", "hold") = rate; });
            note = count(touched, "layer is", "layers are") + " now shown " +
                   (rate == 12.0 ? "on twos: twelve drawings a second." : "on threes: eight drawings a second.");
            break;
        }
        case Refine::SmoothTiming:
            each([](const Layer& l) {
                     const double* hold = property<double>(l, "sprite", "hold");
                     return hold && *hold > 0.0;
                 },
                 [](Layer& l) { *property<double>(l, "sprite", "hold") = 0.0; });
            note = touched > 0 ? count(touched, "layer moves", "layers move") + " smoothly again."
                               : std::string();
            if (touched == 0 && locked == 0) {
                note = "Every layer already moves smoothly.";
                touched = -1;  // said its piece
            }
            break;
        case Refine::SquashStretch: {
            const std::vector<CurveKey>& spring = findCurveShape("squash-spring")->keys;
            int shapes = 0, pieces = 0;
            each([](const Layer& l) {
                     if (!property<Scalar>(l, "overLife", "stretch")) {
                         return false;
                     }
                     const std::string role = roleOf(l);
                     const std::string shape = shapeOf(l);
                     if (!isParticles(l)) {
                         // A shape of its own, not a glow, a ring or a flash of rays.
                         return role == "body" || role == "smoke";
                     }
                     // Flying pieces: those that already point the way they
                     // go, and round ones, which can be made to. (A crowd of
                     // particles that stay where they are is not flying.)
                     const Scalar* speed = property<Scalar>(l, "initial", "speed");
                     if (!speed || !(levelOf(*speed) > 0.05)) {
                         return false;
                     }
                     const std::string* align = property<std::string>(l, "sprite", "align");
                     return (align && *align == "movement") || shape == "disc" || shape == "soft";
                 },
                 [&](Layer& l) {
                     Scalar* stretch = property<Scalar>(l, "overLife", "stretch");
                     if (!isParticles(l)) {
                         *stretch = Scalar::curve(spring);
                         ++shapes;
                         return;
                     }
                     if (std::string* align = property<std::string>(l, "sprite", "align"); align && *align != "movement") {
                         *align = "movement";
                         if (double* streak = property<double>(l, "sprite", "stretch")) {
                             *streak = 0.0;
                         }
                     }
                     // Long while it is fast, its own shape as it slows.
                     *stretch = Scalar::curve({{0.0, 0.8}, {0.5, 0.25, true}, {1.0, 0.0, true}});
                     ++pieces;
                 });
            note = count(shapes, "shape now lands flat and springs", "shapes now land flat and spring") + "; " +
                   count(pieces, "layer of flying pieces stretches", "layers of flying pieces stretch") +
                   " along the flight.";
            break;
        }
        case Refine::Anticipate: {
            const std::vector<CurveKey>& windUp = findCurveShape("wind-up-go")->keys;
            each([](const Layer& l) {
                     const std::string role = roleOf(l);
                     return isParticles(l) ? property<Scalar>(l, "motion", "speedOverLife") != nullptr
                                           : (role == "body" && property<Scalar>(l, "overLife", "size") != nullptr);
                 },
                 [&](Layer& l) {
                     if (isParticles(l)) {
                         *property<Scalar>(l, "motion", "speedOverLife") = Scalar::curve(windUp);
                         return;
                     }
                     // Draws in to three quarters of where it starts, then
                     // goes on as it was going to.
                     Scalar* size = property<Scalar>(l, "overLife", "size");
                     std::vector<CurveKey> keys = curveFrom(*size);
                     const double first = keys.front().v;
                     std::vector<CurveKey> next = {{0.0, first, true}, {0.1, first * 0.72, true}};
                     for (const CurveKey& k : keys) {
                         if (k.t > 0.16) {
                             next.push_back(k);
                         }
                     }
                     if (next.size() < 3) {
                         next.push_back(CurveKey{1.0, keys.back().v, keys.back().smooth});
                     }
                     *size = Scalar::curve(std::move(next));
                 });
            note = count(touched, "layer now winds", "layers now wind") + " up before going.";
            break;
        }
        case Refine::Overshoot: {
            const std::vector<CurveKey>& overshoot = findCurveShape("overshoot-settle")->keys;
            each([](const Layer& l) { return isParticles(l) && property<Scalar>(l, "motion", "speedOverLife"); },
                 [&](Layer& l) { *property<Scalar>(l, "motion", "speedOverLife") = Scalar::curve(overshoot); });
            note = "Flying pieces in " + count(touched, "layer", "layers") + " now go past their mark and settle back.";
            break;
        }
        case Refine::Curl: {
            int layerNumber = 0;
            bool begun = false;
            each([](const Layer& l) { return isParticles(l) && property<double>(l, "motion", "orbit"); },
                 [&](Layer& l) {
                     double* orbit = property<double>(l, "motion", "orbit");
                     if (*orbit != 0.0) {
                         *orbit = std::clamp(*orbit * 1.5, -3600.0, 3600.0);
                     } else {
                         // A third of a turn over a typical life, one layer
                         // one way and the next the other.
                         double life = 1.0;
                         if (const Scalar* s = property<Scalar>(l, "initial", "lifetime")) {
                             life = std::max(0.05, s->kind == Scalar::Kind::Random ? 0.5 * (s->a + s->b)
                                                   : s->kind == Scalar::Kind::Curve ? levelOf(*s) : s->a);
                         }
                         *orbit = std::clamp(120.0 / life, 20.0, 720.0) * (layerNumber % 2 == 0 ? 1.0 : -1.0);
                         begun = true;
                     }
                     ++layerNumber;
                 });
            note = begun ? "Flying pieces in " + count(touched, "layer", "layers") + " now curve round the centre."
                         : "Half as much curl again in " + count(touched, "layer", "layers") + ".";
            break;
        }
        case Refine::SmoothLoop: {
            if (out.loop != "loop") {
                note = "The effect plays once, so it has no loop to smooth.";
                break;
            }
            const std::vector<std::string> jumping = tools::loopJumps(out);
            tools::smoothLoop(out);
            touched = static_cast<int>(jumping.size());
            if (jumping.empty()) {
                note = "Nothing jumps when the loop starts again.";
            } else {
                std::string names;
                for (std::size_t i = 0; i < jumping.size(); ++i) {
                    names += (i == 0 ? "" : i + 1 == jumping.size() ? " and " : ", ") + jumping[i];
                }
                note = names + (jumping.size() == 1 ? " now turns" : " now turn") +
                       " a whole number of times each loop, so the loop has no jump.";
            }
            break;
        }
        case Refine::MatchColours: {
            if (!analysis) {
                return makeError("There is no reference to take colours from.");
            }
            const StillAnalysis& s = analysis->still;
            int group = 0;
            each(any, [&](Layer& l) {
                const std::string role = roleOf(l);
                const bool additive = isAdditive(l);
                const Swatch* colour = nullptr;
                // Read as lines: each layer takes its line's own colours
                // (the first line's, when there are several), the fill its own.
                const LineFound* line = s.linesLead && !s.lines.empty() ? &s.lines.front() : nullptr;
                if (line && l.name == "Fill" && s.lineFill.present) {
                    colour = &s.lineFill.colour;
                } else if (line && role == "body") {
                    colour = &line->colour;
                } else if (line && role == "glow") {
                    colour = &line->haloColour;
                } else if (line && role == "rays") {
                    colour = &line->colour;
                } else if (line && role == "sparks" && line->sparkles > 0) {
                    colour = &line->sparkleColour;
                } else if (role == "glow" && s.hasGlow) {
                    colour = &s.glowOuterColour;
                } else if (role == "core" && s.hasCore) {
                    colour = &s.coreColour;
                } else if (role == "core" && s.glowInnerLevel > 0) {
                    colour = &s.glowInnerColour;
                } else if (role == "body" && s.hasBody) {
                    colour = &s.bodyColour;
                } else if (role == "rays" && s.rays >= 2) {
                    colour = s.hasGlow ? &s.glowOuterColour : &s.rayColour;
                } else if (role == "ring" && !s.rings.empty()) {
                    colour = &s.rings.front().colour;
                } else if (role == "sparks" && s.sparks.count > 0) {
                    colour = &s.sparks.colour;
                } else if (role == "streaks" && s.streaks.count > 0) {
                    colour = &s.streaks.colour;
                } else if (role == "bits" && s.bits.count > 0) {
                    colour = &s.bits.colour;
                } else if (role == "smoke" && s.hasSmoke) {
                    colour = &s.smokeColour;
                } else if (role == "trail" && !s.tailColours.empty()) {
                    colour = &s.tailColours.front();
                } else if (!s.palette.empty()) {
                    colour = &s.palette[static_cast<std::size_t>(group++) % s.palette.size()];
                }
                // A layer drawing a picture keeps its white tint: the
                // picture carries the colour.
                const AssetRef* picture = property<AssetRef>(l, "sprite", "texture");
                if (colour && !(picture && picture->id.valid())) {
                    setColour(l, toLinear(*colour, additive), true);
                }
            });
            note = "Set " + count(touched, "layer", "layers") + " back to the reference's colours.";
            break;
        }
        case Refine::MatchDuration: {
            if (!analysis || !analysis->time.available) {
                return makeError("The reference is a still picture, so it has no length to match.",
                                 "Use a GIF or a video of the effect.");
            }
            const TimeAnalysis& t = analysis->time;
            const double want = t.continuous ? (t.loops && t.loopLength > 0.1 ? t.loopLength : t.frames / t.framesPerSecond - t.steadyFrom)
                                             : (t.loops && t.loopLength > 0.1 ? t.loopLength : (t.end - t.start) + 0.3);
            if (!(want > 0.05) || !(out.duration > 0.0)) {
                return makeError("The reference's length could not be measured.");
            }
            const double factor = out.duration / want;
            if (std::fabs(factor - 1.0) < 0.01) {
                note = "The effect already lasts as long as the reference.";
                break;
            }
            timeAll(factor);
            out.duration = want;
            note = "The effect now lasts " + std::to_string(want).substr(0, 4) + " seconds, like the reference.";
            break;
        }
    }
    if (touched < 0) {
        touched = 0;  // nothing changed, and the note already says why
    } else {
        if (touched == 0 && note.empty()) {
            note = "Nothing to change.";
        }
        if (touched == 0 && what != Refine::MatchDuration && what != Refine::Faster && what != Refine::Slower) {
            note = locked > 0 ? "Every layer this would change is locked." : "No layer here is of the kind this changes.";
        }
    }
    if (locked > 0 && touched > 0) {
        note += " " + count(locked, "locked layer was", "locked layers were") + " left alone.";
    }
    if (said) {
        *said = note;
    }
    return out;
}

}  // namespace vfx::editor
