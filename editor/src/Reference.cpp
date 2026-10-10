#include "vfx/editor/Reference.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "vfx/editor/Shapes.h"

#include "ReferenceTools.h"

namespace vfx::editor {

using namespace ref;

// ------------------------------------------------------------- the basics

const char* backdropName(Backdrop backdrop) {
    switch (backdrop) {
        case Backdrop::Auto: return "auto";
        case Backdrop::Transparent: return "transparent";
        case Backdrop::Dark: return "dark";
        case Backdrop::Light: return "light";
        case Backdrop::Colour: return "colour";
    }
    return "auto";
}

Status validateReference(const Reference& reference) {
    if (reference.frames.empty()) {
        return makeError("The reference has no picture in it.");
    }
    if (reference.frames.size() > static_cast<std::size_t>(kMaxReferenceFrames)) {
        return makeError("The reference has too many frames.",
                         "At most " + std::to_string(kMaxReferenceFrames) +
                             " frames are looked at. Use a shorter part, or a lower sampling rate.");
    }
    const Image& first = reference.frames.front();
    if (first.width < 8 || first.height < 8) {
        return makeError("The reference picture is too small to study.",
                         "It needs to be at least 8 pixels on each side.");
    }
    if (first.width > kMaxReferenceSide || first.height > kMaxReferenceSide) {
        return makeError("The reference picture is too large.",
                         "At most " + std::to_string(kMaxReferenceSide) + " pixels on each side.");
    }
    for (const Image& frame : reference.frames) {
        if (frame.width != first.width || frame.height != first.height) {
            return makeError("The frames of the reference are not all the same size.");
        }
        if (frame.rgba.size() != static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 4u) {
            return makeError("A frame of the reference is damaged.");
        }
    }
    if (reference.frames.size() > 1 && !(reference.framesPerSecond > 0.0 && reference.framesPerSecond <= 240.0)) {
        return makeError("The reference clip has no usable frame rate.");
    }
    return {};
}

void frameRange(const Reference& reference, const ReferenceOptions& options, int& first, int& last) {
    const int count = static_cast<int>(reference.frames.size());
    first = std::clamp(options.firstFrame, 0, std::max(0, count - 1));
    last = options.lastFrame < 0 ? count - 1 : std::clamp(options.lastFrame, first, std::max(0, count - 1));
}

namespace {

struct Rect {
    int left = 0, top = 0, right = 0, bottom = 0;  // right and bottom are one past the end
};

Rect cropOf(const Image& frame, const ReferenceOptions& options) {
    const float l = clamp01(std::min(options.cropLeft, options.cropRight));
    const float r = clamp01(std::max(options.cropLeft, options.cropRight));
    const float t = clamp01(std::min(options.cropTop, options.cropBottom));
    const float b = clamp01(std::max(options.cropTop, options.cropBottom));
    Rect rect;
    rect.left = static_cast<int>(std::floor(l * static_cast<float>(frame.width)));
    rect.right = static_cast<int>(std::ceil(r * static_cast<float>(frame.width)));
    rect.top = static_cast<int>(std::floor(t * static_cast<float>(frame.height)));
    rect.bottom = static_cast<int>(std::ceil(b * static_cast<float>(frame.height)));
    // Never less than eight pixels a side: a crop dragged shut still shows something.
    if (rect.right - rect.left < 8) {
        rect.left = std::clamp(rect.left, 0, std::max(0, frame.width - 8));
        rect.right = std::min(frame.width, rect.left + 8);
    }
    if (rect.bottom - rect.top < 8) {
        rect.top = std::clamp(rect.top, 0, std::max(0, frame.height - 8));
        rect.bottom = std::min(frame.height, rect.top + 8);
    }
    return rect;
}

void gridSize(const Rect& rect, int longestSide, int& width, int& height) {
    const int w = rect.right - rect.left, h = rect.bottom - rect.top;
    const int longest = std::max(w, h);
    const int target = std::min(longest, std::clamp(longestSide, 16, 512));
    width = std::max(8, static_cast<int>(std::lround(static_cast<double>(w) * target / longest)));
    height = std::max(8, static_cast<int>(std::lround(static_cast<double>(h) * target / longest)));
}

// Averages the crop down to width x height. Each grid point is the mean of
// the source pixels it covers, weighted by how much of each it covers.
Grid averageDown(const Image& frame, const Rect& rect, int width, int height) {
    Grid grid;
    grid.width = width;
    grid.height = height;
    const std::size_t n = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    grid.r.assign(n, 0.0f);
    grid.g.assign(n, 0.0f);
    grid.b.assign(n, 0.0f);
    grid.a.assign(n, 0.0f);
    const double sx = static_cast<double>(rect.right - rect.left) / width;
    const double sy = static_cast<double>(rect.bottom - rect.top) / height;
    for (int y = 0; y < height; ++y) {
        const double y0 = rect.top + y * sy, y1 = rect.top + (y + 1) * sy;
        const int iy0 = static_cast<int>(std::floor(y0));
        const int iy1 = std::min(rect.bottom, static_cast<int>(std::ceil(y1)));
        for (int x = 0; x < width; ++x) {
            const double x0 = rect.left + x * sx, x1 = rect.left + (x + 1) * sx;
            const int ix0 = static_cast<int>(std::floor(x0));
            const int ix1 = std::min(rect.right, static_cast<int>(std::ceil(x1)));
            double r = 0, g = 0, b = 0, a = 0, plain = 0, weight = 0;
            double pr = 0, pg = 0, pb = 0;
            for (int py = iy0; py < iy1; ++py) {
                const double wy = std::min(y1, static_cast<double>(py) + 1.0) - std::max(y0, static_cast<double>(py));
                if (wy <= 0) {
                    continue;
                }
                const std::uint8_t* row =
                    &frame.rgba[(static_cast<std::size_t>(py) * static_cast<std::size_t>(frame.width) +
                                 static_cast<std::size_t>(ix0)) * 4u];
                for (int px = ix0; px < ix1; ++px, row += 4) {
                    const double wx =
                        std::min(x1, static_cast<double>(px) + 1.0) - std::max(x0, static_cast<double>(px));
                    if (wx <= 0) {
                        continue;
                    }
                    const double w = wx * wy;
                    const double alpha = row[3] / 255.0;
                    r += w * alpha * row[0];
                    g += w * alpha * row[1];
                    b += w * alpha * row[2];
                    pr += w * row[0];
                    pg += w * row[1];
                    pb += w * row[2];
                    a += w * alpha;
                    plain += w;
                    weight += w;
                }
            }
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                  static_cast<std::size_t>(x);
            if (weight > 0) {
                grid.a[i] = static_cast<float>(a / weight);
                if (a > 1e-6) {
                    grid.r[i] = static_cast<float>(r / a / 255.0);
                    grid.g[i] = static_cast<float>(g / a / 255.0);
                    grid.b[i] = static_cast<float>(b / a / 255.0);
                } else {
                    grid.r[i] = static_cast<float>(pr / plain / 255.0);
                    grid.g[i] = static_cast<float>(pg / plain / 255.0);
                    grid.b[i] = static_cast<float>(pb / plain / 255.0);
                }
            }
        }
    }
    return grid;
}

float median(std::vector<float>& values) {
    if (values.empty()) {
        return 0.0f;
    }
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

}  // namespace

namespace ref {

Grid makeGrid(const Image& frame, const ReferenceOptions& options, int longestSide) {
    const Rect rect = cropOf(frame, options);
    int width = 0, height = 0;
    gridSize(rect, longestSide, width, height);
    return averageDown(frame, rect, width, height);
}

BackdropRead readBackdrop(const std::vector<const Grid*>& frames, const ReferenceOptions& options) {
    BackdropRead read;
    if (frames.empty()) {
        return read;
    }
    // See-through pixels anywhere mean the picture carries its own cut-out.
    std::size_t clear = 0, all = 0;
    for (const Grid* grid : frames) {
        for (float a : grid->a) {
            clear += a < 0.98f ? 1u : 0u;
        }
        all += grid->a.size();
    }
    const bool hasAlpha = all > 0 && static_cast<double>(clear) / static_cast<double>(all) > 0.02;

    // The colour round the edge of the picture, where the effect usually is not.
    std::vector<float> br, bg, bb;
    for (const Grid* grid : frames) {
        const int w = grid->width, h = grid->height;
        const int band = std::max(1, static_cast<int>(std::lround(0.04 * std::min(w, h))));
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (x >= band && x < w - band && y >= band && y < h - band) {
                    continue;
                }
                const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                      static_cast<std::size_t>(x);
                br.push_back(grid->r[i]);
                bg.push_back(grid->g[i]);
                bb.push_back(grid->b[i]);
            }
        }
    }
    std::vector<float> sr = br, sg = bg, sb = bb;
    Swatch edge;
    edge.r = median(sr);
    edge.g = median(sg);
    edge.b = median(sb);
    double spread = 0;
    for (std::size_t i = 0; i < br.size(); ++i) {
        spread += std::max({std::fabs(br[i] - edge.r), std::fabs(bg[i] - edge.g), std::fabs(bb[i] - edge.b)});
    }
    spread = br.empty() ? 0.0 : spread / static_cast<double>(br.size());

