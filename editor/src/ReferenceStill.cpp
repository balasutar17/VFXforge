// What one picture shows: the reading of a single frame.
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

#include "vfx/editor/Shapes.h"

#include "ReferenceTools.h"

namespace vfx::editor::ref {

namespace {

constexpr int kRadialSteps = 48;
constexpr int kSectors = 24;
constexpr int kAngles = 120;

float degrees(float radians) { return radians * 180.0f / kPi; }

// The soft shape's brightness at a distance from its middle, 0 to 1.
float softProfile(float x) {
    if (!(x < 1.0f)) {
        return 0.0f;
    }
    return clamp01(shapeCoverage(SpriteShape::Soft, x < 0.0f ? 0.0f : x, 0.0f, 0.01f, 0.01f));
}

struct Radial {
    float step = 1;  // grid points per bin
    std::array<float, kRadialSteps> mean{}, middle{}, around{};
    std::array<Swatch, kRadialSteps> colour{};
};

// Brightness from a centre outward: the mean round each circle, the median
// (which ignores rays and sparks), how much of the circle is lit, and the
// colour there.
Radial readRadial(const Matte& m, float cx, float cy, float limit) {
    Radial out;
    out.step = std::max(0.5f, limit / kRadialSteps);
    std::array<std::vector<float>, kRadialSteps> values;
    std::array<std::array<float, kSectors>, kRadialSteps> sectorSum{};
    std::array<std::array<int, kSectors>, kRadialSteps> sectorCount{};
    std::array<double, kRadialSteps> r{}, g{}, b{}, weight{};
    for (int y = 0; y < m.height; ++y) {
        for (int x = 0; x < m.width; ++x) {
            const float dx = static_cast<float>(x) - cx, dy = static_cast<float>(y) - cy;
            const float d = std::sqrt(dx * dx + dy * dy);
            const int bin = static_cast<int>(d / out.step);
            if (bin >= kRadialSteps) {
                continue;
            }
            const std::size_t i = m.at(x, y);
            const float c = m.cover[i];
            const auto k = static_cast<std::size_t>(bin);
            values[k].push_back(c);
            int sector = static_cast<int>((std::atan2(-dy, dx) + kPi) / (2.0f * kPi) * kSectors);
            sector = std::clamp(sector, 0, kSectors - 1);
            sectorSum[k][static_cast<std::size_t>(sector)] += c;
            sectorCount[k][static_cast<std::size_t>(sector)] += 1;
            r[k] += c * m.r[i];
            g[k] += c * m.g[i];
            b[k] += c * m.b[i];
            weight[k] += c;
        }
    }
    for (std::size_t k = 0; k < kRadialSteps; ++k) {
        auto& v = values[k];
        if (v.empty()) {
            continue;
        }
        out.mean[k] = std::accumulate(v.begin(), v.end(), 0.0f) / static_cast<float>(v.size());
        const auto mid = v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2);
        std::nth_element(v.begin(), mid, v.end());
        out.middle[k] = *mid;
        int lit = 0, seen = 0;
        for (std::size_t s = 0; s < kSectors; ++s) {
            if (sectorCount[k][s] > 0) {
                ++seen;
                if (sectorSum[k][s] / static_cast<float>(sectorCount[k][s]) > 0.4f * out.mean[k]) {
                    ++lit;
                }
            }
        }
        // A circle that mostly falls outside the picture says nothing.
        out.around[k] = seen >= kSectors * 3 / 4 ? static_cast<float>(lit) / static_cast<float>(seen) : 0.0f;
        if (weight[k] > 1e-6) {
            out.colour[k] = Swatch{static_cast<float>(r[k] / weight[k]), static_cast<float>(g[k] / weight[k]),
                                   static_cast<float>(b[k] / weight[k]), 0};
        }
    }
    return out;
}

// The colour of the effect between two radii: the average there, weighted by
// how much effect each circle holds.
Swatch colourBetween(const Radial& radial, float from, float to) {
    double r = 0, g = 0, b = 0, w = 0;
    for (int k = 0; k < kRadialSteps; ++k) {
        const float radius = (static_cast<float>(k) + 0.5f) * radial.step;
        const float level = radial.mean[static_cast<std::size_t>(k)];
        if (radius < from || radius > to || level < 0.01f) {
            continue;
        }
        const Swatch& c = radial.colour[static_cast<std::size_t>(k)];
        const double weight = static_cast<double>(level) * radius;
        r += weight * c.r;
        g += weight * c.g;
        b += weight * c.b;
        w += weight;
    }
    if (w > 1e-9) {
        return Swatch{static_cast<float>(r / w), static_cast<float>(g / w), static_cast<float>(b / w), 0};
    }
    // Nothing in range: take the nearest circle that has anything.
    Swatch best;
    for (int k = 0; k < kRadialSteps; ++k) {
        if (radial.mean[static_cast<std::size_t>(k)] > 0.01f) {
            best = radial.colour[static_cast<std::size_t>(k)];
            if ((static_cast<float>(k) + 0.5f) * radial.step >= from) {
                break;
            }
        }
    }
    return best;
}

Swatch meanColour(const Matte& m, const std::vector<int>& points, const std::vector<float>* weights = nullptr) {
    double r = 0, g = 0, b = 0, w = 0;
    for (int at : points) {
        const auto i = static_cast<std::size_t>(at);
        const double c = weights ? (*weights)[i] : m.cover[i];
        r += c * m.r[i];
        g += c * m.g[i];
        b += c * m.b[i];
        w += c;
    }
    if (!(w > 1e-9)) {
        return Swatch{1, 1, 1, 0};
    }
    return Swatch{static_cast<float>(r / w), static_cast<float>(g / w), static_cast<float>(b / w), 0};
}

// Up to five colours that between them describe the effect.
std::vector<Swatch> readPalette(const Matte& m) {
    struct Sample {
        float r, g, b, w;
    };
    std::vector<Sample> samples;
    for (std::size_t i = 0; i < m.cover.size(); ++i) {
        if (m.cover[i] > 0.12f) {
            samples.push_back(Sample{m.r[i], m.g[i], m.b[i], m.cover[i]});
        }
    }
    std::vector<Swatch> out;
    if (samples.empty()) {
        return out;
    }
    const auto distance = [](const Sample& s, const Swatch& c) {
        return (s.r - c.r) * (s.r - c.r) + (s.g - c.g) * (s.g - c.g) + (s.b - c.b) * (s.b - c.b);
    };
    std::vector<Swatch> centres;
    {
        double r = 0, g = 0, b = 0, w = 0;
        for (const Sample& s : samples) {
            r += s.w * s.r;
            g += s.w * s.g;
            b += s.w * s.b;
            w += s.w;
        }
        centres.push_back(Swatch{static_cast<float>(r / w), static_cast<float>(g / w), static_cast<float>(b / w), 0});
    }
    while (centres.size() < 5) {
        float far = 0.0f;
        const Sample* pick = nullptr;
        for (const Sample& s : samples) {
            float nearest = 1e9f;
            for (const Swatch& c : centres) {
                nearest = std::min(nearest, distance(s, c));
            }
            const float score = nearest * (s.w > 0.3f ? 1.0f : s.w / 0.3f);
            if (score > far) {
                far = score;
                pick = &s;
            }
        }
        if (!pick || far < 0.01f) {
            break;
        }
        centres.push_back(Swatch{pick->r, pick->g, pick->b, 0});
    }
    std::vector<double> sums(centres.size() * 4);
    for (int pass = 0; pass < 12; ++pass) {
        std::fill(sums.begin(), sums.end(), 0.0);
        for (const Sample& s : samples) {
            std::size_t best = 0;
            float nearest = 1e9f;
            for (std::size_t c = 0; c < centres.size(); ++c) {
                const float d = distance(s, centres[c]);
                if (d < nearest) {
                    nearest = d;
                    best = c;
                }
            }
            sums[best * 4 + 0] += s.w * s.r;
            sums[best * 4 + 1] += s.w * s.g;
            sums[best * 4 + 2] += s.w * s.b;
            sums[best * 4 + 3] += s.w;
        }
        for (std::size_t c = 0; c < centres.size(); ++c) {
            if (sums[c * 4 + 3] > 1e-9) {
                centres[c].r = static_cast<float>(sums[c * 4 + 0] / sums[c * 4 + 3]);
                centres[c].g = static_cast<float>(sums[c * 4 + 1] / sums[c * 4 + 3]);
                centres[c].b = static_cast<float>(sums[c * 4 + 2] / sums[c * 4 + 3]);
            }
        }
    }
    double total = 0;
    for (std::size_t c = 0; c < centres.size(); ++c) {
        total += sums[c * 4 + 3];
    }
    for (std::size_t c = 0; c < centres.size(); ++c) {
        centres[c].share = total > 0 ? static_cast<float>(sums[c * 4 + 3] / total) : 0.0f;
    }
    // Colours that came out nearly the same are one colour.
    for (std::size_t a = 0; a < centres.size(); ++a) {
        for (std::size_t b = a + 1; b < centres.size();) {
            const Swatch& p = centres[a];
            const Swatch& q = centres[b];
            const float d = std::sqrt((p.r - q.r) * (p.r - q.r) + (p.g - q.g) * (p.g - q.g) +
                                      (p.b - q.b) * (p.b - q.b));
            if (d < 0.12f) {
                const float w = p.share + q.share;
                if (w > 0) {
                    centres[a].r = (p.r * p.share + q.r * q.share) / w;
                    centres[a].g = (p.g * p.share + q.g * q.share) / w;
                    centres[a].b = (p.b * p.share + q.b * q.share) / w;
                }
                centres[a].share = w;
                centres.erase(centres.begin() + static_cast<std::ptrdiff_t>(b));
            } else {
                ++b;
            }
        }
    }
    std::stable_sort(centres.begin(), centres.end(), [](const Swatch& a, const Swatch& b) { return a.share > b.share; });
    for (const Swatch& c : centres) {
        if (c.share >= 0.03f || out.empty()) {
            out.push_back(c);
        }
    }
    return out;
}

// Fits the round part of the picture with one or two soft glows of
// different size, the way the effect will be rebuilt.
void fitGlow(const Radial& radial, int firstBin, StillAnalysis& out, float gridHeight) {
    const auto& p = radial.middle;
    float edge = 0.0f, top = 0.0f;
    for (int k = firstBin; k < kRadialSteps; ++k) {
        top = std::max(top, p[static_cast<std::size_t>(k)]);
    }
    if (top < 0.05f) {
        return;
    }
    for (int k = firstBin; k < kRadialSteps; ++k) {
        if (p[static_cast<std::size_t>(k)] >= 0.04f) {
            edge = (static_cast<float>(k) + 1.0f) * radial.step;
        }
    }
    if (edge <= 0.0f) {
        return;
    }
    static constexpr float kRatios[] = {0.0f, 0.12f, 0.18f, 0.26f, 0.36f, 0.48f, 0.62f};
    float bestError = 1e9f, bestS1 = 0, bestA1 = 0, bestS2 = edge, bestA2 = 0;
    for (int step = 0; step < 12; ++step) {
        const float s2 = edge * (0.6f + 0.9f * static_cast<float>(step) / 11.0f);
        for (float ratio : kRatios) {
            const float s1 = ratio * s2;
            // Least squares for the two levels over the bins that are not
            // clipped to full white.
            double a11 = 0, a12 = 0, a22 = 0, b1 = 0, b2 = 0;
            for (int k = firstBin; k < kRadialSteps; ++k) {
                const float radius = (static_cast<float>(k) + 0.5f) * radial.step;
                const float v = p[static_cast<std::size_t>(k)];
                if (v >= 0.97f) {
                    continue;
                }
                const double w = radius + 2.0f * radial.step;
                const double g1 = s1 > 0 ? softProfile(radius / s1) : 0.0f;
                const double g2 = softProfile(radius / s2);
                a11 += w * g1 * g1;
                a12 += w * g1 * g2;
                a22 += w * g2 * g2;
                b1 += w * g1 * v;
                b2 += w * g2 * v;
            }
            double l1 = 0, l2 = 0;
            const double det = a11 * a22 - a12 * a12;
            if (s1 > 0 && std::fabs(det) > 1e-9) {
                l1 = (b1 * a22 - b2 * a12) / det;
                l2 = (a11 * b2 - a12 * b1) / det;
            }
            if (!(s1 > 0) || l1 <= 0 || l2 <= 0) {
                // One glow only.
                l1 = 0;
                l2 = a22 > 1e-9 ? b2 / a22 : 0;
                if (s1 > 0) {
                    continue;
                }
            }
            // Where the picture is clipped to white, the light must reach at least 1.
            for (int k = firstBin; k < kRadialSteps; ++k) {
                if (p[static_cast<std::size_t>(k)] < 0.97f) {
                    continue;
                }
                const float radius = (static_cast<float>(k) + 0.5f) * radial.step;
                const double g1 = s1 > 0 ? softProfile(radius / s1) : 0.0f;
                const double g2 = softProfile(radius / s2);
                const double have = l1 * g1 + l2 * g2;
                if (have < 1.0) {
                    if (g1 > 0.1) {
                        l1 = std::min(4.0, (1.0 - l2 * g2) / g1);
                    } else if (g2 > 0.1 && !(s1 > 0)) {
                        l2 = std::min(4.0, 1.0 / g2);
                    }
                }
            }
            double error = 0, weightSum = 0;
            for (int k = firstBin; k < kRadialSteps; ++k) {
                const float radius = (static_cast<float>(k) + 0.5f) * radial.step;
                const double w = radius + 2.0f * radial.step;
                const double g1 = s1 > 0 ? softProfile(radius / s1) : 0.0f;
                const double g2 = softProfile(radius / s2);
                const double model = std::min(1.0, l1 * g1 + l2 * g2);
                error += w * std::fabs(model - p[static_cast<std::size_t>(k)]);
                weightSum += w;
            }
            error /= std::max(1e-9, weightSum);
            // A second glow has to earn its place.
            const float cost = static_cast<float>(error) + (s1 > 0 ? 0.004f : 0.0f);
            if (cost < bestError) {
                bestError = cost;
                bestS1 = s1;
                bestA1 = static_cast<float>(l1);
                bestS2 = s2;
                bestA2 = static_cast<float>(l2);
            }
        }
    }
    if (bestA2 < 0.03f && bestA1 < 0.03f) {
        return;
    }
    out.hasGlow = true;
    out.glowOuter = bestS2 / gridHeight;
    out.glowOuterLevel = bestA2;
    out.glowOuterColour = colourBetween(radial, 0.4f * bestS2, 0.8f * bestS2);
    if (bestS1 > 0 && bestA1 >= 0.05f) {
        out.glowInner = bestS1 / gridHeight;
        out.glowInnerLevel = bestA1;
        out.glowInnerColour = colourBetween(radial, 0.0f, 0.5f * bestS1);
        // The middle of the inner glow is what the eye reads as its colour;
        // take the plain average there rather than the most colourful bin.
        const Swatch& centre = radial.colour[0];
        if (radial.mean[0] > 0.3f) {
            out.glowInnerColour = centre;
        }
    }
}

// Lit circles away from the centre.
void findRings(const Matte& m, const Radial& radial, float cx, float cy, float extent, StillAnalysis& out) {
    std::array<float, kRadialSteps> p{};
    for (int k = 0; k < kRadialSteps; ++k) {
        const float a = radial.mean[static_cast<std::size_t>(std::max(0, k - 1))];
        const float b = radial.mean[static_cast<std::size_t>(k)];
        const float c = radial.mean[static_cast<std::size_t>(std::min(kRadialSteps - 1, k + 1))];
        p[static_cast<std::size_t>(k)] = 0.25f * a + 0.5f * b + 0.25f * c;
    }
    const float gridHeight = static_cast<float>(m.height);
    for (int k = 2; k < kRadialSteps - 1 && out.rings.size() < 3; ++k) {
        const auto i = static_cast<std::size_t>(k);
        const float radius = (static_cast<float>(k) + 0.5f) * radial.step;
        if (radius < 0.2f * extent || !(p[i] > p[i - 1] && p[i] >= p[i + 1])) {
            continue;
        }
        // The dip between the centre and this peak, and the fall after it.
        float inner = p[i];
        int innerAt = k;
        for (int j = k - 1; j >= 0; --j) {
            if (p[static_cast<std::size_t>(j)] < inner) {
                inner = p[static_cast<std::size_t>(j)];
                innerAt = j;
            }
            if (p[static_cast<std::size_t>(j)] > p[i]) {
                break;
            }
        }
        float outer = p[i];
        for (int j = k + 1; j < kRadialSteps; ++j) {
            outer = std::min(outer, p[static_cast<std::size_t>(j)]);
        }
        const float rise = p[i] - std::max(inner, outer);
        if (rise < 0.05f || p[i] - inner < 0.05f || radial.around[i] < 0.6f) {
            continue;
        }
        const float half = p[i] - 0.5f * rise;
        int lo = k, hi = k;
        while (lo > innerAt && p[static_cast<std::size_t>(lo - 1)] > half) {
            --lo;
        }
        while (hi < kRadialSteps - 1 && p[static_cast<std::size_t>(hi + 1)] > half) {
            ++hi;
        }
        RingFound ring;
        ring.radius = radius / gridHeight;
        ring.thickness = static_cast<float>(hi - lo + 1) * radial.step / gridHeight;
        ring.strength = clamp01(p[i]);
        ring.around = radial.around[i];
        ring.colour = radial.colour[i];
        // Plain, soft or jagged: whichever built-in ring is most like it.
        {
            float best = -2.0f;
            for (SpriteShape shape : {SpriteShape::Ring, SpriteShape::Shockwave, SpriteShape::Shardring}) {
                const float tile = radius / std::max(0.3f, measureShape(shape).ringRadius);
                const float score = matchShape(m, cx, cy, tile, shape, nullptr, shape == SpriteShape::Shardring);
                if (score > best) {
                    best = score;
                    ring.shape = shapeName(shape);
                }
            }
        }
        out.rings.push_back(ring);
        k = hi + 1;
    }
}

// Rays out of the centre. The round part of the picture (the median round
// each circle) is taken away; what is left along each direction is a ray if
// it runs outward unbroken from where the round part stops being solid.
void findRays(const std::vector<float>& cover, const Matte& m, const Radial& radial, float cx, float cy,
              float extent, StillAnalysis& out) {
    const auto round = [&](float radius) {
        const int bin = std::clamp(static_cast<int>(radius / radial.step), 0, kRadialSteps - 1);
        return radial.middle[static_cast<std::size_t>(bin)];
    };
    // Start where the round part is no longer clipped solid.
    float begin = std::max(2.0f, 0.08f * extent);
    while (begin < extent && round(begin) >= 0.9f) {
        begin += 1.0f;
    }
    const float limit = std::min(1.6f * extent, std::max(static_cast<float>(m.width), static_cast<float>(m.height)));
    if (!(limit > begin + 3.0f)) {
        return;
    }
    std::array<float, kAngles> reach{}, level{};
    for (int a = 0; a < kAngles; ++a) {
        const float angle = 2.0f * kPi * static_cast<float>(a) / kAngles;
        const float dx = std::cos(angle), dy = -std::sin(angle);
        float end = 0.0f, sum = 0.0f;
        int gap = 0, steps = 0;
        for (float radius = begin; radius <= limit; radius += 1.0f) {
            // A thin ray may fall between two directions: the brightest of
            // the point and its neighbours a step to either side is read.
            float seen = 0.0f;
            for (const float side : {-0.7f, 0.0f, 0.7f}) {
                seen = std::max(seen, readAt(cover, m.width, m.height, cx + dx * radius - dy * side,
                                             cy + dy * radius + dx * side));
            }
            const float base = round(radius);
            const float extra = seen - base;
            // Where the round part is nearly solid there is little room to
            // stand out, so half of the room left is enough.
            if (extra >= std::min(0.06f, 0.5f * (1.0f - base)) && extra > 0.02f) {
                end = radius;
                sum += extra;
                ++steps;
                gap = 0;
            } else if (++gap > 3) {
                break;
            }
        }
        reach[static_cast<std::size_t>(a)] = end > begin ? end - begin : 0.0f;
        level[static_cast<std::size_t>(a)] = steps > 0 ? sum / static_cast<float>(steps) : 0.0f;
    }
    std::array<float, kAngles> smooth{};
    for (int a = 0; a < kAngles; ++a) {
        smooth[static_cast<std::size_t>(a)] =
            0.25f * reach[static_cast<std::size_t>((a + kAngles - 1) % kAngles)] + 0.5f * reach[static_cast<std::size_t>(a)] +
            0.25f * reach[static_cast<std::size_t>((a + 1) % kAngles)];
    }
    const float least = std::max(3.0f, 0.1f * extent);
    std::vector<int> peaks;
    for (int a = 0; a < kAngles; ++a) {
        const float v = smooth[static_cast<std::size_t>(a)];
        if (v < least) {
            continue;
        }
        if (smooth[static_cast<std::size_t>((a + 1) % kAngles)] > v ||
            smooth[static_cast<std::size_t>((a + kAngles - 1) % kAngles)] >= v) {
            continue;
        }
        // It must stand clear of the dips either side of it.
        float dipLeft = v, dipRight = v;
        for (int d = 1; d < kAngles / 2; ++d) {
            const float l = smooth[static_cast<std::size_t>((a + kAngles - d) % kAngles)];
            if (l > v) {
                break;
            }
            dipLeft = std::min(dipLeft, l);
        }
        for (int d = 1; d < kAngles / 2; ++d) {
            const float r = smooth[static_cast<std::size_t>((a + d) % kAngles)];
            if (r > v) {
                break;
            }
            dipRight = std::min(dipRight, r);
        }
        if (v - std::max(dipLeft, dipRight) >= 0.3f * v) {
            peaks.push_back(a);
        }
    }
    if (peaks.size() < 2) {
        return;
    }
    out.rays = static_cast<int>(peaks.size());
    std::vector<float> lengths;
    double strength = 0, r = 0, g = 0, b = 0, w = 0;
    for (int a : peaks) {
        const float angle = 2.0f * kPi * static_cast<float>(a) / kAngles;
        const float dx = std::cos(angle), dy = -std::sin(angle);
        const float end = begin + reach[static_cast<std::size_t>(a)];
        lengths.push_back(end);
        strength += level[static_cast<std::size_t>(a)];
        for (float radius = begin; radius <= end; radius += 1.0f) {
            const float px = cx + dx * radius, py = cy + dy * radius;
            const float v = readAt(cover, m.width, m.height, px, py);
            const int ix = std::clamp(static_cast<int>(std::lround(px)), 0, m.width - 1);
            const int iy = std::clamp(static_cast<int>(std::lround(py)), 0, m.height - 1);
            const std::size_t i = m.at(ix, iy);
            r += v * m.r[i];
            g += v * m.g[i];
            b += v * m.b[i];
            w += v;
        }
    }
    std::sort(lengths.begin(), lengths.end());
    // The longer rays set the size: the shape drawn has long and short ones.
    out.rayReach = lengths[std::min(lengths.size() - 1, lengths.size() * 3 / 4)] / static_cast<float>(m.height);
    out.rayLongest = lengths.back() / static_cast<float>(m.height);
    out.rayStrength = clamp01(static_cast<float>(strength / static_cast<double>(peaks.size())) / 0.5f);
    if (w > 0) {
        out.rayColour = Swatch{static_cast<float>(r / w), static_cast<float>(g / w), static_cast<float>(b / w), 0};
    }
    std::vector<float> gaps;
    for (std::size_t i = 0; i < peaks.size(); ++i) {
        const int next = peaks[(i + 1) % peaks.size()];
        gaps.push_back(static_cast<float>((next - peaks[i] + kAngles) % kAngles));
    }
    const float meanGap = static_cast<float>(kAngles) / static_cast<float>(peaks.size());
    float off = 0.0f;
    for (float gap : gaps) {
        off += std::fabs(gap - meanGap);
    }
    out.raysEven = off / static_cast<float>(gaps.size()) < 0.22f * meanGap;
}

struct Pieces {
    std::vector<const Patch*> sparks, streaks, bits;
};

void describeGroup(const Matte& m, const std::vector<float>& detail, const std::vector<const Patch*>& patches,
                   float cx, float cy, bool streaks, PieceGroup& out) {
    out = PieceGroup{};
    if (patches.empty()) {
        return;
    }
    const float h = static_cast<float>(m.height);
    out.count = static_cast<int>(patches.size());
    std::vector<float> sizes, lengths, distances, angles;
    double r = 0, g = 0, b = 0, w = 0, soft = 0, radial = 0, ax = 0, ay = 0, dirX = 0, dirY = 0;
    for (const Patch* p : patches) {
        const float across = streaks ? p->minor : 2.0f * std::sqrt(static_cast<float>(p->area) / kPi);
        sizes.push_back(across / h);
        lengths.push_back(p->major / h);
        const float dx = p->x - cx, dy = p->y - cy;
        const float d = std::sqrt(dx * dx + dy * dy);
        distances.push_back(d / h);
        const float angle = std::atan2(-dy, dx);
        angles.push_back(angle);
        ax += std::cos(angle);
        ay += std::sin(angle);
        const Swatch c = meanColour(m, p->points, &detail);
        r += p->mass * c.r;
        g += p->mass * c.g;
        b += p->mass * c.b;
        w += p->mass;
        // Soft pieces have many faint points for each bright one.
        int faint = 0;
        for (int at : p->points) {
            faint += m.cover[static_cast<std::size_t>(at)] < 0.5f * p->peak ? 1 : 0;
        }
        soft += static_cast<double>(faint) / static_cast<double>(p->area);
        if (d > 1e-3f) {
            radial += std::fabs((p->axisX * dx + p->axisY * dy) / d);
        }
        // Streak directions have no front or back: average them at twice the angle.
        const float lean = std::atan2(-p->axisY, p->axisX);
        dirX += std::cos(2.0f * lean);
        dirY += std::sin(2.0f * lean);
    }
    const auto low = [](std::vector<float> v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 10];
    };
    const auto high = [](std::vector<float> v) {
        std::sort(v.begin(), v.end());
        return v[std::min(v.size() - 1, v.size() * 9 / 10)];
    };
    const float n = static_cast<float>(patches.size());
    out.sizeLow = low(sizes);
    out.sizeHigh = high(sizes);
    out.lengthLow = low(lengths);
    out.lengthHigh = high(lengths);
    out.nearest = low(distances);
    out.farthest = high(distances);
    if (w > 0) {
        out.colour = Swatch{static_cast<float>(r / w), static_cast<float>(g / w), static_cast<float>(b / w), 0};
    }
    out.softness = clamp01(static_cast<float>(soft) / n);
    out.radial = radial / n > 0.8;
    // Where they sit round the centre.
    const float together = static_cast<float>(std::sqrt(ax * ax + ay * ay)) / n;
    out.heading = 90.0f;
    out.spread = 180.0f;
    if (together > 0.35f && patches.size() >= 3) {
        const float mean = std::atan2(static_cast<float>(ay), static_cast<float>(ax));
        std::vector<float> off;
        for (float a : angles) {
            float d = std::fabs(a - mean);
            if (d > kPi) {
                d = 2.0f * kPi - d;
            }
            off.push_back(d);
        }
        std::sort(off.begin(), off.end());
        out.heading = degrees(mean);
        if (out.heading < 0) {
            out.heading += 360.0f;
        }
        out.spread = std::clamp(degrees(off[std::min(off.size() - 1, off.size() * 9 / 10)]) * 1.1f, 5.0f, 180.0f);
    }
    // When the pieces come in clearly different colours, say which.
    {
        struct Hue {
            float r, g, b, mass;
        };
        std::vector<Hue> hues;
        for (const Patch* p : patches) {
            Swatch c = meanColour(m, p->points, &detail);
            const float top = std::max({c.r, c.g, c.b, 1e-3f});
            hues.push_back(Hue{c.r / top, c.g / top, c.b / top, 1.0f});
        }
        std::vector<Hue> centres;
        for (const Hue& hue : hues) {
            bool placed = false;
            for (Hue& c : centres) {
                const float d = std::fabs(hue.r - c.r / c.mass) + std::fabs(hue.g - c.g / c.mass) +
                                std::fabs(hue.b - c.b / c.mass);
                if (d < 0.45f) {
                    c.r += hue.r;
                    c.g += hue.g;
                    c.b += hue.b;
                    c.mass += 1.0f;
                    placed = true;
                    break;
                }
            }
            if (!placed) {
                centres.push_back(hue);
            }
        }
        std::stable_sort(centres.begin(), centres.end(), [](const Hue& x, const Hue& y) { return x.mass > y.mass; });
        if (centres.size() >= 2) {
            for (const Hue& c : centres) {
                if (c.mass >= 0.15f * n && out.colours.size() < 3) {
                    out.colours.push_back(Swatch{c.r / c.mass, c.g / c.mass, c.b / c.mass, c.mass / n});
                }
            }
            if (out.colours.size() < 2) {
                out.colours.clear();
            }
        }
    }
    if (streaks && !out.radial) {
        // All leaning one way: that way is the heading.
        const float lean = 0.5f * std::atan2(static_cast<float>(dirY), static_cast<float>(dirX));
        const float agree = static_cast<float>(std::sqrt(dirX * dirX + dirY * dirY)) / n;
        out.heading = degrees(lean);
        if (out.heading < 0) {
            out.heading += 180.0f;
        }
        out.spread = std::clamp((1.0f - agree) * 90.0f, 2.0f, 90.0f);
    }
}

