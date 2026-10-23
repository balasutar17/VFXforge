#include "vfx/editor/MeshVfx.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "LayerMake.h"
#include "EffectTools.h"

namespace vfx::editor {

namespace {

using namespace make;

constexpr float kPi = 3.14159265358979323846f;

const char* const kEdgeOutline = "Edge outline";
const char* const kEdgeCore = "Edge core";
const char* const kOutlineGlow = "Outline glow";
const char* const kSilhouette = "Silhouette outline";
const char* const kCoat = "Energy coat";
const char* const kRipples = "Surface ripples";
const char* const kBands = "Scan bands";
const char* const kBandGlow = "Scan glow";
const char* const kMotes = "Surface motes";
const char* const kDisc = "Ground disc";
const char* const kRing = "Base ring";
const char* const kOuterRing = "Outer ring";
const char* const kRingLight = "Ring light";
const char* const kBeams = "Energy beams";
const char* const kGroundMotes = "Ground motes";

struct Sizes {
    std::size_t proxyTriangles, segments, samples;
};

Sizes sizesFor(MeshQuality q) {
    switch (q) {
        case MeshQuality::Preview: return {1500, 1000, 6000};
        case MeshQuality::High: return {7900, 7900, 40000};
        default: return {4000, 3000, 16000};
    }
}

float totalLength(const std::vector<MeshSegment>& s) {
    float total = 0;
    for (const MeshSegment& g : s) {
        const float dx = g.v[3] - g.v[0], dy = g.v[4] - g.v[1], dz = g.v[5] - g.v[2];
        total += std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    return total;
}

// An ellipse round the footprint, `scale` times the smallest rectangle's own
// size, as points on the ground plane (x, then minus z: see the Mesh and Path
// shapes' Plane setting).
PointList ellipseAround(const Footprint& f, double scale, int points) {
    PointList out;
    const float a = std::max(f.halfA, 0.05f) * 1.4142136f * static_cast<float>(scale);
    const float b = std::max(f.halfB, 0.05f) * 1.4142136f * static_cast<float>(scale);
    // Axis A in the x-z plane: `turn` is counter-clockwise from above.
    const float ang = -f.turn * kPi / 180.0f;
    const float ax = std::cos(ang), az = std::sin(ang);
    for (int i = 0; i < points; ++i) {
        const float t = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(points);
        const float lx = a * std::cos(t), lz = b * std::sin(t);
        const float x = f.cx + lx * ax - lz * az;
        const float z = f.cz + lx * az + lz * ax;
        out.points.push_back(Point2{x, -z});
    }
    return out;
}

// A bright line along edges, as soft dots close together (see glowLine).
Make& edgeLine(Make& m, unsigned colour, double thickness, double length, double alpha, double glow) {
    const double life = 0.12;
    const double alive = std::max(8.0, length / (thickness * 0.22));
    return m.rate(std::min(60000.0, std::round(alive / life))).life(life * 0.8, life * 1.2).speed(0, 0)
        .size(thickness * 3.4, thickness * 3.8).color(colour, alpha * 0.55).gravity(0, 0).additive().glow(glow)
        .fade({{0, 0.5}, {0.4, 1}, {1, 0}});
}

struct Fit {
    WorldMesh mesh;
    MeshReport report;
    std::vector<MeshTriangle> proxy;
    std::vector<MeshSegment> edges, silhouette;
    Sizes sizes{};
    float centreX = 0, centreY = 0, centreZ = 0, height = 1, extent = 1;
    float cameraX = 0, cameraY = 0, cameraZ = 0;
};

Result<Fit> fit(const Model& model, const ModelPlace& place, const View& camera, const MeshVfxOptions& o) {
    Fit f;
    f.mesh = toWorld(model, place);
    if (f.mesh.triangleCount() == 0) {
        return makeError("There is no model to make the effect on.", "no model triangles");
    }
    f.report = analyseMesh(f.mesh);
    f.sizes = sizesFor(o.quality);
    f.proxy = surfaceProxy(f.mesh, f.sizes.proxyTriangles);
    f.edges = featureEdges(f.mesh, o.edgeAngle, f.sizes.segments);
    cameraPosition(camera, f.cameraX, f.cameraY, f.cameraZ);
    f.silhouette = silhouetteEdges(f.mesh, f.cameraX, f.cameraY, f.cameraZ, f.sizes.segments);
    f.centreX = 0.5f * (f.mesh.minX + f.mesh.maxX);
    f.centreY = 0.5f * (f.mesh.minY + f.mesh.maxY);
    f.centreZ = 0.5f * (f.mesh.minZ + f.mesh.maxZ);
    f.height = std::max(f.mesh.maxY - f.mesh.minY, 0.01f);
    f.extent = std::max({f.mesh.maxX - f.mesh.minX, f.height, f.mesh.maxZ - f.mesh.minZ});
    return f;
}

// Where a ripple starts: on the side of the model that faces the camera.
Vec3 rippleOrigin(const Fit& f) {
    float dx = f.cameraX - f.centreX, dy = f.cameraY - f.centreY, dz = f.cameraZ - f.centreZ;
    const float l = std::max(std::sqrt(dx * dx + dy * dy + dz * dz), 1e-4f);
    const float r = 0.5f * f.extent;
    return Vec3{f.centreX + dx / l * r, f.centreY + dy / l * r * 0.3f, f.centreZ + dz / l * r};
}

Layer* findLayer(Effect& e, const char* name) {
    for (Layer& l : e.layers) {
        if (l.name == name) return &l;
    }
    return nullptr;
}

void setShape(Layer& l, const char* key, Value v) {
    for (Module& m : l.modules) {
        if (m.type == "shape") {
            if (Value* slot = m.find(key)) *slot = std::move(v);
        }
    }
}

}  // namespace

Result<MeshVfxResult> generateOnModel(const Model& model, const ModelPlace& place, const View& camera,
                                      const MeshVfxOptions& o, const IdSource& id) {
    Result<Fit> made = fit(model, place, camera, o);
    if (!made.ok()) {
        return made.error();
    }
    const Fit& f = made.value();
    MeshVfxResult out;
    out.report = f.report;
    out.warnings = f.report.warnings;
    out.proxyTriangles = f.proxy.size();
    out.edgeSegments = f.edges.size();
    out.silhouetteSegments = f.silhouette.size();
    if (f.proxy.empty()) {
        return makeError("The model has no surface to work from.", "empty surface proxy");
    }
    if (f.mesh.triangleCount() > f.proxy.size() && f.mesh.triangleCount() > f.sizes.proxyTriangles) {
        out.warnings.push_back("The model has " + std::to_string(f.mesh.triangleCount()) + " triangles; effects follow a simplified copy of " +
                               std::to_string(f.proxy.size()) + " (fine detail is blunted). Use High quality to keep more.");
    }

    Effect e = begin3d(id, "Mesh VFX", 4.0);
    const double H = f.height;
    const double t = std::clamp(H * 0.012, 0.015, 0.06);   // line thickness
    const double spacing = std::sqrt(f.report.area / static_cast<double>(f.sizes.samples));
    const double lenEdges = totalLength(f.edges), lenSil = totalLength(f.silhouette);
    const PointList surface = packTriangles(f.proxy);
    const PointList hard = packSegments(f.edges);
    const PointList sil = packSegments(f.silhouette);
    const Vec3 origin = rippleOrigin(f);
    const std::int64_t N = static_cast<std::int64_t>(f.sizes.samples);
    const double range = std::max<double>(f.height, 0.01);

    if (o.outline && !f.edges.empty()) {
        Make glowM(id, kOutlineGlow);
        add(e, edgeLine(glowM.mesh("edges", hard, N), o.glow, t * 3.5, lenEdges, 0.14, 1.0).surfaceOffset(t * 0.5));
        Make line(id, kEdgeOutline);
        add(e, edgeLine(line.mesh("edges", hard, N), o.primary, t, lenEdges, 0.75, 1.6).surfaceOffset(t * 0.5));
        Make core(id, kEdgeCore);
        add(e, edgeLine(core.mesh("edges", hard, N), o.highlight, t * 0.4, lenEdges, 0.7, 1.4).surfaceOffset(t * 0.5));
    }
    if (o.silhouette && !f.silhouette.empty()) {
        Make line(id, kSilhouette);
        add(e, edgeLine(line.mesh("silhouette", sil, N), o.primary, t * 0.9, lenSil, 0.85, 1.6).surfaceOffset(t * 0.5));
    }
    if (o.surface) {
        // The coat: the whole surface faintly lit, shimmering as dots come and go.
        const double dot = std::max(spacing * 7.0, 0.07 * range);
        const double covered = f.report.area / (3.14159 * dot * dot * 0.25) * 3.0;
        Make coat(id, kCoat);
        add(e, coat.mesh("surface", surface, N).rate(std::min(40000.0, covered / 0.9)).life(0.7, 1.1).speed(0, 0)
                   .size(dot, dot * 1.3).color(o.primary, 0.07).gravity(0, 0).additive().look("soft")
                   .fade({{0, 0}, {0.35, 1}, {1, 0}}).surfaceOffset(t * 0.3));
        // Ripples: broad soft rings spreading over every surface from the side facing the camera.
        const double width = 0.1 * range;
        const double bandPoints = static_cast<double>(N) * width / range * 3.0;
        Make rip(id, kRipples);
        add(e, rip.mesh("surface", surface, N).scan("ripple", 0.25, width, 3, origin)
                   .rate(std::min(40000.0, bandPoints / 4.0 / 0.25)).life(0.2, 0.3).speed(0, 0).size(dot * 0.7, dot * 0.9)
                   .color(o.highlight, 0.22).gravity(0, 0).additive().look("soft").fade({{0, 0}, {0.3, 1}, {1, 0}})
                   .surfaceOffset(t * 0.4));
    }
    if (o.bands) {
        const double width = 0.016 * range;
        const double bandPoints = static_cast<double>(N) * width / range * 4.0;
        const double dot = std::max(spacing * 1.8, t * 1.2);
        Make sg(id, kBandGlow);
        add(e, sg.mesh("surface", surface, N).scan("height", 0.25, width * 3.5, 4)
                   .rate(std::min(40000.0, bandPoints * 3.5 / 1.0 / 0.15)).life(0.12, 0.18).speed(0, 0)
                   .size(dot * 2.0, dot * 2.4).color(o.primary, 0.18).gravity(0, 0).additive().surfaceOffset(t));
        Make sb(id, kBands);
        add(e, sb.mesh("surface", surface, N).scan("height", 0.25, width, 4)
                   .rate(std::min(40000.0, bandPoints / 0.7 / 0.15)).life(0.12, 0.18).speed(0, 0)
                   .size(dot, dot * 1.2).color(o.highlight, 0.9).gravity(0, 0).additive().glow(1.6).surfaceOffset(t));
    }
    if (o.particles) {
        Make mo(id, kMotes);
        add(e, mo.mesh("surface", surface, N).rate(70).life(1.2, 2.0).speed(0.1 * H, 0.3 * H).size(t * 1.6, t * 3.2)
                   .color(o.highlight, 0.9).gravity(0, 0.1).sendOut().additive().glow(1.8).turbulence(0.1 * H, 0.3 * H, 0.5)
                   .fade({{0, 0}, {0.2, 1}, {0.8, 1}, {1, 0}}).surfaceOffset(t * 2).from(0.0, 4.0));
    }
    if (o.ground) {
        const double y = 0.01 * H;
        const PointList ring1 = ellipseAround(f.report.footprint, 1.15, 72);
        const PointList ring2 = ellipseAround(f.report.footprint, 1.55, 72);
        const Vec3 up = Vec3{0, f.report.footprint.baseY + y, 0};
        const double r1 = std::max(f.report.footprint.halfA, f.report.footprint.halfB) * 1.4142 * 1.55;
        Make disc(id, kDisc);
        add(e, disc.path(ring2).fillPath().ground().burst(2400).life(4.0, 4.0).speed(0, 0).size(r1 * 0.2, r1 * 0.26)
                   .color(o.primary, 0.03).gravity(0, 0).additive().solid().flatOnPlane().look("soft")
                   .put("shape", "offset", up));
        Make ringM(id, kRing);
        add(e, edgeLine(ringM.path(ring1).ground().put("shape", "offset", up), o.primary, t * 1.6,
                        2.0 * 3.14159 * r1 * 0.75, 0.7, 1.5).flatOnPlane());
        Make outer(id, kOuterRing);
        add(e, edgeLine(outer.path(ring2).ground().put("shape", "offset", up), o.glow, t * 1.1,
                        2.0 * 3.14159 * r1, 0.6, 1.3).flatOnPlane());
        Make light(id, kRingLight);
        add(e, light.path(ring2).ground().put("shape", "offset", up).travel(0.25, 0.08, 3).rate(220).life(0.1, 0.16)
                   .speed(0, 0).size(t * 5, t * 7).color(o.highlight, 0.9).gravity(0, 0).additive().glow(1.8));
        Make beams(id, kBeams);
        add(e, beams.path(ring1).ground().put("shape", "offset", up).rate(12).life(0.9, 1.4).speed(0.9 * H, 1.2 * H)
                   .toward(0, 1, 0, 0).size(t * 2.0, t * 3.0).color(o.primary, 0.8).gravity(0, 0).look("beam").additive()
                   .glow(1.4).streak(0.4).fade({{0, 0}, {0.2, 1}, {0.7, 0.8}, {1, 0}}));
        Make gm(id, kGroundMotes);
        add(e, gm.path(ring2).fillPath().ground().put("shape", "offset", up).rate(24).life(1.0, 1.8).speed(0.2 * H, 0.5 * H)
                   .toward(0, 1, 0, 10).size(t * 1.4, t * 2.8).color(o.highlight, 0.9).gravity(0, 0.05).additive().glow(1.8)
                   .fade({{0, 0}, {0.2, 1}, {0.8, 1}, {1, 0}}));
    }
    tools::smoothLoop(e);
    out.effect = std::move(e);
    return out;
}

bool hasMeshLayers(const Effect& effect) {
    for (const Layer& l : effect.layers) {
        for (const Module& m : l.modules) {
            if (m.type == "shape") {
                const Value* v = m.find("shape");
                const auto* s = v ? std::get_if<std::string>(v) : nullptr;
                if (s && *s == "mesh") return true;
            }
        }
    }
    return false;
}

std::size_t fitToModel(Effect& effect, const Model& model, const ModelPlace& place, const View& camera,
                       const MeshVfxOptions& o, std::vector<std::string>* warnings) {
    Result<Fit> made = fit(model, place, camera, o);
    if (!made.ok()) {
        return 0;
    }
    const Fit& f = made.value();
    if (warnings) {
        *warnings = f.report.warnings;
    }
    std::size_t fitted = 0;
    const PointList surface = packTriangles(f.proxy), hard = packSegments(f.edges), sil = packSegments(f.silhouette);
    const Vec3 origin = rippleOrigin(f);
    for (const char* name : {kEdgeOutline, kEdgeCore, kOutlineGlow}) {
        if (Layer* l = findLayer(effect, name)) {
            setShape(*l, "meshData", hard);
            ++fitted;
        }
    }
    if (Layer* l = findLayer(effect, kSilhouette)) {
        setShape(*l, "meshData", sil);
        ++fitted;
    }
    for (const char* name : {kCoat, kRipples, kBands, kBandGlow, kMotes}) {
        if (Layer* l = findLayer(effect, name)) {
            setShape(*l, "meshData", surface);
            if (std::string(name) == kRipples) setShape(*l, "scanOrigin", origin);
            ++fitted;
        }
    }
    const double y = 0.01 * f.height;
    const Vec3 up = Vec3{0, f.report.footprint.baseY + y, 0};
    const PointList ring1 = ellipseAround(f.report.footprint, 1.15, 72), ring2 = ellipseAround(f.report.footprint, 1.55, 72);
    for (const auto& pair : {std::pair<const char*, const PointList*>{kRing, &ring1}, {kBeams, &ring1}, {kOuterRing, &ring2},
                             {kRingLight, &ring2}, {kDisc, &ring2}, {kGroundMotes, &ring2}}) {
        if (Layer* l = findLayer(effect, pair.first)) {
            setShape(*l, "path", *pair.second);
            setShape(*l, "offset", up);
            ++fitted;
        }
    }
    return fitted;
}

}  // namespace vfx::editor
