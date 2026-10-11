// VFX Forge editor: reading an effect that lies on the ground.
//
// A magic circle, a portal, a shockwave across the floor: a round, flat
// base seen from an angle, which shows in the picture as a level oval,
// often with shafts of light standing on it. A game engine builds these
// from a flat disc and upright streaks, and so does the rebuild; this file
// finds the oval, how far it leans back, what is drawn on it, and the
// shafts.
//
// Everything is measured on the matte with plain arithmetic. What cannot be
// told from a flat picture is left to the caller to mark as assumed.
#include "ReferenceTools.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace vfx::editor::ref {

namespace {

float degreesOf(float radians) { return radians * 180.0f / kPi; }

}  // namespace

// Each point becomes the least, then the greatest, within `radius` along
// its row only. What is left of a row after this is everything wider than
// the window: upright thin things are taken out, level ones stay.
void openAlongRows(std::vector<float>& values, int width, int height, int radius) {
    std::vector<float> row(static_cast<std::size_t>(width));
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < height; ++y) {
            float* line = &values[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)];
            for (int x = 0; x < width; ++x) {
                float v = line[x];
                for (int k = std::max(0, x - radius); k <= std::min(width - 1, x + radius); ++k) {
                    v = pass == 0 ? std::min(v, line[k]) : std::max(v, line[k]);
                }
                row[static_cast<std::size_t>(x)] = v;
            }
            std::copy(row.begin(), row.end(), line);
        }
    }
}

namespace {

struct Oval {
    float x = 0, y = 0;   // centre, grid points
    float half = 0;       // half its width
    float squash = 1;     // height over width
    float share = 0;      // of all the light in the picture
};

// The bright mass of the picture, once thin things are smoothed away, and
// whether it is a level oval.
bool findOval(const Matte& m, const Basics& basics, Oval& oval) {
    const int w = m.width, h = m.height;
    std::vector<float> soft = m.cover;
    const int reach = std::max(2, static_cast<int>(std::lround(0.03f * static_cast<float>(h))));
    blur(soft, w, h, reach);
    float top = 0.0f;
    for (float v : soft) {
        top = std::max(top, v);
    }
    if (top < 0.08f) {
        return false;
    }
    const std::vector<Patch> lumps = findPatches(soft, w, h, 0.45f * top);
    const Patch* base = nullptr;
    double all = 0;
    for (const Patch& p : lumps) {
        all += p.mass;
        if (!base || p.mass > base->mass) {
            base = &p;
        }
    }
    // One mass, not several of a size.
    if (!base || base->mass < 0.7 * all) {
        return false;
    }
    double sum = 0, sx = 0, sy = 0;
    for (int at : base->points) {
        const double v = soft[static_cast<std::size_t>(at)];
        sum += v;
        sx += v * (at % w);
        sy += v * (at / w);
    }
    if (!(sum > 1e-6)) {
        return false;
    }
    const double cx = sx / sum, cy = sy / sum;
    double xx = 0, xy = 0, yy = 0;
    for (int at : base->points) {
        const double v = soft[static_cast<std::size_t>(at)];
        const double dx = (at % w) - cx, dy = (at / w) - cy;
        xx += v * dx * dx;
        xy += v * dx * dy;
        yy += v * dy * dy;
    }
    xx /= sum;
    xy /= sum;
    yy /= sum;
    // The smoothing spread everything by a known amount: take it back off.
    const double side = 2.0 * reach + 1.0;
    const double spread = 2.0 * (side * side - 1.0) / 12.0;
    xx = std::max(0.25, xx - spread);
    yy = std::max(0.25, yy - spread);
    const double mean = 0.5 * (xx + yy);
    const double diff = std::sqrt(std::max(0.0, 0.25 * (xx - yy) * (xx - yy) + xy * xy));
    const double l1 = mean + diff, l2 = std::max(1e-6, mean - diff);
    const float lean = degreesOf(0.5f * std::atan2(static_cast<float>(2.0 * xy), static_cast<float>(xx - yy)));
    oval.x = static_cast<float>(cx);
    oval.y = static_cast<float>(cy);
    oval.half = 2.0f * static_cast<float>(std::sqrt(l1));
    oval.squash = static_cast<float>(std::sqrt(l2 / l1));
    oval.share = basics.total > 1e-6f ? static_cast<float>(base->mass / all) : 0.0f;
    // Level, clearly wider than tall, and a fair size.
    return std::fabs(lean) <= 12.0f && oval.squash >= 0.16f && oval.squash <= 0.66f &&
           oval.half >= 0.07f * static_cast<float>(w);
}

}  // namespace

std::string groundPatternFrom(const StillAnalysis& s) {
    if (s.groundHollow) {
        return "ring";
    }
    // Plenty of fine line: a circle of lines whatever else. Light that
    // differs from one side to the other as you go round: a whirl. Some
    // fine line and no such difference: a circle of lines again.
    if (s.groundFine >= 0.22f) {
        return "sigil";
    }
    // (Soft light with no rings in it that still differs a little from
    // side to side is a whirl too; with rings, it takes more to say so.)
    if (s.groundSwirl >= 0.2f || (s.groundSwirl >= 0.1f && s.groundRings == 0 && s.groundFine < 0.1f)) {
        return "vortex";
    }
    if (s.groundFine >= 0.14f) {
        return "sigil";
    }
    return "glow";
}

