// VFX Forge editor: lines of light in a reference.
//
// A frame round a slot symbol, the edge of a portal, a bolt of lightning
// down the side of a light column: much of what a slot or casual game shows
// is light along a line. Read as a whole, such a picture misleads: a hollow
// frame has the size and spread of a solid shape, and two bolts side by side
// look like one long comet. So each bright part is also looked at on its
// own. A part that is long and thin all along its length is a line. It is
// thinned down to its middle (Zhang and Suen's method), and that middle, in
// order, becomes the points a path emitter can follow.
//
// For each line this measures: its middle, whether it closes on itself, how
// thick its bright core is, how far its soft halo reaches and how bright
// that is, its colours, how jagged and branched it is (lightning is both),
// and the sparkles sitting on it. The soft light left between the lines is
// read as one fill with its brightness along its length. A clip adds, per
// line, its brightness frame by frame, how much it flickers, whether it is
// drawn on (and from where, which way, how fast), and whether a bright part
// runs round it.
//
// Plain arithmetic on the analysis grid; nothing learned, nothing online.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include "ReferenceTools.h"
#include "vfx/editor/Silhouette.h"

namespace vfx::editor::ref {
namespace {

constexpr float kRoot2 = 1.41421356f;
constexpr float kFar = 1e9f;

// One bright part on a small grid of its own: its box, with a border of one
// empty point all round.
struct Piece {
    int left = 0, top = 0, width = 0, height = 0;  // the small grid's place and size on the matte
    std::vector<std::uint8_t> on;
    int area = 0;
    std::size_t at(int x, int y) const {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
    }
};

Piece cutPiece(const Patch& p, int matteWidth) {
    Piece c;
    c.left = p.left - 1;
    c.top = p.top - 1;
    c.width = p.right - p.left + 3;
    c.height = p.bottom - p.top + 3;
    c.on.assign(static_cast<std::size_t>(c.width) * static_cast<std::size_t>(c.height), 0);
    for (int i : p.points) {
        c.on[c.at(i % matteWidth - c.left, i / matteWidth - c.top)] = 1;
    }
    c.area = static_cast<int>(p.points.size());
    return c;
}

// Thins a part to lines one point wide that keep its shape and its holes
// (Zhang and Suen, 1984).
std::vector<std::uint8_t> thin(const Piece& c) {
    std::vector<std::uint8_t> s = c.on;
    const int w = c.width, h = c.height;
    const auto P = [&](int x, int y) -> int {
        return x < 0 || y < 0 || x >= w || y >= h ? 0 : s[static_cast<std::size_t>(y * w + x)];
    };
    std::vector<int> drop;
    for (bool changed = true; changed;) {
        changed = false;
        for (int pass = 0; pass < 2; ++pass) {
            drop.clear();
            for (int y = 1; y < h - 1; ++y) {
                for (int x = 1; x < w - 1; ++x) {
                    if (!s[static_cast<std::size_t>(y * w + x)]) {
                        continue;
                    }
                    const int p2 = P(x, y - 1), p3 = P(x + 1, y - 1), p4 = P(x + 1, y), p5 = P(x + 1, y + 1);
                    const int p6 = P(x, y + 1), p7 = P(x - 1, y + 1), p8 = P(x - 1, y), p9 = P(x - 1, y - 1);
                    const int around = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9;
                    if (around < 2 || around > 6) {
                        continue;
                    }
                    const int ring[9] = {p2, p3, p4, p5, p6, p7, p8, p9, p2};
                    int rises = 0;
                    for (int k = 0; k < 8; ++k) {
                        rises += ring[k] == 0 && ring[k + 1] == 1 ? 1 : 0;
                    }
                    if (rises != 1) {
                        continue;
                    }
                    if (pass == 0 ? (p2 * p4 * p6 != 0 || p4 * p6 * p8 != 0) : (p2 * p4 * p8 != 0 || p2 * p6 * p8 != 0)) {
                        continue;
                    }
                    drop.push_back(y * w + x);
                }
            }
            for (int i : drop) {
                s[static_cast<std::size_t>(i)] = 0;
            }
            changed = changed || !drop.empty();
        }
    }
    return s;
}

// Distance from each point to the nearest seed (a chamfer measure: steps of
// 1 and the square root of 2), over a grid. When labels are given, each point
// also takes the label of its nearest seed.
void chamfer(std::vector<float>& d, int w, int h, std::vector<int>* labels = nullptr) {
    const auto relax = [&](int x, int y, int nx, int ny, float step) {
        if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
            return;
        }
        const auto i = static_cast<std::size_t>(y * w + x), n = static_cast<std::size_t>(ny * w + nx);
        if (d[n] + step < d[i]) {
            d[i] = d[n] + step;
            if (labels) {
                (*labels)[i] = (*labels)[n];
            }
        }
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            relax(x, y, x - 1, y, 1.0f);
            relax(x, y, x, y - 1, 1.0f);
            relax(x, y, x - 1, y - 1, kRoot2);
            relax(x, y, x + 1, y - 1, kRoot2);
        }
    }
    for (int y = h - 1; y >= 0; --y) {
        for (int x = w - 1; x >= 0; --x) {
            relax(x, y, x + 1, y, 1.0f);
            relax(x, y, x, y + 1, 1.0f);
            relax(x, y, x + 1, y + 1, kRoot2);
            relax(x, y, x - 1, y + 1, kRoot2);
        }
    }
}

// How far inside the part each of its points is: 1 at its edge.
std::vector<float> depthInside(const Piece& c) {
    std::vector<float> d(c.on.size());
    for (std::size_t i = 0; i < d.size(); ++i) {
        d[i] = c.on[i] ? kFar : 0.0f;
    }
    chamfer(d, c.width, c.height);
    return d;
}

// The empty points of a part's box that cannot be reached from its edge.
std::vector<std::uint8_t> holesOf(const Piece& c) {
    std::vector<std::uint8_t> outside(c.on.size(), 0);
    std::vector<int> stack;
    const auto start = [&](int x, int y) {
        const std::size_t i = c.at(x, y);
        if (!c.on[i] && !outside[i]) {
            outside[i] = 1;
            stack.push_back(static_cast<int>(i));
        }
    };
    for (int x = 0; x < c.width; ++x) {
        start(x, 0);
        start(x, c.height - 1);
    }
    for (int y = 0; y < c.height; ++y) {
        start(0, y);
        start(c.width - 1, y);
    }
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        const int x = i % c.width, y = i / c.width;
        // Four ways: a diagonal gap in a line of light does not let the outside in.
        const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
        for (int k = 0; k < 4; ++k) {
            if (nx[k] >= 0 && ny[k] >= 0 && nx[k] < c.width && ny[k] < c.height) {
                start(nx[k], ny[k]);
            }
        }
    }
    std::vector<std::uint8_t> holes(c.on.size(), 0);
    for (std::size_t i = 0; i < holes.size(); ++i) {
        holes[i] = !c.on[i] && !outside[i] ? 1 : 0;
    }
    return holes;
}

