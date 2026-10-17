// VFX Forge editor: the outline of a picture.
//
// A slot symbol, a logo, a gem: its see-through PNG has a shape, and an
// effect can follow it. traceSilhouette finds the outer edge of the largest
// solid part of the picture (where alpha is at least the threshold), smooths
// it and returns it as evenly spaced points, in the effect's units: the
// picture drawn `width` units wide, its middle at (0, 0), y up.
//
// This is a 2D outline in the picture's plane. It is not the surface of a
// 3D model: effects on a 3D model are hidden behind it (see Model.h), but do
// not yet come off its surface.
#pragma once

#include "vfx/Result.h"
#include "vfx/Value.h"
#include "vfx/editor/Image.h"

namespace vfx::editor {

struct TraceOptions {
    double threshold = 0.5;  // alpha, 0 to 1, that counts as solid
    int points = 200;        // how many points the outline is given
    double smooth = 1.0;     // 0 follows every pixel; 1 a little rounded; more, rounder
};

struct Silhouette {
    PointList outline;       // closed, counter-clockwise
    double width = 0, height = 0;  // of the picture, in units
    int parts = 0;           // solid parts found (the largest is traced)
    double coverage = 0;     // share of the picture that is solid
};

Result<Silhouette> traceSilhouette(const Image& image, double width, const TraceOptions& options = {});

// A rounded rectangle, for frames and for a target that is not there yet.
PointList roundedRect(double width, double height, double radius, int pointsPerCorner = 6);

}  // namespace vfx::editor