bool readGround(const Matte& m, const Basics& basics, StillAnalysis& out) {
    const int w = m.width, h = m.height;
    const auto fh = static_cast<float>(h);
    Oval oval;
    if (!findOval(m, basics, oval)) {
        return false;
    }
    // ---- the oval, measured again from its near edge.
    // Light rising from the base makes the whole mass look taller than the
    // base is. The near half (below the middle in the picture) has nothing
    // in front of it, so its outline is the base's own: row by row it
    // narrows as an oval does, and that says where the middle row is and
    // how deep the near half runs.
    {
        std::vector<float> smooth = m.cover;
        blur(smooth, w, h, 1);
        float peak = 0.0f;
        for (int at = 0; at < w * h; ++at) {
            const float dx = static_cast<float>(at % w) - oval.x, dy = static_cast<float>(at / w) - oval.y;
            if (std::fabs(dx) <= oval.half && std::fabs(dy) <= oval.half * oval.squash) {
                peak = std::max(peak, smooth[static_cast<std::size_t>(at)]);
            }
        }
        struct Row {
            float y = 0, half = 0, middle = 0;
        };
        struct Fit {
            float squash = 0, y = 0, half = 0, x = 0;
        };
        std::vector<Fit> fits;
        // Several outlines, from the faint edge inward: the middle answer is
        // kept, so one odd outline does not decide.
        for (const float share0 : {0.2f, 0.3f, 0.4f, 0.5f, 0.6f}) {
        const float line = share0 * peak;
        std::vector<Row> rows;
        float widest = 0.0f;
        const int from = std::max(0, static_cast<int>(std::floor(oval.y - 0.5f * oval.half * oval.squash)));
        for (int y = from; y < h; ++y) {
            // Outward from the middle column until the light gives out.
            const int mid = std::clamp(static_cast<int>(std::lround(oval.x)), 0, w - 1);
            if (smooth[m.at(mid, y)] < line) {
                if (static_cast<float>(y) > oval.y) {
                    break;
                }
                continue;
            }
            int left = mid, right = mid;
            while (left > 0 && smooth[m.at(left - 1, y)] >= line) {
                --left;
            }
            while (right < w - 1 && smooth[m.at(right + 1, y)] >= line) {
                ++right;
            }
            Row row;
            row.y = static_cast<float>(y);
            row.half = 0.5f * static_cast<float>(right - left + 1);
            row.middle = 0.5f * static_cast<float>(left + right);
            widest = std::max(widest, row.half);
            rows.push_back(row);
        }
        if (rows.size() >= 4 && widest >= 3.0f) {
            // y = middle + depth * sqrt(1 - (half / widest)^2), for the rows
            // of the near half: a straight line in that square root.
            float lastWide = rows.front().y;
            for (const Row& row : rows) {
                if (row.half >= 0.985f * widest) {
                    lastWide = row.y;
                }
            }
            double n = 0, su = 0, sy = 0, suu = 0, suy = 0, sm = 0;
            for (const Row& row : rows) {
                const float share = row.half / widest;
                if (row.y <= lastWide || share < 0.2f || share > 0.97f) {
                    continue;
                }
                const double u = std::sqrt(std::max(0.0, 1.0 - static_cast<double>(share) * share));
                n += 1.0;
                su += u;
                sy += row.y;
                suu += u * u;
                suy += u * row.y;
                sm += row.middle;
            }
            const double spread = n * suu - su * su;
            if (n >= 3.0 && spread > 1e-6) {
                const double depth = (n * suy - su * sy) / spread;
                const double middleRow = (sy - depth * su) / n;
                const float squash = static_cast<float>(depth) / widest;
                if (depth > 1.0 && squash >= 0.12f && squash <= 0.7f &&
                    std::fabs(middleRow - oval.y) <= 0.6 * widest) {
                    fits.push_back(Fit{squash, static_cast<float>(middleRow), widest, static_cast<float>(sm / n)});
                }
            }
        }
        }
        if (!fits.empty()) {
            std::sort(fits.begin(), fits.end(), [](const Fit& a, const Fit& b) { return a.squash < b.squash; });
            const Fit& fit = fits[fits.size() / 2];
            oval.squash = fit.squash;
            oval.y = fit.y;
            oval.half = fit.half;
            oval.x = fit.x;
        }
    }
    // ---- and once more, from where its light stops.
    // Outlines at a level of brightness follow the soft light round the
    // base as much as the base. The base itself ends where the light falls
    // away fastest. Looking outward from the middle in every direction of
    // the near half, that place is found, and an oval is put through the
    // places found.
    float edgeRadius = 0.0f;
    float edgeAgreed = 0.0f;  // the share of directions whose edge lies on the oval
    {
        std::vector<float> smooth = m.cover;
        blur(smooth, w, h, 1);
        struct Point {
            double x = 0, y = 0;
        };
        std::vector<Point> points;
        const float reach = std::min(2.2f * oval.half, static_cast<float>(std::max(w, h)));
        // All the way round: the far edge is partly hidden by what rises
        // from the base, but what can be seen of it says where the middle
        // is, which the near half alone cannot. Directions that run up a
        // shaft of light land far from the oval and are dropped below.
        for (int step = 0; step < 48; ++step) {
            const float angle = 2.0f * kPi * static_cast<float>(step) / 48.0f;  // 0 is right, a quarter turn is straight down
            const float dx = std::cos(angle), dy = std::sin(angle);
            float steepest = 0.0f;
            std::vector<std::pair<float, float>> drops;
            for (float t = 3.0f; t <= reach; t += 1.0f) {
                const float inside = readAt(smooth, w, h, oval.x + dx * (t - 1.5f), oval.y + dy * (t - 1.5f));
                const float outside = readAt(smooth, w, h, oval.x + dx * (t + 1.5f), oval.y + dy * (t + 1.5f));
                const float drop = inside - outside;
                drops.emplace_back(t, drop);
                steepest = std::max(steepest, drop);
            }
            if (steepest < 0.1f) {
                continue;
            }
            // The outermost place that is nearly as steep as the steepest:
            // the last ring of a circle of rings, not the first.
            float at = 0.0f;
            for (const auto& [t, drop] : drops) {
                if (drop >= 0.6f * steepest) {
                    at = t;
                }
            }
            if (at > 0.0f) {
                points.push_back(Point{static_cast<double>(dx * at), static_cast<double>(dy * at)});
            }
        }
        // a x^2 + b y^2 + c y = 1: an oval level with the picture, whose
        // middle may be a little above or below where it was taken to be.
        const auto fit = [&](const std::vector<Point>& use, double& a, double& b, double& c) {
            double m11 = 0, m12 = 0, m13 = 0, m22 = 0, m23 = 0, m33 = 0, r1 = 0, r2 = 0, r3 = 0;
            for (const Point& p : use) {
                const double u = p.x * p.x, v = p.y * p.y, q = p.y;
                m11 += u * u;
                m12 += u * v;
                m13 += u * q;
                m22 += v * v;
                m23 += v * q;
                m33 += q * q;
                r1 += u;
                r2 += v;
                r3 += q;
            }
            const double det = m11 * (m22 * m33 - m23 * m23) - m12 * (m12 * m33 - m23 * m13) + m13 * (m12 * m23 - m22 * m13);
            if (std::fabs(det) < 1e-9) {
                return false;
            }
            a = (r1 * (m22 * m33 - m23 * m23) - m12 * (r2 * m33 - m23 * r3) + m13 * (r2 * m23 - m22 * r3)) / det;
            b = (m11 * (r2 * m33 - m23 * r3) - r1 * (m12 * m33 - m23 * m13) + m13 * (m12 * r3 - r2 * m13)) / det;
            c = (m11 * (m22 * r3 - r2 * m23) - m12 * (m12 * r3 - r2 * m13) + r1 * (m12 * m23 - m22 * m13)) / det;
            return a > 1e-9 && b > 1e-9;
        };
        // Many of the places found are not on the base's edge at all: a
        // direction that runs up a shaft of light lands at the top of the
        // shaft. So no one fit through all of them is trusted. Instead,
        // ovals are put through threes of them, spread round the turn, and
        // the oval that the most places agree with is kept; it is then
        // settled by all the places that agree with it.
        const auto agrees = [](const Point& p, double a, double b, double c) {
            return std::fabs(a * p.x * p.x + b * p.y * p.y + c * p.y - 1.0) < 0.16;
        };
        const auto sound = [&](double a, double b, double c, float& halfWidth, float& squash, double& shift) {
            if (!(a > 1e-9 && b > 1e-9)) {
                return false;
            }
            shift = -c / (2.0 * b);
            const double scale = 1.0 + c * c / (4.0 * b);
            if (!(scale > 0.0)) {
                return false;
            }
            halfWidth = static_cast<float>(std::sqrt(scale / a));
            squash = static_cast<float>(std::sqrt(scale / b)) / halfWidth;
            return halfWidth >= 0.5f * oval.half && halfWidth <= 1.6f * oval.half && squash >= 0.12f && squash <= 0.75f &&
                   std::fabs(shift) <= 0.6 * halfWidth;
        };
        const std::size_t n = points.size();
        std::size_t most = 0;
        double bestA = 0, bestB = 0, bestC = 0;
        if (n >= 16) {
            for (const std::size_t second : {n / 3, n / 4, n / 5, n / 7}) {
                for (const std::size_t third : {2 * n / 3, n / 2, 3 * n / 5}) {
                    for (std::size_t i = 0; i < n; ++i) {
                        const std::vector<Point> three = {points[i], points[(i + second) % n], points[(i + third) % n]};
                        double a = 0, b = 0, c = 0;
                        float halfWidth = 0, squash = 0;
                        double shift = 0;
                        if (!fit(three, a, b, c) || !sound(a, b, c, halfWidth, squash, shift)) {
                            continue;
                        }
                        std::size_t count = 0;
                        for (const Point& p : points) {
                            count += agrees(p, a, b, c) ? 1u : 0u;
                        }
                        if (count > most) {
                            most = count;
                            bestA = a;
                            bestB = b;
                            bestC = c;
                        }
                    }
                }
            }
        }
        // At least a third of the turn has to agree, or there is no oval.
        if (most >= std::max<std::size_t>(10, n / 3)) {
            double a = bestA, b = bestB, c = bestC;
            for (int round = 0; round < 2; ++round) {
                std::vector<Point> agreeing;
                for (const Point& p : points) {
                    if (agrees(p, a, b, c)) {
                        agreeing.push_back(p);
                    }
                }
                double a2 = 0, b2 = 0, c2 = 0;
                float halfWidth = 0, squash = 0;
                double shift = 0;
                if (agreeing.size() < 8 || !fit(agreeing, a2, b2, c2) || !sound(a2, b2, c2, halfWidth, squash, shift)) {
                    break;
                }
                a = a2;
                b = b2;
                c = c2;
            }
            float halfWidth = 0, squash = 0;
            double shift = 0;
            if (sound(a, b, c, halfWidth, squash, shift)) {
                oval.half = halfWidth;
                oval.squash = squash;
                oval.y += static_cast<float>(shift);
                edgeRadius = halfWidth;
                std::size_t count = 0;
                for (const Point& p : points) {
                    count += agrees(p, a, b, c) ? 1u : 0u;
                }
                edgeAgreed = static_cast<float>(count) / 48.0f;
            }
        }
    }
    const float k = oval.squash;
    const float cx = oval.x, cy = oval.y;

    // How bright each point looks, not only how covered: where the picture
    // is solid light, lines and arms still show as whiter against colour.
    std::vector<float> seen(m.cover.size());
    for (std::size_t i = 0; i < seen.size(); ++i) {
        seen[i] = m.cover[i] * (0.35f + 0.65f * luminance(m.r[i], m.g[i], m.b[i]));
    }

    // ---- the base, read outward from its middle as if seen from above.
    // Only the near half is read (below the middle in the picture): light
    // rising from the base covers the far half.
    const int steps = std::clamp(static_cast<int>(std::lround(3.0f * oval.half)), 16, 512);
    std::vector<double> amount(static_cast<std::size_t>(steps), 0.0), count(static_cast<std::size_t>(steps), 0.0);
    std::vector<double> red(amount.size(), 0.0), green(amount.size(), 0.0), blue(amount.size(), 0.0);
    for (int y = static_cast<int>(std::floor(cy)); y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float dx = static_cast<float>(x) - cx, dy = (static_cast<float>(y) - cy) / k;
            const float rho = std::sqrt(dx * dx + dy * dy);
            const auto bin = static_cast<std::size_t>(rho);
            if (bin >= amount.size()) {
                continue;
            }
            const std::size_t i = m.at(x, y);
            const float c = m.cover[i];
            amount[bin] += c;
            count[bin] += 1.0;
            red[bin] += c * m.r[i];
            green[bin] += c * m.g[i];
            blue[bin] += c * m.b[i];
        }
    }
    std::vector<float> profile(amount.size(), 0.0f);
    float level = 0.0f;
    for (std::size_t i = 0; i < profile.size(); ++i) {
        profile[i] = count[i] > 0.5 ? static_cast<float>(amount[i] / count[i]) : 0.0f;
        level = std::max(level, profile[i]);
    }
    if (level < 0.15f) {
        return false;
    }
    // The base ends where its light has fallen to a half; its glow, where
    // there is almost none.
    std::size_t edge = 0, glowEnd = 0;
    for (std::size_t i = 0; i < profile.size(); ++i) {
        if (profile[i] >= 0.5f * level) {
            edge = i;
        }
        if (profile[i] >= 0.06f * level) {
            glowEnd = i;
        }
    }
    const float radius = edgeRadius > 0.0f ? std::max(3.0f, edgeRadius) : std::max(3.0f, static_cast<float>(edge) + 0.5f);
    if (radius < 0.06f * static_cast<float>(w)) {
        return false;
    }
    // Hollow: its light is out toward the rim, the middle nearly empty.
    // Ringed: brighter at the rim than in the middle, which no flat drawn
    // shape is.
    bool hollow = false, ringed = false;
    {
        float rim = 0.0f, inside = 0.0f;
        int rimCount = 0, innerCount = 0;
        for (std::size_t i = 0; i < profile.size(); ++i) {
            const float at = (static_cast<float>(i) + 0.5f) / radius;
            if (at <= 0.3f) {
                inside += profile[i];
                ++innerCount;
            } else if (at >= 0.55f && at <= 1.05f) {
                rim = std::max(rim, profile[i]);
                ++rimCount;
            }
        }
        inside = innerCount > 0 ? inside / static_cast<float>(innerCount) : 0.0f;
        hollow = rimCount > 0 && rim > 0.2f && inside <= 0.6f * rim;
        ringed = rimCount > 0 && rim > 0.2f && inside <= 0.8f * rim;
    }
    // One thing, not a scatter of pieces that happen to lie in an oval. A
    // filled base has light over most of its near half. A hollow one has
    // light most of the way round its rim.
    if (ringed) {
        constexpr int kAround = 32;
        int found = 0;
        for (int a = 0; a < kAround; ++a) {
            const float angle = kPi * (static_cast<float>(a) + 0.5f) / static_cast<float>(kAround);
            float most = 0.0f;
            for (float at = 0.55f; at <= 1.1f; at += 0.05f) {
                most = std::max(most, readAt(m.cover, w, h, cx + std::cos(angle) * at * radius,
                                             cy + std::sin(angle) * at * radius * k));
            }
            found += most >= 0.3f * level ? 1 : 0;
        }
        out.groundFill = static_cast<float>(found) / static_cast<float>(kAround);
        if (out.groundFill < 0.8f) {
            return false;
        }
    } else {
        int inside = 0, lit = 0;
        for (int y = static_cast<int>(std::floor(cy)); y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float dx = static_cast<float>(x) - cx, dy = (static_cast<float>(y) - cy) / k;
                if (dx * dx + dy * dy <= 0.72f * radius * radius) {
                    ++inside;
                    lit += m.cover[m.at(x, y)] >= 0.12f * level ? 1 : 0;
                }
            }
        }
        out.groundFill = inside > 0 ? static_cast<float>(lit) / static_cast<float>(inside) : 0.0f;
        if (inside < 12 || out.groundFill < 0.8f) {
            return false;
        }
    }
    // The same on the left as on the right. (A flash with a tongue of flame
    // to one side is long and level too, but it is not this.)
    {
        double cross = 0, left = 0, right = 0;
        for (int y = 0; y < h; ++y) {
            const float dy = (static_cast<float>(y) - cy) / k;
            for (int x = static_cast<int>(std::ceil(cx)); x < w; ++x) {
                const float dx = static_cast<float>(x) - cx;
                if (dx * dx + dy * dy > 1.3f * radius * radius) {
                    continue;
                }
                const float a = m.cover[m.at(x, y)];
                const float b = readAt(m.cover, w, h, 2.0f * cx - static_cast<float>(x), static_cast<float>(y));
                cross += static_cast<double>(a) * b;
                right += static_cast<double>(a) * a;
                left += static_cast<double>(b) * b;
            }
        }
        const double alike = left > 1e-9 && right > 1e-9 ? cross / std::sqrt(left * right) : 0.0;
        if (alike < 0.8) {
            return false;
        }
    }
    // And it is the main thing in the picture, not a patch under a cloud of
    // pieces: a good share of all the light is on or near the base.
    {
        double near = 0;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float dx = static_cast<float>(x) - cx, dy = (static_cast<float>(y) - cy) / k;
                if (dx * dx + dy * dy <= 1.35f * radius * radius) {
                    near += m.cover[m.at(x, y)];
                }
            }
        }
        if (basics.total > 1e-6f && near < 0.4 * basics.total) {
            return false;
        }
    }
    const auto colourBetween = [&](float from, float to) {
        double a = 0, r = 0, g = 0, b = 0;
        for (std::size_t i = 0; i < profile.size(); ++i) {
            const float at = static_cast<float>(i) + 0.5f;
            if (at >= from && at <= to) {
                a += amount[i];
                r += red[i];
                g += green[i];
                b += blue[i];
            }
        }
        return a > 1e-6 ? Swatch{static_cast<float>(r / a), static_cast<float>(g / a), static_cast<float>(b / a), 0}
                        : Swatch{1, 1, 1, 0};
    };

    // ---- shafts of light standing on it: thin, upright, above the middle.
    std::vector<float> level2 = m.cover;
    const int thin = std::max(1, static_cast<int>(std::lround(0.011f * fh)));
    openAlongRows(level2, w, h, thin);
    std::vector<float> upright(level2.size());
    for (std::size_t i = 0; i < upright.size(); ++i) {
        upright[i] = std::max(0.0f, m.cover[i] - level2[i]);
    }
    struct Column {
        int top = 0, bottom = 0, lit = 0;
        double mass = 0, r = 0, g = 0, b = 0;
    };
    const int floorRow = std::clamp(static_cast<int>(std::lround(cy)), 1, h);
    std::vector<Column> columns(static_cast<std::size_t>(w));
    for (int x = 0; x < w; ++x) {
        Column& c = columns[static_cast<std::size_t>(x)];
        c.top = floorRow;
        for (int y = 0; y < floorRow; ++y) {
            const std::size_t i = m.at(x, y);
            const float v = upright[i];
            if (v > 0.06f) {
                c.top = std::min(c.top, y);
                c.bottom = std::max(c.bottom, y);
                ++c.lit;
                c.mass += v;
                c.r += v * m.r[i];
                c.g += v * m.g[i];
                c.b += v * m.b[i];
            }
        }
    }
    struct Shaft {
        float x = 0, height = 0, width = 0;
        double mass = 0, r = 0, g = 0, b = 0;
    };
    std::vector<Shaft> shafts;
    const int tall = std::max(4, static_cast<int>(std::lround(0.09f * fh)));
    for (int x = 0; x < w; ++x) {
        const Column& c = columns[static_cast<std::size_t>(x)];
        const int span = c.bottom - c.top + 1;
        // Tall, and lit for a good part of its height (a dotted shaft counts;
        // two stray sparks one above the other do not).
        const bool standing = c.lit >= tall && c.lit * 5 >= span * 2 && std::fabs(static_cast<float>(x) - cx) <= 1.3f * radius;
        if (!standing) {
            continue;
        }
        const float height = static_cast<float>(floorRow - c.top);
        if (!shafts.empty() && static_cast<float>(x) - shafts.back().x <= shafts.back().width + 0.5f) {
            Shaft& s = shafts.back();
            s.width += 1.0f;
            s.height = std::max(s.height, height);
            s.mass += c.mass;
            s.r += c.r;
            s.g += c.g;
            s.b += c.b;
        } else {
            Shaft s;
            s.x = static_cast<float>(x);
            s.width = 1.0f;
            s.height = height;
            s.mass = c.mass;
            s.r = c.r;
            s.g = c.g;
            s.b = c.b;
            shafts.push_back(s);
        }
    }

    // ---- what is drawn on the base: a whirl, a circle of lines, or plain light.
    // Round the near half at three distances: a whirl's arms make the light
    // rise and fall as you go round; rings and plain light do not.
    float swirl = 0.0f;
    {
        constexpr int kAround = 24;
        int bands = 0;
        for (const float at : {0.4f, 0.58f, 0.76f}) {
            std::array<float, kAround> ring{};
            float mean = 0.0f;
            for (int a = 0; a < kAround; ++a) {
                const float angle = kPi * (static_cast<float>(a) + 0.5f) / static_cast<float>(kAround);
                const float x = cx + std::cos(angle) * at * radius;
                const float y = cy + std::sin(angle) * at * radius * k;
                ring[static_cast<std::size_t>(a)] = readAt(seen, w, h, x, y);
                mean += ring[static_cast<std::size_t>(a)];
            }
            mean /= static_cast<float>(kAround);
            if (mean < 0.05f) {
                continue;
            }
            // Only what differs between left and right counts. Rings, and
            // an oval measured a little wrong, are the same on both sides;
            // the arms of a whirl are not. Neighbours are averaged, so that
            // thin lines crossing the circle do not count either.
            const auto smooth = [&](int a) {
                const int lo = std::max(0, a - 1), hi = std::min(kAround - 1, a + 1);
                return (ring[static_cast<std::size_t>(lo)] + ring[static_cast<std::size_t>(a)] +
                        ring[static_cast<std::size_t>(hi)]) / 3.0f;
            };
            float varied = 0.0f;
            for (int a = 0; a < kAround / 2; ++a) {
                const float gap = smooth(a) - smooth(kAround - 1 - a);
                varied += gap * gap;
            }
            swirl += std::sqrt(varied / static_cast<float>(kAround / 2)) / mean;
            ++bands;
        }
        swirl = bands > 0 ? swirl / static_cast<float>(bands) : 0.0f;
    }
    // Along the level line through the middle, where the picture is not
    // squashed: fine lines show as light that rises and falls sharply.
    float lines = 0.0f;
    int rings = 0;
    {
        const int reach = static_cast<int>(radius);
        std::vector<float> across(static_cast<std::size_t>(reach), 0.0f);
        for (int i = 0; i < reach; ++i) {
            const float left = readAt(seen, w, h, cx - static_cast<float>(i) - 0.5f, cy);
            const float right = readAt(seen, w, h, cx + static_cast<float>(i) + 0.5f, cy);
            across[static_cast<std::size_t>(i)] = 0.5f * (left + right);
        }
        float jumps = 0.0f, mean = 0.0f;
        for (int i = 1; i + 1 < reach; ++i) {
            const float here = across[static_cast<std::size_t>(i)];
            const float around = 0.5f * (across[static_cast<std::size_t>(i - 1)] + across[static_cast<std::size_t>(i + 1)]);
            jumps += std::fabs(here - around);
            mean += here;
            if (here > around + 0.04f && here > across[static_cast<std::size_t>(i - 1)] &&
                here >= across[static_cast<std::size_t>(i + 1)]) {
                ++rings;
            }
        }
        lines = mean > 1e-3f ? jumps / mean : 0.0f;
    }

    // Fine detail anywhere on the near half: how much each point differs
    // from its surroundings. Lines and glyphs do; soft light does not.
    float fine = 0.0f;
    {
        std::vector<float> smooth = seen;
        blur(smooth, w, h, std::max(1, static_cast<int>(std::lround(0.012f * fh))));
        double differs = 0, all = 0;
        for (int y = static_cast<int>(std::floor(cy)); y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float dx = static_cast<float>(x) - cx, dy = (static_cast<float>(y) - cy) / k;
                const float rho2 = dx * dx + dy * dy;
                if (rho2 <= 0.9f * radius * radius && rho2 >= 0.04f * radius * radius) {
                    const std::size_t i = m.at(x, y);
                    differs += std::fabs(seen[i] - smooth[i]);
                    all += smooth[i];
                }
            }
        }
        fine = all > 1e-6 ? static_cast<float>(differs / all) : 0.0f;
    }
    out.groundFine = fine;

    // ---- the middle: a white light of its own?
    const Swatch middle = colourBetween(0.0f, 0.22f * radius);
    const Swatch body = colourBetween(0.6f * radius, 0.95f * radius);
    const Swatch inner = colourBetween(0.28f * radius, 0.55f * radius);
    const float middleLevel = profile[std::min(profile.size() - 1, static_cast<std::size_t>(0.1f * radius))];

    out.ground = true;
    out.groundX = cx / static_cast<float>(w);
    out.groundY = cy / fh;
    out.groundRadius = radius / fh;
    out.groundTilt = std::clamp(degreesOf(std::acos(std::clamp(k, 0.05f, 1.0f))), 30.0f, 82.0f);
    out.groundColour = body;
    out.groundInnerColour = inner;
    out.groundGlowRadius = std::max(radius, static_cast<float>(glowEnd) + 0.5f) / fh;
    out.groundGlowColour = colourBetween(radius, std::max(radius + 2.0f, static_cast<float>(glowEnd)));
    out.groundLevel = level;
    out.groundSwirl = swirl;
    out.groundLines = lines;
    out.groundRings = rings;
    // A light of its own in the middle: a patch about the centre that is
    // bright and much whiter than the base around it. (It may sit a little
    // above the centre, being light that rises.)
    {
        const float bodyTint = saturation(body.r, body.g, body.b);
        double area = 0, r = 0, g = 0, b = 0;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float dx = static_cast<float>(x) - cx, dy = (static_cast<float>(y) - cy) / k;
                if (dx * dx + dy * dy > 0.42f * radius * radius) {
                    continue;
                }
                const std::size_t i = m.at(x, y);
                if (m.cover[i] > 0.8f && saturation(m.r[i], m.g[i], m.b[i]) < std::min(0.35f, bodyTint - 0.2f)) {
                    area += 1.0;
                    r += m.r[i];
                    g += m.g[i];
                    b += m.b[i];
                }
            }
        }
        // As a circle seen from above: the oval patch counted is `k` times
        // as tall as it is wide.
        const float across = static_cast<float>(std::sqrt(area / (kPi * k)));
        out.groundCore = across >= 0.1f * radius && middleLevel > 0.5f;
        out.groundCoreRadius = out.groundCore ? across / fh : 0.0f;
        out.groundCoreColour = area > 0.5 ? Swatch{static_cast<float>(r / area), static_cast<float>(g / area),
                                                   static_cast<float>(b / area), 0}
                                          : middle;
    }
    out.groundHollow = hollow;
    out.groundPattern = groundPatternFrom(out);

    out.beams = 0;
    if (shafts.size() >= 2) {
        std::vector<float> heights;
        double mass = 0, r = 0, g = 0, b = 0;
        float widest = 0.0f, widths = 0.0f;
        for (const Shaft& s : shafts) {
            heights.push_back(s.height);
            widths += s.width;
            widest = std::max(widest, std::fabs(s.x + 0.5f * s.width - cx));
            mass += s.mass;
            r += s.r;
            g += s.g;
            b += s.b;
        }
        std::sort(heights.begin(), heights.end());
        out.beams = static_cast<int>(shafts.size());
        for (const Shaft& s : shafts) {
            out.beamAt.push_back((s.x + 0.5f * s.width) / static_cast<float>(w));
        }
        out.beamHeight = heights[heights.size() / 2] / fh;
        out.beamTallest = heights.back() / fh;
        out.beamWidth = std::max(1.0f, widths / static_cast<float>(shafts.size())) / fh;
        out.beamSpread = std::max(widest, 1.0f) / fh;
        out.beamColour = mass > 1e-6 ? Swatch{static_cast<float>(r / mass), static_cast<float>(g / mass),
                                              static_cast<float>(b / mass), 0}
                                     : body;
    }
    // A crisp, filled, level oval with nothing rising from it is more likely
    // a drawn shape (a bean, a sweet, a pebble) than something on the floor.
    // A hollow one is a ring seen from an angle whatever its edge.
    if (out.beams < 2 && !ringed && out.hardness > 0.6f) {
        out.ground = false;
        return false;
    }
    // And with nothing rising from it, only a clearly flattened oval is
    // taken for the floor: a glow a little wider than tall is just a glow.
    if (out.beams < 2 && k > 0.55f) {
        out.ground = false;
        return false;
    }
    // Nor is a tongue of flame or a tangle of sparks that happens to be long
    // and level: with nothing in front of it, most of the way round its
    // edge has to lie on the oval.
    if (out.beams < 2 && edgeAgreed < 0.6f) {
        out.ground = false;
        return false;
    }
    return true;
}