// Which built-in shape the larger pieces look most like. Small sparks are
// only ever told apart as dots, stars and glints; hearts, drops and the like
// are for pieces large enough to really show such an outline.
std::string voteShape(const Matte& m, const std::vector<const Patch*>& patches, bool large) {
    static constexpr SpriteShape kSmall[] = {SpriteShape::Soft,    SpriteShape::Disc, SpriteShape::Sparkle,
                                             SpriteShape::Glint,   SpriteShape::Twinkle, SpriteShape::Star,
                                             SpriteShape::Diamond};
    static constexpr SpriteShape kLarge[] = {SpriteShape::Soft,  SpriteShape::Disc,   SpriteShape::Sparkle, SpriteShape::Glint,
                                             SpriteShape::Twinkle, SpriteShape::Star, SpriteShape::Diamond, SpriteShape::Heart,
                                             SpriteShape::Square, SpriteShape::Drop,  SpriteShape::Bubble,  SpriteShape::Shard,
                                             SpriteShape::Puff,   SpriteShape::Candy};
    std::array<float, kSpriteShapeCount> score{};
    int judged = 0;
    for (const Patch* p : patches) {
        const float half = 0.5f * static_cast<float>(std::max(p->right - p->left, p->bottom - p->top) + 1);
        if (half < 6.0f) {
            continue;
        }
        if (++judged > 12) {
            break;
        }
        const auto consider = [&](SpriteShape shape) {
            score[static_cast<std::size_t>(shape)] +=
                matchShape(m, p->x, p->y, half / std::max(0.3f, measureShape(shape).extent), shape);
        };
        if (large) {
            for (SpriteShape shape : kLarge) {
                consider(shape);
            }
        } else {
            for (SpriteShape shape : kSmall) {
                consider(shape);
            }
        }
    }
    if (judged < 2) {
        return {};
    }
    std::size_t best = 0;
    for (std::size_t i = 1; i < score.size(); ++i) {
        if (score[i] > score[best]) {
            best = i;
        }
    }
    return score[best] / static_cast<float>(judged) >= 0.7f ? shapeName(static_cast<SpriteShape>(best)) : std::string();
}

