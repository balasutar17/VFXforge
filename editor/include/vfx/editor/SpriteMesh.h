// VFX Forge editor: turning a simulated frame into triangles.
//
// The simulation hands over a RenderFrame: plain particles. This turns them
// into one list of corner points in window pixels, ready to be copied to the
// graphics card. Doing it here, in ordinary code, means the camera, the
// colours and the blending rules can all be tested without a window.
#pragma once

#include <cstdint>
#include <vector>

#include "vfx/Simulation.h"

namespace vfx::editor {

// One corner of a particle. Colour is premultiplied and ready for the screen:
// the graphics card only ever does "source + destination * (1 - alpha)", and
// an additive particle is simply one whose alpha is zero.
struct SpriteVertex {
    float x = 0, y = 0;  // pixels from the top-left of the viewport
    float u = 0, v = 0;  // 0..1 across the particle, v running downward; for a
                         // picture, where in the picture (its sprite-sheet cell)
    float r = 0, g = 0, b = 0, a = 0;
    float shape = 0;         // which built-in shape (a SpriteShape, as a number);
                             // kPictureShape when the artist's picture is drawn
    float aaX = 1, aaY = 1;  // the size of one pixel, in the particle's own -1..1 units
    float edge = 0;          // dissolving: how brightly the eaten edge glows, 0 to 1
    float ownX = 0, ownY = 0;  // where this corner is on the particle's own -1..1
                               // square, y up (the same for a shape or a picture)
    float dissolve = 0;        // how far it is eaten away: 0 whole, 1 gone
    float seed = 0;            // the particle's own random number, 0..1
    float soft = 0;            // dissolving: how soft the eaten edge is, 0 to 1
    // How far this corner is from the viewer, for hiding what is behind a
    // model pixel by pixel. In 3D it is one over the distance, which, unlike
    // the distance, changes evenly across the screen; in 2D the distance
    // itself (see depthFromKey).
    float depth = 0;
};
static_assert(sizeof(SpriteVertex) == 72, "the graphics code copies these as raw bytes");

// The distance a corner's depth stands for.
inline float depthFromKey(float key, bool flat) { return flat ? key : (key > 0.0f ? 1.0f / key : 1e30f); }

inline constexpr float kPictureShape = -1.0f;
// A segment of a trail ribbon: u runs along it (0 at the particle, 1 at the
// end), v across it.
inline constexpr float kRibbonShape = -2.0f;

class ImageSet;

// How the effect is looked at.
struct View {
    float width = 800;   // viewport size in pixels
    float height = 600;

    // 2D effects: a flat camera. centre is the world point in the middle of
    // the viewport, and unitsHigh is how much of the world fits top to bottom.
    float centerX = 0;
    float centerY = 1.5f;
    float unitsHigh = 7;

    // 3D effects: a camera that orbits a target point.
    float targetX = 0, targetY = 1.5f, targetZ = 0;
    float yaw = 30;       // degrees around the vertical axis
    float pitch = 15;     // degrees above the horizon
    float distance = 10;  // world units from the target
    float fieldOfView = 40;  // degrees, top to bottom
};

// A stretch of the index list drawn with one picture, in drawing order. An
// invalid texture means the shapes drawn from formulas. A shape from the
// library is drawn from a picture too: its run carries shapeTextureId (see
// ShapeLibrary.h), and its corners carry the shape's number as usual.
struct SpriteRun {
    Id texture;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    // How its particles are laid over what is behind them. Only Normal and
    // Linear Dodge (Add) are in the corners' colours (alpha 0 is "add"); the
    // other modes keep their alpha and are told apart here, so a run never
    // spans two modes.
    BlendMode blend = BlendMode::Alpha;
};

struct SpriteMesh {
    std::vector<SpriteVertex> vertices;  // four per particle
    std::vector<std::uint32_t> indices;  // six per particle
    std::vector<SpriteRun> runs;         // covering every index, in order
    std::uint32_t drawn = 0;             // particles that made it on screen
};

// Rebuilds the mesh from a frame. Reuse one mesh object from frame to frame
// and no memory is allocated once it has grown to size. A layer whose
// picture is not in images (or with no images given) draws its Shape instead.
// Every corner carries its depth, so whatever draws the mesh can hide what
// is behind a model pixel by pixel (see Model.h).
void buildSpriteMesh(const RenderFrame& frame, const View& view, SpriteMesh& mesh,
                     const ImageSet* images = nullptr);

// Where a world point lands in the viewport. Returns false when a 3D point
// is behind the camera. scale receives pixels per world unit at that point.
bool projectPoint(const View& view, bool flat, float x, float y, float z, float& px, float& py,
                  float& scale);

}  // namespace vfx::editor