// The longest way along a thinned part, end to end, as point indices on its
// small grid. Side branches are left off.
std::vector<int> trunkOf(const std::vector<std::uint8_t>& skeleton, int w, int h) {
    std::vector<int> any;
    for (std::size_t i = 0; i < skeleton.size(); ++i) {
        if (skeleton[i]) {
            any.push_back(static_cast<int>(i));
            break;
        }
    }
    if (any.empty()) {
        return {};
    }
    std::vector<int> steps(skeleton.size()), from(skeleton.size());
    const auto walk = [&](int start) {
        std::fill(steps.begin(), steps.end(), -1);
        std::vector<int> queue{start};
        steps[static_cast<std::size_t>(start)] = 0;
        from[static_cast<std::size_t>(start)] = -1;
        int last = start;
        for (std::size_t q = 0; q < queue.size(); ++q) {
            const int i = queue[q];
            last = i;
            const int x = i % w, y = i / w;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if ((dx || dy) && nx >= 0 && ny >= 0 && nx < w && ny < h) {
                        const int n = ny * w + nx;
                        if (skeleton[static_cast<std::size_t>(n)] && steps[static_cast<std::size_t>(n)] < 0) {
                            steps[static_cast<std::size_t>(n)] = steps[static_cast<std::size_t>(i)] + 1;
                            from[static_cast<std::size_t>(n)] = i;
                            queue.push_back(n);
                        }
                    }
                }
            }
        }
        return last;  // the farthest, by steps
    };
    const int a = walk(any.front());
    const int b = walk(a);
    std::vector<int> path;
    for (int i = b; i >= 0; i = from[static_cast<std::size_t>(i)]) {
        path.push_back(i);
    }
    return path;
}

float lengthOf(const std::vector<Point2>& points, bool closed) {
    double total = 0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        total += std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
    }
    if (closed && points.size() > 2) {
        total += std::hypot(points.front().x - points.back().x, points.front().y - points.back().y);
    }
    return static_cast<float>(total);
}

// A moving average along the points; `reach` points to either side.
std::vector<Point2> smoothed(const std::vector<Point2>& points, int reach, bool closed) {
    const int n = static_cast<int>(points.size());
    if (n < 3 || reach <= 0) {
        return points;
    }
    std::vector<Point2> out(points.size());
    for (int i = 0; i < n; ++i) {
        double sx = 0, sy = 0;
        int count = 0;
        for (int k = -reach; k <= reach; ++k) {
            int j = i + k;
            if (closed) {
                j = ((j % n) + n) % n;
            } else if (j < 0 || j >= n) {
                continue;
            }
            sx += points[static_cast<std::size_t>(j)].x;
            sy += points[static_cast<std::size_t>(j)].y;
            ++count;
        }
        out[static_cast<std::size_t>(i)] = Point2{sx / count, sy / count};
    }
    if (!closed) {
        out.front() = points.front();
        out.back() = points.back();
    }
    return out;
}

// Evenly spaced points along a line: `count` of them.
std::vector<Point2> resampled(const std::vector<Point2>& points, bool closed, int count) {
    std::vector<Point2> path = points;
    if (closed && !path.empty()) {
        path.push_back(path.front());
    }
    std::vector<double> along(path.size(), 0.0);
    for (std::size_t i = 1; i < path.size(); ++i) {
        along[i] = along[i - 1] + std::hypot(path[i].x - path[i - 1].x, path[i].y - path[i - 1].y);
    }
    std::vector<Point2> out;
    if (path.size() < 2 || along.back() <= 0.0 || count < 2) {
        return points;
    }
    const double total = along.back();
    std::size_t seg = 1;
    for (int k = 0; k < count; ++k) {
        const double target = closed ? total * k / count : total * k / (count - 1);
        while (seg + 1 < path.size() && along[seg] < target) {
            ++seg;
        }
        const double span = along[seg] - along[seg - 1];
        const double t = span > 0 ? std::clamp((target - along[seg - 1]) / span, 0.0, 1.0) : 0.0;
        out.push_back(Point2{path[seg - 1].x + t * (path[seg].x - path[seg - 1].x),
                             path[seg - 1].y + t * (path[seg].y - path[seg - 1].y)});
    }
    return out;
}

// Marks the points of a small grid that a line through `points` (on the
// small grid's own places) passes over.
void rasterise(const std::vector<Point2>& points, bool closed, int w, int h, std::vector<float>& seeds,
               std::vector<int>* labels = nullptr) {
    const std::size_t n = points.size();
    for (std::size_t i = 0; i + (closed ? 0 : 1) < n; ++i) {
        const Point2& a = points[i];
        const Point2& b = points[(i + 1) % n];
        const int steps = std::max(1, static_cast<int>(std::ceil(2.0 * std::hypot(b.x - a.x, b.y - a.y))));
        for (int k = 0; k <= steps; ++k) {
            const double t = static_cast<double>(k) / steps;
            const int x = static_cast<int>(std::lround(a.x + t * (b.x - a.x)));
            const int y = static_cast<int>(std::lround(a.y + t * (b.y - a.y)));
            if (x >= 0 && y >= 0 && x < w && y < h) {
                seeds[static_cast<std::size_t>(y * w + x)] = 0.0f;
                if (labels) {
                    (*labels)[static_cast<std::size_t>(y * w + x)] = static_cast<int>(i);
                }
            }
        }
    }
}

float percentile(std::vector<float> values, float share) {
    if (values.empty()) {
        return 0.0f;
    }
    const auto k = static_cast<std::size_t>(std::clamp(share, 0.0f, 1.0f) * static_cast<float>(values.size() - 1));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(k), values.end());
    return values[k];
}

// Crackling, from 0 (a clean stroke) to 1 (lightning). What tells lightning
// from a clean line with a few sparkles stuck to it is strands: a bundle of
// thin crooked lines that braid and branch, so the whole line is several
// times wider than any one strand of it. A bend in an open stroke counts
// too. (An outline's own wiggle is not used: sparkles stuck to it make it
// just as bumpy.)
float jaggedness(const LineFound& line) {
    const float strands = line.thickness > 0 ? line.band / line.thickness : 1.0f;
    return clamp01((strands - 1.3f) / 1.5f + line.branches / 0.6f + (line.closed ? 0.0f : line.wiggle / 0.1f));
}

// A line found on one part, in matte points, before it is measured against
// the rest of the picture.
struct Candidate {
    LineFound line;
    std::vector<int> points;  // the part's points on the matte
    std::vector<Point2> middle;  // in matte points
};

