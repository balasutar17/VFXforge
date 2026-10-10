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
};
static_assert(sizeof(SpriteVertex) == 44, "the graphics code copies these as raw bytes");

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
// invalid texture means the built-in shapes.
struct SpriteRun {
    Id texture;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
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
void buildSpriteMesh(const RenderFrame& frame, const View& view, SpriteMesh& mesh,
                     const ImageSet* images = nullptr);

// Where a world point lands in the viewport. Returns false when a 3D point
// is behind the camera. scale receives pixels per world unit at that point.
bool projectPoint(const View& view, bool flat, float x, float y, float z, float& px, float& py,
                  float& scale);

}  // namespace vfx::editor
