// VFX Forge editor: how heavy an effect is to draw on a phone.
//
// The effect is run through once (a looping effect twice, so the second pass
// is measured with the first pass's particles still alive) and looked at 30
// times a second. Three things decide most of what a particle effect costs on
// a mobile GPU: how many particles there are, how much of the screen they
// paint over (overdraw: a big soft puff costs its whole square, however
// see-through its edges are), and how many separate draws it takes.
//
// Fill is counted in "screens": 1.0 means the effect paints as many pixels as
// a whole phone screen once. The phone screen is taken as 10 units tall and
// 9:16, which is a Unity orthographic camera of size 5 in portrait, the usual
// set-up for a casual game. The limits are rules of thumb for low-end phones
// with the whole effect on screen; a big screen-filling effect on a high-end
// phone can afford much more.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vfx/Effect.h"

namespace vfx::editor {

enum class BudgetGrade { Light = 0, Fine = 1, Heavy = 2, TooHeavy = 3 };

const char* budgetGradeName(BudgetGrade grade);

struct LayerCost {
    Id layer;
    std::string name;
    std::uint32_t peakParticles = 0;
    double peakFill = 0;  // screens, at the effect's busiest moment for this layer
    double meanFill = 0;  // screens, averaged over the moments the layer shows anything
    bool trail = false, lights = false, collision = false, subEmitter = false;
    double share = 0;     // its part of the effect's fill at the effect's busiest moment, 0 to 1
};

struct BudgetReport {
    std::uint32_t peakParticles = 0;
    double peakFill = 0;   // screens painted at the busiest moment
    double peakTime = 0;   // seconds into the effect (into the pass, for a loop)
    double meanFill = 0;   // averaged over the moments anything shows
    int draws = 0;         // drawn layers, a trail counting as one more
    int lights = 0;        // real lights at most (Unity only)
    BudgetGrade particlesGrade = BudgetGrade::Light, fillGrade = BudgetGrade::Light,
                drawsGrade = BudgetGrade::Light, grade = BudgetGrade::Light;
    std::vector<LayerCost> layers;  // heaviest first
    std::vector<std::string> notes; // plain words: what makes it heavy, and what would help
};

struct BudgetOptions {
    double screenHeight = 10.0;   // units
    double screenAspect = 9.0 / 16.0;  // width over height
    double samplesPerSecond = 30.0;
};

BudgetReport measureBudget(const Effect& effect, const BudgetOptions& options = {});

// A lighter copy of the effect, aiming for OK or better on a low-end phone:
// fewer particles in the layers that have the most, smaller big glows (a
// layer of a few large sprites keeps its count and loses some size instead),
// shorter trails where they are heavy, and at most two real lights a layer.
// Each layer changes by no more than it must, and never past half its count
// or 80% of its size, so the effect still looks like itself. Locked layers
// are left alone. `said`, when given, gets what was changed, in plain words.
Effect makeMobileSafe(const Effect& effect, std::string* said = nullptr, const BudgetOptions& options = {});

}  // namespace vfx::editor
