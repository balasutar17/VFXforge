#include "vfx/editor/SpriteMesh.h"

#include "vfx/DetMath.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "vfx/editor/Image.h"
#include "vfx/editor/Session.h"
#include "vfx/editor/ShapeLibrary.h"
#include "Camera.h"

namespace vfx::editor {

namespace {

using camera::Camera;
using camera::kNear;
using camera::makeCamera;
using camera::project;

constexpr float kPi = 3.14159265358979323846f;

float toScreen(float linear) { return static_cast<float>(linearToSrgb(static_cast<double>(linear))); }

// A ribbon along the path a particle took. The path is found by undoing the
// simulation's own steps from where the particle is now (each step adds
// gravity, divides by drag, then moves), so it is exactly the path it took,
// back to its birth at most.
void addTrail(SpriteMesh& mesh, const Camera& camera, bool flat, const RenderBatch& batch,
              const SpriteInstance& p, float glow, bool additive, std::int64_t frameStep) {
    const float h = batch.step;
    // A held layer's particles are those of an earlier step.
    const std::int64_t shownStep = batch.hold > 0.0f ? batch.shownStep : frameStep;
    const float back = std::min(batch.trail, p.age > 0.0f ? p.age : 0.0f);
    const int steps = static_cast<int>(back / h + 1e-3f);
    if (steps < 2) {
        return;
    }
    const int samples = std::min(steps, 24);
    struct Point {
        float x, y, scale, depth;
    };
    Point points[25];
    int count = 0;
    float x = p.x, y = p.y, z = flat ? 0.0f : p.z;
    float vx = p.vx, vy = p.vy, vz = flat ? 0.0f : p.vz;
    // A particle whose speed follows a curve covered, in each step, its
    // speed times the curve as it stood at the end of that step.
    const std::vector<CurveKeyF>* easeKeys =
        batch.eases && batch.program && batch.program->speedOverLife.isCurve() && p.life > 0.0f
            ? &batch.program->speedOverLife.keys
            : nullptr;
    int next = 0;  // the next sample is taken this many steps back
    for (int s = 0; s <= steps && count <= samples; ++s) {
        if (s == next) {
            float px = 0, py = 0, sc = 1;
            if (!project(camera, x, y, z, px, py, sc)) {
                break;
            }
            const float d = camera::depthOf(camera, x, y, z);
            points[count++] = Point{px, py, sc, flat ? d : 1.0f / d};
            next = static_cast<int>(std::lround(static_cast<double>(count) * steps / samples));
        }
        // Undo one step: the turn about the layer's centre first, because
        // it was made last.
        if (batch.orbit != 0.0f && batch.ground && !flat) {
            // About the upright, the other way.
            const float cs = batch.orbitCos, sn = batch.orbitSin;
            const float rx = x - batch.centerX, rz = z - batch.centerZ;
            x = batch.centerX + (rx * cs - rz * sn);
            z = batch.centerZ + (rz * cs + rx * sn);
            const float wx = vx, wz = vz;
            vx = wx * cs - wz * sn;
            vz = wz * cs + wx * sn;
        } else if (batch.orbit != 0.0f) {
            const float cs = batch.orbitCos, sn = batch.orbitSin;
            const float rx = x - batch.centerX, ry = y - batch.centerY;
            x = batch.centerX + (rx * cs + ry * sn);
            y = batch.centerY + (ry * cs - rx * sn);
            const float wx = vx, wy = vy;
            vx = wx * cs + wy * sn;
            vy = wy * cs - wx * sn;
        }
        float go = h;
        if (batch.eases) {
            float m = p.ease;
            if (easeKeys) {
                const float lived = (p.age - static_cast<float>(s) * h) / p.life;
                m = evalCurve(*easeKeys, lived < 0.0f ? 0.0f : (lived > 1.0f ? 1.0f : lived));
            }
            go = m * h;
        }
        x -= vx * go;
        y -= vy * go;
        z -= vz * go;
        if (batch.turbulence > 0.0f) {
            // The step also carried the particle along the swirls as they
            // were, where it was, when the step began. Where it was is what
            // is being worked out, so it is closed in on: each guess gives
            // a better one, and three are plenty for so small a step.
            const double began = static_cast<double>(shownStep - 1 - s) * static_cast<double>(h);
            float ox = x, oy = y, oz = z;
            for (int guess = 0; guess < 3; ++guess) {
                const det::Vec3f w = det::turbulence(batch.turbulenceKey, batch.turbulencePerUnit, batch.turbulenceChange,
                                                     flat, ox, oy, oz, began);
                ox = x - w.x * batch.turbulence * h;
                oy = y - w.y * batch.turbulence * h;
                oz = z - w.z * batch.turbulence * h;
            }
            x = ox;
            y = oy;
            z = flat ? 0.0f : oz;
        }
        vx = vx * (1.0f + batch.drag * h) - batch.gravityX * h;
        vy = vy * (1.0f + batch.drag * h) - batch.gravityY * h;
        vz = vz * (1.0f + batch.drag * h) - batch.gravityZ * h;
    }
    if (count < 2) {
        return;
    }

    // Across the ribbon at each point: the average of the two neighbouring
    // directions, so segments meet without gaps.
    float nx[25], ny[25];
    for (int i = 0; i < count; ++i) {
        const Point& a = points[i > 0 ? i - 1 : 0];
        const Point& b = points[i + 1 < count ? i + 1 : count - 1];
        float dx = b.x - a.x, dy = b.y - a.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length > 1e-4f) {
            dx /= length;
            dy /= length;
        } else {
            dx = 1.0f;
            dy = 0.0f;
        }
        nx[i] = -dy;
        ny[i] = dx;
    }