int openingRadius(float extent) { return std::clamp(static_cast<int>(std::lround(0.07f * extent)), 1, 7); }

// Sorts the small features of a frame into sparks, streaks and loose bits.
// `detail` is what a smoothing pass removes: everything thin or small.
Pieces sortPieces(const std::vector<Patch>& patches, const std::vector<float>& opened, const Matte& m, float cx,
                  float cy, float extent) {
    Pieces out;
    for (const Patch& p : patches) {
        if (p.area < 2 && p.peak < 0.4f) {
            continue;  // a single faint point is noise
        }
        if (p.peak < 0.18f) {
            continue;  // too faint to be a piece of its own: texture in the glow
        }
        const float dx = p.x - cx, dy = p.y - cy;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d < 0.12f * extent) {
            continue;  // part of the centre
        }
        float nearest = 1e9f;
        for (int at : p.points) {
            const float px = static_cast<float>(at % m.width) - cx, py = static_cast<float>(at / m.width) - cy;
            nearest = std::min(nearest, std::sqrt(px * px + py * py));
        }
        const float stretch = p.major / std::max(0.8f, p.minor);
        const float along = std::fabs((p.axisX * dx + p.axisY * dy) / std::max(1e-3f, d));
        if (stretch >= 2.0f && along > 0.9f && nearest < 0.5f * extent) {
            continue;  // a ray out of the centre
        }
        if (stretch >= 1.6f && along > 0.85f) {
            // The tip of a ray: thin, pointing outward, and joined to the
            // centre by something just as solid. A loose streak has a gap
            // (or only faint glow) between it and the centre.
            float solid = 0.0f, nx = 0.0f, ny = 0.0f, nd = 1e9f;
            for (int at : p.points) {
                solid = std::max(solid, m.cover[static_cast<std::size_t>(at)]);
                const float px = static_cast<float>(at % m.width) - cx, py = static_cast<float>(at / m.width) - cy;
                const float pd = std::sqrt(px * px + py * py);
                if (pd < nd) {
                    nd = pd;
                    nx = px;
                    ny = py;
                }
            }
            const int steps = static_cast<int>(std::min(nd, 0.4f * extent));
            bool joined = steps >= 1 && nd > 1.0f;
            for (int k = 1; k <= steps && joined; ++k) {
                const float t = (nd - static_cast<float>(k)) / nd;
                if (readAt(m.cover, m.width, m.height, cx + nx * t, cy + ny * t) < 0.6f * solid) {
                    joined = false;
                }
            }
            if (joined) {
                continue;
            }
        }
        if (stretch >= 2.5f && along < 0.35f && d > 0.3f * extent && p.major > 0.35f * d) {
            continue;  // an arc: part of a ring
        }
        // A streak is thin as well as long. (Round pieces that overlap also
        // make a long patch, but a fat one.)
        if (stretch >= 2.2f && p.major >= 5.0f && p.minor <= std::max(3.0f, 0.08f * extent)) {
            out.streaks.push_back(&p);
            continue;
        }
        const float across = 2.0f * std::sqrt(static_cast<float>(p.area) / kPi);
        if (across <= 0.3f * extent) {
            out.sparks.push_back(&p);
            continue;
        }
        // Larger: a loose bit only when it stands apart from the main body.
        double under = 0;
        for (int at : p.points) {
            under += opened[static_cast<std::size_t>(at)];
        }
        if (under / static_cast<double>(p.area) < 0.15) {
            out.bits.push_back(&p);
        }
    }
    return out;
}

}  // namespace

