#include "vfx/editor/Budget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "vfx/Program.h"
#include "vfx/Simulation.h"
#include "EffectTools.h"

namespace vfx::editor {

const char* budgetGradeName(BudgetGrade grade) {
    switch (grade) {
        case BudgetGrade::Light: return "Light";
        case BudgetGrade::Fine: return "OK";
        case BudgetGrade::Heavy: return "Heavy";
        case BudgetGrade::TooHeavy: return "Too heavy";
    }
    return "?";
}

namespace {

// Rules of thumb for a low-end phone, the whole effect on screen at once.
BudgetGrade gradeOf(double value, double light, double fine, double heavy) {
    return value <= light ? BudgetGrade::Light
         : value <= fine ? BudgetGrade::Fine
         : value <= heavy ? BudgetGrade::Heavy : BudgetGrade::TooHeavy;
}

std::string format(const char* pattern, double a, double b = 0) {
    char text[200];
    std::snprintf(text, sizeof text, pattern, a, b);
    return text;
}

}  // namespace

BudgetReport measureBudget(const Effect& effect, const BudgetOptions& options) {
    BudgetReport report;
    const auto program = compileEffect(effect);
    Simulation sim(program);
    const double step = program->step;
    const double screenArea = options.screenHeight * options.screenHeight * options.screenAspect;

    // A one-shot effect is looked at until its last particle can have died;
    // a loop through a second pass, which is what plays from then on.
    double longest = 0;
    for (const auto& e : program->emitters) {
        longest = std::max(longest, e->maxLifetime);
    }
    const double from = program->loop ? program->duration : 0.0;
    const double until = program->loop ? 2.0 * program->duration : program->duration + longest;
    const auto stride = std::max<std::int64_t>(1, std::llround(1.0 / (options.samplesPerSecond * step)));
    const auto first = static_cast<std::int64_t>(std::llround(from / step));
    const auto last = static_cast<std::int64_t>(std::llround(until / step));

    struct Running {
        LayerCost cost;
        double fillSum = 0;
        int shown = 0;
    };
    std::map<Id, Running> byLayer;
    for (const auto& e : program->emitters) {
        Running& r = byLayer[e->layer];
        r.cost.layer = e->layer;
        const Layer* layer = findLayer(effect, e->layer);
        r.cost.name = layer ? layer->name : "Layer";
        r.cost.trail = e->trail > 0.0f;
        r.cost.lights = e->lightShare > 0.0f;
        r.cost.collision = e->collide || e->zone != 0;
        r.cost.subEmitter = e->subTrigger != 0;
        report.draws += 1 + (r.cost.trail ? 1 : 0);
        if (r.cost.lights) {
            report.lights += std::max(0, e->lightMax);
        }
    }

    RenderFrame frame;
    std::map<Id, double> fillAtPeak;
    double fillSum = 0;
    int shownSamples = 0;
    for (std::int64_t s = first; s <= last; s += stride) {
        sim.seek(s);
        sim.extract(frame);
        double total = 0;
        std::uint32_t alive = 0;
        std::map<Id, double> fillNow;
        for (const RenderBatch& batch : frame.batches) {
            double fill = 0;
            for (std::uint32_t i = 0; i < batch.count; ++i) {
                const SpriteInstance& p = frame.instances[batch.first + i];
                const double size = std::max(0.0f, p.size);
                // The whole quad is painted, see-through or not; a streak paints its length.
                double area = size * size;
                const double speed = std::sqrt(double(p.vx) * p.vx + double(p.vy) * p.vy + double(p.vz) * p.vz);
                if (batch.stretch > 0.0f) {
                    area = size * (size + speed * batch.stretch);
                }
                if (batch.trail > 0.0f) {
                    const double reach = speed * std::min<double>(batch.trail, p.age);
                    area += batch.trailRatio * reach * size * batch.trailWidth;
                }
                fill += area;
            }
            fill /= screenArea;
            fillNow[batch.layer] += fill;
            total += fill;
            alive += batch.count;
            Running& r = byLayer[batch.layer];
            r.cost.peakParticles = std::max(r.cost.peakParticles, batch.count);
            r.cost.peakFill = std::max(r.cost.peakFill, fill);
            if (batch.count > 0) {
                r.fillSum += fill;
                r.shown += 1;
            }
        }
        if (alive > 0) {
            fillSum += total;
            shownSamples += 1;
        }
        report.peakParticles = std::max(report.peakParticles, alive);
        if (total > report.peakFill) {
            report.peakFill = total;
            report.peakTime = static_cast<double>(s) * step - from;
            fillAtPeak = fillNow;
        }
    }
    report.meanFill = shownSamples > 0 ? fillSum / shownSamples : 0.0;

    for (auto& [id, r] : byLayer) {
        r.cost.meanFill = r.shown > 0 ? r.fillSum / r.shown : 0.0;
        r.cost.share = report.peakFill > 0 ? fillAtPeak[id] / report.peakFill : 0.0;
        report.layers.push_back(r.cost);
    }
    std::sort(report.layers.begin(), report.layers.end(), [](const LayerCost& a, const LayerCost& b) {
        return a.peakFill != b.peakFill ? a.peakFill > b.peakFill : a.peakParticles > b.peakParticles;
    });

    report.particlesGrade = gradeOf(report.peakParticles, 150, 400, 1000);
    report.fillGrade = gradeOf(report.peakFill, 1.5, 4.0, 8.0);
    report.drawsGrade = gradeOf(report.draws, 6, 12, 24);
    report.grade = std::max({report.particlesGrade, report.fillGrade, report.drawsGrade});
    if (report.lights > 4 && report.grade < BudgetGrade::Heavy) {
        report.grade = BudgetGrade::Heavy;
    }

    // What makes it heavy, in plain words, heaviest cause first.
    if (report.fillGrade >= BudgetGrade::Heavy && !report.layers.empty()) {
        const LayerCost& top = report.layers.front();
        report.notes.push_back(format("It paints the screen %.1f times over at its busiest moment. ", report.peakFill) +
                               "\"" + top.name + "\" does the most: fewer or smaller particles there help most.");
    }
    if (report.particlesGrade >= BudgetGrade::Heavy) {
        report.notes.push_back(format("Up to %.0f particles at once: under 400 is comfortable on low-end phones.",
                                      report.peakParticles));
    }
    if (report.drawsGrade >= BudgetGrade::Heavy) {
        report.notes.push_back(format("%.0f separate draws (each layer is one, a trail one more): "
                                      "merging similar layers helps.", report.draws));
    }
    if (report.lights > 0) {
        report.notes.push_back(format("Up to %.0f real lights in Unity: each lights everything near it, "
                                      "which is costly on phones.", report.lights));
    }
    return report;
}

Effect makeMobileSafe(const Effect& effect, std::string* said, const BudgetOptions& options) {
    using namespace tools;
    Effect out = effect;
    const BudgetReport before = measureBudget(out, options);
    // A little under the limits, so the result is not left just over them.
    const double particleLimit = 360, fillLimit = 3.6;
    // How much each layer has been changed in all, so no layer goes past the floor.
    std::map<Id, double> countLeft, sizeLeft;
    std::map<Id, bool> trailShortened;
    BudgetReport now = before;
    for (int round = 0; round < 6 && now.grade > BudgetGrade::Fine; ++round) {
        bool changedAny = false;
        auto layerOf = [&out](Id id) -> Layer* {
            Layer* l = findLayer(out, id);
            return l && !l->locked ? l : nullptr;
        };
        auto lessCount = [&](Layer& l, double factor) {
            double& left = countLeft.try_emplace(l.id, 1.0).first->second;
            const double f = std::max(factor, 0.5 / left);
            if (f < 0.99) {
                scaleCount(l, f);
                left *= f;
                changedAny = true;
            }
        };
        auto lessSize = [&](Layer& l, double factor) {
            double& left = sizeLeft.try_emplace(l.id, 1.0).first->second;
            const double f = std::max(factor, 0.8 / left);
            if (f < 0.99) {
                scaleSize(l, f);
                left *= f;
                changedAny = true;
            }
        };
        if (now.peakParticles > particleLimit) {
            const double factor = std::max(0.5, 0.95 * particleLimit / now.peakParticles);
            for (const LayerCost& c : now.layers) {
                if (c.peakParticles >= 0.1 * now.peakParticles) {
                    if (Layer* l = layerOf(c.layer)) lessCount(*l, factor);
                }
            }
        }
        if (now.peakFill > fillLimit) {
            const double need = fillLimit / now.peakFill;  // the share of the fill to keep
            double covered = 0;
            for (const LayerCost& c : now.layers) {
                if (covered >= 0.7) break;
                covered += c.share;
                Layer* l = layerOf(c.layer);
                if (!l) continue;
                const bool countSpent = countLeft.count(c.layer) && countLeft[c.layer] <= 0.51;
                if (c.peakParticles > 4 && !countSpent) {
                    lessCount(*l, std::max(0.6, need));
                } else {
                    lessSize(*l, std::max(0.82, std::sqrt(need)));  // the area goes with the size squared
                }
                if (c.trail && !trailShortened[c.layer]) {
                    if (double* trail = property<double>(*l, "sprite", "trail")) {
                        *trail *= 0.7;
                        trailShortened[c.layer] = true;
                        changedAny = true;
                    }
                }
            }
        }
        if (now.lights > 4) {
            for (Layer& l : out.layers) {
                if (l.locked) continue;
                std::int64_t* most = property<std::int64_t>(l, "sprite", "lightMax");
                const double* share = property<double>(l, "sprite", "lightShare");
                if (most && share && *share > 0 && *most > 2) {
                    *most = 2;
                    changedAny = true;
                }
            }
        }
        if (!changedAny) break;
        now = measureBudget(out, options);
    }

    if (said) {
        std::string text;
        if (before.grade <= BudgetGrade::Fine) {
            text = std::string("Already ") + budgetGradeName(before.grade) + " for phones: nothing to change.";
        } else {
            std::vector<std::string> parts;
            for (const Layer& l : out.layers) {
                std::string what;
                if (auto it = countLeft.find(l.id); it != countLeft.end() && it->second < 0.99) {
                    what += format("%.0f%% fewer particles", 100.0 * (1.0 - it->second));
                }
                if (auto it = sizeLeft.find(l.id); it != sizeLeft.end() && it->second < 0.99) {
                    what += (what.empty() ? "" : ", ") + format("%.0f%% smaller", 100.0 * (1.0 - it->second));
                }
                if (trailShortened.count(l.id)) {
                    what += std::string(what.empty() ? "" : ", ") + "a shorter trail";
                }
                if (!what.empty()) parts.push_back(l.name + ": " + what);
            }
            if (now.grade < before.grade) {
                text = std::string(budgetGradeName(before.grade)) + " to " + budgetGradeName(now.grade) + ". ";
            } else if (!parts.empty() || now.lights < before.lights) {
                text = std::string("Lighter, but still ") + budgetGradeName(now.grade) + ". ";
            } else {
                text = std::string("Still ") + budgetGradeName(now.grade) + ": nothing could be trimmed. ";
            }
            for (std::size_t i = 0; i < parts.size(); ++i) {
                text += parts[i] + (i + 1 < parts.size() ? "; " : ". ");
            }
            if (now.lights < before.lights) {
                text += "At most two real lights a layer. ";
            }
            if (now.grade > BudgetGrade::Fine) {
                if (now.drawsGrade > BudgetGrade::Fine) {
                    text += format("%.0f separate draws: merging similar layers by hand would help. ", now.draws);
                }
                bool locked = false;
                for (const Layer& l : out.layers) locked = locked || l.locked;
                if (now.particlesGrade > BudgetGrade::Fine || now.fillGrade > BudgetGrade::Fine) {
                    text += std::string("Going further would change the look (a layer never loses more than half its "
                                        "particles or a fifth of its size)") +
                            (locked ? ", and locked layers were left alone. " : ". ");
                }
            }
            while (!text.empty() && text.back() == ' ') text.pop_back();
        }
        *said = text;
    }
    return out;
}

}  // namespace vfx::editor