    Backdrop kind = options.backdrop;
    if (kind == Backdrop::Auto) {
        kind = hasAlpha ? Backdrop::Transparent
                        : (luminance(edge.r, edge.g, edge.b) < 0.4f ? Backdrop::Dark : Backdrop::Light);
    }
    read.kind = kind;
    switch (kind) {
        case Backdrop::Transparent:
            read.colour = Swatch{0, 0, 0, 0};
            read.evenness = 1.0f;
            read.cutoff = 0.02f;
            read.additive = false;
            break;
        case Backdrop::Colour:
            read.colour = Swatch{clamp01(options.keyR), clamp01(options.keyG), clamp01(options.keyB), 0};
            // How even the background is can only be judged against the colour picked.
            {
                double off = 0;
                for (std::size_t i = 0; i < br.size(); ++i) {
                    off += std::max({std::fabs(br[i] - read.colour.r), std::fabs(bg[i] - read.colour.g),
                                     std::fabs(bb[i] - read.colour.b)});
                }
                spread = br.empty() ? 0.0 : off / static_cast<double>(br.size());
            }
            read.evenness = clamp01(1.0f - static_cast<float>(spread) / 0.25f);
            read.cutoff = std::clamp(0.035f + 1.5f * static_cast<float>(spread), 0.035f, 0.3f);
            read.additive = luminance(read.colour.r, read.colour.g, read.colour.b) < 0.4f;
            break;
        case Backdrop::Dark:
        case Backdrop::Light:
        case Backdrop::Auto:
            read.colour = edge;
            if (options.backdrop == Backdrop::Dark && luminance(edge.r, edge.g, edge.b) >= 0.4f) {
                read.colour = Swatch{0, 0, 0, 0};  // told it is dark, and the edge is not: take black
            }
            if (options.backdrop == Backdrop::Light && luminance(edge.r, edge.g, edge.b) < 0.4f) {
                read.colour = Swatch{1, 1, 1, 0};
            }
            read.evenness = clamp01(1.0f - static_cast<float>(spread) / 0.25f);
            read.cutoff = std::clamp(0.035f + 1.5f * static_cast<float>(spread), 0.035f, 0.3f);
            read.additive = kind == Backdrop::Dark;
            break;
    }
    if (options.cutoff >= 0.0f) {
        read.cutoff = std::clamp(options.cutoff, 0.0f, 0.9f);
    }
    return read;
}

BackdropRead backdropFrom(const StillAnalysis& still) {
    BackdropRead read;
    read.kind = still.backdrop;
    read.colour = still.backdropColour;
    read.evenness = still.backdropEvenness;
    read.cutoff = still.cutoff;
    read.additive = still.additive;
    return read;
}

// "Colour to alpha": the least amount of some colour that, laid over the
// background, gives the pixel seen. For a black background this is simply
// the brightest channel, and the colour is the pixel at full brightness.
Matte matteFromGrid(const Grid& grid, const BackdropRead& backdrop) {
    Matte matte;
    matte.width = grid.width;
    matte.height = grid.height;
    const std::size_t n = grid.a.size();
    matte.cover.assign(n, 0.0f);
    matte.r.assign(n, 0.0f);
    matte.g.assign(n, 0.0f);
    matte.b.assign(n, 0.0f);
    const float cut = backdrop.cutoff;
    const float keep = 1.0f / std::max(1e-3f, 1.0f - cut);
    if (backdrop.kind == Backdrop::Transparent) {
        for (std::size_t i = 0; i < n; ++i) {
            const float a = grid.a[i];
            matte.cover[i] = a > cut ? (a - cut) * keep : 0.0f;
            matte.r[i] = grid.r[i];
            matte.g[i] = grid.g[i];
            matte.b[i] = grid.b[i];
        }
        return matte;
    }
    const float back[3] = {backdrop.colour.r, backdrop.colour.g, backdrop.colour.b};
    for (std::size_t i = 0; i < n; ++i) {
        const float pixel[3] = {grid.r[i], grid.g[i], grid.b[i]};
        float amount = 0.0f;
        for (int c = 0; c < 3; ++c) {
            float need = 0.0f;
            if (pixel[c] > back[c]) {
                need = (pixel[c] - back[c]) / std::max(1e-3f, 1.0f - back[c]);
            } else if (pixel[c] < back[c]) {
                need = (back[c] - pixel[c]) / std::max(1e-3f, back[c]);
            }
            amount = std::max(amount, need);
        }
        amount = clamp01(amount);
        float colour[3] = {pixel[0], pixel[1], pixel[2]};
        if (amount > 1e-3f) {
            for (int c = 0; c < 3; ++c) {
                colour[c] = clamp01(back[c] + (pixel[c] - back[c]) / amount);
            }
        }
        matte.cover[i] = amount > cut ? (amount - cut) * keep : 0.0f;
        matte.r[i] = colour[0];
        matte.g[i] = colour[1];
        matte.b[i] = colour[2];
    }
    return matte;
}

namespace {

void blurPass(std::vector<float>& values, std::vector<float>& scratch, int width, int height, int radius) {
    const float scale = 1.0f / static_cast<float>(2 * radius + 1);
    // Across.
    for (int y = 0; y < height; ++y) {
        const float* in = &values[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)];
        float* out = &scratch[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)];
        float sum = 0.0f;
        for (int x = -radius; x <= radius; ++x) {
            sum += in[std::clamp(x, 0, width - 1)];
        }
        for (int x = 0; x < width; ++x) {
            out[x] = sum * scale;
            sum += in[std::min(width - 1, x + radius + 1)] - in[std::max(0, x - radius)];
        }
    }
    // Down.
    for (int x = 0; x < width; ++x) {
        float sum = 0.0f;
        const auto at = [&](int y) {
            return scratch[static_cast<std::size_t>(std::clamp(y, 0, height - 1)) * static_cast<std::size_t>(width) +
                           static_cast<std::size_t>(x)];
        };
        for (int y = -radius; y <= radius; ++y) {
            sum += at(y);
        }
        for (int y = 0; y < height; ++y) {
            values[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
                sum * scale;
            sum += at(y + radius + 1) - at(y - radius);
        }
    }
}

template <class Pick>
void extreme(std::vector<float>& values, int width, int height, int radius, Pick pick) {
    std::vector<float> scratch(values.size());
    for (int y = 0; y < height; ++y) {
        const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        for (int x = 0; x < width; ++x) {
            float v = values[row + static_cast<std::size_t>(x)];
            for (int k = std::max(0, x - radius); k <= std::min(width - 1, x + radius); ++k) {
                v = pick(v, values[row + static_cast<std::size_t>(k)]);
            }
            scratch[row + static_cast<std::size_t>(x)] = v;
        }
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float v = scratch[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
            for (int k = std::max(0, y - radius); k <= std::min(height - 1, y + radius); ++k) {
                v = pick(v, scratch[static_cast<std::size_t>(k) * static_cast<std::size_t>(width) +
                                    static_cast<std::size_t>(x)]);
            }
            values[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = v;
        }
    }
}

}  // namespace

void blur(std::vector<float>& values, int width, int height, int radius) {
    if (radius < 1 || values.empty()) {
        return;
    }
    std::vector<float> scratch(values.size());
    blurPass(values, scratch, width, height, radius);
    blurPass(values, scratch, width, height, radius);
}

void erode(std::vector<float>& values, int width, int height, int radius) {
    if (radius >= 1) {
        extreme(values, width, height, radius, [](float a, float b) { return a < b ? a : b; });
    }
}

void dilate(std::vector<float>& values, int width, int height, int radius) {
    if (radius >= 1) {
        extreme(values, width, height, radius, [](float a, float b) { return a > b ? a : b; });
    }
}

std::vector<Patch> findPatches(const std::vector<float>& values, int width, int height, float threshold) {
    std::vector<Patch> patches;
    std::vector<char> seen(values.size(), 0);
    std::vector<int> stack;
    for (int start = 0; start < width * height; ++start) {
        if (seen[static_cast<std::size_t>(start)] || !(values[static_cast<std::size_t>(start)] > threshold)) {
            continue;
        }
        Patch patch;
        patch.left = width;
        patch.top = height;
        patch.right = -1;
        patch.bottom = -1;
        double sx = 0, sy = 0, mass = 0;
        stack.push_back(start);
        seen[static_cast<std::size_t>(start)] = 1;
        while (!stack.empty()) {
            const int at = stack.back();
            stack.pop_back();
            const int x = at % width, y = at / width;
            const float v = values[static_cast<std::size_t>(at)];
            patch.points.push_back(at);
            mass += v;
            sx += v * x;
            sy += v * y;
            patch.peak = std::max(patch.peak, v);
            patch.left = std::min(patch.left, x);
            patch.right = std::max(patch.right, x);
            patch.top = std::min(patch.top, y);
            patch.bottom = std::max(patch.bottom, y);
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                        continue;
                    }
                    const int next = ny * width + nx;
                    if (!seen[static_cast<std::size_t>(next)] && values[static_cast<std::size_t>(next)] > threshold) {
                        seen[static_cast<std::size_t>(next)] = 1;
                        stack.push_back(next);
                    }
                }
            }
        }
        patch.area = static_cast<int>(patch.points.size());
        patch.mass = static_cast<float>(mass);
        patch.x = static_cast<float>(sx / mass);
        patch.y = static_cast<float>(sy / mass);
        // Its spread in each direction, plus a twelfth for the size of a point.
        double xx = 0, xy = 0, yy = 0;
        for (int at : patch.points) {
            const double v = values[static_cast<std::size_t>(at)];
            const double dx = at % width - patch.x, dy = at / width - patch.y;
            xx += v * dx * dx;
            xy += v * dx * dy;
            yy += v * dy * dy;
        }
        xx = xx / mass + 1.0 / 12.0;
        yy = yy / mass + 1.0 / 12.0;
        xy /= mass;
        const double mean = 0.5 * (xx + yy);
        const double diff = std::sqrt(std::max(0.0, 0.25 * (xx - yy) * (xx - yy) + xy * xy));
        const double l1 = mean + diff, l2 = std::max(1.0 / 12.0, mean - diff);
        patch.major = static_cast<float>(4.0 * std::sqrt(l1));
        patch.minor = static_cast<float>(4.0 * std::sqrt(l2));
        if (std::fabs(xy) > 1e-9 || std::fabs(xx - yy) > 1e-9) {
            const double angle = 0.5 * std::atan2(2.0 * xy, xx - yy);
            patch.axisX = static_cast<float>(std::cos(angle));
            patch.axisY = static_cast<float>(std::sin(angle));
        }
        patches.push_back(std::move(patch));
    }
    return patches;
}

