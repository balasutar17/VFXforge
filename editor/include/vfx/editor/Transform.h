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

namespace vfx::editor {

struct LayerTransform {
    Vec3 pivot{0, 0, 0};  // what Turn and Size happen about
    Vec3 move{0, 0, 0};
    double turn = 0;      // degrees
    double factor = 1;    // Size: 2 is twice as large

    bool identity() const {
        return move.x == 0 && move.y == 0 && move.z == 0 && turn == 0 && factor == 1;
    }
};

// The layer with the change made. threeD is the effect's own space.
Layer transformLayer(const Layer& layer, const LayerTransform& t, bool threeD);

// Where a set of layers is: the middle of their Positions.
Vec3 middleOf(const std::vector<const Layer*>& layers);

}  // namespace vfx::editor
