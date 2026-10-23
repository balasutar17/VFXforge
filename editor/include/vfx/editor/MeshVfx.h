// VFX Forge editor: effects made from a model's own shape.
//
// "Generate on model" reads the model standing in the effect (Model.h),
// examines it (MeshAnalysis.h) and builds layers that follow it: an outline
// along its hard edges, one along its silhouette from the camera that made
// it, a glow, an energy coat that ripples across its surfaces, scan bands
// that wrap round it as they rise, particles off its surface, and rings,
// a disc and beams on the ground sized to its footprint.
//
// The layers are ordinary layers: edited, hidden, soloed, duplicated like any
// other. What follows the model is one value in each (the Shape module's
// Mesh Data, or a ring's path). Fitting again, after the model has moved or
// been swapped, replaces only those values and leaves every setting the
// artist changed alone.
#pragma once

#include <string>
#include <vector>

#include "vfx/Effect.h"
#include "vfx/Id.h"
#include "vfx/Result.h"
#include "vfx/Templates.h"
#include "vfx/editor/MeshAnalysis.h"
#include "vfx/editor/Model.h"
#include "vfx/editor/SpriteMesh.h"

namespace vfx::editor {

enum class MeshQuality { Preview, Balanced, High };

struct MeshVfxOptions {
    unsigned primary = 0x19e8ff;    // the main colour of the energy
    unsigned highlight = 0xffffff;  // its hot lines
    unsigned glow = 0x1a7bff;       // the outer glow
    MeshQuality quality = MeshQuality::Balanced;
    double edgeAngle = 35;          // degrees between faces for a hard edge
    bool outline = true, silhouette = true, surface = true, bands = true, particles = true, ground = true;
};

// What generating found and made, for the artist.
struct MeshVfxResult {
    Effect effect;                       // the layers, as a small 3D effect to add as a group
    MeshReport report;
    std::size_t proxyTriangles = 0, edgeSegments = 0, silhouetteSegments = 0;
    std::vector<std::string> warnings;   // the report's, and what generating could not do
};

Result<MeshVfxResult> generateOnModel(const Model& model, const ModelPlace& place, const View& camera,
                                      const MeshVfxOptions& options, const IdSource& newId);

// Whether `effect` holds layers made by generateOnModel.
bool hasMeshLayers(const Effect& effect);

// The layers of `effect` that were made by generateOnModel (by their names)
// fitted to the model as it stands now. Returns how many layers were fitted.
std::size_t fitToModel(Effect& effect, const Model& model, const ModelPlace& place, const View& camera,
                       const MeshVfxOptions& options, std::vector<std::string>* warnings = nullptr);

}  // namespace vfx::editor