float readAt(const std::vector<float>& values, int width, int height, float x, float y) {
    if (!(x > -1.0f && y > -1.0f && x < static_cast<float>(width) && y < static_cast<float>(height))) {
        return 0.0f;
    }
    const float fx = std::floor(x), fy = std::floor(y);
    const int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
    const float tx = x - fx, ty = y - fy;
    const auto at = [&](int px, int py) {
        if (px < 0 || py < 0 || px >= width || py >= height) {
            return 0.0f;
        }
        return values[static_cast<std::size_t>(py) * static_cast<std::size_t>(width) + static_cast<std::size_t>(px)];
    };
    return (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) +
           (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
}

Basics measureBasics(const Matte& matte) {
    Basics out;
    const int w = matte.width, h = matte.height;
    double total = 0, sx = 0, sy = 0, r = 0, g = 0, b = 0;
    std::size_t lit = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = matte.at(x, y);
            const double c = matte.cover[i];
            if (c <= 0) {
                continue;
            }
            total += c;
            sx += c * x;
            sy += c * y;
            r += c * matte.r[i];
            g += c * matte.g[i];
            b += c * matte.b[i];
            lit += c > 0.1 ? 1u : 0u;
        }
    }
    out.total = static_cast<float>(total);
    out.fill = static_cast<float>(lit) / static_cast<float>(matte.cover.size());
    if (!(total > 1e-6)) {
        out.centreX = out.hotX = 0.5f * static_cast<float>(w);
        out.centreY = out.hotY = 0.5f * static_cast<float>(h);
        return out;
    }
    out.centreX = static_cast<float>(sx / total);
    out.centreY = static_cast<float>(sy / total);
    out.colour = Swatch{static_cast<float>(r / total), static_cast<float>(g / total),
                        static_cast<float>(b / total), 1.0f};

    // The brightest area: the middle of the top of a blurred copy.
    std::vector<float> soft = matte.cover;
    blur(soft, w, h, std::max(1, static_cast<int>(std::lround(0.02 * h))));
    float top = 0.0f;
    for (float v : soft) {
        top = std::max(top, v);
    }
    double hx = 0, hy = 0, hm = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float v = soft[matte.at(x, y)];
            if (v >= 0.92f * top) {
                hx += static_cast<double>(v) * x;
                hy += static_cast<double>(v) * y;
                hm += v;
            }
        }
    }
    out.hotX = hm > 0 ? static_cast<float>(hx / hm) : out.centreX;
    out.hotY = hm > 0 ? static_cast<float>(hy / hm) : out.centreY;

    // Spread, and the radii that hold half and nearly all of it.
    double xx = 0, xy = 0, yy = 0;
    std::vector<std::pair<float, float>> byDistance;
    byDistance.reserve(matte.cover.size() / 4);
    std::vector<float> bright;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float c = matte.cover[matte.at(x, y)];
            if (c <= 0) {
                continue;
            }
            const double dx = x - out.centreX, dy = y - out.centreY;
            xx += c * dx * dx;
            xy += c * dx * dy;
            yy += c * dy * dy;
            byDistance.emplace_back(static_cast<float>(std::sqrt(dx * dx + dy * dy)), c);
            if (c > 0.05f) {
                bright.push_back(c);
            }
        }
    }
    out.varXX = static_cast<float>(xx / total);
    out.varXY = static_cast<float>(xy / total);
    out.varYY = static_cast<float>(yy / total);
    std::sort(byDistance.begin(), byDistance.end());
    double running = 0;
    bool half = false;
    for (const auto& [distance, c] : byDistance) {
        running += c;
        if (!half && running >= 0.5 * total) {
            out.halfExtent = distance;
            half = true;
        }
        if (running >= 0.95 * total) {
            out.extent = distance;
            break;
        }
    }
    out.extent = std::max(out.extent, 1.0f);
    out.halfExtent = std::max(out.halfExtent, 0.5f);
    if (!bright.empty()) {
        const std::size_t keep = std::max<std::size_t>(1, bright.size() / 20);
        std::nth_element(bright.begin(), bright.begin() + static_cast<std::ptrdiff_t>(keep - 1), bright.end(),
                         std::greater<float>());
        double sum = 0;
        for (std::size_t i = 0; i < keep; ++i) {
            sum += bright[i];
        }
        out.brightness = static_cast<float>(sum / static_cast<double>(keep));
    }
    return out;
}

}  // namespace ref