// Reads one bright part as a line, if it is one.
bool readPart(const Patch& patch, const Matte& m, Candidate& out) {
    const float h = static_cast<float>(m.height);
    const Piece c = cutPiece(patch, m.width);
    const std::vector<std::uint8_t> skeleton = thin(c);
    const std::vector<float> depth = depthInside(c);
    int skeletonCount = 0;
    double depthSum = 0;
    for (std::size_t i = 0; i < skeleton.size(); ++i) {
        if (skeleton[i]) {
            ++skeletonCount;
            depthSum += depth[i];
        }
    }
    if (skeletonCount < 3) {
        return false;
    }
    // The core's full width: twice the depth of its middle, less the point
    // the middle stands on.
    const float thickness = std::max(1.0f, static_cast<float>(2.0 * depthSum / skeletonCount) - 1.0f);
    // Long and thin all along, or not a line.
    const float roughLength = static_cast<float>(c.area) / thickness;
    if (roughLength < 8.0f * thickness || roughLength < 0.06f * h || thickness > 0.06f * h) {
        return false;
    }

    // A hole as large as a good share of the part means it goes round.
    const std::vector<std::uint8_t> holes = holesOf(c);
    int holeArea = 0;
    for (std::uint8_t v : holes) {
        holeArea += v;
    }
    const bool closed = static_cast<float>(holeArea) >= std::max(0.6f * static_cast<float>(c.area), 9.0f * thickness * thickness);

    LineFound& line = out.line;
    line.closed = closed;
    line.thickness = thickness / h;
    std::vector<Point2> middle;  // on the small grid
    int trunkCount = 0;
    if (closed) {
        // The outer edge of the part with its hole filled, moved in by half
        // the core's width: the middle of the line.
        Image filled;
        filled.width = c.width;
        filled.height = c.height;
        filled.rgba.assign(static_cast<std::size_t>(c.width) * static_cast<std::size_t>(c.height) * 4u, 0);
        for (std::size_t i = 0; i < c.on.size(); ++i) {
            if (c.on[i] || holes[i]) {
                filled.rgba[i * 4 + 3] = 255;
            }
        }
        TraceOptions trace;
        trace.points = std::clamp(static_cast<int>(roughLength / 1.5f), 48, 240);
        trace.smooth = 0.3;  // corners stay corners
        Result<Silhouette> edge = traceSilhouette(filled, static_cast<double>(c.width), trace);
        if (!edge || edge.value().outline.points.size() < 8) {
            return false;
        }
        const std::vector<Point2>& ring = edge.value().outline.points;  // y up, middle at 0, counter-clockwise
        const double half = 0.5 * thickness;
        const std::size_t n = ring.size();
        for (std::size_t i = 0; i < n; ++i) {
            const Point2& prev = ring[(i + n - 1) % n];
            const Point2& next = ring[(i + 1) % n];
            double dx = next.x - prev.x, dy = next.y - prev.y;
            const double len = std::hypot(dx, dy);
            dx = len > 0 ? dx / len : 0;
            dy = len > 0 ? dy / len : 0;
            // Inside is to the left of a counter-clockwise outline: (-dy, dx).
            const double ux = ring[i].x - half * dy, uy = ring[i].y + half * dx;
            // Back to the small grid: an outline runs along the points' outer
            // edges, half a point beyond their places.
            middle.push_back(Point2{ux + 0.5 * c.width - 0.5, 0.5 * c.height - uy - 0.5});
        }
        trunkCount = skeletonCount;
    } else {
        const std::vector<int> trunk = trunkOf(skeleton, c.width, c.height);
        trunkCount = static_cast<int>(trunk.size());
        if (trunkCount < 4) {
            return false;
        }
        // A head with a tail narrows along its length; a line does not.
        const std::size_t quarter = std::max<std::size_t>(1, trunk.size() / 4);
        double headDepth = 0, tailDepth = 0;
        for (std::size_t k = 0; k < quarter; ++k) {
            headDepth += depth[static_cast<std::size_t>(trunk[k])];
            tailDepth += depth[static_cast<std::size_t>(trunk[trunk.size() - 1 - k])];
        }
        const double taper = std::max(headDepth, tailDepth) / std::max(1e-6, std::min(headDepth, tailDepth));
        if (taper > 2.2 && std::max(headDepth, tailDepth) / static_cast<double>(quarter) > 2.5) {
            return false;
        }
        std::vector<Point2> raw;
        for (int i : trunk) {
            raw.push_back(Point2{static_cast<double>(i % c.width), static_cast<double>(i / c.width)});
        }
        middle = smoothed(raw, 1, false);
    }

    // How much longer than a smooth line through it: wiggle. Side strands:
    // branches.
    // Wiggle is measured at the scale of a few core-widths: a corner is not a
    // wiggle, a crackle is.
    const float length = lengthOf(middle, closed);
    const float spacing = length / std::max<float>(1.0f, static_cast<float>(middle.size()));
    const std::vector<Point2> calm =
        smoothed(middle, std::max(2, static_cast<int>(std::lround(2.0f * thickness / std::max(0.5f, spacing)))), closed);
    const float calmLength = std::max(1.0f, lengthOf(calm, closed));
    line.wiggle = std::clamp(length / calmLength - 1.0f, 0.0f, 1.0f);
    line.branches = std::clamp(static_cast<float>(skeletonCount - trunkCount) / std::max(1.0f, length), 0.0f, 3.0f);

    // How wide the whole line is from side to side.
    {
        // (Away from a stroke's ends, where a blob may sit.)
        std::vector<float> seeds(c.on.size(), kFar);
        std::vector<int> nearestAt(c.on.size(), -1);
        rasterise(middle, closed, c.width, c.height, seeds, &nearestAt);
        chamfer(seeds, c.width, c.height, &nearestAt);
        const int n = static_cast<int>(middle.size());
        const int skip = closed ? 0 : std::max(1, n / 10);
        std::vector<float> across;
        for (std::size_t i = 0; i < c.on.size(); ++i) {
            if (c.on[i] && nearestAt[i] >= skip && nearestAt[i] < n - skip) {
                across.push_back(seeds[i]);
            }
        }
        if (across.empty()) {
            for (std::size_t i = 0; i < c.on.size(); ++i) {
                if (c.on[i]) {
                    across.push_back(seeds[i]);
                }
            }
        }
        line.band = std::max(thickness, 2.0f * percentile(across, 0.9f) + 1.0f) / h;
    }
    line.jagged = jaggedness(line);

    // Evenly spaced points, about two core-widths apart, on the whole matte.
    const int count = std::clamp(static_cast<int>(length / std::max(2.0f, 2.0f * thickness)), closed ? 12 : 4, 160);
    for (const Point2& p : resampled(middle, closed, count)) {
        out.middle.push_back(Point2{p.x + c.left, p.y + c.top});
    }
    line.length = length / h;
    out.points = patch.points;
    line.confidence = clamp01(0.5f + 0.5f * std::min(1.0f, roughLength / (20.0f * thickness)) -
                              (closed ? 0.0f : 0.15f * line.branches));
    return true;
}