float groundTurning(const std::vector<const Matte*>& frames, double gap, const StillAnalysis& ground,
                    float* confidence) {
    if (confidence) {
        *confidence = 0.0f;
    }
    if (frames.size() < 2 || !ground.ground || !(gap > 0.0)) {
        return 0.0f;
    }
    // Round the near half of the base, as seen from above, at three
    // distances. From one frame to the next the same pattern of light and
    // dark has moved along a little; the move that lines the two up best,
    // added over every pair of frames, is the turning.
    constexpr int kAround = 96;           // over half a turn
    constexpr int kMost = 28;             // the furthest move tried, in steps
    const float k = std::cos(ground.groundTilt * kPi / 180.0f);
    std::array<double, 2 * kMost + 1> score{};
    int pairs = 0;
    using Rings = std::array<std::array<float, kAround>, 3>;
    const auto read = [&](const Matte& m, Rings& rings) {
        const auto fw = static_cast<float>(m.width), fh = static_cast<float>(m.height);
        const float cx = ground.groundX * fw, cy = ground.groundY * fh, radius = ground.groundRadius * fh;
        std::size_t band = 0;
        for (const float at : {0.45f, 0.62f, 0.8f}) {
            for (int a = 0; a < kAround; ++a) {
                const float angle = kPi * (static_cast<float>(a) + 0.5f) / static_cast<float>(kAround);
                const float x = cx + std::cos(angle) * at * radius, y = cy + std::sin(angle) * at * radius * k;
                const float xi = std::clamp(x, 0.0f, fw - 1.0f), yi = std::clamp(y, 0.0f, fh - 1.0f);
                const std::size_t i = m.at(static_cast<int>(xi), static_cast<int>(yi));
                rings[band][static_cast<std::size_t>(a)] =
                    readAt(m.cover, m.width, m.height, x, y) * (0.35f + 0.65f * luminance(m.r[i], m.g[i], m.b[i]));
            }
            ++band;
        }
    };
    std::vector<Rings> all(frames.size());
    for (std::size_t f = 0; f < frames.size(); ++f) {
        read(*frames[f], all[f]);
    }
    // What stays put from frame to frame (the lie of the light, an oval
    // measured a little wrong) lines up best with no move at all, and would
    // drown the part that turns. So each place's average over the whole
    // clip is taken off first: only what changes is left to follow.
    Rings usual{};
    for (const Rings& rings : all) {
        for (std::size_t band = 0; band < 3; ++band) {
            for (std::size_t a = 0; a < kAround; ++a) {
                usual[band][a] += rings[band][a] / static_cast<float>(all.size());
            }
        }
    }
    for (Rings& rings : all) {
        for (std::size_t band = 0; band < 3; ++band) {
            for (std::size_t a = 0; a < kAround; ++a) {
                rings[band][a] -= usual[band][a];
            }
        }
    }
    for (std::size_t f = 1; f < all.size(); ++f) {
        const Rings& before = all[f - 1];
        const Rings& now = all[f];
        for (int move = -kMost; move <= kMost; ++move) {
            double cross = 0, a2 = 0, b2 = 0;
            for (std::size_t band = 0; band < 3; ++band) {
                for (int a = kMost; a < kAround - kMost; ++a) {
                    const float was = before[band][static_cast<std::size_t>(a)];
                    const float is = now[band][static_cast<std::size_t>(a + move)];
                    cross += static_cast<double>(was) * is;
                    a2 += static_cast<double>(was) * was;
                    b2 += static_cast<double>(is) * is;
                }
            }
            if (a2 > 1e-9 && b2 > 1e-9) {
                score[static_cast<std::size_t>(move + kMost)] += cross / std::sqrt(a2 * b2);
            }
        }
        ++pairs;
    }
    if (pairs == 0) {
        return 0.0f;
    }
    int best = 0;
    double top = -1e9, sum = 0;
    for (int move = -kMost; move <= kMost; ++move) {
        const double v = score[static_cast<std::size_t>(move + kMost)] / pairs;
        sum += v;
        if (v > top) {
            top = v;
            best = move;
        }
    }
    const double mean = sum / (2 * kMost + 1);
    const double still = score[static_cast<std::size_t>(kMost)] / pairs;
    // Sure when the best line-up is good, clearly better than the average
    // one, and not at the very end of what was tried.
    float sure = 0.9f * clamp01(static_cast<float>((top - mean) / 0.35)) * clamp01(static_cast<float>(top / 0.5));
    if (std::abs(best) >= kMost - 1) {
        sure *= 0.3f;
    }
    if (best == 0 || top - still < 0.02) {
        if (confidence) {
            *confidence = sure;
        }
        return 0.0f;
    }
    // Between steps: where a curve through the best and its neighbours peaks.
    double fineMove = best;
    if (std::abs(best) < kMost) {
        const double l = score[static_cast<std::size_t>(best - 1 + kMost)], c = score[static_cast<std::size_t>(best + kMost)],
                     r = score[static_cast<std::size_t>(best + 1 + kMost)];
        const double bend = l - 2 * c + r;
        if (bend < -1e-9) {
            fineMove += std::clamp(0.5 * (l - r) / bend, -0.5, 0.5);
        }
    }
    if (confidence) {
        *confidence = sure;
    }
    // A step is 180 / kAround degrees. The angle here runs clockwise on the
    // screen (y is down), so a move toward larger angles is a clockwise turn.
    const double degreesMoved = fineMove * 180.0 / kAround;
    return static_cast<float>(-degreesMoved / gap);
}