// ------------------------------------------------------- public: one frame

Result<Matte> makeMatte(const Image& frame, const ReferenceOptions& options, StillAnalysis* describe) {
    if (frame.width < 8 || frame.height < 8 ||
        frame.rgba.size() != static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 4u) {
        return makeError("The reference picture is too small or damaged.");
    }
    const Grid grid = makeGrid(frame, options, options.detail);
    const BackdropRead backdrop = readBackdrop({&grid}, options);
    if (describe) {
        describe->backdrop = backdrop.kind;
        describe->backdropColour = backdrop.colour;
        describe->backdropEvenness = backdrop.evenness;
        describe->cutoff = backdrop.cutoff;
        describe->additive = backdrop.additive;
    }
    return matteFromGrid(grid, backdrop);
}

Image referencePicture(const Reference& reference, const ReferenceOptions& options, int frame, int maxSide) {
    Image out;
    if (reference.frames.empty()) {
        return out;
    }
    const Image& source = reference.frames[static_cast<std::size_t>(
        std::clamp(frame, 0, static_cast<int>(reference.frames.size()) - 1))];
    const Rect rect = cropOf(source, options);
    int width = 0, height = 0;
    gridSize(rect, std::max(16, maxSide), width, height);
    const Grid grid = averageDown(source, rect, width, height);
    out.width = width;
    out.height = height;
    out.rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (std::size_t i = 0; i < grid.a.size(); ++i) {
        out.rgba[i * 4 + 0] = static_cast<std::uint8_t>(std::lround(clamp01(grid.r[i]) * 255.0f));
        out.rgba[i * 4 + 1] = static_cast<std::uint8_t>(std::lround(clamp01(grid.g[i]) * 255.0f));
        out.rgba[i * 4 + 2] = static_cast<std::uint8_t>(std::lround(clamp01(grid.b[i]) * 255.0f));
        out.rgba[i * 4 + 3] = static_cast<std::uint8_t>(std::lround(clamp01(grid.a[i]) * 255.0f));
    }
    return out;
}

