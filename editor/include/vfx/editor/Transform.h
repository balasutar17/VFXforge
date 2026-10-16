// VFX Forge editor: moving, turning and sizing layers as a whole.
//
// What the viewport's handles (the gizmo) do to the selected layer or group.
// Everything is an ordinary change to ordinary settings, so the result
// exports to Unity like any other effect:
//
// - Move adds to each layer's Position.
// - Turn turns each Position about the pivot, the way the particles are
//   sent (Direction) and, where the sprites lie in the turning plane, their
//   Rotation. A 2D effect turns in the picture (counter-clockwise above 0);
//   a 3D effect turns about the upright (counter-clockwise seen from above),
//   as a character turns. Gravity stays pointing down.
// - Size makes the particles and how far they go larger or smaller
//   (Size, Speed, the emitter's Radius and Size, Gravity, swirls), and the
//   Positions spread out from the pivot by as much.
#pragma once

#include "vfx/Effect.h"
#include "vfx/Value.h"

#include <vector>

namespace vfx::editor {

struct LayerTransform {
    Vec3 pivot{0, 0, 0};  // what Turn and Size happen about
    Vec3 move{0, 0, 0};

    // Turn, in degrees about one axis (counter-clockwise looking down the
    // axis towards its start, as everywhere in VFX Forge). axis -1 is the
    // usual one: Z (the picture) in 2D, Y (the upright) in 3D. 0 is X, 1 Y,
    // 2 Z; 3 is axisVector (any direction, of length 1: a layer's own axes).
    // A 2D effect only turns about Z.
    double turn = 0;
    int axis = -1;
    Vec3 axisVector{0, 1, 0};

    // Size. factor makes everything larger: the particles, how far they go
    // and how far apart the emitters are. scale stretches along one axis
    // only: how far apart the emitters are that way, and the emitter's own
    // width, height or depth; the particles keep their size.
    double factor = 1;
    Vec3 scale{1, 1, 1};

    // Each layer turns and sizes about its own Position instead of the pivot.
    bool individual = false;

    bool identity() const {
        return move.x == 0 && move.y == 0 && move.z == 0 && turn == 0 && factor == 1 && scale.x == 1 &&
               scale.y == 1 && scale.z == 1;
    }
};

// The layer with the change made. threeD is the effect's own space.
Layer transformLayer(const Layer& layer, const LayerTransform& t, bool threeD);

// Where a set of layers is: the middle of their Positions.
Vec3 middleOf(const std::vector<const Layer*>& layers);

}  // namespace vfx::editor
