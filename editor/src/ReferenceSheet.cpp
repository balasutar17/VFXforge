// VFX Forge editor: finding each effect on a sheet of several.
//
// Artists often share a reel: thirty frames, columns and orbs laid out on
// one picture, all playing at once. The analysis reads one effect at a
// time, so the sheet is first divided up. Everything that lights up at any
// moment of the clip is gathered on one grid; pieces close enough to belong
// together (a frame and its sparkles) are joined; each joined group is one
// effect, and its box is where its own light lies.
#include <algorithm>
#include <cmath>

#include "ReferenceTools.h"

namespace vfx::editor {

std::vector<SheetEffect> findEffects(const Reference& reference, const ReferenceOptions& given, int most) {
    using namespace ref;
    std::vector<SheetEffect> out;
    if (!validateReference(reference) || reference.frames.empty()) {
        return out;
    }
    ReferenceOptions options = given;
    options.cropLeft = options.cropTop = 0.0f;
    options.cropRight = options.cropBottom = 1.0f;
    int first = 0, last = 0;
    frameRange(reference, options, first, last);
    constexpr int kSide = 192;

    // Up to sixteen moments, spread over the clip.
    std::vector<int> moments;
    const int span = last - first + 1;
    const int count = std::min(span, 16);
    for (int k = 0; k < count; ++k) {
        moments.push_back(first + (count > 1 ? k * (span - 1) / (count - 1) : 0));
    }
    std::vector<Grid> grids;
    for (int f : moments) {
        grids.push_back(makeGrid(reference.frames[static_cast<std::size_t>(f)], options, kSide));
    }
    std::vector<const Grid*> probes;
    for (const Grid& g : grids) {
        probes.push_back(&g);
    }
    const BackdropRead backdrop = readBackdrop(probes, options);
    const int w = grids.front().width, h = grids.front().height;
    const std::size_t size = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    std::vector<float> lit(size, 0.0f);
    for (const Grid& g : grids) {
        const Matte m = matteFromGrid(g, backdrop);
        for (std::size_t i = 0; i < size && i < m.cover.size(); ++i) {
            lit[i] = std::max(lit[i], m.cover[i]);
        }
    }
    // Only what is clearly lit counts: taking the brightest moment of every
    // point over many frames also gathers up a video's faint noise, which
    // would tie everything together.
    double total = 0;
    for (float& v : lit) {
        v = v >= std::max(0.18f, 3.0f * backdrop.cutoff) ? v : 0.0f;
        total += v;
    }
    if (total <= 0) {
        return out;
    }

    // Join what touches or nearly touches (a frame and the sparkles on it);
    // effects on a sheet are set further apart than that, though sometimes
    // only a little.
    std::vector<float> joined = lit;
    dilate(joined, w, h, 1);
    struct Group {
        double mass = 0;
        int l = 0, t = 0, r = -1, b = -1;
        float x = 0, y = 0;
    };
    std::vector<Group> groups;
    for (const Patch& p : findPatches(joined, w, h, 0.0f)) {
        Group g;
        g.l = w;
        g.t = h;
        double sx = 0, sy = 0;
        for (int i : p.points) {
            const float v = lit[static_cast<std::size_t>(i)];
            if (v > 0.0f) {
                const int x = i % w, y = i / w;
                g.mass += v;
                sx += v * x;
                sy += v * y;
                g.l = std::min(g.l, x);
                g.r = std::max(g.r, x);
                g.t = std::min(g.t, y);
                g.b = std::max(g.b, y);
            }
        }
        if (g.r >= 0 && g.mass > 0) {
            g.x = static_cast<float>(sx / g.mass);
            g.y = static_cast<float>(sy / g.mass);
            groups.push_back(g);
        }
    }
    // Specks join the nearest effect when they are close to its box (a spark
    // flying off it); otherwise they are left out.
    const float near = 0.04f * static_cast<float>(std::max(w, h));
    std::vector<std::size_t> big;
    for (std::size_t k = 0; k < groups.size(); ++k) {
        if (groups[k].mass >= 0.004 * total && (groups[k].r - groups[k].l + 1) * (groups[k].b - groups[k].t + 1) >= 9) {
            big.push_back(k);
        }
    }
    for (std::size_t k = 0; k < groups.size(); ++k) {
        if (std::find(big.begin(), big.end(), k) != big.end()) {
            continue;
        }
        const Group& speck = groups[k];
        float best = near;
        std::size_t into = groups.size();
        for (std::size_t j : big) {
            const Group& g = groups[j];
            const float dx = std::max({static_cast<float>(g.l) - speck.x, 0.0f, speck.x - static_cast<float>(g.r)});
            const float dy = std::max({static_cast<float>(g.t) - speck.y, 0.0f, speck.y - static_cast<float>(g.b)});
            const float d = std::hypot(dx, dy);
            if (d < best) {
                best = d;
                into = j;
            }
        }
        if (into < groups.size()) {
            groups[into].mass += speck.mass;
        }
    }
    std::vector<SheetEffect> found;
    for (std::size_t j : big) {
        const Group& g = groups[j];
        const float padX = std::max(2.0f, 0.08f * static_cast<float>(g.r - g.l + 1));
        const float padY = std::max(2.0f, 0.08f * static_cast<float>(g.b - g.t + 1));
        SheetEffect e;
        e.left = std::clamp((static_cast<float>(g.l) - padX) / static_cast<float>(w), 0.0f, 1.0f);
        e.right = std::clamp((static_cast<float>(g.r + 1) + padX) / static_cast<float>(w), 0.0f, 1.0f);
        e.top = std::clamp((static_cast<float>(g.t) - padY) / static_cast<float>(h), 0.0f, 1.0f);
        e.bottom = std::clamp((static_cast<float>(g.b + 1) + padY) / static_cast<float>(h), 0.0f, 1.0f);
        e.share = static_cast<float>(g.mass / total);
        found.push_back(e);
    }
    // Only so many, the largest.
    std::sort(found.begin(), found.end(), [](const SheetEffect& a, const SheetEffect& b) { return a.share > b.share; });
    if (static_cast<int>(found.size()) > most) {
        found.resize(static_cast<std::size_t>(most));
    }
    // Reading order: rows from the top (a row is boxes whose middles lie
    // within half a typical box's height), left to right in each.
    std::vector<float> heights;
    for (const SheetEffect& e : found) {
        heights.push_back(e.bottom - e.top);
    }
    std::sort(heights.begin(), heights.end());
    const float rowStep = heights.empty() ? 1.0f : std::max(0.02f, 0.5f * heights[heights.size() / 2]);
    std::sort(found.begin(), found.end(), [](const SheetEffect& a, const SheetEffect& b) {
        return a.top + a.bottom < b.top + b.bottom;
    });
    std::vector<std::vector<SheetEffect>> rows;
    for (const SheetEffect& e : found) {
        const float middle = 0.5f * (e.top + e.bottom);
        if (rows.empty() || middle - 0.5f * (rows.back().front().top + rows.back().front().bottom) > rowStep) {
            rows.emplace_back();
        }
        rows.back().push_back(e);
    }
    for (auto& row : rows) {
        std::sort(row.begin(), row.end(), [](const SheetEffect& a, const SheetEffect& b) { return a.left < b.left; });
        out.insert(out.end(), row.begin(), row.end());
    }
    return out;
}

}  // namespace vfx::editor
