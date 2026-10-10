// VFX Forge editor: one-click changes to a whole effect.
//
// "More sparks", "less glow", "faster": each is an ordinary edit to ordinary
// properties, applied across the layers it concerns. Nothing is hidden: the
// result is the same effect with different numbers in it, and the caller
// applies it as one undo step. Locked layers are never touched.
#pragma once

#include <string>
#include <vector>

#include "vfx/Effect.h"
#include "vfx/Result.h"
#include "vfx/Templates.h"
#include "vfx/editor/Reference.h"

namespace vfx::editor {

enum class Refine {
    Bigger,
    Smaller,
    Wider,          // reaches further; particles the same size
    Tighter,
    MoreSparks,
    FewerSparks,
    MoreGlow,
    LessGlow,
    Faster,
    Slower,
    Sharper,        // crisper: less halo, harder small pieces
    Softer,
    Stylize,        // snappier timing, drawn-looking shapes
    AddSecondary,   // a second, finer layer of small particles
    ForMobile,      // fewer particles, no trails
    MatchColours,   // needs a reference: its colours again
    MatchDuration,  // needs a clip: its length again
};

struct RefineInfo {
    Refine id;
    const char* key;    // stable, for code and tests
    const char* label;  // for the artist
    const char* help;
    bool needsReference = false;
    bool needsClip = false;
};

// Every refinement, in the order they are offered.
const std::vector<RefineInfo>& refinements();
const RefineInfo* findRefinement(std::string_view key);

// Returns the effect with the change made. `said` receives a sentence
// describing what changed (or why nothing did). The analysis may be null
// for refinements that do not need the reference.
Result<Effect> refine(const Effect& effect, Refine what, const ReferenceAnalysis* analysis, const IdSource& newId,
                      std::string* said = nullptr);

}  // namespace vfx::editor