// ----------------------------------------------------------- colour words

std::string hexColour(const Swatch& colour) {
    char text[16];
    std::snprintf(text, sizeof text, "#%02x%02x%02x",
                  static_cast<unsigned>(std::lround(clamp01(colour.r) * 255.0f)),
                  static_cast<unsigned>(std::lround(clamp01(colour.g) * 255.0f)),
                  static_cast<unsigned>(std::lround(clamp01(colour.b) * 255.0f)));
    return text;
}

std::string colourName(const Swatch& colour) {
    const float hi = most(colour.r, colour.g, colour.b), lo = least(colour.r, colour.g, colour.b);
    const float sat = saturation(colour.r, colour.g, colour.b);
    if (hi < 0.12f) {
        return "black";
    }
    if (sat < 0.12f) {
        return hi > 0.85f ? "white" : (hi > 0.45f ? "grey" : "dark grey");
    }
    float hue = 0.0f;
    const float span = hi - lo;
    if (hi == colour.r) {
        hue = 60.0f * std::fmod((colour.g - colour.b) / span, 6.0f);
    } else if (hi == colour.g) {
        hue = 60.0f * ((colour.b - colour.r) / span + 2.0f);
    } else {
        hue = 60.0f * ((colour.r - colour.g) / span + 4.0f);
    }
    if (hue < 0) {
        hue += 360.0f;
    }
    const char* word = "red";
    if (hue < 15) word = "red";
    else if (hue < 40) word = "orange";
    else if (hue < 65) word = "yellow";
    else if (hue < 95) word = "lime";
    else if (hue < 150) word = "green";
    else if (hue < 190) word = "cyan";
    else if (hue < 250) word = "blue";
    else if (hue < 285) word = "purple";
    else if (hue < 335) word = "pink";
    std::string name;
    if (sat < 0.4f && hi > 0.7f) {
        name = "pale ";
    } else if (hi < 0.45f) {
        name = "dark ";
    }
    return name + word;
}

// ------------------------------------------------- measuring built-in shapes

