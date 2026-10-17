#include "vfx/editor/Silhouette.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace vfx::editor {

namespace {

struct P2 {
    double x, y;
};

}  // namespace

Result<Silhouette> traceSilhouette(const Image& image, double width, const TraceOptions& options) {
    const int w = image.width, h = image.height;
    if (w <= 0 || h <= 0 || image.rgba.size() < static_cast<std::size_t>(w) * h * 4) {
        return makeError("The picture is empty.");
    }
    if (!(width > 0.0)) {
        return makeError("The target needs a width above 0.");
    }
    const int threshold = static_cast<int>(std::lround(std::clamp(options.threshold, 0.0, 1.0) * 255.0));
    auto solidAt = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h &&
               image.rgba[(static_cast<std::size_t>(y) * w + x) * 4 + 3] >= std::max(1, threshold);
    };
    // The solid parts (touching sideways or diagonally), and the largest.
    std::vector<int> label(static_cast<std::size_t>(w) * h, 0);
    int parts = 0, best = 0;
    std::size_t bestSize = 0, solid = 0;
    std::vector<int> stack;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * w + x;
            if (label[i] != 0 || !solidAt(x, y)) {
                continue;
            }
            ++parts;
            std::size_t size = 0;
            stack.assign(1, static_cast<int>(i));
            label[i] = parts;
            while (!stack.empty()) {
                const int at = stack.back();
                stack.pop_back();
                ++size;
                const int cx = at % w, cy = at / w;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = cx + dx, ny = cy + dy;
                        if ((dx || dy) && solidAt(nx, ny)) {
                            const std::size_t n = static_cast<std::size_t>(ny) * w + nx;
                            if (label[n] == 0) {
                                label[n] = parts;
                                stack.push_back(static_cast<int>(n));
                            }
                        }
                    }
                }
            }
            solid += size;
            if (size > bestSize) {
                bestSize = size;
                best = parts;
            }
        }
    }
    if (bestSize < 4) {
        return makeError("The picture has nothing solid to trace: is its background see-through?",
                         "no pixels at or above the threshold");
    }
    auto in = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h && label[static_cast<std::size_t>(y) * w + x] == best;
    };
    // The first pixel of the part, reading from the top: on its outer edge.
    int sx = 0, sy = 0;
    for (std::size_t i = 0; i < label.size(); ++i) {
        if (label[i] == best) {
            sx = static_cast<int>(i % w);
            sy = static_cast<int>(i / w);
            break;
        }
    }
    // Round the outer edge, neighbour to neighbour (Moore tracing), with
    // the edge kept on the left... the outside on one side the whole way.
    static const int ox[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    static const int oy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    std::vector<P2> chain;
    int cx = sx, cy = sy, from = 4;  // came from the west (outside, as nothing is above or left)
    const std::size_t limit = static_cast<std::size_t>(w) * h * 4 + 16;
    int startDir = -1;
    for (std::size_t steps = 0; steps < limit; ++steps) {
        chain.push_back(P2{cx + 0.5, cy + 0.5});
        int found = -1;
        for (int k = 1; k <= 8; ++k) {
            const int d = (from + k) % 8;
            if (in(cx + ox[d], cy + oy[d])) {
                found = d;
                break;
            }
        }
        if (found < 0) {
            break;  // a single pixel
        }
        if (steps == 0) {
            startDir = found;
        } else if (cx == sx && cy == sy && found == startDir) {
            chain.pop_back();
            break;
        }
        cx += ox[found];
        cy += oy[found];
        from = (found + 4) % 8;  // look round from where we came
    }
    if (chain.size() < 3) {
        return makeError("The picture's solid part is too small to trace.");
    }
    // Smoothed: each point the mean of its neighbours along the edge.
    const std::size_t n = chain.size();
    const int radius = static_cast<int>(std::lround(std::max(0.0, options.smooth) * std::max(1.0, std::min(w, h) / 160.0)));
    std::vector<P2> soft(n);
    for (std::size_t i = 0; i < n; ++i) {
        double ax = 0, ay = 0;
        for (int k = -radius; k <= radius; ++k) {
            const P2& p = chain[(i + n + static_cast<std::size_t>(k + static_cast<int>(n))) % n];
            ax += p.x;
            ay += p.y;
        }
        soft[i] = P2{ax / (2 * radius + 1), ay / (2 * radius + 1)};
    }
    // Evenly spaced along its length.
    std::vector<double> cum(n + 1, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        const P2& a = soft[i];
        const P2& b = soft[(i + 1) % n];
        cum[i + 1] = cum[i] + std::hypot(b.x - a.x, b.y - a.y);
    }
    const double total = cum[n];
    const int count = std::clamp(options.points, 8, 4000);
    const double scale = width / w;
    Silhouette out;
    out.parts = parts;
    out.coverage = static_cast<double>(solid) / (static_cast<double>(w) * h);
    out.width = width;
    out.height = h * scale;
    std::size_t seg = 0;
    for (int k = 0; k < count; ++k) {
        const double target = total * k / count;
        while (seg + 1 < n && cum[seg + 1] < target) {
            ++seg;
        }
        const P2& a = soft[seg];
        const P2& b = soft[(seg + 1) % n];
        const double len = cum[seg + 1] - cum[seg];
        const double t = len > 0 ? (target - cum[seg]) / len : 0.0;
        const double px = a.x + (b.x - a.x) * t, py = a.y + (b.y - a.y) * t;
        out.outline.points.push_back(Point2{(px - w * 0.5) * scale, (h * 0.5 - py) * scale});
    }
    // Counter-clockwise (in units, y up).
    double area = 0;
    const auto& pts = out.outline.points;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const Point2& a = pts[i];
        const Point2& b = pts[(i + 1) % pts.size()];
        area += a.x * b.y - b.x * a.y;
    }
    if (area < 0) {
        std::reverse(out.outline.points.begin(), out.outline.points.end());
    }
    return out;
}

PointList roundedRect(double width, double height, double radius, int pointsPerCorner) {
    PointList out;
    const double hw = width / 2, hh = height / 2;
    const double r = std::clamp(radius, 0.0, std::min(hw, hh));
    const int steps = r > 0 ? std::max(1, pointsPerCorner) : 0;
    const double cx[4] = {hw - r, -hw + r, -hw + r, hw - r};
    const double cy[4] = {-hh + r, -hh + r, hh - r, hh - r};
    const double start[4] = {-90, 180, 90, 0};
    // Counter-clockwise from the bottom right: each corner in turn.
    const int order[4] = {0, 3, 2, 1};
    for (int c : order) {
        const double a0 = c == 0 ? -90 : c == 3 ? 0 : c == 2 ? 90 : 180;
        (void)start;
        for (int s = 0; s <= steps; ++s) {
            const double a = (a0 + 90.0 * s / std::max(1, steps)) * 3.14159265358979323846 / 180.0;
            out.points.push_back(Point2{cx[c] + r * std::cos(a), cy[c] + r * std::sin(a)});
            if (steps == 0) {
                break;
            }
        }
    }
    return out;
}

}  // namespace vfx::editor
