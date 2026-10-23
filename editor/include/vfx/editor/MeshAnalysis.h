// VFX Forge editor: what an effect needs to know about a model's shape.
//
// An effect on a model is not made around its box. The model is put where it
// stands in the effect's world (Model.h's place), welded (corners that share
// a position are one vertex, so neighbouring triangles find each other),
// examined, and from it come the things the Mesh emitter uses (MeshData.h in
// the core): a rough copy of the surface, the edges worth lighting, the
// outline as seen from a camera, and the footprint a ground ring should fit.
//
// Nothing here changes the model. It is worked out again, on request, when
// the model moves or the artist asks (see AppController::generateOnModel).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vfx/MeshData.h"
#include "vfx/editor/Model.h"
#include "vfx/editor/SpriteMesh.h"

namespace vfx::editor {

// The model in the effect's world, welded.
struct WorldMesh {
    std::vector<float> positions;          // three a vertex
    std::vector<std::uint32_t> triangles;  // three vertices each, counter-clockwise from outside
    float minX = 0, minY = 0, minZ = 0, maxX = 0, maxY = 0, maxZ = 0;
    std::size_t vertexCount() const { return positions.size() / 3; }
    std::size_t triangleCount() const { return triangles.size() / 3; }
};

WorldMesh toWorld(const Model& model, const ModelPlace& place);

struct Footprint {
    float cx = 0, cz = 0;      // middle of the model's footprint
    float halfA = 0, halfB = 0;  // half sizes along its two axes: the smallest rectangle round the model
    float turn = 0;            // degrees, counter-clockwise from above: where axis A points
    float baseY = 0;           // the floor, at the model's lowest point
};

// What was found. Limits are said in `warnings`, in words an artist can act on.
struct MeshReport {
    std::size_t triangles = 0, vertices = 0;
    std::size_t degenerate = 0;        // triangles with no area
    std::size_t openEdges = 0;         // edges with one face: holes or thin sheets
    std::size_t nonManifoldEdges = 0;  // edges with more than two faces
    std::size_t components = 0;        // separate connected pieces
    double area = 0;                   // surface area, world units squared
    bool closed = false;               // no open edges: a volume is well defined
    Footprint footprint;
    std::vector<std::string> warnings;
};

MeshReport analyseMesh(const WorldMesh& mesh);

// A rough copy of the surface with at most `maxTriangles` triangles. A model
// already under the limit is copied as it is; a bigger one is simplified by
// merging vertices that fall in the same cell of a grid, the grid made as
// fine as the limit allows, so nothing is cut away, only blunted.
std::vector<MeshTriangle> surfaceProxy(const WorldMesh& mesh, std::size_t maxTriangles);

// Edges between faces that meet at more than `angle` degrees (a box's
// corners, a cylinder's rims, not the seams of a rounded surface) and edges
// with one face. At most `maxSegments`, the longest kept.
std::vector<MeshSegment> featureEdges(const WorldMesh& mesh, double angle, std::size_t maxSegments);

// The outline as a camera sees it: edges where one face looks toward the
// camera and the next looks away, and edges with one face. It is a property
// of this viewpoint and is out of date when the camera moves.
std::vector<MeshSegment> silhouetteEdges(const WorldMesh& mesh, float cameraX, float cameraY, float cameraZ,
                                         std::size_t maxSegments);

// Where the camera of this View is, in the world.
void cameraPosition(const View& view, float& x, float& y, float& z);

}  // namespace vfx::editor