namespace {

constexpr int kTile = 40;  // a shape is studied on a grid this many points a side

struct ShapeTile {
    std::array<float, kTile * kTile> cover{};
    float mean = 0, deviation = 0;
    // Where the shape itself is lighter (above 0) or darker (below).
    std::array<float, kTile * kTile> tone{};
    // The same with the round part taken away: only rays and points remain.
    std::array<float, kTile * kTile> rays{};
    float raysMean = 0, raysDeviation = 0;
    ShapeMeasure measure;
};

// Takes away from a tile the level reached nearly all the way round each
// circle (its lower quarter), leaving what is not round.
void removeRound(std::array<float, kTile * kTile>& tile) {
    constexpr int kRings = kTile;  // half-point steps out to the corner
    std::array<std::vector<float>, kRings> rings;
    const auto ringOf = [](int x, int y) {
        const float dx = static_cast<float>(x) + 0.5f - kTile * 0.5f, dy = static_cast<float>(y) + 0.5f - kTile * 0.5f;
        return std::min(kRings - 1, static_cast<int>(std::sqrt(dx * dx + dy * dy) * 1.4f));
    };
    for (int y = 0; y < kTile; ++y) {
        for (int x = 0; x < kTile; ++x) {
            rings[static_cast<std::size_t>(ringOf(x, y))].push_back(tile[static_cast<std::size_t>(y * kTile + x)]);
        }
    }
    std::array<float, kRings> middle{};
    for (std::size_t r = 0; r < kRings; ++r) {
        if (!rings[r].empty()) {
            const auto mid = rings[r].begin() + static_cast<std::ptrdiff_t>(rings[r].size() / 4);
            std::nth_element(rings[r].begin(), mid, rings[r].end());
            middle[r] = *mid;
        }
    }
    for (int y = 0; y < kTile; ++y) {
        for (int x = 0; x < kTile; ++x) {
            float& v = tile[static_cast<std::size_t>(y * kTile + x)];
            v = std::max(0.0f, v - middle[static_cast<std::size_t>(ringOf(x, y))]);
        }
    }
}

void meanAndDeviation(const std::array<float, kTile * kTile>& tile, float& mean, float& deviation) {
    double total = 0;
    for (float c : tile) {
        total += c;
    }
    mean = static_cast<float>(total / (kTile * kTile));
    double var = 0;
    for (float c : tile) {
        var += (c - mean) * (c - mean);
    }
    deviation = static_cast<float>(std::sqrt(var / (kTile * kTile)));
}

ShapeTile makeTile(SpriteShape shape) {
    ShapeTile tile;
    const float pixel = 2.0f / kTile;
    double total = 0, shine = 0;
    for (int y = 0; y < kTile; ++y) {
        for (int x = 0; x < kTile; ++x) {
            const float px = (static_cast<float>(x) + 0.5f) * pixel - 1.0f;
            const float py = 1.0f - (static_cast<float>(y) + 0.5f) * pixel;
            const ShapeSample sample = sampleShape(shape, px, py, pixel, pixel);
            const float c = clamp01(sample.cover);
            tile.cover[static_cast<std::size_t>(y * kTile + x)] = c;
            tile.tone[static_cast<std::size_t>(y * kTile + x)] = sample.tone + clamp01(sample.shine);
            total += c;
            shine += c * clamp01(sample.shine);
        }
    }
    tile.measure.shine = total > 1e-6 ? static_cast<float>(shine / total) : 0.0f;
    {
        double mx = 0, my = 0;
        for (int y = 0; y < kTile; ++y) {
            for (int x = 0; x < kTile; ++x) {
                const float c = tile.cover[static_cast<std::size_t>(y * kTile + x)];
                mx += c * ((static_cast<float>(x) + 0.5f) * pixel - 1.0f);
                my += c * (1.0f - (static_cast<float>(y) + 0.5f) * pixel);
            }
        }
        if (total > 1e-6) {
            tile.measure.middleX = static_cast<float>(mx / total);
            tile.measure.middleY = static_cast<float>(my / total);
        }
    }
    (void)total;
    meanAndDeviation(tile.cover, tile.mean, tile.deviation);
    tile.rays = tile.cover;
    removeRound(tile.rays);
    meanAndDeviation(tile.rays, tile.raysMean, tile.raysDeviation);

    // Round averages from the middle outward, on a finer reading.
    constexpr int kSteps = 64, kAround = 96;
    std::array<float, kSteps> ringMean{};
    for (int s = 0; s < kSteps; ++s) {
        const float radius = (static_cast<float>(s) + 0.5f) * 1.4f / kSteps;
        double sum = 0;
        for (int a = 0; a < kAround; ++a) {
            const float angle = 2.0f * kPi * static_cast<float>(a) / kAround;
            const float px = radius * std::cos(angle), py = radius * std::sin(angle);
            if (std::fabs(px) <= 1.0f && std::fabs(py) <= 1.0f) {
                sum += clamp01(shapeCoverage(shape, px, py, 0.02f, 0.02f));
            }
        }
        ringMean[static_cast<std::size_t>(s)] = static_cast<float>(sum / kAround);
    }
    ShapeMeasure& m = tile.measure;
    m.centre = clamp01(shapeCoverage(shape, 0.0f, 0.0f, 0.02f, 0.02f));
    double mass = 0;
    for (int s = 0; s < kSteps; ++s) {
        mass += ringMean[static_cast<std::size_t>(s)] * (static_cast<float>(s) + 0.5f);
    }
    double running = 0;
    m.extent = 1.0f;
    for (int s = 0; s < kSteps; ++s) {
        running += ringMean[static_cast<std::size_t>(s)] * (static_cast<float>(s) + 0.5f);
        if (running >= 0.95 * mass) {
            m.extent = (static_cast<float>(s) + 1.0f) * 1.4f / kSteps;
            break;
        }
    }
    const float start = std::max(ringMean[0], 1e-3f);
    m.halfLevel = 1.0f;
    m.outer = 1.0f;
    bool gotHalf = false;
    float best = 0.0f;
    for (int s = 0; s < kSteps; ++s) {
        const float radius = (static_cast<float>(s) + 0.5f) * 1.4f / kSteps;
        const float v = ringMean[static_cast<std::size_t>(s)];
        if (!gotHalf && v < 0.5f * start) {
            m.halfLevel = radius;
            gotHalf = true;
        }
        if (v >= 0.05f * std::max(start, 0.2f)) {
            m.outer = radius;
        }
        if (v > best) {
            best = v;
            m.ringRadius = radius;
        }
        if (v > 0.004f) {
            m.reach = std::min(1.4f, radius + 0.7f / kSteps);
        }
    }
    return tile;
}

const ShapeTile& tileOf(SpriteShape shape) {
    static const std::array<ShapeTile, kSpriteShapeCount> tiles = [] {
        std::array<ShapeTile, kSpriteShapeCount> all{};
        for (int i = 0; i < kSpriteShapeCount; ++i) {
            all[static_cast<std::size_t>(i)] = makeTile(static_cast<SpriteShape>(i));
        }
        return all;
    }();
    return tiles[static_cast<std::size_t>(shape)];
}

}  // namespace

