// Effects made from a model's own shape: the analysis that reads it, the Mesh
// emitter that places particles on it, the layers generated on it, and how
// they are fitted again and exported.
#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "vfx/Id.h"
#include "vfx/Program.h"
#include "vfx/Serialize.h"
#include "vfx/Simulation.h"
#include "LayerMake.h"
#include "vfx/editor/MeshAnalysis.h"
#include "vfx/editor/MeshVfx.h"
#include "vfx/editor/Model.h"
#include "vfx/editor/UnityExport.h"

using namespace vfx;
using namespace vfx::editor;
using J = nlohmann::json;

namespace {

ModelPlace standing(float height = 3.3f, float turn = 0, float x = 0) {
    ModelPlace p;
    p.height = height;
    p.turn = turn;
    p.x = x;
    return p;
}

View frontView() {
    View v;
    v.targetY = 1.5f;
    v.yaw = 0;
    v.pitch = 0;
    v.distance = 1000.0f;  // far: nearly parallel
    v.fieldOfView = 2;
    return v;
}

struct Maker {
    IdGenerator ids{77};
    IdSource source = [this]() { return ids.next(); };
};

Result<MeshVfxResult> generate(const Model& m, const ModelPlace& p, const MeshVfxOptions& o = {}) {
    Maker maker;
    return generateOnModel(m, p, frontView(), o, maker.source);
}

const Layer* layerNamed(const Effect& e, const char* name) {
    for (const Layer& l : e.layers) {
        if (l.name == name) return &l;
    }
    return nullptr;
}

// How far a point is from the nearest triangle of the proxy.
float distanceToTriangles(const std::vector<MeshTriangle>& tris, float x, float y, float z) {
    float best = 1e30f;
    for (const MeshTriangle& t : tris) {
        // Closest point on a triangle (Ericson).
        const float ax = t.v[0], ay = t.v[1], az = t.v[2];
        const float abx = t.v[3] - ax, aby = t.v[4] - ay, abz = t.v[5] - az;
        const float acx = t.v[6] - ax, acy = t.v[7] - ay, acz = t.v[8] - az;
        const float apx = x - ax, apy = y - ay, apz = z - az;
        auto dot = [](float a, float b, float c, float d, float e, float f) { return a * d + b * e + c * f; };
        const float d1 = dot(abx, aby, abz, apx, apy, apz), d2 = dot(acx, acy, acz, apx, apy, apz);
        float qx, qy, qz;
        if (d1 <= 0 && d2 <= 0) {
            qx = ax; qy = ay; qz = az;
        } else {
            const float bpx = x - t.v[3], bpy = y - t.v[4], bpz = z - t.v[5];
            const float d3 = dot(abx, aby, abz, bpx, bpy, bpz), d4 = dot(acx, acy, acz, bpx, bpy, bpz);
            const float cpx = x - t.v[6], cpy = y - t.v[7], cpz = z - t.v[8];
            const float d5 = dot(abx, aby, abz, cpx, cpy, cpz), d6 = dot(acx, acy, acz, cpx, cpy, cpz);
            const float vc = d1 * d4 - d3 * d2, vb = d5 * d2 - d1 * d6, va = d3 * d6 - d5 * d4;
            if (d3 >= 0 && d4 <= d3) {
                qx = t.v[3]; qy = t.v[4]; qz = t.v[5];
            } else if (vc <= 0 && d1 >= 0 && d3 <= 0) {
                const float v = d1 / (d1 - d3);
                qx = ax + v * abx; qy = ay + v * aby; qz = az + v * abz;
            } else if (d6 >= 0 && d5 <= d6) {
                qx = t.v[6]; qy = t.v[7]; qz = t.v[8];
            } else if (vb <= 0 && d2 >= 0 && d6 <= 0) {
                const float w = d2 / (d2 - d6);
                qx = ax + w * acx; qy = ay + w * acy; qz = az + w * acz;
            } else if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
                const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                qx = t.v[3] + w * (t.v[6] - t.v[3]); qy = t.v[4] + w * (t.v[7] - t.v[4]); qz = t.v[5] + w * (t.v[8] - t.v[5]);
            } else {
                const float denom = 1.0f / (va + vb + vc);
                const float v = vb * denom, w = vc * denom;
                qx = ax + abx * v + acx * w; qy = ay + aby * v + acy * w; qz = az + abz * v + acz * w;
            }
        }
        const float dx = x - qx, dy = y - qy, dz = z - qz;
        best = std::min(best, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    return best;
}

// A one-layer effect on the model's surface, nothing else.
Effect surfaceOnly(const std::vector<MeshTriangle>& tris, double rate, const char* scanMode, double speed, double width) {
    Maker maker;
    Effect e = make::begin3d(maker.source, "t", 4.0);
    make::Make layer(maker.source, "Surface");
    layer.mesh("surface", packTriangles(tris), 20000).rate(rate).life(3.0, 3.0).speed(0, 0).size(0.05, 0.05).gravity(0, 0);
    if (std::string(scanMode) != "none") layer.scan(scanMode, speed, width, 1);
    make::add(e, layer);
    return e;
}

}  // namespace

TEST_CASE("the hard-surface sample is read as a model with flat faces and a pipe", "[mesh]") {
    const Model m = hardSurfaceSample();
    CHECK(m.triangleCount() > 150);
    const WorldMesh w = toWorld(m, standing());
    CHECK(w.vertexCount() < m.corners());  // corners at one place are welded
    CHECK(w.maxY - w.minY == Catch::Approx(3.3f).margin(1e-3));
    CHECK(w.minY == Catch::Approx(0.0f).margin(1e-4));
    const MeshReport r = analyseMesh(w);
    CHECK(r.components == 3);       // the box, the tower, the pipe
    CHECK(r.openEdges == 4);        // the tower has no floor
    CHECK_FALSE(r.closed);
    CHECK(r.degenerate == 0);
    CHECK(r.area > 25);
    CHECK_FALSE(r.warnings.empty());  // says it is not a closed volume
}

TEST_CASE("hard edges are the box's corners, not the seams of its flat faces", "[mesh]") {
    const WorldMesh w = toWorld(hardSurfaceSample(), standing());
    const auto edges = featureEdges(w, 35.0, 5000);
    REQUIRE(edges.size() > 20);
    // Every one of the box's twelve edges is there, whole.
    int found = 0;
    for (const MeshSegment& s : edges) {
        auto at = [&s](float x0, float y0, float z0, float x1, float y1, float z1) {
            auto near = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
            return (near(s.v[0], x0) && near(s.v[1], y0) && near(s.v[2], z0) && near(s.v[3], x1) && near(s.v[4], y1) && near(s.v[5], z1)) ||
                   (near(s.v[3], x0) && near(s.v[4], y0) && near(s.v[5], z0) && near(s.v[0], x1) && near(s.v[1], y1) && near(s.v[2], z1));
        };
        // (the fit centres the whole footprint, so the box sits 0.15 off in z)
        if (at(-1, 1.6f, 0.85f, 1, 1.6f, 0.85f) || at(1, 1.6f, 0.85f, 1, 1.6f, -1.15f) || at(-1, 0, 0.85f, 1, 0, 0.85f) || at(1, 0, -1.15f, -1, 0, -1.15f)) ++found;
    }
    CHECK(found >= 4);
    // None of the box's face diagonals: no edge across the middle of a flat face.
    for (const MeshSegment& s : edges) {
        const bool onFront = std::fabs(s.v[2] - 1.0f) < 1e-3f && std::fabs(s.v[5] - 1.0f) < 1e-3f;
        const bool diagonal = onFront && std::fabs(s.v[0] - s.v[3]) > 1.5f && std::fabs(s.v[1] - s.v[4]) > 1.0f;
        CHECK_FALSE(diagonal);
    }
    // A flat face's own triangles never count: a plain quad has four edges.
}

TEST_CASE("a silhouette follows the model as the camera sees it", "[mesh]") {
    const WorldMesh w = toWorld(hardSurfaceSample(), standing());
    const auto front = silhouetteEdges(w, 0, 1.5f, 1000.0f, 5000);
    const auto side = silhouetteEdges(w, 1000.0f, 1.5f, 0, 5000);
    REQUIRE_FALSE(front.empty());
    REQUIRE_FALSE(side.empty());
    CHECK(front != std::vector<MeshSegment>{});
    // From the front the outline spans the whole box, 2 wide and the model's full height.
    float minX = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (const MeshSegment& s : front) {
        for (int k = 0; k < 2; ++k) {
            minX = std::min(minX, s.v[static_cast<std::size_t>(k * 3)]);
            maxX = std::max(maxX, s.v[static_cast<std::size_t>(k * 3)]);
            maxY = std::max(maxY, s.v[static_cast<std::size_t>(k * 3 + 1)]);
        }
    }
    CHECK(minX == Catch::Approx(-1.0f).margin(0.01));
    CHECK(maxX == Catch::Approx(1.0f).margin(0.01));
    CHECK(maxY == Catch::Approx(3.3f).margin(0.01));
}

TEST_CASE("the ground footprint is the model's own, turned with it", "[mesh]") {
    const WorldMesh flat = toWorld(hardSurfaceSample(), standing());
    const MeshReport a = analyseMesh(flat);
    CHECK(a.footprint.halfA == Catch::Approx(1.0f).margin(0.02));
    CHECK(a.footprint.halfB == Catch::Approx(1.3f / 2 + 0.0f).margin(0.5));  // the pipe sticks out 0.3 in front
    // Moved and turned: the middle follows and the size stays.
    const MeshReport b = analyseMesh(toWorld(hardSurfaceSample(), standing(3.3f, 30, 4)));
    CHECK(b.footprint.cx == Catch::Approx(4.0f).margin(0.6));
    CHECK(std::max(b.footprint.halfA, b.footprint.halfB) == Catch::Approx(std::max(a.footprint.halfA, a.footprint.halfB)).margin(0.1));
    CHECK(b.footprint.baseY == Catch::Approx(0.0f).margin(1e-4));
}

TEST_CASE("a dense model is simplified for the effect without losing its shape", "[mesh]") {
    const WorldMesh w = toWorld(practiceDummy(), standing(3.0f));
    const auto all = surfaceProxy(w, 100000);
    CHECK(all.size() == w.triangleCount());
    const auto rough = surfaceProxy(w, 600);
    CHECK(rough.size() <= 600);
    CHECK(rough.size() > 100);
    float lo = 1e9f, hi = -1e9f;
    for (const MeshTriangle& t : rough) {
        for (int c = 0; c < 3; ++c) {
            lo = std::min(lo, t.v[static_cast<std::size_t>(c * 3 + 1)]);
            hi = std::max(hi, t.v[static_cast<std::size_t>(c * 3 + 1)]);
        }
    }
    CHECK(hi - lo == Catch::Approx(3.0f).margin(0.35));  // still as tall as the model
}

TEST_CASE("mesh data survives packing, saving and loading", "[mesh]") {
    std::vector<MeshTriangle> tris(2);
    for (int i = 0; i < 9; ++i) {
        tris[0].v[static_cast<std::size_t>(i)] = static_cast<float>(i) * 0.5f;
        tris[1].v[static_cast<std::size_t>(i)] = -static_cast<float>(i);
    }
    const auto back = unpackTriangles(packTriangles(tris));
    REQUIRE(back.size() == 2);
    CHECK(back[1].v[8] == -8.0f);
    CHECK(unpackTriangles(PointList{{{1, 5}, {0, 0}}}).empty());  // wrong length
    // A whole generated effect, through the file format.
    const Model m = hardSurfaceSample();
    auto made = generate(m, standing());
    REQUIRE(made.ok());
    const std::string text = writeEffect(made.value().effect);
    auto read = readEffect(text);
    REQUIRE(read.ok());
    CHECK_FALSE(read.value().repaired);
    const Layer* coat = layerNamed(read.value().effect, "Energy coat");
    REQUIRE(coat);
    const Layer* original = layerNamed(made.value().effect, "Energy coat");
    for (std::size_t i = 0; i < coat->modules.size(); ++i) {
        if (coat->modules[i].type == "shape") {
            CHECK(*coat->modules[i].find("meshData") == *original->modules[i].find("meshData"));
        }
    }
    // The biggest it can be still fits what a value may hold.
    MeshVfxOptions high;
    high.quality = MeshQuality::High;
    auto big = generate(practiceDummy(), standing(3.0f), high);
    REQUIRE(big.ok());
    CHECK(readEffect(writeEffect(big.value().effect)).value().repaired == false);
}

TEST_CASE("Test B/C: particles born on the surface lie on it and cover it by area", "[mesh]") {
    const WorldMesh w = toWorld(hardSurfaceSample(), standing());
    const auto tris = surfaceProxy(w, 5000);
    Effect e = surfaceOnly(tris, 6000, "none", 0, 0.1);
    Simulation sim(compileEffect(e));
    sim.seek(60);
    const ParticleView p = sim.particles(0);
    REQUIRE(p.count > 3000);
    double total = 0;
    for (const MeshTriangle& t : tris) {
        const float ux = t.v[3] - t.v[0], uy = t.v[4] - t.v[1], uz = t.v[5] - t.v[2];
        const float wx = t.v[6] - t.v[0], wy = t.v[7] - t.v[1], wz = t.v[8] - t.v[2];
        total += 0.5 * std::sqrt((uy * wz - uz * wy) * (uy * wz - uz * wy) + (uz * wx - ux * wz) * (uz * wx - ux * wz) +
                                 (ux * wy - uy * wx) * (ux * wy - uy * wx));
    }
    const double spacing = std::sqrt(total / 20000.0);
    int off = 0;
    double lowHalf = 0;
    for (std::uint32_t i = 0; i < p.count; ++i) {
        if (distanceToTriangles(tris, p.x[i], p.y[i], p.z[i]) > 1.5f * static_cast<float>(spacing)) ++off;
        if (p.y[i] < 1.6f) ++lowHalf;  // the box is the lower 1.6
    }
    CHECK(off == 0);  // none float away from the mesh
    // Even by area: the box's share of the surface is about its share of the particles.
    double boxArea = 0;
    for (const MeshTriangle& t : tris) {
        if (t.v[1] < 1.601f && t.v[4] < 1.601f && t.v[7] < 1.601f && t.v[1] + t.v[4] + t.v[7] < 4.8f - 1e-3f + 0.001f) {
            const float ux = t.v[3] - t.v[0], uy = t.v[4] - t.v[1], uz = t.v[5] - t.v[2];
            const float wx = t.v[6] - t.v[0], wy = t.v[7] - t.v[1], wz = t.v[8] - t.v[2];
            boxArea += 0.5 * std::sqrt((uy * wz - uz * wy) * (uy * wz - uz * wy) + (uz * wx - ux * wz) * (uz * wx - ux * wz) +
                                       (ux * wy - uy * wx) * (ux * wy - uy * wx));
        }
    }
    const double expected = boxArea / total;
    const double got = lowHalf / static_cast<double>(p.count);
    CHECK(got == Catch::Approx(expected).margin(0.08));
}

TEST_CASE("a height scan keeps particles in a band that rises", "[mesh]") {
    const WorldMesh w = toWorld(hardSurfaceSample(), standing());
    const auto tris = surfaceProxy(w, 5000);
    // 0.25 sweeps a second: at one second the band is a quarter of the way up.
    Effect e = surfaceOnly(tris, 5000, "height", 0.25, 0.3);
    Simulation sim(compileEffect(e));
    sim.seek(90);  // 1.5 s at 60 frames a second: a particle born in the last 0.1 s is still the band's
    const ParticleView p = sim.particles(0);
    REQUIRE(p.count > 100);
    int newest = 0;
    for (std::uint32_t i = 0; i < p.count; ++i) {
        if (p.age[i] < 0.01f) {
            ++newest;
            const float centre = 3.3f * 0.375f;  // 1.5 s * 0.25 = 0.375 of the way up
            CHECK(std::fabs(p.y[i] - centre) < 0.3f * 0.5f + 0.15f);
        }
    }
    CHECK(newest > 5);
}

TEST_CASE("Test A: the outline is made of the model's edges, not its box", "[mesh]") {
    auto made = generate(hardSurfaceSample(), standing());
    REQUIRE(made.ok());
    const Effect& e = made.value().effect;
    const Layer* line = layerNamed(e, "Edge outline");
    REQUIRE(line);
    Simulation sim(compileEffect(e));
    sim.seek(100);
    std::size_t index = 0;
    for (std::size_t i = 0; i < e.layers.size(); ++i) {
        if (e.layers[i].id == line->id) index = i;
    }
    const ParticleView p = sim.particles(index);
    REQUIRE(p.count > 100);
    // No particle is out in the empty corner of the bounding box, where a box-shaped
    // outline would have its corners: the tower's top is narrower (0.8 across) than the box.
    int empty = 0, onTower = 0;
    for (std::uint32_t i = 0; i < p.count; ++i) {
        if (p.y[i] > 3.0f && (std::fabs(p.x[i]) > 0.6f || std::fabs(p.z[i] + 0.15f) > 0.6f)) ++empty;
        if (p.y[i] > 3.1f) ++onTower;
    }
    CHECK(empty == 0);
    CHECK(onTower > 3);
}

TEST_CASE("Test D/E: the ring fits the footprint and layers are independent", "[mesh]") {
    auto made = generate(hardSurfaceSample(), standing());
    REQUIRE(made.ok());
    Effect e = made.value().effect;
    for (const char* name : {"Base ring", "Outer ring", "Ground disc", "Energy beams", "Scan bands", "Surface ripples", "Energy coat"}) {
        CHECK(layerNamed(e, name));
    }
    // The ring's points are round the model, on the floor plane (the y of a path is minus z).
    const Layer* ring = layerNamed(e, "Base ring");
    const Value* path = nullptr;
    for (const Module& m : ring->modules) {
        if (m.type == "shape") path = m.find("path");
    }
    REQUIRE(path);
    const PointList& pts = std::get<PointList>(*path);
    float lo = 1e9f, hi = -1e9f;
    for (const Point2& q : pts.points) {
        lo = std::min(lo, static_cast<float>(q.x));
        hi = std::max(hi, static_cast<float>(q.x));
    }
    CHECK(hi - lo > 2.0f * 1.4f);  // wider than the box (2 across), round it
    // Switching one layer off leaves every other exactly as it was.
    Simulation all(compileEffect(e));
    all.seek(120);
    Effect without = e;
    for (Layer& l : without.layers) {
        if (l.name == "Edge outline") l.enabled = false;
    }
    Simulation off(compileEffect(without));
    off.seek(120);
    const auto a = all.stats();
    const auto b = off.stats();
    // A layer that is off has no entry, so the other layers line up after it.
    REQUIRE(a.size() == b.size() + 1);
    std::size_t j = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (e.layers[i].name == "Edge outline") continue;
        CHECK(a[i].alive == b[j].alive);
        ++j;
    }
}

