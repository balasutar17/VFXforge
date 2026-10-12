// VFX Forge editor: a 3D model to make effects on.
//
// An artist brings in the character, weapon or prop an effect is for, and
// sees the effect on it: the model is drawn behind the particles, lit in a
// toon style, and particles that pass behind it are hidden. The model is
// for seeing only: it is not part of the effect, not saved in the effect
// file and not exported (in the game it is already there).
//
// Models are read from Wavefront OBJ (with its MTL colours) and glTF 2.0
// (.glb, or .gltf with its .bin), the two formats every 3D program writes.
// Only the shape and plain colours are read: no textures, no animation.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "vfx/Result.h"
#include "vfx/editor/Picture.h"
#include "vfx/editor/SpriteMesh.h"

namespace vfx::editor {

struct Model {
    std::string name;
    // Three numbers a corner each, as read (in the file's own units).
    std::vector<float> positions;
    std::vector<float> normals;   // same count; worked out when the file has none
    std::vector<float> colors;    // same count; the screen colour of each corner, 0 to 1
    std::vector<std::uint32_t> triangles;  // three corners each

    std::size_t corners() const { return positions.size() / 3; }
    std::size_t triangleCount() const { return triangles.size() / 3; }

    // The box around it, in the file's units.
    float minX = 0, minY = 0, minZ = 0, maxX = 0, maxY = 0, maxZ = 0;
};

// Where the model stands in the effect's world, and how big it is. It is
// fitted by its box: scaled so it stands `height` tall, its feet at y, the
// middle of its footprint at x, z, then turned about its own upright.
struct ModelPlace {
    float x = 0, y = 0, z = 0;
    float turn = 0;     // degrees, counter-clockwise seen from above
    float height = 3;   // world units
};

// Reading. A model with no triangles, or bigger than kMaxModelTriangles,
// is refused with a message that says why.
inline constexpr std::size_t kMaxModelTriangles = 2'000'000;
Result<Model> readObj(std::string_view obj, std::string_view mtl = {});
// A .glb file, or a .gltf file's text; a .gltf's buffers are read from
// `folder` (or from inside the text, when written there).
Result<Model> readGltf(std::string_view bytes, const std::filesystem::path& folder = {});
// By the file's extension: .obj (its .mtl beside it is read too), .glb, .gltf.
Result<Model> loadModel(const std::filesystem::path& file);

// A drawn model: its colours with coverage in alpha (straight, not
// premultiplied), and for each pixel how far from the viewer the nearest
// surface is (infinity where there is none), as the mesh builder measures
// a particle.
struct ModelPicture {
    Picture picture;
    std::vector<float> depth;

    bool empty() const { return picture.width <= 0 || picture.height <= 0; }

    // How much of a particle at this point on the viewport (in the View's
    // pixels, which may be fewer than the picture's) and this far away is
    // seen: 1 in front of the model or beside it, 0 well behind it, and in
    // between for a particle just behind its surface, so nothing pops.
    float seen(float viewX, float viewY, float particleDepth, float viewWidth, float viewHeight) const;
};

// How far behind the surface a particle fades out over, in world units.
inline constexpr float kBehindFade = 0.15f;

// Draws the model as the View sees it, `width` by `height` pixels (the
// View's own size, or that times the screen's pixel ratio). flat is the
// effect's own: a 2D effect sees the model from the front, without
// perspective. `smooth` is how many samples a pixel takes each way.
ModelPicture drawModel(const Model& model, const ModelPlace& place, const View& view, bool flat, int width,
                       int height, int smooth = 2);

// A practice dummy, made here rather than read: a wooden training post with
// a padded body, a head and arms, about 2 600 triangles. For trying effects
// on before bringing in a model of one's own.
Model practiceDummy();

}  // namespace vfx::editor