const ShapeMeasure& measureShape(SpriteShape shape) { return tileOf(shape).measure; }

float matchShape(const Matte& matte, float cx, float cy, float halfSize, SpriteShape shape, float* bestTurn,
                 bool raysOnly) {
    const ShapeTile& tile = tileOf(shape);
    const auto& want = raysOnly ? tile.rays : tile.cover;
    const float wantMean = raysOnly ? tile.raysMean : tile.mean;
    const float wantDeviation = raysOnly ? tile.raysDeviation : tile.deviation;
    if (!(halfSize > 0.5f) || wantDeviation < 1e-4f) {
        return -1.0f;
    }
    constexpr int kTurns = 48;
    float best = -2.0f, bestAngle = 0.0f;
    std::array<float, kTile * kTile> patch{};
    for (int turn = 0; turn < kTurns; ++turn) {
        const float angle = 2.0f * kPi * static_cast<float>(turn) / kTurns;
        const float ca = std::cos(angle), sa = std::sin(angle);
        for (int y = 0; y < kTile; ++y) {
            for (int x = 0; x < kTile; ++x) {
                // Shape space, y up.
                const float sx = (static_cast<float>(x) + 0.5f) * 2.0f / kTile - 1.0f;
                const float sy = 1.0f - (static_cast<float>(y) + 0.5f) * 2.0f / kTile;
                // Where that point of the shape lands in the picture once
                // the shape is turned counter-clockwise by the angle.
                const float wx = sx * ca - sy * sa, wy = sx * sa + sy * ca;
                patch[static_cast<std::size_t>(y * kTile + x)] =
                    readAt(matte.cover, matte.width, matte.height, cx + wx * halfSize, cy - wy * halfSize);
            }
        }
        if (raysOnly) {
            removeRound(patch);
        }
        float mean = 0, deviation = 0;
        meanAndDeviation(patch, mean, deviation);
        if (deviation < 1e-4f) {
            continue;
        }
        double cross = 0;
        for (std::size_t i = 0; i < patch.size(); ++i) {
            cross += (patch[i] - mean) * (want[i] - wantMean);
        }
        const float score = static_cast<float>(cross / (kTile * kTile)) / (deviation * wantDeviation);
        if (score > best) {
            best = score;
            bestAngle = angle;
        }
    }
    if (bestTurn) {
        *bestTurn = bestAngle * 180.0f / kPi;
    }
    return best < -1.0f ? -1.0f : best;
}

float matchOutline(const Matte& matte, float cx, float cy, float halfSize, SpriteShape shape, float* bestTurn,
                   float* overlapOut) {
    const ShapeTile& tile = tileOf(shape);
    if (!(halfSize > 0.5f)) {
        return 0.0f;
    }
    constexpr int kTurns = 48;
    float best = -1.0f, bestAngle = 0.0f;
    std::array<float, kTile * kTile> patch{};
    const auto sample = [&](const std::vector<float>& plane, float angle, std::array<float, kTile * kTile>& out) {
        const float ca = std::cos(angle), sa = std::sin(angle);
        for (int y = 0; y < kTile; ++y) {
            for (int x = 0; x < kTile; ++x) {
                // The patch's middle (cx, cy) is laid on the shape's own
                // middle, which for a lopsided shape is not its centre.
                const float sx = (static_cast<float>(x) + 0.5f) * 2.0f / kTile - 1.0f - tile.measure.middleX;
                const float sy = 1.0f - (static_cast<float>(y) + 0.5f) * 2.0f / kTile - tile.measure.middleY;
                const float wx = sx * ca - sy * sa, wy = sx * sa + sy * ca;
                out[static_cast<std::size_t>(y * kTile + x)] =
                    readAt(plane, matte.width, matte.height, cx + wx * halfSize, cy - wy * halfSize);
            }
        }
    };
    for (int turn = 0; turn < kTurns; ++turn) {
        const float angle = 2.0f * kPi * static_cast<float>(turn) / kTurns;
        sample(matte.cover, angle, patch);
        float both = 0.0f, either = 0.0f;
        for (std::size_t i = 0; i < patch.size(); ++i) {
            const float a = clamp01((patch[i] - 0.3f) / 0.4f), b = clamp01((tile.cover[i] - 0.3f) / 0.4f);
            both += std::min(a, b);
            either += std::max(a, b);
        }
        const float overlap = either > 1e-3f ? both / either : 0.0f;
        if (overlap > best) {
            best = overlap;
            bestAngle = angle;
        }
    }
    if (overlapOut) {
        *overlapOut = best;
    }
    // Light and dark in the same places, at the turn that fitted best.
    {
        std::array<float, kTile * kTile> r{}, g{}, b{};
        sample(matte.cover, bestAngle, patch);
        sample(matte.r, bestAngle, r);
        sample(matte.g, bestAngle, g);
        sample(matte.b, bestAngle, b);
        double sa = 0, sb = 0;
        int n = 0;
        for (std::size_t i = 0; i < patch.size(); ++i) {
            if (patch[i] > 0.7f && tile.cover[i] > 0.7f) {
                sa += luminance(r[i], g[i], b[i]);
                sb += tile.tone[i];
                ++n;
            }
        }
        if (n > 20) {
            const double ma = sa / n, mb = sb / n;
            double cross = 0, va = 0, vb = 0;
            for (std::size_t i = 0; i < patch.size(); ++i) {
                if (patch[i] > 0.7f && tile.cover[i] > 0.7f) {
                    const double da = luminance(r[i], g[i], b[i]) - ma, db = tile.tone[i] - mb;
                    cross += da * db;
                    va += da * da;
                    vb += db * db;
                }
            }
            const bool flatPicture = va / n < 0.0004, flatShape = vb / n < 0.0004;
            if (flatPicture && flatShape) {
                best += 0.08f;  // both plain: that is a match too
            } else if (!flatPicture && !flatShape) {
                best += 0.2f * std::max(0.0f, static_cast<float>(cross / std::sqrt(va * vb)));
            }
        }
    }
    if (bestTurn) {
        *bestTurn = bestAngle * 180.0f / kPi;
    }
    return best;
}