    // A particle being eaten away takes its trail with it.
    const float left = 1.0f - (p.dissolve > 1.0f ? 1.0f : (p.dissolve > 0.0f ? p.dissolve : 0.0f));
    const float alpha = (p.a > 1.0f ? 1.0f : p.a) * left;
    const float strength = alpha * glow;
    const float cr = toScreen(p.r) * strength, cg = toScreen(p.g) * strength, cb = toScreen(p.b) * strength;
    const float last = static_cast<float>(count - 1);
    auto corner = [&](int i, float side) {
        const float t = static_cast<float>(i) / last;  // 0 at the particle, 1 at the end
        // Swells out of the particle, then narrows to nothing at the end.
        const float swell = std::sqrt(std::min(1.0f, t / 0.12f + 0.15f));
        const float width = 0.5f * batch.trailWidth * p.size * points[i].scale * swell *
                            std::pow(1.0f - t, 0.8f);
        const float fade = 1.0f - t;
        SpriteVertex v;
        v.x = points[i].x + nx[i] * width * side;
        v.y = points[i].y + ny[i] * width * side;
        v.u = t;
        v.v = side > 0.0f ? 0.0f : 1.0f;
        v.r = cr * fade;
        v.g = cg * fade;
        v.b = cb * fade;
        v.a = additive ? 0.0f : alpha * fade;
        v.shape = kRibbonShape;
        v.depth = points[i].depth;
        const float across = 2.0f * (width > 0.25f ? width : 0.25f);
        v.aaX = 1.0f;
        v.aaY = 2.0f / across;
        return v;
    };
    for (int i = 0; i + 1 < count; ++i) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(corner(i, 1.0f));
        mesh.vertices.push_back(corner(i, -1.0f));
        mesh.vertices.push_back(corner(i + 1, -1.0f));
        mesh.vertices.push_back(corner(i + 1, 1.0f));
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

}  // namespace

bool projectPoint(const View& view, bool flat, float x, float y, float z, float& px, float& py,
                  float& scale) {
    return project(makeCamera(view, flat), x, y, flat ? 0.0f : z, px, py, scale);
}

void buildSpriteMesh(const RenderFrame& frame, const View& view, SpriteMesh& mesh,
                     const ImageSet* images) {
    mesh.vertices.clear();
    mesh.indices.clear();
    mesh.runs.clear();
    mesh.drawn = 0;
    if (!(view.width > 0.0f) || !(view.height > 0.0f)) {
        return;
    }
    const Camera camera = makeCamera(view, frame.flat);

    static const float cornerX[4] = {-1.0f, 1.0f, 1.0f, -1.0f};
    static const float cornerY[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
    static const float cornerU[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    static const float cornerV[4] = {1.0f, 1.0f, 0.0f, 0.0f};

    for (const RenderBatch& batch : frame.batches) {
        const bool additive = batch.blend == BlendMode::Additive;
        const bool onPlane = !frame.flat && batch.facing == Facing::Plane;
        const float glow = batch.glow > 0.0f ? batch.glow : 0.0f;
        const float lean = frame.flat && !batch.alongMotion && batch.tilt > 0.0f
                               ? std::cos(std::min(batch.tilt, 89.0f) * 3.14159265f / 180.0f)
                               : 1.0f;
        const Image* picture =
            images && batch.texture.valid() ? images->find(batch.texture) : nullptr;
        const auto shape = picture ? kPictureShape : static_cast<float>(batch.shape);
        const int columns = batch.columns > 0 ? batch.columns : 1;
        const int rows = batch.rows > 0 ? batch.rows : 1;
        // Half a pixel in from each cell's edge, so smoothing never picks up
        // the neighbouring picture.
        const float insetU = picture ? 0.5f / static_cast<float>(picture->width) : 0.0f;
        const float insetV = picture ? 0.5f / static_cast<float>(picture->height) : 0.0f;
        const auto runStart = static_cast<std::uint32_t>(mesh.indices.size());

        // In 3D a layer of ordinary (not glowing) particles is drawn far
        // ones first, so a near one is never covered by one behind it.
        // Glowing particles add their light, so their order does not show,
        // and in 2D every particle is equally far: both keep birth order.
        thread_local std::vector<std::pair<float, std::uint32_t>> order;
        const bool sorted = !frame.flat && !additive && batch.count > 1;
        if (sorted) {
            order.clear();
            for (std::uint32_t n = 0; n < batch.count; ++n) {
                const SpriteInstance& q = frame.instances[batch.first + n];
                order.emplace_back(camera::depthOf(camera, q.x, q.y, q.z), n);
            }
            std::stable_sort(order.begin(), order.end(),
                             [](const auto& a, const auto& b) { return a.first > b.first; });
        }

        for (std::uint32_t drawn = 0; drawn < batch.count; ++drawn) {
            const std::uint32_t n = sorted ? order[drawn].second : drawn;
            const SpriteInstance& original = frame.instances[batch.first + n];
            if (!(original.a > 0.0f) || !(original.size > 0.0f)) {
                continue;
            }
            const float z = frame.flat ? 0.0f : original.z;
            float px = 0, py = 0, scale = 1;
            if (!project(camera, original.x, original.y, z, px, py, scale)) {
                continue;
            }
            const SpriteInstance& p = original;
            // Its depth, as every corner of it facing the camera has it.
            const float centreDepth = camera::depthOf(camera, p.x, p.y, z);
            const float centreKey = frame.flat ? centreDepth : 1.0f / centreDepth;
            const float half = p.size * 0.5f;
            const float halfPx = half * scale;

            if (batch.trail > 0.0f && batch.trailWidth > 0.0f && batch.step > 0.0f) {
                addTrail(mesh, camera, frame.flat, batch, p, glow, additive, frame.step);
            }

            // The particle's own two axes, as steps across the screen in
            // pixels: axisX runs from its centre to the middle of its right
            // edge, axisY to the middle of its top edge.
            const float cs = std::cos(p.rotation), sn = std::sin(p.rotation);
            float axX = cs * halfPx, axY = -sn * halfPx;
            float ayX = -sn * halfPx, ayY = -cs * halfPx;

            // Squash and stretch: longer one way and thinner the other, by
            // amounts that keep the area. `along` is the particle's own
            // upright, or its direction of travel when it points that way.
            float along = 1.0f, across = 1.0f;
            if (batch.squashStretches && p.squashStretch != 0.0f && p.squashStretch > -64.0f &&
                p.squashStretch < 64.0f) {
                stretchFactors(p.squashStretch, along, across);
            }

            if (batch.alongMotion && !onPlane) {
                // Where it will be a moment from now says which way it is
                // heading on screen, and how fast.
                const float moment = 1.0f / 120.0f;
                float qx = 0, qy = 0, qs = 1;
                // Its own speed (as fast as Speed over Life has it going
                // just now), the turn about the layer's centre, and whatever
                // the swirls are adding.
                const float pace = batch.eases ? p.ease : 1.0f;
                float goX = p.vx * pace, goY = p.vy * pace, goZ = p.vz * pace;
                if (batch.orbit != 0.0f && batch.ground && !frame.flat) {
                    goX += batch.orbit * (p.z - batch.centerZ);
                    goZ -= batch.orbit * (p.x - batch.centerX);
                } else if (batch.orbit != 0.0f) {
                    goX -= batch.orbit * (p.y - batch.centerY);
                    goY += batch.orbit * (p.x - batch.centerX);
                }
                if (batch.turbulence > 0.0f) {
                    const det::Vec3f w = det::turbulence(batch.turbulenceKey, batch.turbulencePerUnit,
                                                         batch.turbulenceChange, frame.flat, p.x, p.y, z,
                                                         static_cast<double>(batch.hold > 0.0f ? batch.shownStep : frame.step) *
                                                             static_cast<double>(batch.step));
                    goX += w.x * batch.turbulence;
                    goY += w.y * batch.turbulence;
                    goZ += w.z * batch.turbulence;
                }
                if (project(camera, p.x + goX * moment, p.y + goY * moment,
                            frame.flat ? 0.0f : z + goZ * moment, qx, qy, qs)) {
                    const float sx = (qx - px) / moment, sy = (qy - py) / moment;
                    const float speed = std::sqrt(sx * sx + sy * sy);
                    if (speed > 1e-3f) {
                        const float dx = sx / speed, dy = sy / speed;
                        const float length = halfPx * along + 0.5f * batch.stretch * speed;
                        axX = dx * length;
                        axY = dy * length;
                        ayX = dy * halfPx * across;
                        ayY = -dx * halfPx * across;
                        along = across = 1.0f;  // done
                    }
                }
            }
            if (along != 1.0f || across != 1.0f) {
                axX *= across;
                axY *= across;
                ayX *= along;
                ayY *= along;
            }

            if (lean < 1.0f) {
                // Lying back: the sprite has already turned in its own plane,
                // and that plane is now seen from an angle, which only
                // shortens whatever runs up and down the screen.
                axY *= lean;
                ayY *= lean;
            }

            const float lenX = std::sqrt(axX * axX + axY * axY);
            const float lenY = std::sqrt(ayX * ayX + ayY * ayY);
            const float reach = (lenX + lenY) * 1.05f;
            if (px + reach < 0.0f || px - reach > view.width || py + reach < 0.0f ||
                py - reach > view.height) {
                continue;
            }

            const float alpha = p.a > 1.0f ? 1.0f : p.a;
            const float strength = alpha * glow;
            SpriteVertex v;
            v.r = toScreen(p.r) * strength;
            v.g = toScreen(p.g) * strength;
            v.b = toScreen(p.b) * strength;
            v.a = additive ? 0.0f : alpha;
            v.shape = shape;
            v.aaX = 1.0f / (lenX > 0.25f ? lenX : 0.25f);
            v.aaY = 1.0f / (lenY > 0.25f ? lenY : 0.25f);
            v.dissolve = batch.dissolves ? p.dissolve : 0.0f;
            v.seed = batch.dissolves ? p.seed : 0.0f;
            v.edge = batch.dissolveEdge;
            v.soft = batch.dissolveSoft;

            int cellX = 0, cellY = 0;
            if (picture) {
                const int cell = std::clamp(static_cast<int>(p.frame), 0, columns * rows - 1);
                cellX = cell % columns;
                cellY = cell / columns;
            }
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            bool visible = true;
            float planeAlong = 1.0f, planeAcross = 1.0f;
            if (onPlane && along != across) {
                planeAlong = along;
                planeAcross = across;
            }
            for (int k = 0; k < 4; ++k) {
                if (onPlane) {
                    // Lying flat on the effect's own plane: each corner is a
                    // real point in the world and is projected by itself.
                    const float ownX = cornerX[k] * planeAcross, ownY = cornerY[k] * planeAlong;
                    const float ox = (ownX * cs - ownY * sn) * half;
                    const float oy = (ownX * sn + ownY * cs) * half;
                    float s = 1;
                    // Upright, the particle's own up is the world's; on the
                    // ground it points away from the front.
                    const bool down = batch.ground;
                    const float wx = p.x + ox, wy = down ? p.y : p.y + oy, wz = down ? p.z - oy : p.z;
                    if (!project(camera, wx, wy, wz, v.x, v.y, s)) {
                        visible = false;
                        break;
                    }
                    v.depth = 1.0f / camera::depthOf(camera, wx, wy, wz);
                } else {
                    v.x = px + cornerX[k] * axX + cornerY[k] * ayX;
                    v.y = py + cornerX[k] * axY + cornerY[k] * ayY;
                    v.depth = centreKey;
                }
                if (picture) {
                    v.u = (static_cast<float>(cellX) + (cornerU[k] > 0.5f ? 1.0f : 0.0f)) /
                              static_cast<float>(columns) +
                          (cornerU[k] > 0.5f ? -insetU : insetU);
                    v.v = (static_cast<float>(cellY) + (cornerV[k] > 0.5f ? 1.0f : 0.0f)) /
                              static_cast<float>(rows) +
                          (cornerV[k] > 0.5f ? -insetV : insetV);
                } else {
                    v.u = cornerU[k];
                    v.v = cornerV[k];
                }
                v.ownX = cornerX[k];
                v.ownY = cornerY[k];
                mesh.vertices.push_back(v);
            }
            if (!visible) {
                mesh.vertices.resize(base);
                continue;
            }
            mesh.indices.insert(mesh.indices.end(),
                                {base, base + 1, base + 2, base, base + 2, base + 3});
            ++mesh.drawn;
        }

        const auto runEnd = static_cast<std::uint32_t>(mesh.indices.size());
        if (runEnd == runStart) {
            continue;
        }
        // A library shape, or any shape that has been changed, is drawn
        // from its own picture, which stands in the list under an ID of its
        // own (see ShapeLibrary.h).
        const int shapeNumber = static_cast<int>(batch.shape);
        const Id texture = picture ? batch.texture
                           : drawnFromPicture(shapeNumber, batch.shapeEdit)
                                 ? shapeTextureId(shapeNumber, batch.shapeEdit)
                                 : Id{};
        if (!mesh.runs.empty() && mesh.runs.back().texture == texture && mesh.runs.back().blend == batch.blend) {
            mesh.runs.back().indexCount += runEnd - runStart;
        } else {
            mesh.runs.push_back(SpriteRun{texture, runStart, runEnd - runStart, batch.blend});
        }
    }
}

}  // namespace vfx::editor