void countPieces(const Matte& matte, float centreX, float centreY, float extent, float& count, float& reach) {
    count = 0.0f;
    reach = 0.0f;
    std::vector<float> opened = matte.cover;
    const int radius = openingRadius(extent);
    erode(opened, matte.width, matte.height, radius);
    dilate(opened, matte.width, matte.height, radius);
    std::vector<float> detail(opened.size());
    for (std::size_t i = 0; i < detail.size(); ++i) {
        detail[i] = std::max(0.0f, matte.cover[i] - opened[i]);
    }
    const std::vector<Patch> patches = findPatches(detail, matte.width, matte.height, 0.1f);
    const Pieces pieces = sortPieces(patches, opened, matte, centreX, centreY, extent);
    double sum = 0;
    int n = 0;
    for (const auto* group : {&pieces.sparks, &pieces.streaks, &pieces.bits}) {
        for (const Patch* p : *group) {
            const float dx = p->x - centreX, dy = p->y - centreY;
            sum += std::sqrt(dx * dx + dy * dy);
            ++n;
        }
    }
    count = static_cast<float>(n);
    reach = n > 0 ? static_cast<float>(sum / n) : 0.0f;
}

StillAnalysis analyzeStill(const Matte& m, const BackdropRead& backdrop) {
    StillAnalysis out;
    out.width = m.width;
    out.height = m.height;
    out.backdrop = backdrop.kind;
    out.backdropColour = backdrop.colour;
    out.backdropEvenness = backdrop.evenness;
    out.cutoff = backdrop.cutoff;
    out.additive = backdrop.additive;

    const Basics basics = measureBasics(m);
    const float w = static_cast<float>(m.width), h = static_cast<float>(m.height);
    out.fill = basics.fill;
    // Something has to be there: more than a few stray points.
    if (basics.total < 4.0f || basics.fill < 0.0008f) {
        out.fill = 0.0f;
        return out;
    }

    // ---- overall shape
    const double mean = 0.5 * (static_cast<double>(basics.varXX) + basics.varYY);
    const double diff = std::sqrt(std::max(
        0.0, 0.25 * (static_cast<double>(basics.varXX) - basics.varYY) * (static_cast<double>(basics.varXX) - basics.varYY) +
                 static_cast<double>(basics.varXY) * basics.varXY));
    const double l1 = mean + diff, l2 = std::max(1e-6, mean - diff);
    out.elongation = static_cast<float>(std::sqrt(l1 / l2));
    const float axisAngle = 0.5f * std::atan2(2.0f * basics.varXY, basics.varXX - basics.varYY);
    float axisX = std::cos(axisAngle), axisY = std::sin(axisAngle);  // y down
    out.axis = degrees(std::atan2(-axisY, axisX));
    if (out.axis < 0) {
        out.axis += 180.0f;
    }
    if (out.axis >= 180.0f) {
        out.axis -= 180.0f;
    }

    // Hard or soft: soft effects are mostly in-between values.
    {
        std::size_t lit = 0, between = 0;
        for (float c : m.cover) {
            if (c > 0.08f) {
                ++lit;
                between += (c > 0.2f && c < 0.8f) ? 1u : 0u;
            }
        }
        out.hardness = lit > 0 ? clamp01(1.0f - static_cast<float>(between) / static_cast<float>(lit) / 0.7f) : 0.0f;
    }

    // ---- what a smoothing pass keeps (the body) and removes (the detail)
    const int radius = openingRadius(basics.extent);
    std::vector<float> opened = m.cover;
    erode(opened, m.width, m.height, radius);
    dilate(opened, m.width, m.height, radius);
    std::vector<float> detail(opened.size());
    for (std::size_t i = 0; i < detail.size(); ++i) {
        detail[i] = std::max(0.0f, m.cover[i] - opened[i]);
    }

    // ---- the centre
    float cx = basics.centreX, cy = basics.centreY;
    const float hotOff = std::sqrt((basics.hotX - cx) * (basics.hotX - cx) + (basics.hotY - cy) * (basics.hotY - cy));
    const float halfLength = 2.0f * static_cast<float>(std::sqrt(l1));
    // A head with a tail is one joined thing, long, with its brightest part
    // toward one end. (A scatter of pieces can be long and lopsided too.)
    bool joined = false;
    {
        double largest = 0;
        for (const Patch& p : findPatches(m.cover, m.width, m.height, 0.2f)) {
            largest = std::max<double>(largest, p.mass);
        }
        joined = largest >= 0.6 * basics.total;
    }
    const bool lopsided = joined && out.elongation >= 1.7f && hotOff >= 0.25f * halfLength;
    const bool elongated = out.elongation >= 1.9f && !lopsided;

    // A solid main shape with a sharp edge, when there is one. A glow that
    // is merely clipped to white in the middle is not one: just outside its
    // solid part it is still nearly as bright.
    std::vector<Patch> solids;
    const Patch* body = nullptr;
    bool manySolids = false;
    {
        solids = findPatches(opened, m.width, m.height, 0.75f);
        long allSolid = 0;
        for (const Patch& p : solids) {
            allSolid += p.area;
            if (!body || p.area > body->area) {
                body = &p;
            }
        }
        std::size_t lit = 0;
        for (float c : m.cover) {
            lit += c > 0.08f ? 1u : 0u;
        }
        int alike = 0;
        for (const Patch& p : solids) {
            alike += (body && &p != body && p.area * 20 >= body->area) ? 1 : 0;
        }
        if (body && (static_cast<float>(body->area) < std::max(12.0f, 0.06f * static_cast<float>(lit)) ||
                     static_cast<float>(body->area) < 0.3f * static_cast<float>(allSolid) || alike >= 4)) {
            body = nullptr;  // too small, or one of many pieces of similar size
            manySolids = solids.size() >= 3;
        }
        if (body) {
            std::vector<float> near(m.cover.size(), 0.0f), far;
            double inside = 0;
            for (int at : body->points) {
                near[static_cast<std::size_t>(at)] = 1.0f;
                inside += m.cover[static_cast<std::size_t>(at)];
            }
            inside /= static_cast<double>(body->area);
            far = near;
            dilate(near, m.width, m.height, 1);
            dilate(far, m.width, m.height, 3);
            double outside = 0;
            int count = 0;
            for (std::size_t i = 0; i < far.size(); ++i) {
                if (far[i] > 0.0f && !(near[i] > 0.0f)) {
                    outside += m.cover[i];
                    ++count;
                }
            }
            const double sharp = count > 0 && inside > 1e-6 ? 1.0 - outside / count / inside : 0.0;
            // A ring has an empty middle and is found another way.
            const float middle = readAt(m.cover, m.width, m.height, body->x, body->y);
            if (sharp < 0.4 || middle < 0.5f) {
                body = nullptr;
            }
        }
    }
    // A long soft shape (a beam, a slash) also counts as a main shape.
    std::vector<Patch> longs;
    if (!body && elongated) {
        longs = findPatches(m.cover, m.width, m.height, 0.5f);
        for (const Patch& p : longs) {
            if (!body || p.area > body->area) {
                body = &p;
            }
        }
    }

    if (lopsided) {
        out.comet = true;
    } else if (body) {
        cx = body->x;
        cy = body->y;
    } else if (hotOff < 0.2f * basics.extent) {
        cx = basics.hotX;
        cy = basics.hotY;
    }
    if (out.comet) {
        cx = basics.hotX;
        cy = basics.hotY;
    }
    out.centreX = cx / w;
    out.centreY = cy / h;
    out.hotX = basics.hotX / w;
    out.hotY = basics.hotY / h;

    // Extent about the chosen centre.
    float extent = basics.extent, halfExtent = basics.halfExtent;
    {
        std::vector<std::pair<float, float>> byDistance;
        for (int y = 0; y < m.height; ++y) {
            for (int x = 0; x < m.width; ++x) {
                const float c = m.cover[m.at(x, y)];
                if (c > 0) {
                    const float dx = static_cast<float>(x) - cx, dy = static_cast<float>(y) - cy;
                    byDistance.emplace_back(std::sqrt(dx * dx + dy * dy), c);
                }
            }
        }
        std::sort(byDistance.begin(), byDistance.end());
        double running = 0;
        bool half = false;
        for (const auto& [distance, c] : byDistance) {
            running += c;
            if (!half && running >= 0.5 * basics.total) {
                halfExtent = std::max(0.5f, distance);
                half = true;
            }
            if (running >= 0.95 * basics.total) {
                extent = std::max(1.0f, distance);
                break;
            }
        }
    }
    out.extent = extent / h;
    out.halfExtent = halfExtent / h;

    out.palette = readPalette(m);

    // Several separate effects in one picture? Blur until near pieces join,
    // then count the big lumps.
    {
        std::vector<float> lumps = m.cover;
        blur(lumps, m.width, m.height, std::max(2, static_cast<int>(std::lround(0.03f * h))));
        int big = 0;
        for (const Patch& p : findPatches(lumps, m.width, m.height, 0.12f)) {
            big += p.mass >= 0.15f * basics.total ? 1 : 0;
        }
        out.separate = std::max(1, big);
    }

    // Left and right alike?
    {
        double cross = 0, a2 = 0, b2 = 0;
        for (int y = 0; y < m.height; ++y) {
            for (int x = 0; x < m.width; ++x) {
                const float a = m.cover[m.at(x, y)];
                const float b = readAt(m.cover, m.width, m.height, 2.0f * cx - static_cast<float>(x),
                                       static_cast<float>(y));
                cross += a * b;
                a2 += a * a;
                b2 += b * b;
            }
        }
        out.mirror = a2 > 0 && b2 > 0 ? clamp01(static_cast<float>(cross / std::sqrt(a2 * b2))) : 0.0f;
    }

    // ---- small loose pieces
    const std::vector<Patch> patches = findPatches(detail, m.width, m.height, 0.1f);
    Pieces pieces = sortPieces(patches, opened, m, cx, cy, extent);
    std::vector<Patch> split;
    if (manySolids) {
        // Many solid pieces and no one main shape (coins, confetti, gems):
        // each is a loose piece. Ones that overlap are counted by their
        // area against a typical single piece.
        std::vector<int> areas;
        for (const Patch& p : solids) {
            areas.push_back(p.area);
        }
        std::sort(areas.begin(), areas.end());
        const int typical = std::max(4, areas[areas.size() / 3]);
        for (const Patch& p : solids) {
            const int copies = std::clamp(static_cast<int>(std::lround(static_cast<double>(p.area) / typical)), 1, 12);
            for (int k = 0; k < copies; ++k) {
                Patch one = p;
                if (copies > 1) {
                    one.area = typical;
                    one.points.resize(static_cast<std::size_t>(std::min<int>(typical, static_cast<int>(p.points.size()))));
                }
                split.push_back(std::move(one));
            }
        }
        for (const Patch& p : split) {
            pieces.bits.push_back(&p);
        }
    }
    describeGroup(m, manySolids ? m.cover : detail, pieces.sparks, cx, cy, false, out.sparks);
    describeGroup(m, detail, pieces.streaks, cx, cy, true, out.streaks);
    describeGroup(m, manySolids ? m.cover : detail, pieces.bits, cx, cy, false, out.bits);
    out.sparks.shape = voteShape(m, pieces.sparks, false);
    out.bits.shape = voteShape(m, pieces.bits, true);

    // The picture with those pieces taken out, for reading the round part.
    std::vector<float> plain = m.cover;
    for (const auto* group : {&pieces.sparks, &pieces.streaks, &pieces.bits}) {
        for (const Patch* p : *group) {
            for (int at : p->points) {
                plain[static_cast<std::size_t>(at)] = opened[static_cast<std::size_t>(at)];
            }
        }
    }
    Matte plainMatte = m;
    plainMatte.cover = plain;

    const float limit = 1.5f * extent;
    const Radial radial = readRadial(plainMatte, cx, cy, limit);
    out.radial.assign(radial.mean.begin(), radial.mean.end());

    // ---- a head with a tail
    if (out.comet) {
        // Point the axis from the head toward the rest of the effect.
        if ((basics.centreX - cx) * axisX + (basics.centreY - cy) * axisY < 0) {
            axisX = -axisX;
            axisY = -axisY;
        }
        std::vector<std::pair<float, float>> along;
        for (int y = 0; y < m.height; ++y) {
            for (int x = 0; x < m.width; ++x) {
                const float c = plain[m.at(x, y)];
                if (c > 0.02f) {
                    along.emplace_back((static_cast<float>(x) - cx) * axisX + (static_cast<float>(y) - cy) * axisY, c);
                }
            }
        }
        std::sort(along.begin(), along.end());
        double total = 0, running = 0;
        for (const auto& a : along) {
            total += a.second;
        }
        float length = 1.0f;
        for (const auto& a : along) {
            running += a.second;
            if (running >= 0.97 * total) {
                length = std::max(2.0f, a.first);
                break;
            }
        }
        constexpr int kStations = 5;
        std::array<double, kStations> sw{}, ss{}, sr{}, sg{}, sb{};
        for (int y = 0; y < m.height; ++y) {
            for (int x = 0; x < m.width; ++x) {
                const std::size_t i = m.at(x, y);
                const float c = plain[i];
                if (c <= 0.02f) {
                    continue;
                }
                const float dx = static_cast<float>(x) - cx, dy = static_cast<float>(y) - cy;
                const float t = dx * axisX + dy * axisY;
                if (t < 0 || t > length) {
                    continue;
                }
                const float side = -dx * axisY + dy * axisX;
                const auto s = static_cast<std::size_t>(std::min(kStations - 1, static_cast<int>(t / length * kStations)));
                sw[s] += c;
                ss[s] += static_cast<double>(c) * side * side;
                sr[s] += c * m.r[i];
                sg[s] += c * m.g[i];
                sb[s] += c * m.b[i];
            }
        }
        std::array<float, kStations> width{};
        for (std::size_t s = 0; s < kStations; ++s) {
            if (sw[s] > 1e-6) {
                width[s] = 3.2f * static_cast<float>(std::sqrt(ss[s] / sw[s]));
                out.tailColours.push_back(Swatch{static_cast<float>(sr[s] / sw[s]), static_cast<float>(sg[s] / sw[s]),
                                                 static_cast<float>(sb[s] / sw[s]), 0});
            } else if (!out.tailColours.empty()) {
                out.tailColours.push_back(out.tailColours.back());
            }
        }
        out.headX = cx / w;
        out.headY = cy / h;
        out.tailHeading = degrees(std::atan2(-axisY, axisX));
        if (out.tailHeading < 0) {
            out.tailHeading += 360.0f;
        }
        out.tailLength = length / h;
        out.tailWidthStart = std::max(width[0], 2.0f) / h;
        out.tailWidthEnd = std::max(width[kStations - 1], 1.0f) / h;
        out.headRadius = 0.5f * out.tailWidthStart;
        return out;
    }

    // ---- a crisp or long main shape
    if (body) {
        out.hasBody = true;
        // How far the body reaches from its middle.
        std::vector<float> reach;
        for (int at : body->points) {
            const float dx = static_cast<float>(at % m.width) - cx, dy = static_cast<float>(at / m.width) - cy;
            reach.push_back(std::sqrt(dx * dx + dy * dy));
        }
        std::sort(reach.begin(), reach.end());
        const float bodyReach = std::max(2.0f, reach[std::min(reach.size() - 1, reach.size() * 95 / 100)]);
        out.bodyRadius = bodyReach / h;
        out.bodyColour = meanColour(m, body->points);
        // Only the body itself is compared with the built-in shapes.
        Matte only = m;
        std::fill(only.cover.begin(), only.cover.end(), 0.0f);
        for (int at : body->points) {
            only.cover[static_cast<std::size_t>(at)] = m.cover[static_cast<std::size_t>(at)];
        }
        // Its soft edge belongs to it too.
        {
            std::vector<float> near = only.cover;
            dilate(near, m.width, m.height, 2);
            for (std::size_t i = 0; i < near.size(); ++i) {
                if (near[i] > 0.0f) {
                    only.cover[i] = plain[i];
                }
            }
        }
        float best = -2.0f, bestTurn = 0.0f;
        SpriteShape bestShape = SpriteShape::Disc;
        const bool crisp = !longs.size();
        for (int i = 0; i < kSpriteShapeCount; ++i) {
            const auto shape = static_cast<SpriteShape>(i);
            // Shapes that are mostly empty (rings, bubbles) are found another way.
            if (shape == SpriteShape::Ring || shape == SpriteShape::Shockwave || shape == SpriteShape::Shardring ||
                shape == SpriteShape::Bubble) {
                continue;
            }
            const bool softShape = shape == SpriteShape::Soft || shape == SpriteShape::Smoke || shape == SpriteShape::Flare ||
                                   shape == SpriteShape::Rays || shape == SpriteShape::Twinkle ||
                                   shape == SpriteShape::Sparkle || shape == SpriteShape::Streak ||
                                   shape == SpriteShape::Flame;
            float turn = 0.0f, score = 0.0f;
            if (crisp) {
                // A crisp shape is judged by its outline, among crisp shapes.
                if (softShape) {
                    continue;
                }
                for (const float grow : {0.92f, 1.0f, 1.1f}) {
                    float t = 0.0f;
                    const float sc = matchOutline(only, cx, cy, grow * bodyReach / std::max(0.3f, measureShape(shape).extent),
                                                  shape, &t);
                    if (sc > score) {
                        score = sc;
                        turn = t;
                    }
                }
                // The plainest shapes win a tie.
                score += shape == SpriteShape::Disc ? 0.02f : (shape == SpriteShape::Puff ? 0.01f : 0.0f);
            } else {
                score = matchShape(only, cx, cy, bodyReach / std::max(0.3f, measureShape(shape).extent), shape, &turn);
            }
            if (score > best) {
                best = score;
                bestTurn = turn;
                bestShape = shape;
            }
        }
        out.bodyShape = shapeName(bestShape);
        out.bodyMatch = clamp01(best);
        out.bodyTurn = bestTurn;
    }

    // ---- rings
    findRings(plainMatte, radial, cx, cy, extent, out);

    // ---- rays
    {
        findRays(plain, m, radial, cx, cy, extent, out);
        if (out.hasBody) {
            out.bodyLobes = out.rays;
        }
        if (out.rays >= 2 && !out.hasBody) {
            // Which built-in flashes could show this many rays at all.
            std::vector<SpriteShape> flashes;
            if (out.rays <= 4) {
                flashes = {SpriteShape::Glint, SpriteShape::Twinkle};
            } else if (out.rays <= 6) {
                flashes = {SpriteShape::Star, SpriteShape::Twinkle, SpriteShape::Starflash};
            } else if (out.rays <= 9) {
                flashes = {SpriteShape::Twinkle, SpriteShape::Starflash, SpriteShape::Burst};
            } else {
                flashes = {SpriteShape::Starflash, SpriteShape::Burst, SpriteShape::Rays};
            }
            float best = -2.0f, bestTurn = 0.0f, bestHalf = 0.0f;
            SpriteShape bestShape = flashes.front();
            const float reach = std::max(4.0f, out.rayReach * h);
            for (SpriteShape shape : flashes) {
                // The longest rays may reach further than the typical one
                // measured, so a few sizes are tried.
                for (const float grow : {0.9f, 1.1f, 1.3f, 1.55f}) {
                    float turn = 0.0f;
                    const float half = grow * reach / std::max(0.5f, measureShape(shape).reach);
                    // Only the rays are compared: the round glow is the same for all.
                    const float score = matchShape(plainMatte, cx, cy, half, shape, &turn, true);
                    if (score > best) {
                        best = score;
                        bestTurn = turn;
                        bestShape = shape;
                        bestHalf = half;
                    }
                }
            }
            if (best >= 0.2f) {
                // Settle the size more finely for the shape that won.
                const float coarse = bestHalf;
                for (int k = -3; k <= 3; ++k) {
                    if (k == 0) {
                        continue;
                    }
                    float turn = 0.0f;
                    const float half = coarse * (1.0f + 0.05f * static_cast<float>(k));
                    const float score = matchShape(plainMatte, cx, cy, half, bestShape, &turn, true);
                    if (score > best) {
                        best = score;
                        bestTurn = turn;
                        bestHalf = half;
                    }
                }
                // The size of the shape that fitted is the better measure of reach.
                out.rayReach = bestHalf * measureShape(bestShape).reach / h;
            }
            out.rayShape = shapeName(bestShape);
            out.rayMatch = clamp01(best);
            out.rayTurn = bestTurn;
        }
    }

    // ---- the round, soft part
    if (!out.hasBody) {
        fitGlow(radial, 0, out, h);
    } else {
        // A halo round a crisp body: only what lies beyond the body counts.
        const int firstBin = std::min(kRadialSteps - 1, static_cast<int>(1.15f * out.bodyRadius * h / radial.step));
        StillAnalysis halo;
        fitGlow(radial, firstBin, halo, h);
        if (halo.hasGlow && halo.glowOuterLevel > 0.08f && halo.glowOuter > 1.25f * out.bodyRadius) {
            out.hasGlow = true;
            out.glowOuter = halo.glowOuter;
            out.glowOuterLevel = halo.glowOuterLevel;
            out.glowOuterColour = halo.glowOuterColour;
        }
    }

    // A white-hot middle inside a coloured glow.
    {
        const Swatch& centre = radial.colour[0];
        const Swatch outer = colourBetween(radial, 0.4f * extent, 0.9f * extent);
        const float satCentre = saturation(centre.r, centre.g, centre.b);
        const float satOuter = saturation(outer.r, outer.g, outer.b);
        if (radial.middle[0] > 0.75f && satCentre < 0.3f && satOuter > satCentre + 0.2f) {
            out.hasCore = true;
            out.coreColour = centre;
            float edge = radial.step;
            for (int k = 0; k < kRadialSteps; ++k) {
                const Swatch& c = radial.colour[static_cast<std::size_t>(k)];
                if (radial.middle[static_cast<std::size_t>(k)] < 0.6f ||
                    saturation(c.r, c.g, c.b) > satCentre + 0.5f * (satOuter - satCentre)) {
                    break;
                }
                edge = (static_cast<float>(k) + 1.0f) * radial.step;
            }
            out.coreRadius = edge / h;
        }
    }
    // A white-hot middle has to come out white: the inner glow alone must
    // reach full brightness across it, whatever colour lies behind.
    if (out.hasCore && out.hasGlow && !out.hasBody) {
        if (!(out.glowInnerLevel > 0.0f) || out.glowInner < 1.2f * out.coreRadius) {
            out.glowInner = std::max(out.glowInner, 1.9f * out.coreRadius);
            out.glowInnerLevel = std::max(out.glowInnerLevel, 1.0f);
        }
        const float need = 1.0f / std::max(0.2f, softProfile(0.85f * out.coreRadius / out.glowInner));
        out.glowInnerLevel = std::clamp(std::max(out.glowInnerLevel, need), 0.0f, 4.0f);
        out.glowInnerColour = out.coreColour;
    }

    // ---- smoke: grey and soft, where the picture can show such a thing
    if (!out.additive && out.hardness < 0.6f) {
        for (const Swatch& c : out.palette) {
            if (saturation(c.r, c.g, c.b) < 0.18f && most(c.r, c.g, c.b) < 0.7f && c.share >= 0.2f) {
                out.hasSmoke = true;
                out.smokeColour = c;
                out.smokeRadius = out.extent;
                break;
            }
        }
    }
    return out;
}

}  // namespace vfx::editor::ref