// ------------------------------------------------------- the whole analysis

Result<ReferenceAnalysis> analyzeReference(const Reference& reference, const ReferenceOptions& options,
                                           const Progress& progress) {
    if (Status ok = validateReference(reference); !ok) {
        return ok.error();
    }
    const auto began = std::chrono::steady_clock::now();
    const auto step = [&](float done, const char* stage) { return !progress || progress(done, stage); };
    const auto cancelled = [] { return makeError("The analysis was cancelled."); };

    int first = 0, last = 0;
    frameRange(reference, options, first, last);
    const bool moving = reference.moving() && last > first;
    const int count = last - first + 1;

    if (!step(0.02f, "Reading the reference")) {
        return cancelled();
    }

    // A clip is studied on a coarser grid than a picture: there are many frames.
    const int detail = std::clamp(options.detail, 64, 320);
    const int clipDetail = std::min(detail, 128);

    // The background is judged from the start, middle and end together.
    std::vector<Grid> probe;
    probe.push_back(makeGrid(reference.frames[static_cast<std::size_t>(first)], options, clipDetail));
    if (moving) {
        probe.push_back(makeGrid(reference.frames[static_cast<std::size_t>((first + last) / 2)], options, clipDetail));
        probe.push_back(makeGrid(reference.frames[static_cast<std::size_t>(last)], options, clipDetail));
    }
    std::vector<const Grid*> probes;
    for (const Grid& g : probe) {
        probes.push_back(&g);
    }
    const BackdropRead backdrop = readBackdrop(probes, options);

    ReferenceAnalysis analysis;
    int stillFrame = first;

    if (moving) {
        std::vector<Matte> mattes;
        mattes.reserve(static_cast<std::size_t>(count));
        for (int f = first; f <= last; ++f) {
            if (!step(0.05f + 0.45f * static_cast<float>(f - first) / static_cast<float>(count),
                      "Separating the effect from the background")) {
                return cancelled();
            }
            mattes.push_back(
                matteFromGrid(makeGrid(reference.frames[static_cast<std::size_t>(f)], options, clipDetail), backdrop));
        }
        bool stop = false;
        const double fps = reference.framesPerSecond * (options.speed > 0.01 ? options.speed : 1.0);
        analysis.time = analyzeTime(mattes, fps, [&](float done, const char* stage) {
            return step(0.5f + 0.25f * done, stage);
        }, stop);
        if (stop) {
            return cancelled();
        }
        // The frame that best stands for the effect is described in detail.
        const double fps2 = fps;
        const auto local = [&](double seconds) {
            return std::clamp(static_cast<int>(std::lround(seconds * fps2)), 0, count - 1);
        };
        stillFrame = first + local(analysis.time.main);
        // And, when the brightest moment came clearly earlier, that too.
        const double life = std::max(0.05, analysis.time.end - analysis.time.start);
        if (analysis.time.available && !analysis.time.continuous &&
            analysis.time.main - analysis.time.peak >= std::max(0.1, 0.15 * life)) {
            analysis.hasEarly = true;
            analysis.earlyFrame = first + local(analysis.time.peak);
            analysis.earlyTime = analysis.time.peak;
            analysis.earlyEnd = 0.5 * (analysis.time.peak + analysis.time.main);
        }
    }

    if (!step(0.78f, "Measuring shapes and colours")) {
        return cancelled();
    }
    const Grid grid = makeGrid(reference.frames[static_cast<std::size_t>(stillFrame)], options, detail);
    const Matte matte = matteFromGrid(grid, backdrop);
    analysis.still = analyzeStill(matte, backdrop);
    analysis.stillFrame = stillFrame;
    if (analysis.hasEarly) {
        const Matte early =
            matteFromGrid(makeGrid(reference.frames[static_cast<std::size_t>(analysis.earlyFrame)], options, detail), backdrop);
        analysis.early = analyzeStill(early, backdrop);
        // Only a flash with a form of its own is worth a layer of its own.
        if (analysis.early.fill <= 0.0f || !(analysis.early.hasBody || analysis.early.rays >= 2)) {
            analysis.hasEarly = false;
        }
    }
    if (analysis.still.fill <= 0.0f || analysis.still.extent <= 0.0f) {
        return makeError("No effect could be found in the reference.",
                         "Nothing stands out from the background. Try another background setting, a lower "
                         "cut-off, or crop closer to the effect.");
    }

    if (!step(0.95f, "Writing the report")) {
        return cancelled();
    }
    writeFindings(analysis, moving);
    analysis.cost = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    step(1.0f, "Done");
    return analysis;
}

}  // namespace vfx::editor
