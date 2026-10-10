// VFX Forge editor: holding a rebuilt effect up against its reference.
//
// The effect is drawn by the reference renderer, framed as the reference is
// framed, and the two pictures are measured the same way. The numbers that
// come out are an aid for finding the largest differences. They are not a
// measure of how alike the two look to a person, and nothing here claims so.
//
// The same measurements drive the fit: a handful of sizes and brightnesses
// are nudged, the effect is drawn again, and a change is kept only when the
// pictures come out closer.
#pragma once

#include <string>
#include <vector>

#include "vfx/Effect.h"
#include "vfx/editor/Picture.h"
#include "vfx/editor/Reconstruct.h"
#include "vfx/editor/Reference.h"

namespace vfx::editor {

// How the effect sits against the reference: the camera, and how the two
// clocks line up.
struct Placement {
    View view;                  // from Reconstruction::view
    double peakTime = 0;        // effect time that matches the analysed frame
    double referenceStart = 0;  // seconds into the trimmed clip at effect time 0
};
inline Placement placementOf(const Reconstruction& built) {
    return Placement{built.view, built.peakTime, built.referenceStart};
}

// How much each kind of likeness matters, 0 (ignore) upward. 1 is normal.
struct Priorities {
    float silhouette = 1;
    float colour = 1;
    float brightness = 1;
    float density = 1;
    float motion = 1;
    float timing = 1;
    float detail = 1;
};

// Each part runs from 0 (nothing alike) to 1 (the same by this measure), or
// is -1 when it could not be measured: a still reference has no motion.
struct Similarity {
    float silhouette = -1;  // do the outlines overlap
    float colour = -1;      // are the same colours in the same places
    float brightness = -1;  // is it as bright from the centre outward
    float density = -1;     // are there as many small pieces
    float motion = -1;      // does it grow and fade the same way
    float timing = -1;      // does it start, peak and end at the same moments
    float detail = -1;      // is there as much fine detail
    float overall = 0;      // the measured parts, weighted by the priorities
    // The largest differences, in words, biggest first.
    std::vector<std::string> differences;
};

// Which frame of the reference goes with a moment of the effect.
int referenceFrameAt(const Reference& reference, const ReferenceOptions& options, const Placement& placement,
                     double effectTime);
// And the other way: the effect time a reference frame corresponds to.
double effectTimeAt(const Reference& reference, const ReferenceOptions& options, const Placement& placement,
                    int frame);

// Draws the effect as the reference frames it, over the reference's
// background (or over nothing, for a see-through reference), at a size.
Picture drawLikeReference(const Effect& effect, const Placement& placement, const StillAnalysis& still, int width,
                          int height, double effectTime, const ImageSet* images = nullptr);

// The reference frame as a picture of that size, over the same background.
Picture drawReference(const Reference& reference, const ReferenceOptions& options, const StillAnalysis& still,
                      int frame, int width, int height);

// The two laid over each other: opacity 0 shows only the reference, 1 only
// the effect.
Picture overlayPictures(const Picture& reference, const Picture& made, float opacity);
// Where they differ: black where they agree, brighter the more they differ,
// tinted warm where the effect is too bright and cool where it is too dim.
Picture differencePicture(const Picture& reference, const Picture& made);

// Measures the effect against the reference.
Similarity compareToReference(const Reference& reference, const ReferenceOptions& options,
                              const ReferenceAnalysis& analysis, const Effect& effect, const Placement& placement,
                              const Priorities& priorities = {}, const Progress& progress = {},
                              const ImageSet* images = nullptr);

struct FitOptions {
    Priorities priorities;
    int rounds = 3;         // passes over the knobs: 1 quick, 3 normal, 5 thorough
    bool onlyOutline = false;  // adjust sizes only, judged by the outline alone
};

struct FitResult {
    Similarity before, after;
    int drawings = 0;       // how many times the effect was drawn
    double cost = 0;        // seconds
    bool cancelled = false;
    std::vector<std::string> changes;  // what was adjusted, in words
};

// Adjusts sizes and brightnesses of the effect's layers (never a locked
// one) so it measures closer to the reference. The effect is changed in
// place; nothing is changed unless it measured better.
FitResult fitToReference(Effect& effect, const Reference& reference, const ReferenceOptions& options,
                         const ReferenceAnalysis& analysis, const Placement& placement, const FitOptions& fit = {},
                         const Progress& progress = {}, const ImageSet* images = nullptr);

}  // namespace vfx::editor