void groundOverClip(const std::vector<Matte>& mattes, double framesPerSecond, StillAnalysis& still) {
    if (!still.ground || mattes.size() < 2 || !(framesPerSecond > 0.0)) {
        return;
    }
    const auto middle = [](std::vector<float>& values) {
        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
    };
    const std::size_t samples = std::min<std::size_t>(12, mattes.size());
    std::vector<float> tilt, radius, x, y, swirl, fine, glow, beams, beamHeight, beamTallest, beamSpread, beamWidth;
    int hollow = 0, read = 0;
    for (std::size_t k = 0; k < samples; ++k) {
        const Matte& m = mattes[samples > 1 ? k * (mattes.size() - 1) / (samples - 1) : 0];
        StillAnalysis one;
        one.hardness = still.hardness;
        if (!readGround(m, measureBasics(m), one)) {
            continue;
        }
        ++read;
        tilt.push_back(one.groundTilt);
        radius.push_back(one.groundRadius);
        x.push_back(one.groundX);
        y.push_back(one.groundY);
        swirl.push_back(one.groundSwirl);
        fine.push_back(one.groundFine);
        glow.push_back(one.groundGlowRadius);
        hollow += one.groundHollow ? 1 : 0;
        beams.push_back(static_cast<float>(one.beams));
        if (one.beams >= 2) {
            beamHeight.push_back(one.beamHeight);
            beamTallest.push_back(one.beamTallest);
            beamSpread.push_back(one.beamSpread);
            beamWidth.push_back(one.beamWidth);
        }
    }
    // Most of the clip has to agree that this is a base on the ground.
    if (read * 2 >= static_cast<int>(samples) && read >= 2) {
        still.groundTilt = middle(tilt);
        still.groundRadius = middle(radius);
        still.groundX = middle(x);
        still.groundY = middle(y);
        still.groundSwirl = middle(swirl);
        still.groundFine = middle(fine);
        still.groundGlowRadius = std::max(still.groundRadius, middle(glow));
        still.groundHollow = hollow * 2 > read;
        still.centreX = still.groundX;
        still.centreY = still.groundY;
        const int count = static_cast<int>(std::lround(middle(beams)));
        if (count >= 2 && !beamHeight.empty()) {
            still.beams = count;
            still.beamHeight = middle(beamHeight);
            still.beamTallest = middle(beamTallest);
            still.beamSpread = middle(beamSpread);
            still.beamWidth = middle(beamWidth);
        } else if (count < 2) {
            still.beams = 0;
        }
    }

    // Turning: frames about a tenth of a second apart, across the clip.
    const auto step = static_cast<std::size_t>(std::max(1.0, std::round(framesPerSecond / 10.0)));
    std::vector<const Matte*> frames;
    for (std::size_t f = 0; f < mattes.size() && frames.size() < 48; f += step) {
        frames.push_back(&mattes[f]);
    }
    float sure = 0.0f;
    const float turn = groundTurning(frames, static_cast<double>(step) / framesPerSecond, still, &sure);
    still.groundTurn = turn;
    still.groundTurnConfidence = sure;

    still.groundPattern = groundPatternFrom(still);
    // Something that visibly turns has a pattern to turn: where nothing
    // finer could be made out, a whirl is the nearest thing to it.
    if (still.groundPattern == "glow" && sure >= 0.45f && std::fabs(turn) >= 8.0f) {
        still.groundPattern = "vortex";
    }
}

}  // namespace vfx::editor::ref
