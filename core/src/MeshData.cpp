#include "vfx/MeshData.h"

#include <cmath>

namespace vfx {

namespace {

bool headerOk(const PointList& list, std::size_t& count) {
    if (list.points.empty() || list.points[0].x != kMeshDataVersion) {
        return false;
    }
    const double n = list.points[0].y;
    if (!(n >= 0) || n != std::floor(n) || n > 1e7) {
        return false;
    }
    count = static_cast<std::size_t>(n);
    return list.points.size() == 1 + count * kMeshRecordPoints;
}

void put(PointList& out, const double (&r)[10]) {
    for (int i = 0; i < 10; i += 2) {
        out.points.push_back(Point2{r[i], r[i + 1]});
    }
}

}  // namespace

PointList packTriangles(const std::vector<MeshTriangle>& triangles) {
    PointList out;
    out.points.reserve(1 + triangles.size() * kMeshRecordPoints);
    out.points.push_back(Point2{kMeshDataVersion, static_cast<double>(triangles.size())});
    for (const MeshTriangle& t : triangles) {
        const double r[10] = {t.v[0], t.v[1], t.v[2], t.v[3], t.v[4], t.v[5], t.v[6], t.v[7], t.v[8], 0};
        put(out, r);
    }
    return out;
}

PointList packSegments(const std::vector<MeshSegment>& segments) {
    PointList out;
    out.points.reserve(1 + segments.size() * kMeshRecordPoints);
    out.points.push_back(Point2{kMeshDataVersion, static_cast<double>(segments.size())});
    for (const MeshSegment& s : segments) {
        const double r[10] = {s.v[0], s.v[1], s.v[2], s.v[3], s.v[4], s.v[5], 0, 0, 0, 0};
        put(out, r);
    }
    return out;
}

std::vector<MeshTriangle> unpackTriangles(const PointList& list) {
    std::vector<MeshTriangle> out;
    std::size_t n = 0;
    if (!headerOk(list, n)) {
        return out;
    }
    out.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Point2* p = &list.points[1 + i * kMeshRecordPoints];
        const double r[9] = {p[0].x, p[0].y, p[1].x, p[1].y, p[2].x, p[2].y, p[3].x, p[3].y, p[4].x};
        for (int k = 0; k < 9; ++k) {
            out[i].v[static_cast<std::size_t>(k)] = static_cast<float>(r[k]);
        }
    }
    return out;
}

std::vector<MeshSegment> unpackSegments(const PointList& list) {
    std::vector<MeshSegment> out;
    std::size_t n = 0;
    if (!headerOk(list, n)) {
        return out;
    }
    out.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Point2* p = &list.points[1 + i * kMeshRecordPoints];
        const double r[6] = {p[0].x, p[0].y, p[1].x, p[1].y, p[2].x, p[2].y};
        for (int k = 0; k < 6; ++k) {
            out[i].v[static_cast<std::size_t>(k)] = static_cast<float>(r[k]);
        }
    }
    return out;
}

}  // namespace vfx
