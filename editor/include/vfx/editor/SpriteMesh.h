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
    float u = 0, v = 0;  // 0..1 across the particle
    float r = 0, g = 0, b = 0, a = 0;
};
static_assert(sizeof(SpriteVertex) == 32, "the graphics code copies these as raw bytes");

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

struct SpriteMesh {
    std::vector<SpriteVertex> vertices;  // four per particle
    std::vector<std::uint32_t> indices;  // six per particle
    std::uint32_t drawn = 0;             // particles that made it on screen
};

// Rebuilds the mesh from a frame. Reuse one mesh object from frame to frame
// and no memory is allocated once it has grown to size.
void buildSpriteMesh(const RenderFrame& frame, const View& view, SpriteMesh& mesh);

// Where a world point lands in the viewport. Returns false when a 3D point
// is behind the camera. scale receives pixels per world unit at that point.
bool projectPoint(const View& view, bool flat, float x, float y, float z, float& px, float& py,
                  float& scale);

}  // namespace vfx::editor