namespace vfx::editor {

using namespace ref;

std::vector<Cutout> makeCutouts(const Reference& reference, const ReferenceOptions& options,
                                const ReferenceAnalysis& analysis, int maxSide) {
    std::vector<Cutout> out;
    const StillAnalysis& s = analysis.still;
    if (reference.frames.empty() || s.comet) {
        return out;
    }
    const bool wantRays = s.rays >= 2 && !s.hasBody;
    const bool wantBody = s.hasBody;
    if (!wantRays && !wantBody) {
        return out;
    }
    const Image& frame = reference.frames[static_cast<std::size_t>(
        std::clamp(analysis.stillFrame, 0, static_cast<int>(reference.frames.size()) - 1))];
    // Read finer than the analysis did: this is a picture someone will look at.
    const Grid grid = makeGrid(frame, options, 640);
    const Matte m = matteFromGrid(grid, backdropFrom(s));
    const float w = static_cast<float>(m.width), h = static_cast<float>(m.height);
    const float cx = s.centreX * w, cy = s.centreY * h;
    const float extent = std::max(2.0f, s.extent * h);

    // The loose pieces are drawn as particles, so they are left out here.
    const int radius = openingRadius(extent);
    std::vector<float> opened = m.cover;
    erode(opened, m.width, m.height, radius);
    dilate(opened, m.width, m.height, radius);
    std::vector<float> detail(opened.size());
    for (std::size_t i = 0; i < detail.size(); ++i) {
        detail[i] = std::max(0.0f, m.cover[i] - opened[i]);
    }
    const std::vector<Patch> patches = findPatches(detail, m.width, m.height, 0.1f);
    const Pieces pieces = sortPieces(patches, opened, m, cx, cy, extent);
    std::vector<float> plain = m.cover;
    for (const auto* group : {&pieces.sparks, &pieces.streaks, &pieces.bits}) {
        for (const Patch* p : *group) {
            for (int at : p->points) {
                plain[static_cast<std::size_t>(at)] = opened[static_cast<std::size_t>(at)];
            }
        }
    }

    const auto lift = [&](const char* part, float half, const std::vector<float>& alpha) {
        Cutout cut;
        cut.part = part;
        cut.centreX = s.centreX;
        cut.centreY = s.centreY;
        cut.halfSize = half / h;
        const int side = std::clamp(static_cast<int>(std::lround(2.0f * half)), 16, std::max(16, maxSide));
        cut.image.width = side;
        cut.image.height = side;
        cut.image.rgba.assign(static_cast<std::size_t>(side) * static_cast<std::size_t>(side) * 4u, 0);
        float most = 0.0f;
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x) {
                const float px = cx + ((static_cast<float>(x) + 0.5f) / static_cast<float>(side) * 2.0f - 1.0f) * half;
                const float py = cy + ((static_cast<float>(y) + 0.5f) / static_cast<float>(side) * 2.0f - 1.0f) * half;
                // Fade to nothing at the rim, so the square never shows.
                const float dx = (px - cx) / half, dy = (py - cy) / half;
                const float rim = clamp01((1.0f - std::sqrt(dx * dx + dy * dy)) / 0.08f);
                const float a = clamp01(readAt(alpha, m.width, m.height, px, py)) * rim;
                std::uint8_t* d = &cut.image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(side) +
                                                   static_cast<std::size_t>(x)) * 4u];
                d[0] = static_cast<std::uint8_t>(std::lround(clamp01(readAt(m.r, m.width, m.height, px, py)) * 255.0f));
                d[1] = static_cast<std::uint8_t>(std::lround(clamp01(readAt(m.g, m.width, m.height, px, py)) * 255.0f));
                d[2] = static_cast<std::uint8_t>(std::lround(clamp01(readAt(m.b, m.width, m.height, px, py)) * 255.0f));
                d[3] = static_cast<std::uint8_t>(std::lround(a * 255.0f));
                most = std::max(most, a);
            }
        }
        if (most > 0.1f) {
            out.push_back(std::move(cut));
        }
    };

    if (wantRays) {
        // What is left when the round glow is taken away.
        const float limit = std::min(1.25f * std::max(s.rayLongest, s.rayReach) * h,
                                     std::max(w, h));
        const int steps = std::max(8, static_cast<int>(limit) + 2);
        std::vector<std::vector<float>> rings(static_cast<std::size_t>(steps));
        for (int y = 0; y < m.height; ++y) {
            for (int x = 0; x < m.width; ++x) {
                const float dx = static_cast<float>(x) - cx, dy = static_cast<float>(y) - cy;
                const int bin = static_cast<int>(std::sqrt(dx * dx + dy * dy));
                if (bin < steps) {
                    rings[static_cast<std::size_t>(bin)].push_back(plain[m.at(x, y)]);
                }
            }
        }
        std::vector<float> round(static_cast<std::size_t>(steps), 0.0f);
        for (std::size_t k = 0; k < rings.size(); ++k) {
            if (!rings[k].empty()) {
                const auto mid = rings[k].begin() + static_cast<std::ptrdiff_t>(rings[k].size() / 2);
                std::nth_element(rings[k].begin(), mid, rings[k].end());
                round[k] = *mid;
            }
        }
        std::vector<float> alpha(plain.size(), 0.0f);
        for (int y = 0; y < m.height; ++y) {
            for (int x = 0; x < m.width; ++x) {
                const float dx = static_cast<float>(x) - cx, dy = static_cast<float>(y) - cy;
                const float d = std::sqrt(dx * dx + dy * dy);
                const int bin = static_cast<int>(d);
                if (bin + 1 < steps) {
                    const float t = d - static_cast<float>(bin);
                    const float base = round[static_cast<std::size_t>(bin)] * (1.0f - t) + round[static_cast<std::size_t>(bin + 1)] * t;
                    alpha[m.at(x, y)] = std::max(0.0f, plain[m.at(x, y)] - base);
                }
            }
        }
        lift("rays", std::max(8.0f, limit), alpha);
    }
    if (wantBody) {
        // The main shape, as crisp as it was drawn, without its halo.
        const std::vector<Patch> solids = findPatches(s.hardness > 0.5f ? opened : m.cover, m.width, m.height, 0.5f);
        const Patch* body = nullptr;
        for (const Patch& p : solids) {
            if (!body || p.area > body->area) {
                body = &p;
            }
        }
        if (body) {
            std::vector<float> mask(plain.size(), 0.0f);
            for (int at : body->points) {
                mask[static_cast<std::size_t>(at)] = 1.0f;
            }
            dilate(mask, m.width, m.height, std::max(2, radius));
            std::vector<float> alpha(plain.size(), 0.0f);
            for (std::size_t i = 0; i < alpha.size(); ++i) {
                alpha[i] = mask[i] > 0.0f ? plain[i] : 0.0f;
            }
            lift("body", std::max(8.0f, 1.12f * s.bodyRadius * h), alpha);
        }
    }
    return out;
}

}  // namespace vfx::editor