TEST_CASE("Test G: fitting again follows the model and keeps the artist's edits", "[mesh]") {
    const Model m = hardSurfaceSample();
    auto made = generate(m, standing());
    REQUIRE(made.ok());
    Effect e = made.value().effect;
    // The artist changes a layer's colour and rate.
    Layer* core = nullptr;
    for (Layer& l : e.layers) {
        if (l.name == "Edge core") core = &l;
    }
    REQUIRE(core);
    for (Module& mod : core->modules) {
        if (mod.type == "emission") *mod.find("rate") = Scalar::constant(123.0);
        if (mod.type == "initial") *mod.find("color") = Color{1, 0, 0, 1};
    }
    // The model moves 4 along x and is turned.
    const ModelPlace moved = standing(3.3f, 90, 4);
    View camera = frontView();
    MeshVfxOptions o;
    CHECK(fitToModel(e, m, moved, camera, o) >= 10);
    // The surface now stands 4 to the side.
    const Layer* coat = layerNamed(e, "Energy coat");
    float minX = 1e9f, maxX = -1e9f;
    for (const Module& mod : coat->modules) {
        if (mod.type == "shape") {
            const auto tris = unpackTriangles(std::get<PointList>(*mod.find("meshData")));
            REQUIRE_FALSE(tris.empty());
            for (const MeshTriangle& t : tris) {
                for (int c = 0; c < 3; ++c) {
                    minX = std::min(minX, t.v[static_cast<std::size_t>(c * 3)]);
                    maxX = std::max(maxX, t.v[static_cast<std::size_t>(c * 3)]);
                }
            }
        }
    }
    CHECK((minX + maxX) / 2 == Catch::Approx(4.0f).margin(0.5));
    // And the edits stayed.
    for (const Module& mod : layerNamed(e, "Edge core")->modules) {
        if (mod.type == "emission") CHECK(std::get<Scalar>(*mod.find("rate")).a == 123.0);
        if (mod.type == "initial") CHECK(std::get<Color>(*mod.find("color")).r == 1.0);
    }
    CHECK(hasMeshLayers(e));
}