// Whether a closed line is a round ring about the effect's middle: those are
// read as rings already.
bool roundRing(const std::vector<Point2>& middle) {
    if (middle.size() < 8) {
        return false;
    }
    double cx = 0, cy = 0;
    for (const Point2& p : middle) {
        cx += p.x;
        cy += p.y;
    }
    cx /= static_cast<double>(middle.size());
    cy /= static_cast<double>(middle.size());
    double mean = 0, square = 0;
    for (const Point2& p : middle) {
        const double r = std::hypot(p.x - cx, p.y - cy);
        mean += r;
        square += r * r;
    }
    mean /= static_cast<double>(middle.size());
    square /= static_cast<double>(middle.size());
    const double spread = std::sqrt(std::max(0.0, square - mean * mean));
    return mean > 0 && spread / mean < 0.08;
}

}  // namespace

void readLines(const Matte& m, float total, StillAnalysis& out) {
    out.lines.clear();
    out.lineShare = 0;
    out.lineFill = LineFill{};
    out.linesLead = false;
    if (out.ground || total <= 0.0f || m.width < 16 || m.height < 16) {
        return;
    }
    const int w = m.width, hgt = m.height;
    const float h = static_cast<float>(hgt);
    const std::size_t size = m.cover.size();

    // A line stands out from what is round it. Taking away a smoothing pass
    // that removes anything thin (erode, then dilate) leaves the thin bright
    // things alone: lines over a dark background, and lines over a bright
    // fill (the bolts down a column of light) alike.
    const int across = std::max(2, static_cast<int>(std::lround(0.035f * h)));
    std::vector<float> opened = m.cover;
    erode(opened, w, hgt, across);
    dilate(opened, w, hgt, across);
    std::vector<float> ridge(size), lit;
    for (std::size_t i = 0; i < size; ++i) {
        ridge[i] = std::max(0.0f, m.cover[i] - opened[i]);
        if (ridge[i] > 0.05f) {
            lit.push_back(ridge[i]);
        }
    }
    if (lit.size() < 16) {
        return;
    }
    const float bright = percentile(lit, 0.98f);
    const float core = std::clamp(0.4f * bright, 0.1f, 0.5f);
    // Small breaks in a line (a flicker, a dim stretch) are closed over.
    std::vector<float> mask(size);
    for (std::size_t i = 0; i < size; ++i) {
        mask[i] = ridge[i] >= core ? 1.0f : 0.0f;
    }
    dilate(mask, w, hgt, 1);
    erode(mask, w, hgt, 1);
    for (std::size_t i = 0; i < size; ++i) {
        // Closing must not add what was never there at all.
        mask[i] = mask[i] > 0.5f && ridge[i] >= 0.5f * core ? 1.0f : 0.0f;
    }

    std::vector<Candidate> found;
    for (const Patch& patch : findPatches(mask, w, hgt, 0.5f)) {
        if (patch.area < std::max(10, static_cast<int>(0.0005f * static_cast<float>(size)))) {
            continue;
        }
        Candidate c;
        if (readPart(patch, m, c) && !(c.line.closed && roundRing(c.middle))) {
            found.push_back(std::move(c));
        }
    }
    if (found.empty()) {
        return;
    }
    // Strokes broken where they dim are joined: an end near another's end,
    // running the same way.
    {
        const auto direction = [](const std::vector<Point2>& p, bool atEnd) {
            const std::size_t n = p.size(), k = std::min<std::size_t>(n - 1, 4);
            const Point2 a = atEnd ? p[n - 1 - k] : p[k];
            const Point2 b = atEnd ? p[n - 1] : p[0];
            const double len = std::max(1e-6, std::hypot(b.x - a.x, b.y - a.y));
            return Point2{(b.x - a.x) / len, (b.y - a.y) / len};  // pointing out of the line
        };
        for (bool joined = true; joined;) {
            joined = false;
            for (std::size_t i = 0; i < found.size() && !joined; ++i) {
                for (std::size_t j = 0; j < found.size() && !joined; ++j) {
                    Candidate &a = found[i], &b = found[j];
                    if (i == j || a.line.closed || b.line.closed || a.middle.size() < 2 || b.middle.size() < 2) {
                        continue;
                    }
                    const float t = std::max(a.line.thickness, b.line.thickness) * h;
                    const double gap = std::max(4.0f * t, 0.06f * h);
                    // a's end to b's start (b is turned round as needed).
                    for (int turn = 0; turn < 2 && !joined; ++turn) {
                        std::vector<Point2> next = b.middle;
                        if (turn) {
                            std::reverse(next.begin(), next.end());
                        }
                        const Point2 &e = a.middle.back(), &st = next.front();
                        const double d = std::hypot(st.x - e.x, st.y - e.y);
                        const Point2 leaving = direction(a.middle, true), in = direction(next, false);
                        const double ux = d > 1e-6 ? (st.x - e.x) / d : leaving.x, uy = d > 1e-6 ? (st.y - e.y) / d : leaving.y;
                        const bool straightOn = d <= gap && leaving.x * -in.x + leaving.y * -in.y > 0.7 && leaving.x * ux + leaving.y * uy > 0.5;
                        // Ends that nearly touch join whichever way they run: a frame breaks at its corners.
                        const bool touching = d <= std::max(3.0f * t, 0.03f * h);
                        if (straightOn || touching) {
                            const double lengthA = a.line.length, lengthB = b.line.length;
                            a.middle.insert(a.middle.end(), next.begin(), next.end());
                            a.points.insert(a.points.end(), b.points.begin(), b.points.end());
                            const double sum = std::max(1e-6, lengthA + lengthB);
                            a.line.thickness = static_cast<float>((a.line.thickness * lengthA + b.line.thickness * lengthB) / sum);
                            a.line.band = std::max(a.line.band, b.line.band);
                            a.line.wiggle = static_cast<float>((a.line.wiggle * lengthA + b.line.wiggle * lengthB) / sum);
                            a.line.branches = static_cast<float>((a.line.branches * lengthA + b.line.branches * lengthB) / sum);
                            a.line.jagged = std::max(a.line.jagged, b.line.jagged);
                            a.line.confidence = std::min(a.line.confidence, b.line.confidence);
                            a.line.length = lengthOf(a.middle, false) / h;
                            found.erase(found.begin() + static_cast<std::ptrdiff_t>(j));
                            joined = true;
                        }
                    }
                }
            }
        }
    }
    // Several straight strokes pointing out from the middle every which way
    // are rays or streaks thrown outward, not lines laid down.
    {
        const float cx = out.centreX * static_cast<float>(w), cy = out.centreY * h;
        int open = 0;
        std::vector<double> outward;
        for (const Candidate& c : found) {
            if (c.line.closed) {
                continue;
            }
            ++open;
            const Point2 &a = c.middle.front(), &b = c.middle.back();
            const double dx = b.x - a.x, dy = b.y - a.y;
            const double mx = 0.5 * (a.x + b.x) - cx, my = 0.5 * (a.y + b.y) - cy;
            const double along = std::fabs(dx * mx + dy * my) / std::max(1e-6, std::hypot(dx, dy) * std::hypot(mx, my));
            if (along > 0.9 && c.line.wiggle < 0.08f) {
                outward.push_back(std::atan2(my, mx));
            }
        }
        // Rays go round: their directions leave no gap of half a turn.
        std::sort(outward.begin(), outward.end());
        double widestGap = 0;
        for (std::size_t i = 0; i < outward.size(); ++i) {
            const double next = i + 1 < outward.size() ? outward[i + 1] : outward.front() + 2.0 * kPi;
            widestGap = std::max(widestGap, next - outward[i]);
        }
        if (open >= 3 && outward.size() * 2 > static_cast<std::size_t>(open) && widestGap < kPi) {
            return;
        }
    }
    // A stroke whose ends meet goes round: a frame with a gap in it.
    for (Candidate& c : found) {
        LineFound& line = c.line;
        if (line.closed || c.middle.size() < 8) {
            continue;
        }
        const double d = std::hypot(c.middle.front().x - c.middle.back().x, c.middle.front().y - c.middle.back().y);
        if (d <= std::max(4.0f * line.thickness * h, 0.08f * h) && line.length * h >= 6.0 * std::max(1.0, d) &&
            line.length >= 0.5f) {
            line.closed = true;
            line.length = lengthOf(c.middle, true) / h;
        }
        line.jagged = jaggedness(line);
    }
    // Pieces too short to be lines on their own once joining is done.
    found.erase(std::remove_if(found.begin(), found.end(),
                               [&](const Candidate& c) { return c.line.length < 0.15f; }),
                found.end());
    if (found.empty()) {
        return;
    }
    // Longest first, at most eight.
    std::sort(found.begin(), found.end(),
              [](const Candidate& a, const Candidate& b) { return a.line.length > b.line.length; });
    if (found.size() > 8) {
        found.resize(8);
    }

    // ---- what lies around the lines: halo, sparkles, fill
    std::vector<int> owner(size, -1);  // the line whose core a point is in
    std::vector<float> away(size, kFar);
    for (std::size_t k = 0; k < found.size(); ++k) {
        for (int i : found[k].points) {
            owner[static_cast<std::size_t>(i)] = static_cast<int>(k);
            away[static_cast<std::size_t>(i)] = 0.0f;
        }
    }
    std::vector<int> nearest = owner;
    chamfer(away, w, hgt, &nearest);

    const int reachMost = std::max(3, static_cast<int>(std::lround(0.1f * h)));
    std::vector<int> reach(found.size(), 0);
    for (std::size_t k = 0; k < found.size(); ++k) {
        LineFound& line = found[k].line;
        double coreSum = 0, cr = 0, cg = 0, cb = 0, weight = 0;
        for (int i : found[k].points) {
            const auto u = static_cast<std::size_t>(i);
            const float v = m.cover[u];
            coreSum += v;
            cr += v * m.r[u];
            cg += v * m.g[u];
            cb += v * m.b[u];
            weight += v;
        }
        line.level = static_cast<float>(coreSum / std::max<std::size_t>(1, found[k].points.size()));
        if (weight > 0) {
            line.colour = Swatch{static_cast<float>(cr / weight), static_cast<float>(cg / weight),
                                 static_cast<float>(cb / weight), 1.0f};
        }
        // Rings of points one step further out each time.
        std::vector<double> sum(static_cast<std::size_t>(reachMost) + 1, 0.0), rs(sum), gs(sum), bs(sum);
        std::vector<int> n(sum.size(), 0);
        for (std::size_t i = 0; i < size; ++i) {
            if (nearest[i] != static_cast<int>(k) || owner[i] >= 0) {
                continue;
            }
            const int ring = static_cast<int>(std::lround(away[i]));
            if (ring < 1 || ring > reachMost) {
                continue;
            }
            const auto r = static_cast<std::size_t>(ring);
            const float v = m.cover[i];
            sum[r] += v;
            rs[r] += v * m.r[i];
            gs[r] += v * m.g[i];
            bs[r] += v * m.b[i];
            ++n[r];
        }
        // The halo ends where it has faded, or where it stops fading: past
        // that is light of another kind (the fill of a column, say).
        int edge = reachMost;
        double before = line.level;
        for (int r = 1; r <= reachMost; ++r) {
            const auto u = static_cast<std::size_t>(r);
            const double here = n[u] > 0 ? sum[u] / n[u] : 0.0;
            if (n[u] == 0 || here < 0.06 * line.level || (r >= 2 && before - here < 0.02 * line.level)) {
                edge = r - 1;
                break;
            }
            before = here;
        }
        reach[k] = edge;
        double haloSum = 0, haloR = 0, haloG = 0, haloB = 0;
        int haloCount = 0;
        for (int r = 1; r <= edge; ++r) {
            const auto u = static_cast<std::size_t>(r);
            haloSum += sum[u];
            haloR += rs[u];
            haloG += gs[u];
            haloB += bs[u];
            haloCount += n[u];
        }
        line.halo = static_cast<float>(edge) / h;
        line.haloLevel = haloCount > 0 && line.level > 0
                             ? clamp01(static_cast<float>(haloSum / haloCount) / line.level)
                             : 0.0f;
        line.haloColour = haloSum > 0 ? Swatch{static_cast<float>(haloR / haloSum), static_cast<float>(haloG / haloSum),
                                               static_cast<float>(haloB / haloSum), 1.0f}
                                      : line.colour;
    }

    // Blobs at the ends of strokes: round, bright, much more than a line's
    // end holds (a fireball at a frame's corner).
    for (Candidate& c : found) {
        LineFound& line = c.line;
        if (line.closed || c.middle.size() < 2) {
            continue;
        }
        const float t = std::max(1.0f, line.thickness * h);
        const float around = std::max(5.0f * t, 0.05f * h);
        const float wide = std::max(t, line.band * h);  // a braided bolt is as wide as all its strands
        for (const Point2& end : {c.middle.front(), c.middle.back()}) {
            double ring = 0, rim = 0, area = 0, sx = 0, sy = 0, sum = 0, cr = 0, cg = 0, cb = 0;
            std::vector<Point2> spot;
            const int x0 = std::max(0, static_cast<int>(end.x - around)), x1 = std::min(w - 1, static_cast<int>(end.x + around));
            const int y0 = std::max(0, static_cast<int>(end.y - around)), y1 = std::min(hgt - 1, static_cast<int>(end.y + around));
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const auto i = static_cast<std::size_t>(y * w + x);
                    const double d = std::hypot(static_cast<double>(x) - end.x, static_cast<double>(y) - end.y);
                    const bool onRim = d <= around && d > around - 1.5;
                    ring += onRim ? 1.0 : 0.0;  // (cut short at the picture's edge: the full ring is used then)
                    if (d > around || m.cover[i] < 0.5f * line.level) {
                        continue;
                    }
                    // On the rim of the area looked at: a blob stays inside it,
                    // a broad wash of light (a column's glowing foot) runs out of it.
                    rim += onRim ? 1.0 : 0.0;
                    spot.push_back(Point2{static_cast<double>(x), static_cast<double>(y)});
                    area += 1.0;
                    sx += x;
                    sy += y;
                    sum += m.cover[i];
                    cr += m.cover[i] * m.r[i];
                    cg += m.cover[i] * m.g[i];
                    cb += m.cover[i] * m.b[i];
                }
            }
            // A plain end holds about `around` of line, `wide` across. A blob is
            // much more, and little of the rim is lit (the line itself crosses
            // it, and some glow): measured on the slot sheet, a quarter
            // separates fireballs (a tenth to a fifth) from a column's foot
            // (a third or more).
            if (area < 2.0 * around * wide || sum <= 0 || rim > 0.25 * std::max(ring, 2.0 * 3.14159265 * around * 1.5 * 0.9)) {
                continue;
            }
            // And brighter in its middle than the line that leads to it (a
            // fireball burns white; the wedge of a bright fill where a stroke
            // ends at its corner is dimmer than the stroke).
            {
                const double mx = sx / area, my = sy / area;
                const double inner = 0.5 * std::sqrt(area / 3.14159265);
                double middle = 0;
                int count = 0;
                for (const Point2& q : spot) {
                    if (std::hypot(q.x - mx, q.y - my) <= inner) {
                        middle += m.cover[static_cast<std::size_t>(q.y) * static_cast<std::size_t>(w) +
                                          static_cast<std::size_t>(q.x)];
                        ++count;
                    }
                }
                if (count == 0 || middle / count < 0.95 * line.level) {
                    continue;
                }
            }
            LineEnd blob;
            blob.x = static_cast<float>(sx / area) / static_cast<float>(w);
            blob.y = static_cast<float>(sy / area) / h;
            blob.radius = static_cast<float>(std::sqrt(area / 3.14159265)) / h;
            blob.level = static_cast<float>(sum / area);
            blob.colour = Swatch{static_cast<float>(cr / sum), static_cast<float>(cg / sum), static_cast<float>(cb / sum), 1};
            line.ends.push_back(blob);
        }
    }

    // Sparkles: small bright spots that are not part of a line, near one.
    std::vector<float> spots(size, 0.0f);
    for (std::size_t i = 0; i < size; ++i) {
        spots[i] = owner[i] < 0 && ridge[i] >= 0.8f * core ? m.cover[i] : 0.0f;
    }
    std::vector<std::uint8_t> taken(size, 0);  // explained by a line: its core, its halo, its sparkles
    for (std::size_t i = 0; i < size; ++i) {
        const int k = nearest[i];
        taken[i] = owner[i] >= 0 || (k >= 0 && away[i] <= static_cast<float>(reach[static_cast<std::size_t>(k)]) + 0.5f)
                       ? 1
                       : 0;
    }
    {
        std::vector<double> sizes(found.size(), 0.0), dist(found.size(), 0.0), sr(found.size(), 0.0),
            sg(found.size(), 0.0), sb(found.size(), 0.0), sw(found.size(), 0.0);
        for (const Patch& p : findPatches(spots, w, hgt, 0.0f)) {
            const auto centre = static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(p.y)), 0, hgt - 1) * w +
                                                         std::clamp(static_cast<int>(std::lround(p.x)), 0, w - 1));
            const int k = nearest[centre];
            if (k < 0) {
                continue;
            }
            const LineFound& line = found[static_cast<std::size_t>(k)].line;
            const float t = line.thickness * h;
            if (static_cast<float>(p.area) > std::max(6.0f, 6.0f * t * t) ||
                away[centre] > static_cast<float>(reach[static_cast<std::size_t>(k)]) + 6.0f * t) {
                continue;
            }
            const auto u = static_cast<std::size_t>(k);
            found[u].line.sparkles += 1;
            sizes[u] += std::sqrt(static_cast<double>(p.area));
            dist[u] += away[centre];
            for (int i : p.points) {
                const auto v = static_cast<std::size_t>(i);
                taken[v] = 1;
                sr[u] += m.cover[v] * m.r[v];
                sg[u] += m.cover[v] * m.g[v];
                sb[u] += m.cover[v] * m.b[v];
                sw[u] += m.cover[v];
            }
        }
        for (std::size_t k = 0; k < found.size(); ++k) {
            LineFound& line = found[k].line;
            if (line.sparkles > 0) {
                line.sparkleSize = static_cast<float>(sizes[k] / line.sparkles) / h;
                line.sparkleReach = static_cast<float>(dist[k] / line.sparkles) / h;
                line.sparkleColour = sw[k] > 0 ? Swatch{static_cast<float>(sr[k] / sw[k]), static_cast<float>(sg[k] / sw[k]),
                                                        static_cast<float>(sb[k] / sw[k]), 1.0f}
                                               : line.colour;
            }
        }
    }

    // How much of the effect the lines explain.
    double explained = 0;
    for (std::size_t i = 0; i < size; ++i) {
        explained += taken[i] ? m.cover[i] : 0.0f;
    }
    out.lineShare = clamp01(static_cast<float>(explained / total));

    // The light that is left, between the lines and inside them.
    {
        float boxL = static_cast<float>(w), boxT = h, boxR = 0, boxB = 0;
        for (const Candidate& c : found) {
            for (const Point2& p : c.middle) {
                boxL = std::min(boxL, static_cast<float>(p.x));
                boxR = std::max(boxR, static_cast<float>(p.x));
                boxT = std::min(boxT, static_cast<float>(p.y));
                boxB = std::max(boxB, static_cast<float>(p.y));
            }
        }
        double left = 0, inBox = 0;
        std::vector<float> rest(size, 0.0f);
        for (int y = 0; y < hgt; ++y) {
            for (int x = 0; x < w; ++x) {
                const auto i = static_cast<std::size_t>(y * w + x);
                if (!taken[i]) {
                    rest[i] = m.cover[i];
                    left += m.cover[i];
                    if (static_cast<float>(x) >= boxL && static_cast<float>(x) <= boxR && static_cast<float>(y) >= boxT &&
                        static_cast<float>(y) <= boxB) {
                        inBox += m.cover[i];
                    }
                }
            }
        }
        LineFill& fill = out.lineFill;
        if (left >= 0.12 * total && inBox >= 0.7 * left && boxR - boxL >= 3.0f && boxB - boxT >= 3.0f) {
            // Its box: where nearly all of it lies, inside the lines' box.
            std::vector<double> byX(static_cast<std::size_t>(w), 0.0), byY(static_cast<std::size_t>(hgt), 0.0);
            double cr = 0, cg = 0, cb = 0;
            for (int y = 0; y < hgt; ++y) {
                for (int x = 0; x < w; ++x) {
                    const auto i = static_cast<std::size_t>(y * w + x);
                    byX[static_cast<std::size_t>(x)] += rest[i];
                    byY[static_cast<std::size_t>(y)] += rest[i];
                    cr += rest[i] * m.r[i];
                    cg += rest[i] * m.g[i];
                    cb += rest[i] * m.b[i];
                }
            }
            const auto quantile = [&](const std::vector<double>& along, double share) {
                double run = 0;
                for (std::size_t i = 0; i < along.size(); ++i) {
                    run += along[i];
                    if (run >= share * left) {
                        return static_cast<float>(i);
                    }
                }
                return static_cast<float>(along.size() - 1);
            };
            const float l = std::max(boxL, quantile(byX, 0.02)), r = std::min(boxR, quantile(byX, 0.98));
            const float t = std::max(boxT, quantile(byY, 0.02)), b = std::min(boxB, quantile(byY, 0.98));
            if (r - l >= 2.0f && b - t >= 2.0f) {
                fill.present = true;
                fill.left = l / static_cast<float>(w);
                fill.right = r / static_cast<float>(w);
                fill.top = t / h;
                fill.bottom = b / h;
                fill.share = clamp01(static_cast<float>(left / total));
                fill.colour = Swatch{static_cast<float>(cr / left), static_cast<float>(cg / left), static_cast<float>(cb / left), 1};
                fill.upright = b - t >= r - l;
                constexpr int kBins = 8;
                std::array<double, kBins> bin{};
                std::array<int, kBins> count{};
                double all = 0;
                int allCount = 0;
                for (int y = static_cast<int>(t); y <= static_cast<int>(b); ++y) {
                    for (int x = static_cast<int>(l); x <= static_cast<int>(r); ++x) {
                        const auto i = static_cast<std::size_t>(y * w + x);
                        const float along = fill.upright ? (static_cast<float>(y) - t) / std::max(1.0f, b - t)
                                                         : (static_cast<float>(x) - l) / std::max(1.0f, r - l);
                        const int k = std::clamp(static_cast<int>(along * kBins), 0, kBins - 1);
                        bin[static_cast<std::size_t>(k)] += rest[i];
                        ++count[static_cast<std::size_t>(k)];
                        all += rest[i];
                        ++allCount;
                    }
                }
                fill.level = allCount > 0 ? static_cast<float>(all / allCount) : 0.0f;
                std::array<float, kBins> mean{};
                float top = 0;
                for (int k = 0; k < kBins; ++k) {
                    const auto u = static_cast<std::size_t>(k);
                    mean[u] = count[u] > 0 ? static_cast<float>(bin[u] / count[u]) : 0.0f;
                    top = std::max(top, mean[u]);
                }
                fill.brightFirst = mean[0] + mean[1] >= mean[kBins - 1] + mean[kBins - 2];
                for (int k = 0; k < kBins; ++k) {
                    const float v = mean[static_cast<std::size_t>(fill.brightFirst ? k : kBins - 1 - k)];
                    fill.profile.push_back(top > 0 ? v / top : 0.0f);
                }
            }
        }
    }

    // In fractions of the picture.
    for (Candidate& c : found) {
        for (const Point2& p : c.middle) {
            c.line.points.push_back(Point2{p.x / static_cast<double>(w), p.y / static_cast<double>(hgt)});
        }
        out.lines.push_back(std::move(c.line));
    }
    const float fillShare = out.lineFill.present ? out.lineFill.share : 0.0f;
    out.linesLead = out.lineShare >= 0.5f || (out.lineShare >= 0.3f && out.lineShare + fillShare >= 0.75f);
}

