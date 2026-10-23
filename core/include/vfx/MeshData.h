// VFX Forge core: a model's shape, as an effect carries it.
//
// An effect never holds a model file. What a Mesh emitter needs of a model is
// small: a rough copy of its surface (triangles) or the lines worth lighting
// (edges), already standing where the model stands in the effect's world. It
// is kept in the Shape module's "Mesh Data" property, a list of points, so
// that it is saved, undone and carried like every other value.
//
// Layout of the list: the first point is the header {version, count}; after
// it come `count` records of kMeshRecordPoints points each (a record is
// ten numbers; unused ones are 0).
//   triangles: ax ay | az bx | by bz | cx cy | cz 0     (counter-clockwise from outside)
//   edges:     ax ay | az bx | by bz | 0 0   | 0 0      (a line from a to b)
#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "vfx/Value.h"

namespace vfx {

inline constexpr std::size_t kMeshRecordPoints = 5;
inline constexpr double kMeshDataVersion = 1;

struct MeshTriangle {
    std::array<float, 9> v{};  // three corners, x y z each
    bool operator==(const MeshTriangle&) const = default;
};
struct MeshSegment {
    std::array<float, 6> v{};  // two ends, x y z each
    bool operator==(const MeshSegment&) const = default;
};

PointList packTriangles(const std::vector<MeshTriangle>& triangles);
PointList packSegments(const std::vector<MeshSegment>& segments);
// Empty when the list is not a valid pack (wrong header or length).
std::vector<MeshTriangle> unpackTriangles(const PointList& list);
std::vector<MeshSegment> unpackSegments(const PointList& list);

}  // namespace vfx