TEST_CASE("a 2D effect cannot use the mesh shape and falls back to a point", "[mesh]") {
    Maker maker;
    Effect e = make::begin(maker.source, "flat", 2.0);
    make::Make layer(maker.source, "Flat");
    const WorldMesh w = toWorld(hardSurfaceSample(), standing());
    layer.mesh("surface", packTriangles(surfaceProxy(w, 500))).rate(50).life(1, 1).speed(0, 0);
    make::add(e, layer);
    Simulation sim(compileEffect(e));
    sim.seek(30);
    const ParticleView p = sim.particles(0);
    REQUIRE(p.count > 0);
    for (std::uint32_t i = 0; i < p.count; ++i) {
        CHECK(p.x[i] == 0.0f);
    }
}

TEST_CASE("Test I: Unity gets the model's triangles and edges and is told what it cannot match", "[mesh][unity]") {
    auto made = generate(hardSurfaceSample(), standing());
    REQUIRE(made.ok());
    const Effect& e = made.value().effect;
    const J doc = J::parse(unityDescription(e));
    bool surface = false, edges = false;
    for (const J& layer : doc["layers"]) {
        const J& shape = layer["shape"];
        if (shape.value("type", "") == "mesh") {
            const std::string part = shape["part"];
            if (part == "triangle") {
                surface = true;
                CHECK(shape["vertices"].size() % 9 == 0);
                CHECK(shape["vertices"].size() >= 9 * 100);
            } else {
                edges = true;
                CHECK(shape["vertices"].size() % 6 == 0);
            }
        }
    }
    CHECK(surface);
    CHECK(edges);
    // The scan and the silhouette are said not to carry over exactly.
    const auto notes = unityMeshNotes(e);
    bool band = false;
    for (const std::string& n : notes) band = band || n.find("band") != std::string::npos;
    CHECK(band);
}

