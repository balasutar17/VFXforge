#include "vfx/editor/MeshAnalysis.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <numeric>
#include <unordered_map>

#include "Camera.h"

namespace vfx::editor {

namespace {

constexpr float kPi = 3.14159265358979323846f;

struct Edge {
    std::uint32_t a, b;
    bool operator<(const Edge& o) const { return a != o.a ? a < o.a : b < o.b; }
};

Edge edgeOf(std::uint32_t a, std::uint32_t b) { return a < b ? Edge{a, b} : Edge{b, a}; }

struct Faces {
    std::uint32_t first = 0, second = 0;
    int count = 0;
};

// Every edge and the triangles that use it; triangles with no area left out.
std::map<Edge, Faces> adjacency(const WorldMesh& m, std::vector<float>& normals, std::vector<float>& areas) {
    const std::size_t n = m.triangleCount();
    normals.assign(n * 3, 0.0f);
    areas.assign(n, 0.0f);
    std::map<Edge, Faces> edges;
    for (std::size_t t = 0; t < n; ++t) {
        const std::uint32_t i0 = m.triangles[t * 3], i1 = m.triangles[t * 3 + 1], i2 = m.triangles[t * 3 + 2];
        const float* p0 = &m.positions[i0 * 3];
        const float* p1 = &m.positions[i1 * 3];
        const float* p2 = &m.positions[i2 * 3];
        const float ux = p1[0] - p0[0], uy = p1[1] - p0[1], uz = p1[2] - p0[2];
        const float wx = p2[0] - p0[0], wy = p2[1] - p0[1], wz = p2[2] - p0[2];
        const float cx = uy * wz - uz * wy, cy = uz * wx - ux * wz, cz = ux * wy - uy * wx;
        const float twice = std::sqrt(cx * cx + cy * cy + cz * cz);
        if (!(twice > 1e-12f) || i0 == i1 || i1 == i2 || i0 == i2) {
            continue;
        }
        normals[t * 3] = cx / twice;
        normals[t * 3 + 1] = cy / twice;
        normals[t * 3 + 2] = cz / twice;
        areas[t] = 0.5f * twice;
        const Edge es[3] = {edgeOf(i0, i1), edgeOf(i1, i2), edgeOf(i2, i0)};
        for (const Edge& e : es) {
            Faces& f = edges[e];
            if (f.count == 0) {
                f.first = static_cast<std::uint32_t>(t);
            } else if (f.count == 1) {
                f.second = static_cast<std::uint32_t>(t);
            }
            ++f.count;
        }
    }
    return edges;
}

MeshSegment segment(const WorldMesh& m, const Edge& e) {
    MeshSegment s;
    for (int k = 0; k < 3; ++k) {
        s.v[static_cast<std::size_t>(k)] = m.positions[e.a * 3 + static_cast<std::size_t>(k)];
        s.v[static_cast<std::size_t>(3 + k)] = m.positions[e.b * 3 + static_cast<std::size_t>(k)];
    }
    return s;
}

float lengthOf(const MeshSegment& s) {
    const float dx = s.v[3] - s.v[0], dy = s.v[4] - s.v[1], dz = s.v[5] - s.v[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::vector<MeshSegment> keepLongest(std::vector<MeshSegment> list, std::size_t maxSegments) {
    if (list.size() > maxSegments) {
        std::stable_sort(list.begin(), list.end(),
                         [](const MeshSegment& a, const MeshSegment& b) { return lengthOf(a) > lengthOf(b); });
        list.resize(maxSegments);
    }
    return list;
}

}  // namespace

WorldMesh toWorld(const Model& model, const ModelPlace& place) {
    WorldMesh out;
    if (model.triangles.empty() || model.positions.empty()) {
        return out;
    }
    // The same fitting drawModel does: scaled to stand `height` tall, feet at
    // y, the middle of the footprint at x z, turned about the upright.
    const float tall = model.maxY - model.minY;
    const float scale = (place.height > 0.0f ? place.height : 1.0f) / (tall > 1e-6f ? tall : 1.0f);
    const float midX = 0.5f * (model.minX + model.maxX), midZ = 0.5f * (model.minZ + model.maxZ);
    const float turn = place.turn * kPi / 180.0f;
    const float ct = std::cos(turn), st = std::sin(turn);

    // Weld: corners at one position are one vertex.
    std::unordered_map<std::uint64_t, std::uint32_t> seen;
    std::vector<std::uint32_t> remap(model.corners());
    auto quant = [&](float v) { return static_cast<std::int64_t>(std::llround(static_cast<double>(v) * 4096.0)); };
    for (std::size_t i = 0; i < model.corners(); ++i) {
        const float px = (model.positions[i * 3] - midX) * scale;
        const float py = (model.positions[i * 3 + 1] - model.minY) * scale;
        const float pz = (model.positions[i * 3 + 2] - midZ) * scale;
        const float x = ct * px + st * pz + place.x, y = py + place.y, z = -st * px + ct * pz + place.z;
        const std::uint64_t key = (static_cast<std::uint64_t>(quant(x)) * 73856093ull) ^
                                  (static_cast<std::uint64_t>(quant(y)) * 19349663ull) ^
                                  (static_cast<std::uint64_t>(quant(z)) * 83492791ull);
        // The hash alone could merge two far-apart points; confirm by position.
        auto it = seen.find(key);
        bool merged = false;
        while (it != seen.end()) {
            const std::uint32_t v = it->second;
            if (std::fabs(out.positions[v * 3] - x) < 1e-4f && std::fabs(out.positions[v * 3 + 1] - y) < 1e-4f &&
                std::fabs(out.positions[v * 3 + 2] - z) < 1e-4f) {
                remap[i] = v;
                merged = true;
            }
            break;
        }
        if (!merged) {
            const auto v = static_cast<std::uint32_t>(out.positions.size() / 3);
            out.positions.push_back(x);
            out.positions.push_back(y);
            out.positions.push_back(z);
            remap[i] = v;
            seen[key] = v;
        }
    }
    out.triangles.reserve(model.triangles.size());
    for (const std::uint32_t c : model.triangles) {
        out.triangles.push_back(remap[c]);
    }
    out.minX = out.minY = out.minZ = 1e30f;
    out.maxX = out.maxY = out.maxZ = -1e30f;
    for (std::size_t i = 0; i < out.vertexCount(); ++i) {
        out.minX = std::min(out.minX, out.positions[i * 3]);
        out.maxX = std::max(out.maxX, out.positions[i * 3]);
        out.minY = std::min(out.minY, out.positions[i * 3 + 1]);
        out.maxY = std::max(out.maxY, out.positions[i * 3 + 1]);
        out.minZ = std::min(out.minZ, out.positions[i * 3 + 2]);
        out.maxZ = std::max(out.maxZ, out.positions[i * 3 + 2]);
    }
    return out;
}

MeshReport analyseMesh(const WorldMesh& mesh) {
    MeshReport r;
    r.triangles = mesh.triangleCount();
    r.vertices = mesh.vertexCount();
    if (r.triangles == 0) {
        r.warnings.push_back("The model has no triangles to work from.");
        return r;
    }
    std::vector<float> normals, areas;
    const auto edges = adjacency(mesh, normals, areas);
    for (std::size_t t = 0; t < areas.size(); ++t) {
        if (areas[t] > 0.0f) {
            r.area += static_cast<double>(areas[t]);
        } else {
            ++r.degenerate;
        }
    }
    for (const auto& kv : edges) {
        if (kv.second.count == 1) ++r.openEdges;
        if (kv.second.count > 2) ++r.nonManifoldEdges;
    }
    r.closed = r.openEdges == 0 && r.nonManifoldEdges == 0;

    // Connected pieces, by union of vertices that share a triangle.
    std::vector<std::uint32_t> parent(mesh.vertexCount());
    std::iota(parent.begin(), parent.end(), 0u);
    auto find = [&](std::uint32_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    std::vector<unsigned char> used(mesh.vertexCount(), 0);
    for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
        if (areas[t] <= 0.0f) continue;
        const std::uint32_t a = mesh.triangles[t * 3], b = mesh.triangles[t * 3 + 1], c = mesh.triangles[t * 3 + 2];
        used[a] = used[b] = used[c] = 1;
        parent[find(a)] = find(b);
        parent[find(b)] = find(c);
    }
    std::size_t comps = 0;
    for (std::size_t v = 0; v < parent.size(); ++v) {
        if (used[v] && find(static_cast<std::uint32_t>(v)) == v) ++comps;
    }
    r.components = comps;

    // The smallest rectangle round the footprint, found by turning it a few
    // degrees at a time; a ring fitted to it follows the model's proportions
    // rather than the world's axes.
    float bestArea = 1e30f;
    for (int deg = 0; deg < 90; deg += 3) {
        const float a = static_cast<float>(deg) * kPi / 180.0f;
        const float c = std::cos(a), s = std::sin(a);
        float lo0 = 1e30f, hi0 = -1e30f, lo1 = 1e30f, hi1 = -1e30f;
        for (std::size_t i = 0; i < mesh.vertexCount(); ++i) {
            if (!used[i]) continue;
            const float x = mesh.positions[i * 3], z = mesh.positions[i * 3 + 2];
            const float p = c * x + s * z, q = -s * x + c * z;
            lo0 = std::min(lo0, p); hi0 = std::max(hi0, p);
            lo1 = std::min(lo1, q); hi1 = std::max(hi1, q);
        }
        const float area = (hi0 - lo0) * (hi1 - lo1);
        if (area < bestArea) {
            bestArea = area;
            const float mp = 0.5f * (lo0 + hi0), mq = 0.5f * (lo1 + hi1);
            r.footprint.cx = c * mp - s * mq;
            r.footprint.cz = s * mp + c * mq;
            r.footprint.halfA = 0.5f * (hi0 - lo0);
            r.footprint.halfB = 0.5f * (hi1 - lo1);
            // Axis A points along (c, s) in x z; counter-clockwise seen from above is x toward -z.
            r.footprint.turn = -static_cast<float>(deg);
        }
    }
    r.footprint.baseY = mesh.minY;

    if (r.degenerate > 0) {
        r.warnings.push_back(std::to_string(r.degenerate) + " triangles have no area and are skipped.");
    }
    if (r.openEdges > 0) {
        r.warnings.push_back("The model has holes or open edges (" + std::to_string(r.openEdges) +
                             "), so it is not a closed volume: surface and edge effects work, but anything that "
                             "needs an inside (volume fills) would be unreliable.");
    }
    if (r.nonManifoldEdges > 0) {
        r.warnings.push_back(std::to_string(r.nonManifoldEdges) +
                             " edges are shared by more than two faces; outlines there may look doubled.");
    }
    if (r.components > 1) {
        r.warnings.push_back("The model is " + std::to_string(r.components) + " separate pieces.");
    }
    return r;
}

std::vector<MeshTriangle> surfaceProxy(const WorldMesh& mesh, std::size_t maxTriangles) {
    std::vector<MeshTriangle> out;
    const std::size_t n = mesh.triangleCount();
    if (n == 0 || maxTriangles == 0) {
        return out;
    }
    auto triangleOf = [&](std::size_t t) {
        MeshTriangle tri;
        for (int c = 0; c < 3; ++c) {
            const std::uint32_t v = mesh.triangles[t * 3 + static_cast<std::size_t>(c)];
            for (int k = 0; k < 3; ++k) {
                tri.v[static_cast<std::size_t>(c * 3 + k)] = mesh.positions[v * 3 + static_cast<std::size_t>(k)];
            }
        }
        return tri;
    };
    if (n <= maxTriangles) {
        out.reserve(n);
        for (std::size_t t = 0; t < n; ++t) out.push_back(triangleOf(t));
        return out;
    }
    // Vertex clustering on a grid, the finest that fits.
    const float sx = mesh.maxX - mesh.minX, sy = mesh.maxY - mesh.minY, sz = mesh.maxZ - mesh.minZ;
    const float extent = std::max({sx, sy, sz, 1e-6f});
    int lo = 4, hi = 512;
    std::vector<MeshTriangle> best;
    while (lo <= hi) {
        const int res = (lo + hi) / 2;
        const float cell = extent / static_cast<float>(res);
        std::unordered_map<std::uint64_t, std::uint32_t> cluster;
        std::vector<std::uint32_t> rep(mesh.vertexCount());
        std::vector<double> sum;
        std::vector<int> count;
        for (std::size_t v = 0; v < mesh.vertexCount(); ++v) {
            const auto cx = static_cast<std::int64_t>(std::floor((mesh.positions[v * 3] - mesh.minX) / cell));
            const auto cy = static_cast<std::int64_t>(std::floor((mesh.positions[v * 3 + 1] - mesh.minY) / cell));
            const auto cz = static_cast<std::int64_t>(std::floor((mesh.positions[v * 3 + 2] - mesh.minZ) / cell));
            const std::uint64_t key = (static_cast<std::uint64_t>(cx) << 42) ^ (static_cast<std::uint64_t>(cy) << 21) ^
                                      static_cast<std::uint64_t>(cz);
            auto it = cluster.find(key);
            if (it == cluster.end()) {
                it = cluster.emplace(key, static_cast<std::uint32_t>(count.size())).first;
                count.push_back(0);
                sum.insert(sum.end(), {0.0, 0.0, 0.0});
            }
            rep[v] = it->second;
            ++count[it->second];
            for (int k = 0; k < 3; ++k) sum[it->second * 3 + static_cast<std::size_t>(k)] += static_cast<double>(mesh.positions[v * 3 + static_cast<std::size_t>(k)]);
        }
        std::vector<MeshTriangle> made;
        for (std::size_t t = 0; t < n; ++t) {
            const std::uint32_t a = rep[mesh.triangles[t * 3]], b = rep[mesh.triangles[t * 3 + 1]], c = rep[mesh.triangles[t * 3 + 2]];
            if (a == b || b == c || a == c) continue;
            MeshTriangle tri;
            const std::uint32_t ids[3] = {a, b, c};
            for (int i = 0; i < 3; ++i) {
                for (int k = 0; k < 3; ++k) {
                    tri.v[static_cast<std::size_t>(i * 3 + k)] =
                        static_cast<float>(sum[ids[i] * 3 + static_cast<std::size_t>(k)] / static_cast<double>(count[ids[i]]));
                }
            }
            made.push_back(tri);
        }
        if (made.size() <= maxTriangles) {
            best = std::move(made);
            lo = res + 1;
        } else {
            hi = res - 1;
        }
    }
    return best;
}

std::vector<MeshSegment> featureEdges(const WorldMesh& mesh, double angle, std::size_t maxSegments) {
    std::vector<float> normals, areas;
    const auto edges = adjacency(mesh, normals, areas);
    const float cosLimit = static_cast<float>(std::cos(angle * 3.14159265358979323846 / 180.0));
    std::vector<MeshSegment> out;
    for (const auto& kv : edges) {
        const Faces& f = kv.second;
        bool keep = false;
        if (f.count == 1) {
            keep = true;
        } else if (f.count == 2) {
            const float* a = &normals[f.first * 3];
            const float* b = &normals[f.second * 3];
            keep = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] < cosLimit;
        }
        if (keep) out.push_back(segment(mesh, kv.first));
    }
    return keepLongest(std::move(out), maxSegments);
}

std::vector<MeshSegment> silhouetteEdges(const WorldMesh& mesh, float cx, float cy, float cz, std::size_t maxSegments) {
    std::vector<float> normals, areas;
    const auto edges = adjacency(mesh, normals, areas);
    auto facesCamera = [&](std::uint32_t t) {
        const std::uint32_t v = mesh.triangles[t * 3];
        const float vx = cx - mesh.positions[v * 3], vy = cy - mesh.positions[v * 3 + 1], vz = cz - mesh.positions[v * 3 + 2];
        return normals[t * 3] * vx + normals[t * 3 + 1] * vy + normals[t * 3 + 2] * vz > 0.0f;
    };
    std::vector<MeshSegment> out;
    for (const auto& kv : edges) {
        const Faces& f = kv.second;
        if (f.count == 1 || (f.count == 2 && facesCamera(f.first) != facesCamera(f.second))) {
            out.push_back(segment(mesh, kv.first));
        }
    }
    return keepLongest(std::move(out), maxSegments);
}

void cameraPosition(const View& view, float& x, float& y, float& z) {
    const camera::Camera cam = camera::makeCamera(view, false);
    x = cam.ex;
    y = cam.ey;
    z = cam.ez;
}

}  // namespace vfx::editor
