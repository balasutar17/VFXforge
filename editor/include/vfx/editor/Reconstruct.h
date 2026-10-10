// VFX Forge editor: building an editable effect from a reference analysis.
//
// The result is an ordinary effect: layers, modules, curves, all of it
// editable, saved in the same .vfx file as anything else. It is never a
// flattened picture of the reference. Each layer records what it is for
// (its role) and a note says which finding it came from and how much of it
// was seen rather than assumed.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "vfx/Effect.h"
#include "vfx/Templates.h"
#include "vfx/editor/Reference.h"
#include "vfx/editor/SpriteMesh.h"

namespace vfx::editor {

// What to favour when rebuilding.
enum class MatchMode {
    ShapeColour,  // look as much like the reference picture as possible
    Motion,       // follow the clip's timing and movement first
    Layered,      // split into as many separate, editable parts as were found
    Balanced,     // a sensible middle: the default
};
const char* matchModeName(MatchMode mode);
bool parseMatchMode(std::string_view text, MatchMode& mode);

// Three takes on the same reference. Closest is the default; the other two
// are offered beside it and never replace it on their own.
enum class Variation {
    Closest,      // as near to the reference as the analysis allows
    Performance,  // fewer particles and layers, for slower devices
    Enhanced,     // adds polish the reference does not have (marked as such)
};
const char* variationName(Variation variation);
bool parseVariation(std::string_view text, Variation& variation);

// Where the effect will run, which sets how many particles it may use.
enum class Target { Mobile, Desktop, VR };
const char* targetName(Target target);
bool parseTarget(std::string_view text, Target& target);
int particleBudget(Target target);

// A still picture does not say whether the effect is one burst or keeps
// going. Auto decides from what was found; the artist can overrule it.
enum class Pace { Auto, Burst, Steady };

struct ReconstructOptions {
    MatchMode mode = MatchMode::Balanced;
    Variation variation = Variation::Closest;
    Target target = Target::Desktop;
    Pace pace = Pace::Auto;
    // Let layers draw pieces cut from the reference itself (see Cutout)
    // where no built-in shape is close. Off unless the artist turns it on:
    // those are the reference's own pixels.
    bool cutouts = false;
    std::string name = "From reference";
};

struct LayerNote {
    Id layer;
    std::string name;
    std::string role;       // the same word as the layer's role
    std::string technique;  // how it is built, for the artist
    std::string from;       // the key of the finding it came from; empty when added for polish
    Basis look = Basis::Observed;    // whether what it looks like was seen
    Basis motion = Basis::Inferred;  // whether how it moves was seen
    std::string note;
};

// A picture a rebuilt layer draws. The effect's asset list already names it
// (by this ID and path); whoever keeps the effect writes the PNG there.
struct PictureUse {
    Id asset;
    std::string path;  // relative to the project folder
    std::string png;   // the file's bytes
    std::shared_ptr<const Image> image;
};

struct Reconstruction {
    Effect effect;
    std::vector<PictureUse> pictures;

    // The flat camera that shows the effect framed as the reference frames
    // it: draw with this view at the reference's proportions and the two
    // line up. width and height hold the analysis grid's size.
    View view;

    // The moment of the effect that corresponds to the picture analysed.
    double peakTime = 0;
    // Effect time 0 is this many seconds into the reference clip (as trimmed).
    double referenceStart = 0;

    std::vector<LayerNote> layers;
    // Things in the reference that this version cannot build.
    std::vector<std::string> notes;

    int particlesAtBusiest = 0;  // an estimate, from rates, bursts and lifetimes
    int budget = 0;              // the target's limit that was applied
};

// World units that the reference's height is mapped to.
inline constexpr float kReferenceUnits = 8.0f;

// Builds the effect. Deterministic: the same analysis, options and IDs give
// the same effect.
// Cut-outs are used only when given and the options allow them.
Reconstruction reconstruct(const ReferenceAnalysis& analysis, const ReconstructOptions& options,
                           const IdSource& newId, const std::vector<Cutout>* cutouts = nullptr);

// The pictures of a reconstruction, ready for the renderers.
ImageSet picturesOf(const Reconstruction& built);

// A curve with few keys that stays close to the samples given (each sample
// is a time 0..1 and a value). Used to turn per-frame measurements into
// editable curves.
std::vector<CurveKey> simplifyCurve(const std::vector<CurveKey>& samples, int maxKeys, double tolerance);

}  // namespace vfx::editor