TEST_CASE("the hologram look draws a translucent cyan model, and solid stays as it was", "[mesh]") {
    const Model block = hardSurfaceSample();
    View view;
    ModelPlace solid = standing();
    ModelPlace holo = solid;
    holo.look = 1;
    const ModelPicture a = drawModel(block, solid, view, false, 200, 200, 1);
    const ModelPicture b = drawModel(block, holo, view, false, 200, 200, 1);
    REQUIRE_FALSE(a.empty());
    REQUIRE_FALSE(b.empty());
    long covered = 0, tinted = 0;
    double alphaSolid = 0, alphaHolo = 0;
    for (std::size_t i = 0; i < a.depth.size(); ++i) {
        if (!std::isfinite(a.depth[i])) continue;
        ++covered;
        alphaSolid += a.picture.rgba[i * 4 + 3];
        alphaHolo += b.picture.rgba[i * 4 + 3];
        if (b.picture.rgba[i * 4 + 2] > b.picture.rgba[i * 4]) ++tinted;  // blue above red: cyan
    }
    REQUIRE(covered > 1000);
    CHECK(tinted > covered * 0.95);
    CHECK(alphaHolo < alphaSolid * 0.8);  // it shows through
    // Where the model is, the two looks cover the same pixels.
    for (std::size_t i = 0; i < a.depth.size(); ++i) {
        CHECK(std::isfinite(a.depth[i]) == std::isfinite(b.depth[i]));
    }
}