void linesOverClip(const std::vector<Matte>& mattes, double framesPerSecond, StillAnalysis& still) {
    if (mattes.size() < 2 || framesPerSecond <= 0) {
        return;
    }
    constexpr int kSamples = 64;
    const std::size_t frames = mattes.size();
    for (LineFound& line : still.lines) {
        const std::vector<Point2> at = resampled(line.points, line.closed, kSamples);
        const int n = static_cast<int>(at.size());
        if (n < 4) {
            continue;
        }
        // How much effect lies on each sample of the line in each frame: the
        // most within a point either side, as a line moves a little.
        std::vector<std::vector<float>> seen(frames, std::vector<float>(static_cast<std::size_t>(n), 0.0f));
        for (std::size_t f = 0; f < frames; ++f) {
            const Matte& m = mattes[f];
            for (int k = 0; k < n; ++k) {
                const float x = static_cast<float>(at[static_cast<std::size_t>(k)].x) * static_cast<float>(m.width);
                const float y = static_cast<float>(at[static_cast<std::size_t>(k)].y) * static_cast<float>(m.height);
                float best = 0.0f;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        best = std::max(best, readAt(m.cover, m.width, m.height, x + static_cast<float>(dx),
                                                     y + static_cast<float>(dy)));
                    }
                }
                seen[f][static_cast<std::size_t>(k)] = best;
            }
        }
        std::vector<float> mean(frames, 0.0f), share(frames, 0.0f);
        float most = 0.0f;
        for (std::size_t f = 0; f < frames; ++f) {
            for (float v : seen[f]) {
                mean[f] += v;
            }
            mean[f] /= static_cast<float>(n);
            most = std::max(most, mean[f]);
        }
        if (most <= 1e-4f) {
            continue;
        }
        float strongest = 0.0f;
        for (const auto& row : seen) {
            for (float v : row) {
                strongest = std::max(strongest, v);
            }
        }
        const float on = 0.35f * strongest;
        for (std::size_t f = 0; f < frames; ++f) {
            int count = 0;
            for (float v : seen[f]) {
                count += v >= on ? 1 : 0;
            }
            share[f] = static_cast<float>(count) / static_cast<float>(n);
        }

        // Flicker: how much each part of the line changes from one frame to
        // the next, against how bright it is.
        {
            double change = 0, level = 0;
            for (std::size_t f = 1; f < frames; ++f) {
                for (int k = 0; k < n; ++k) {
                    change += std::fabs(seen[f][static_cast<std::size_t>(k)] - seen[f - 1][static_cast<std::size_t>(k)]);
                    level += seen[f][static_cast<std::size_t>(k)];
                }
            }
            line.flicker = level > 0 ? clamp01(static_cast<float>(change / level) / 0.6f) : 0.0f;
        }

        // Drawn on: the lit share of the line climbs from little to nearly
        // all over a few frames, mostly without falling back.
        {
            std::size_t begin = frames, full = frames;
            for (std::size_t f = 0; f < frames; ++f) {
                if (begin == frames && share[f] > 0.05f) {
                    begin = f;
                }
                if (begin != frames && share[f] >= 0.85f) {
                    full = f;
                    break;
                }
            }
            if (begin < frames && full < frames && full >= begin + 3 && share[begin] <= 0.3f) {
                int rising = 0;
                for (std::size_t f = begin + 1; f <= full; ++f) {
                    rising += share[f] + 0.02f >= share[f - 1] ? 1 : 0;
                }
                if (static_cast<double>(rising) >= 0.7 * static_cast<double>(full - begin)) {
                    line.drawn = true;
                    line.drawFrom = static_cast<double>(begin) / framesPerSecond;
                    line.drawTime = static_cast<double>(full - begin + 1) / framesPerSecond;
                    // Which way: watch the longest lit stretch grow.
                    const auto stretch = [&](std::size_t f, int& from, int& to) {
                        // The longest run of lit samples (round the end, when it closes).
                        const auto litAt = [&](int k) {
                            const int j = line.closed ? ((k % n) + n) % n : k;
                            return j >= 0 && j < n && seen[f][static_cast<std::size_t>(j)] >= on;
                        };
                        int bestLength = 0;
                        from = to = 0;
                        for (int s = 0; s < n; ++s) {
                            if (!litAt(s) || (line.closed ? litAt(s - 1) : (s > 0 && litAt(s - 1)))) {
                                continue;
                            }
                            int e = s;
                            while (e + 1 < s + n && litAt(e + 1) && (line.closed || e + 1 < n)) {
                                ++e;
                            }
                            if (e - s + 1 > bestLength) {
                                bestLength = e - s + 1;
                                from = s;
                                to = e;
                            }
                        }
                        return bestLength;
                    };
                    std::size_t early = begin;
                    while (early < full && share[early] < 0.15f) {
                        ++early;
                    }
                    const std::size_t later = std::min(full, early + 2);
                    int a0 = 0, b0 = 0, a1 = 0, b1 = 0;
                    stretch(early, a0, b0);
                    stretch(later, a1, b1);
                    // Line the later stretch up with the earlier one, round the end.
                    const auto near = [&](int x, int y) {
                        int d = std::abs(x - y);
                        if (line.closed) {
                            d = std::min(d, n - d);
                        }
                        return d <= 2;
                    };
                    const bool startStays = near(((a1 % n) + n) % n, ((a0 % n) + n) % n);
                    const bool endStays = near(((b1 % n) + n) % n, ((b0 % n) + n) % n);
                    if (startStays && !endStays) {
                        line.drawWay = 1;
                        line.drawStart = static_cast<float>(((a0 % n) + n) % n) / static_cast<float>(n);
                    } else if (endStays && !startStays) {
                        line.drawWay = -1;
                        line.drawStart = static_cast<float>(((b0 % n) + n) % n) / static_cast<float>(n);
                    } else {
                        line.drawWay = 0;
                        line.drawStart = static_cast<float>((((a0 + b0) / 2) % n + n) % n) / static_cast<float>(n);
                    }
                    if (!line.closed) {
                        // An open line is drawn from one of its ends.
                        line.drawWay = line.drawStart < 0.5f ? 1 : -1;
                        line.drawStart = line.drawWay > 0 ? 0.0f : 1.0f;
                    }
                }
            }
        }

        // Brightness frame by frame: of the lit part while it is being drawn
        // (a half-drawn line is not a dim one), of the whole line otherwise.
        line.levelOverTime.assign(frames, 0.0f);
        float top = 0.0f;
        for (std::size_t f = 0; f < frames; ++f) {
            float v = mean[f];
            if (line.drawn) {
                double sum = 0;
                int count = 0;
                for (float s : seen[f]) {
                    if (s >= on) {
                        sum += s;
                        ++count;
                    }
                }
                v = count > 0 ? static_cast<float>(sum / count) * std::min(1.0f, share[f] / 0.15f) : 0.0f;
            }
            line.levelOverTime[f] = v;
            top = std::max(top, v);
        }
        for (float& v : line.levelOverTime) {
            v = top > 0 ? v / top : 0.0f;
        }

        // Light running round: line up the pattern along the line from one
        // frame to the next.
        if (line.closed) {
            double shiftSum = 0, weight = 0, scoreSum = 0;
            int pairs = 0;
            std::vector<int> shifts;
            for (std::size_t f = 1; f < frames; ++f) {
                const std::vector<float>&a = seen[f - 1], &b = seen[f];
                float ma = 0, mb = 0;
                for (int k = 0; k < n; ++k) {
                    ma += a[static_cast<std::size_t>(k)];
                    mb += b[static_cast<std::size_t>(k)];
                }
                ma /= static_cast<float>(n);
                mb /= static_cast<float>(n);
                double va = 0, vb = 0;
                for (int k = 0; k < n; ++k) {
                    va += (a[static_cast<std::size_t>(k)] - ma) * (a[static_cast<std::size_t>(k)] - ma);
                    vb += (b[static_cast<std::size_t>(k)] - mb) * (b[static_cast<std::size_t>(k)] - mb);
                }
                if (va <= 1e-6 || vb <= 1e-6) {
                    continue;
                }
                double best = -2;
                int bestShift = 0;
                for (int s = -n / 4; s <= n / 4; ++s) {
                    double cross = 0;
                    for (int k = 0; k < n; ++k) {
                        cross += (a[static_cast<std::size_t>(k)] - ma) *
                                 (b[static_cast<std::size_t>(((k + s) % n + n) % n)] - mb);
                    }
                    const double score = cross / std::sqrt(va * vb);
                    if (score > best) {
                        best = score;
                        bestShift = s;
                    }
                }
                const double strength = std::sqrt(va / n);
                shifts.push_back(bestShift);
                shiftSum += bestShift * strength;
                weight += strength;
                scoreSum += best;
                ++pairs;
            }
            if (pairs > 0 && weight > 0) {
                const double perFrame = shiftSum / weight;
                line.runConfidence = clamp01(static_cast<float>(scoreSum / pairs));
                const double rounds = perFrame / n * framesPerSecond;
                // It must run the same way nearly every frame: crackle that
                // jumps about lines up somewhere each time, but not steadily.
                int sameWay = 0;
                for (int shift : shifts) {
                    sameWay += (shift > 0) == (perFrame > 0) && shift != 0 ? 1 : 0;
                }
                const bool steadily = static_cast<double>(sameWay) >= 0.8 * static_cast<double>(shifts.size());
                if (line.runConfidence >= 0.6f && std::fabs(perFrame) >= 0.5 && steadily) {
                    line.run = static_cast<float>(rounds);
                }
            }
        }
    }
}

}  // namespace vfx::editor::ref
