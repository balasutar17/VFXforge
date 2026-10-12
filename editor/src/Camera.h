// VFX Forge editor (internal): the camera the mesh builder and the model
// renderer share, so a model and the particles in front of it are seen
// from exactly the same place.
#pragma once

#include "vfx/editor/SpriteMesh.h"

namespace vfx::editor::camera {

inline constexpr float kNear = 0.05f;  // 3D: nothing closer to the camera than this is drawn

// The camera worked out once per frame.
struct Camera {
    bool flat = true;
    float halfW = 0, halfH = 0;

    // 2D
    float cx = 0, cy = 0, pixelsPerUnit = 1;

    // 3D: camera position and its three axes in world space.
    float ex = 0, ey = 0, ez = 0;
    float rx = 1, ry = 0, rz = 0;  // right
    float ux = 0, uy = 1, uz = 0;  // up
    float fx = 0, fy = 0, fz = -1; // forward
    float focal = 1;               // pixels at one unit of depth
};

inline Camera makeCamera(const View& view, bool flat) {
    Camera c;
    c.flat = flat;
    c.halfW = view.width * 0.5f;
    c.halfH = view.height * 0.5f;
    if (flat) {
        c.cx = view.centerX;
        c.cy = view.centerY;
        c.pixelsPerUnit = view.height / (view.unitsHigh > 1e-6f ? view.unitsHigh : 1e-6f);
        return c;
    }
    const float yaw = view.yaw * 3.14159265358979323846f / 180.0f;
    float pitch = view.pitch;
    pitch = pitch > 89.0f ? 89.0f : (pitch < -89.0f ? -89.0f : pitch);
    pitch *= 3.14159265358979323846f / 180.0f;
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const float cyaw = std::cos(yaw), syaw = std::sin(yaw);

    // From the target back to the camera.
    const float bx = syaw * cp, by = sp, bz = cyaw * cp;
    const float distance = view.distance > 0.1f ? view.distance : 0.1f;
    c.ex = view.targetX + bx * distance;
    c.ey = view.targetY + by * distance;
    c.ez = view.targetZ + bz * distance;
    c.fx = -bx;
    c.fy = -by;
    c.fz = -bz;
    // right = forward x worldUp, which is never degenerate because pitch
    // stops short of straight up or down.
    c.rx = cyaw;
    c.ry = 0.0f;
    c.rz = -syaw;
    // up = right x forward
    c.ux = c.ry * c.fz - c.rz * c.fy;
    c.uy = c.rz * c.fx - c.rx * c.fz;
    c.uz = c.rx * c.fy - c.ry * c.fx;

    float fov = view.fieldOfView;
    fov = fov < 5.0f ? 5.0f : (fov > 120.0f ? 120.0f : fov);
    c.focal = c.halfH / std::tan(fov * 0.5f * 3.14159265358979323846f / 180.0f);
    return c;
}

inline bool project(const Camera& c, float x, float y, float z, float& px, float& py, float& scale) {
    if (c.flat) {
        px = c.halfW + (x - c.cx) * c.pixelsPerUnit;
        py = c.halfH - (y - c.cy) * c.pixelsPerUnit;
        scale = c.pixelsPerUnit;
        return true;
    }
    const float dx = x - c.ex, dy = y - c.ey, dz = z - c.ez;
    const float depth = dx * c.fx + dy * c.fy + dz * c.fz;
    if (!(depth > kNear)) {
        return false;
    }
    scale = c.focal / depth;
    px = c.halfW + (dx * c.rx + dy * c.ry + dz * c.rz) * scale;
    py = c.halfH - (dx * c.ux + dy * c.uy + dz * c.uz) * scale;
    return true;
}

// How far a world point is from the viewer, along the way the camera looks.
// A flat (2D) view looks along -z from in front of the plane, so a point
// with a larger z is nearer.
inline float depthOf(const Camera& c, float x, float y, float z) {
    if (c.flat) {
        return -z;
    }
    return (x - c.ex) * c.fx + (y - c.ey) * c.fy + (z - c.ez) * c.fz;
}

}  // namespace vfx::editor::camera
