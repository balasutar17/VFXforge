// How a clip changes over time, and the written report.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "ReferenceTools.h"

namespace vfx::editor::ref {

namespace {

constexpr int kThumb = 16;
constexpr int kTurnBins = 72;
constexpr float kPresent = 0.06f;  // share of the fullest frame that counts as "there"

using Thumb = std::array<float, kThumb * kThumb>;

Thumb makeThumb(const Matte& m) {
    Thumb t{};
    std::array<int, kThumb * kThumb> n{};
    for (int y = 0; y < m.height; ++y) {
        const int ty = std::min(kThumb - 1, y * kThumb / m.height);
        for (int x = 0; x < m.width; ++x) {
            const int tx = std::min(kThumb - 1, x * kThumb / m.width);
            t[static_cast<std::size_t>(ty * kThumb + tx)] += m.cover[m.at(x, y)];
            n[static_cast<std::size_t>(ty * kThumb + tx)] += 1;
        }
    }
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (n[i] > 0) {
            t[i] /= static_cast<float>(n[i]);
        }
    }
    return t;
}

float thumbDifference(const Thumb& a, const Thumb& b) {
    float sum = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        sum += std::fabs(a[i] - b[i]);
    }
    return sum / static_cast<float>(a.size());
}

// Brightness round the effect's own centre, for following a turn.
bool turnProfile(const Matte& m, const Basics& b, std::array<float, kTurnBins>& out) {
    out.fill(0.0f);
    const float from = 0.3f * b.extent, to = b.extent;
    if (!(to > from + 2.0f)) {
        return false;
    }
    const int samples = std::max(4, static_cast<int>(to - from));
    double mean = 0;
    for (int a = 0; a < kTurnBins; ++a) {
        const float angle = 2.0f * kPi * static_cast<float>(a) / kTurnBins;
        const float dx = std::cos(angle), dy = -std::sin(angle);
        float sum = 0.0f;
        for (int s = 0; s < samples; ++s) {
            const float radius = from + (to - from) * (static_cast<float>(s) + 0.5f) / static_cast<float>(samples);
            sum += readAt(m.cover, m.width, m.height, b.centreX + dx * radius, b.centreY + dy * radius);
        }
        out[static_cast<std::size_t>(a)] = sum / static_cast<float>(samples);
        mean += out[static_cast<std::size_t>(a)];
    }
    mean /= kTurnBins;
    double var = 0;
    for (float& v : out) {
        v -= static_cast<float>(mean);
        var += static_cast<double>(v) * v;
    }
    return std::sqrt(var / kTurnBins) > 0.02;
}

// Fits y = a + b t (+ c t^2 / 2) by weighted least squares.
void fitPath(const std::vector<double>& t, const std::vector<double>& y, const std::vector<double>& w, bool curved,
             double& speed, double& bend) {
    speed = 0;
    bend = 0;
    const std::size_t n = t.size();
    if (n < 3) {
        return;
    }
    const int k = curved && n >= 6 ? 3 : 2;
    double a[3][4] = {};
    for (std::size_t i = 0; i < n; ++i) {
        const double basis[3] = {1.0, t[i], 0.5 * t[i] * t[i]};
        for (int r = 0; r < k; ++r) {
            for (int c = 0; c < k; ++c) {
                a[r][c] += w[i] * basis[r] * basis[c];
            }
            a[r][3] += w[i] * basis[r] * y[i];
        }
    }
    // Gaussian elimination on the small system.
    for (int col = 0; col < k; ++col) {
        int pivot = col;
        for (int r = col + 1; r < k; ++r) {
            if (std::fabs(a[r][col]) > std::fabs(a[pivot][col])) {
                pivot = r;
            }
        }
        if (std::fabs(a[pivot][col]) < 1e-12) {
            return;
        }
        for (int c = 0; c < 4; ++c) {
            std::swap(a[col][c], a[pivot][c]);
        }
        for (int r = 0; r < k; ++r) {
            if (r == col) {
                continue;
            }
            const double f = a[r][col] / a[col][col];
            for (int c = col; c < 4; ++c) {
                a[r][c] -= f * a[col][c];
            }
        }
    }
    speed = a[1][3] / a[1][1];
    if (k == 3) {
        bend = a[2][3] / a[2][2];
    }
}

std::string seconds(double value) {
    char text[32];
    std::snprintf(text, sizeof text, "%.2f s", value);
    return text;
}

std::string percent(float value) {
    char text[32];
    std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::lround(value * 100.0f)));
    return text;
}

std::string number(float value, int places = 1) {
    char text[32];
    std::snprintf(text, sizeof text, "%.*f", places, static_cast<double>(value));
    return text;
}

std::string compass(float heading) {
    static const char* kWords[] = {"right", "up and right", "up", "up and left",
                                   "left",  "down and left", "down", "down and right"};
    float a = std::fmod(heading, 360.0f);
    if (a < 0) {
        a += 360.0f;
    }
    return kWords[static_cast<int>((a + 22.5f) / 45.0f) % 8];
}

}  // namespace

TimeAnalysis analyzeTime(const std::vector<Matte>& mattes, double framesPerSecond, const Progress& progress,
                         bool& cancelled) {
    TimeAnalysis out;
    cancelled = false;
    const int n = static_cast<int>(mattes.size());
    if (n < 2 || !(framesPerSecond > 0)) {
        return out;
    }
    out.frames = n;
    out.framesPerSecond = framesPerSecond;
    const float h = static_cast<float>(mattes.front().height);
    const float w = static_cast<float>(mattes.front().width);

    std::vector<Basics> basics(static_cast<std::size_t>(n));
    std::vector<Thumb> thumbs(static_cast<std::size_t>(n));
    std::vector<std::array<float, kTurnBins>> turns(static_cast<std::size_t>(n));
    std::vector<char> turnOk(static_cast<std::size_t>(n), 0);
    out.pieces.assign(static_cast<std::size_t>(n), 0.0f);
    out.pieceReach.assign(static_cast<std::size_t>(n), 0.0f);
    float most = 0.0f;
    for (int f = 0; f < n; ++f) {
        if (progress && !progress(static_cast<float>(f) / static_cast<float>(n), "Following the effect through the clip")) {
            cancelled = true;
            return out;
        }
        const auto i = static_cast<std::size_t>(f);
        basics[i] = measureBasics(mattes[i]);
        thumbs[i] = makeThumb(mattes[i]);
        most = std::max(most, basics[i].total);
    }
    if (!(most > 4.0f)) {
        return out;
    }
    out.available = true;
    out.energy.resize(static_cast<std::size_t>(n));
    out.radius.resize(static_cast<std::size_t>(n));
    out.brightness.resize(static_cast<std::size_t>(n));
    out.centreX.resize(static_cast<std::size_t>(n));
    out.centreY.resize(static_cast<std::size_t>(n));
    out.colour.resize(static_cast<std::size_t>(n));
    for (int f = 0; f < n; ++f) {
        const auto i = static_cast<std::size_t>(f);
        const Basics& b = basics[i];
        out.energy[i] = b.total / most;
        const bool there = out.energy[i] >= kPresent;
        out.radius[i] = there ? b.extent / h : 0.0f;
        out.brightness[i] = there ? b.brightness : 0.0f;
        out.centreX[i] = b.centreX / w;
        out.centreY[i] = b.centreY / h;
        out.colour[i] = b.colour;
        if (there) {
            countPieces(mattes[i], b.centreX, b.centreY, b.extent, out.pieces[i], out.pieceReach[i]);
            out.pieceReach[i] /= h;
            turnOk[i] = turnProfile(mattes[i], b, turns[i]) ? 1 : 0;
        }
    }
    const auto time = [&](int frame) { return static_cast<double>(frame) / framesPerSecond; };

    // ---- one burst that dies away, or something that keeps going
    // "Keeps going" means it is still there when the clip ends and has been
    // for a while. A clip may well show it starting up first.
    {
        const int tail = std::max(2, n * 2 / 5);
        float least = 1.0f, mean = 0.0f;
        for (int f = n - tail; f < n; ++f) {
            least = std::min(least, out.energy[static_cast<std::size_t>(f)]);
            mean += out.energy[static_cast<std::size_t>(f)];
        }
        mean /= static_cast<float>(tail);
        out.continuous = least >= 0.25f && mean >= 0.45f;
    }
    int steady = 0;
    if (out.continuous) {
        while (steady < n - 2 && out.energy[static_cast<std::size_t>(steady)] < 0.6f) {
            ++steady;
        }
        // Too little left after the start-up to describe: use it all.
        if (n - steady < std::max(4, n / 4)) {
            steady = 0;
        }
    }
    out.steadyFrom = time(steady);

    // ---- does it loop
    {
        double near = 0;
        for (int f = 0; f + 1 < n; ++f) {
            near += thumbDifference(thumbs[static_cast<std::size_t>(f)], thumbs[static_cast<std::size_t>(f + 1)]);
        }
        near /= std::max(1, n - 1);
        double far = 0;
        int farCount = 0;
        for (int f = 0; f + n / 2 < n; ++f) {
            far += thumbDifference(thumbs[static_cast<std::size_t>(f)], thumbs[static_cast<std::size_t>(f + n / 2)]);
            ++farCount;
        }
        far = farCount > 0 ? far / farCount : 0.0;
        const double wrap = thumbDifference(thumbs[static_cast<std::size_t>(steady)], thumbs.back());
        if (out.continuous) {
            // The last frame leads back into the first about as smoothly as
            // one frame leads into the next.
            if (wrap <= 2.0 * near + 0.004) {
                out.loops = true;
                out.loopLength = time(n - steady);
                out.loopConfidence = clamp01(static_cast<float>(1.0 - wrap / std::max(1e-6, far + near)));
            }
            // A shorter repeat inside the clip: the smallest step at which
            // frames come round to nearly the same picture again.
            std::vector<double> apartBy(static_cast<std::size_t>(n), -1.0);
            double typical = 0, lowest = 1e9;
            int measured = 0;
            for (int period = 3; period <= n - 3; ++period) {
                double sum = 0;
                int count = 0;
                for (int f = steady; f + period < n; ++f) {
                    sum += thumbDifference(thumbs[static_cast<std::size_t>(f)], thumbs[static_cast<std::size_t>(f + period)]);
                    ++count;
                }
                if (count < 3) {
                    break;
                }
                apartBy[static_cast<std::size_t>(period)] = sum / count;
                typical += sum / count;
                lowest = std::min(lowest, sum / count);
                ++measured;
            }
            if (measured > 0) {
                typical /= measured;
                if (typical > 0.002 && lowest <= 0.35 * typical + 0.001) {
                    for (int period = 3; period <= n - 3; ++period) {
                        const double v = apartBy[static_cast<std::size_t>(period)];
                        if (v >= 0 && v <= 1.5 * lowest + 0.0015) {
                            out.loops = true;
                            out.loopLength = time(period);
                            out.loopConfidence = clamp01(static_cast<float>(1.0 - v / typical));
                            break;
                        }
                    }
                }
            }
        }
    }

    // ---- sudden growth
    {
        const int gap = std::max(1, static_cast<int>(std::lround(0.05 * framesPerSecond)));
        std::vector<float> rise(static_cast<std::size_t>(n), 0.0f);
        for (int f = 0; f < n; ++f) {
            const float before = f - gap >= 0 ? out.energy[static_cast<std::size_t>(f - gap)] : 0.0f;
            rise[static_cast<std::size_t>(f)] = out.energy[static_cast<std::size_t>(f)] - before;
        }
        const int apart = std::max(2, static_cast<int>(std::lround(0.12 * framesPerSecond)));
        for (int f = 0; f < n; ++f) {
            const float v = rise[static_cast<std::size_t>(f)];
            if (v < 0.2f) {
                continue;
            }
            bool top = true;
            for (int d = 1; d <= apart && top; ++d) {
                if ((f - d >= 0 && rise[static_cast<std::size_t>(f - d)] >= v) ||
                    (f + d < n && rise[static_cast<std::size_t>(f + d)] > v)) {
                    top = false;
                }
            }
            // Something that keeps going has swells, not bursts.
            if (top && !out.continuous) {
                // The burst began where the rise began.
                out.bursts.push_back(time(std::max(0, f - gap)));
            }
        }
    }

    // ---- the same burst twice? Then the clip is a loop of it, and only
    // the first time round is described.
    int limit = n;  // frames from here on are a repeat
    if (!out.continuous && out.bursts.size() >= 2) {
        const int b0 = static_cast<int>(std::lround(out.bursts[0] * framesPerSecond));
        const int b1 = static_cast<int>(std::lround(out.bursts[1] * framesPerSecond));
        const int period = b1 - b0;
        const int overlap = std::min(period, n - b1);
        if (period >= 4 && overlap >= 3) {
            double same = 0, other = 0;
            for (int k = 0; k < overlap; ++k) {
                same += thumbDifference(thumbs[static_cast<std::size_t>(b0 + k)], thumbs[static_cast<std::size_t>(b1 + k)]);
                other += thumbDifference(thumbs[static_cast<std::size_t>(b0 + k)],
                                         thumbs[static_cast<std::size_t>(b0 + (k + period / 2) % period)]);
            }
            if (same <= 0.5 * other + 0.003 * overlap) {
                limit = b1;
                out.loops = true;
                out.loopLength = time(period);
                out.loopConfidence = clamp01(static_cast<float>(1.0 - same / std::max(1e-6, other)));
                out.bursts.resize(1);
            }
        }
    }

    // ---- when it is there
    int first = 0, last = limit - 1, fullest = 0;
    while (first < limit - 1 && out.energy[static_cast<std::size_t>(first)] < kPresent) {
        ++first;
    }
    while (last > first && out.energy[static_cast<std::size_t>(last)] < kPresent) {
        --last;
    }
    if (out.continuous) {
        first = steady;
    }
    for (int f = first; f <= last; ++f) {
        if (out.energy[static_cast<std::size_t>(f)] > out.energy[static_cast<std::size_t>(fullest < first ? first : fullest)]) {
            fullest = f;
        }
    }
    fullest = std::clamp(fullest, first, last);
    out.start = time(first);
    out.peak = time(fullest);
    out.end = time(last + 1);
    // The frame that best stands for the whole effect: for a burst, the
    // moment by which half of all its light has been shown, and never
    // before its fullest. A flash that is over in an instant is brightest
    // early; what follows it lasts longer and matters more.
    out.main = out.peak;
    if (!out.continuous && last > fullest) {
        double total = 0;
        for (int f = first; f <= last; ++f) {
            total += out.energy[static_cast<std::size_t>(f)];
        }
        double running = 0;
        for (int f = first; f <= last; ++f) {
            running += out.energy[static_cast<std::size_t>(f)];
            if (running >= 0.5 * total) {
                out.main = time(std::max(f, fullest));
                break;
            }
        }
    }

    // ---- how it spreads
    int widest = first;
    for (int f = first; f <= last; ++f) {
        if (out.radius[static_cast<std::size_t>(f)] > out.radius[static_cast<std::size_t>(widest)]) {
            widest = f;
        }
    }
    out.widest = time(widest);
    if (widest > first) {
        const float full = out.radius[static_cast<std::size_t>(widest)];
        const float begin = out.radius[static_cast<std::size_t>(first)];
        if (full > begin * 1.15f) {
            double halfAt = time(widest) - time(first);
            for (int f = first; f <= widest; ++f) {
                if (out.radius[static_cast<std::size_t>(f)] >= begin + 0.5f * (full - begin)) {
                    halfAt = std::max(0.5 / framesPerSecond, time(f) - time(first));
                    break;
                }
            }
            out.slowing = static_cast<float>(0.693147 / halfAt);
            out.growth = (full - begin) * out.slowing;
        }
    }

    // ---- how it travels
    {
        std::vector<double> t, x, y, weight;
        for (int f = first; f <= last; ++f) {
            const auto i = static_cast<std::size_t>(f);
            if (out.energy[i] < 0.15f) {
                continue;
            }
            t.push_back(time(f) - time(first));
            x.push_back(static_cast<double>(out.centreX[i]) * w / h);
            y.push_back(-static_cast<double>(out.centreY[i]));
            weight.push_back(out.energy[i]);
        }
        if (t.size() >= 4) {
            double vx = 0, ax = 0, vy = 0, ay = 0;
            fitPath(t, x, weight, true, vx, ax);
            fitPath(t, y, weight, true, vy, ay);
            const double span = t.back() - t.front();
            const double movedX = vx * span + 0.5 * ax * span * span;
            const double movedY = vy * span + 0.5 * ay * span * span;
            double farthest = 0;
            for (std::size_t i = 0; i < t.size(); ++i) {
                farthest = std::max(farthest, std::hypot(x[i] - x.front(), y[i] - y.front()));
            }
            if (std::max(std::hypot(movedX, movedY), farthest) > 0.06) {
                out.driftX = static_cast<float>(vx);
                out.driftY = static_cast<float>(vy);
                // A bend only counts when it changes where the effect ends up.
                if (0.5 * std::hypot(ax, ay) * span * span > 0.04) {
                    out.pullX = static_cast<float>(ax);
                    out.pullY = static_cast<float>(ay);
                } else {
                    // Without a bend, a straight line is the better description.
                    fitPath(t, x, weight, false, vx, ax);
                    fitPath(t, y, weight, false, vy, ay);
                    out.driftX = static_cast<float>(vx);
                    out.driftY = static_cast<float>(vy);
                }
            }
        }
    }

    // ---- does it turn
    {
        std::vector<float> rates;
        int pairs = 0;
        constexpr int kReach = 12;
        for (int f = first; f < last; ++f) {
            const auto i = static_cast<std::size_t>(f);
            if (!turnOk[i] || !turnOk[i + 1]) {
                continue;
            }
            ++pairs;
            std::array<double, 2 * kReach + 1> corr{};
            double na = 0, nb = 0;
            for (int a = 0; a < kTurnBins; ++a) {
                na += static_cast<double>(turns[i][static_cast<std::size_t>(a)]) * turns[i][static_cast<std::size_t>(a)];
                nb += static_cast<double>(turns[i + 1][static_cast<std::size_t>(a)]) * turns[i + 1][static_cast<std::size_t>(a)];
            }
            const double norm = std::sqrt(na * nb);
            if (norm < 1e-9) {
                continue;
            }
            int best = 0;
            for (int s = -kReach; s <= kReach; ++s) {
                double sum = 0;
                for (int a = 0; a < kTurnBins; ++a) {
                    sum += static_cast<double>(turns[i][static_cast<std::size_t>(a)]) *
                           turns[i + 1][static_cast<std::size_t>((a + s + kTurnBins) % kTurnBins)];
                }
                corr[static_cast<std::size_t>(s + kReach)] = sum / norm;
                if (corr[static_cast<std::size_t>(s + kReach)] > corr[static_cast<std::size_t>(best + kReach)]) {
                    best = s;
                }
            }
            const double top = corr[static_cast<std::size_t>(best + kReach)];
            if (top < 0.5 || best == -kReach || best == kReach) {
                continue;
            }
            // The true peak lies between bins: a parabola through three points.
            const double l = corr[static_cast<std::size_t>(best + kReach - 1)], r = corr[static_cast<std::size_t>(best + kReach + 1)];
            const double denom = l - 2.0 * top + r;
            const double fine = std::fabs(denom) > 1e-9 ? 0.5 * (l - r) / denom : 0.0;
            rates.push_back(static_cast<float>((best + std::clamp(fine, -0.5, 0.5)) * (360.0 / kTurnBins) *
                                               framesPerSecond));
        }
        if (rates.size() >= 3) {
            std::sort(rates.begin(), rates.end());
            const float middle = rates[rates.size() / 2];
            const float spreadOf = rates[rates.size() * 3 / 4] - rates[rates.size() / 4];
            if (std::fabs(middle) >= 8.0f) {
                out.turning = middle;
                out.turningConfidence =
                    clamp01(static_cast<float>(rates.size()) / static_cast<float>(std::max(1, pairs))) *
                    clamp01(1.0f - spreadOf / (std::fabs(middle) + 20.0f));
            }
        }
    }

    // ---- the moments worth marking
    const auto mark = [&](double at, const char* key, const char* label) {
        out.markers.push_back(TimeMarker{at, key, label});
    };
    if (!out.continuous) {
        mark(out.start, "appear", "Appears");
        if (!out.bursts.empty()) {
            mark(out.bursts.front(), "burst", "Main burst");
        }
    }
    mark(out.peak, "peak", "Fullest");
    if (std::abs(widest - fullest) > 2) {
        mark(out.widest, "widest", "Widest");
    }
    for (std::size_t i = 1; i < out.bursts.size(); ++i) {
        mark(out.bursts[i], "second", "Second burst");
    }
    if (!out.continuous) {
        for (int f = fullest; f <= last; ++f) {
            if (out.energy[static_cast<std::size_t>(f)] < 0.5f) {
                mark(time(f), "fading", "Fading");
                break;
            }
        }
        if (last < n - 1) {
            mark(out.end, "gone", "Gone");
        }
        if (out.main > out.peak + 1.5 / framesPerSecond) {
            mark(out.main, "main", "Main part");
        }
    }
    std::stable_sort(out.markers.begin(), out.markers.end(),
                     [](const TimeMarker& a, const TimeMarker& b) { return a.time < b.time; });
    return out;
}

namespace {

// What a clip shows of the small pieces as a group: how fast they move out
// and how long they last.
void pieceMotion(const TimeAnalysis& time, PieceGroup& group) {
    if (!time.available || group.count <= 0 || time.pieces.empty()) {
        return;
    }
    const int n = time.frames;
    float most = 0.0f;
    for (float c : time.pieces) {
        most = std::max(most, c);
    }
    if (most < 3.0f) {
        return;
    }
    int from = -1, to = -1;
    for (int f = 0; f < n; ++f) {
        if (time.pieces[static_cast<std::size_t>(f)] >= 0.3f * most) {
            if (from < 0) {
                from = f;
            }
            to = f;
        }
    }
    if (from < 0 || to <= from) {
        return;
    }
    if (!time.continuous) {
        group.lifetime = static_cast<float>((to - from + 1) / time.framesPerSecond);
    }
    // Outward speed: how fast the pieces' average distance grows at first.
    float farthest = 0.0f;
    int farAt = from;
    for (int f = from; f <= to; ++f) {
        if (time.pieceReach[static_cast<std::size_t>(f)] > farthest) {
            farthest = time.pieceReach[static_cast<std::size_t>(f)];
            farAt = f;
        }
    }
    if (farAt > from && !time.continuous) {
        const float begin = time.pieceReach[static_cast<std::size_t>(from)];
        int mid = farAt;
        for (int f = from; f <= farAt; ++f) {
            if (time.pieceReach[static_cast<std::size_t>(f)] >= begin + 0.6f * (farthest - begin)) {
                mid = f;
                break;
            }
        }
        if (mid > from) {
            group.speed = (time.pieceReach[static_cast<std::size_t>(mid)] - begin) /
                          static_cast<float>((mid - from) / time.framesPerSecond);
        }
    }
}

}  // namespace

void writeFindings(ReferenceAnalysis& a, bool moving) {
    StillAnalysis& s = a.still;
    TimeAnalysis& t = a.time;
    if (moving && t.available) {
        pieceMotion(t, s.sparks);
        pieceMotion(t, s.streaks);
        pieceMotion(t, s.bits);
    }
    a.findings.clear();
    a.uncertain.clear();
    a.methods.clear();
    const auto add = [&](const char* key, const char* label, std::string value, float confidence, Basis basis,
                         std::string note = {}) {
        a.findings.push_back(Finding{key, label, std::move(value), clamp01(confidence), basis, std::move(note)});
    };

    // ---- background
    {
        std::string value;
        switch (s.backdrop) {
            case Backdrop::Transparent: value = "See-through (the picture has its own cut-out)"; break;
            case Backdrop::Dark: value = "Dark, " + hexColour(s.backdropColour); break;
            case Backdrop::Light: value = "Light, " + hexColour(s.backdropColour); break;
            case Backdrop::Colour: value = "The colour you picked, " + hexColour(s.backdropColour); break;
            case Backdrop::Auto: value = "Unknown"; break;
        }
        std::string note;
        if (s.backdrop != Backdrop::Transparent && s.backdropEvenness < 0.6f) {
            note = "The background is not one plain colour, so parts of it may be read as effect. "
                   "Crop closer to the effect, or pick the background colour yourself.";
            a.uncertain.push_back("Where the effect ends and a busy background begins.");
        }
        add("background", "Background", value, s.backdrop == Backdrop::Transparent ? 0.95f : 0.35f + 0.6f * s.backdropEvenness,
            Basis::Observed, note);
        a.methods.push_back(s.backdrop == Backdrop::Transparent
                                ? "Background: the picture's own see-through channel."
                                : "Background: one plain colour removed. A trained cut-out model could handle busy "
                                  "backgrounds; none is used.");
    }
    add("blend", "How it mixes with what is behind", s.additive ? "Adds light (additive)" : "Paints over (normal)",
        s.backdrop == Backdrop::Dark ? 0.7f : 0.5f, Basis::Inferred,
        s.additive ? "On a dark background, glowing light and pale paint look the same. Light is assumed."
                   : "Soft bright parts may really be added light; that cannot be told from this background.");

    // ---- overall
    add("place", "Where and how big",
        "Centre at " + percent(s.centreX) + " across, " + percent(s.centreY) + " down; reaches " +
            percent(2.0f * s.extent) + " of the height",
        0.9f, Basis::Observed);
    add("style", "Edges",
        s.hardness > 0.6f ? "Crisp, flat shapes (drawn or toon style)"
                          : (s.hardness < 0.3f ? "Soft and glowing" : "A mix of crisp shapes and soft glow"),
        0.8f, Basis::Observed);
    {
        std::string value;
        for (std::size_t i = 0; i < s.palette.size() && i < 4; ++i) {
            if (i) {
                value += ", ";
            }
            value += colourName(s.palette[i]) + " " + hexColour(s.palette[i]) + " (" + percent(s.palette[i].share) + ")";
        }
        add("colours", "Colours", value.empty() ? "None found" : value, 0.9f, Basis::Observed,
            s.additive ? "Where light is clipped to white, the true colour underneath is hidden." : "");
    }
    add("outline", "Overall shape",
        s.elongation < 1.3f ? "Round"
                            : "Long: " + number(s.elongation) + " times as long as wide, lying at " +
                                  number(s.axis, 0) + " degrees",
        0.85f, Basis::Observed);
    add("symmetry", "Left and right", s.mirror > 0.9f ? "Alike (symmetric)" : (s.mirror > 0.7f ? "Roughly alike" : "Different"),
        0.8f, Basis::Observed);

    if (s.separate >= 2) {
        add("several", "More than one effect",
            "The picture seems to hold " + std::to_string(s.separate) + " separate effects", 0.6f, Basis::Observed,
            "One reference should show one effect. Crop to just one of them for a better result.");
        a.uncertain.push_back("Which of the separate effects in the picture is meant.");
    }

    // ---- parts
    if (s.comet) {
        add("comet", "Head and tail",
            "A bright head with a tail " + percent(s.tailLength) + " of the height long, pointing " +
                compass(s.tailHeading),
            0.75f, Basis::Observed);
    }
    if (s.hasCore) {
        add("core", "White core", "A white light of its own at the centre, radius " + percent(0.5f * s.coreRadius), 0.75f,
            Basis::Observed);
    } else if (s.whiteHot > 0.01f) {
        add("core", "White-hot middle",
            "The glow is strong enough to burn white out to " + percent(s.whiteHot) + " of the height", 0.7f, Basis::Inferred,
            "Where the picture is clipped to white, how strong the light really is has to be worked out from the "
            "colour channel with the most room left.");
    }
    if (s.hasGlow) {
        std::string value = "Outer glow, " + colourName(s.glowOuterColour) + ", radius " + percent(s.glowOuter);
        if (s.glowInnerLevel > 0) {
            value += "; inner glow, " + colourName(s.glowInnerColour) + ", radius " + percent(s.glowInner);
        }
        add("glow", "Glow", value, 0.75f, Basis::Observed);
    }
    if (s.hasBody) {
        add("body", "Main shape",
            "Closest built-in shape: " + s.bodyShape + " (" + percent(s.bodyMatch) + " alike), " +
                colourName(s.bodyColour),
            s.bodyMatch, Basis::Observed,
            s.bodyMatch < 0.6f ? "No built-in shape is a close match. Paint this part and use it as the layer's "
                                 "picture for an exact outline."
                               : "");
        if (s.bodyMatch < 0.6f) {
            a.uncertain.push_back("The exact outline of the main shape: no built-in shape matches it closely.");
        }
    }
    if (s.rays >= 2 && !s.hasBody) {
        add("rays", "Rays",
            std::to_string(s.rays) + (s.raysEven ? " evenly spaced" : " uneven") + " rays reaching " +
                percent(s.rayReach) + (s.rayShape.empty() ? "" : "; closest flash shape: " + s.rayShape),
            0.4f + 0.5f * s.rayStrength, Basis::Observed);
    }
    for (std::size_t i = 0; i < s.rings.size(); ++i) {
        const RingFound& ring = s.rings[i];
        add("ring", "Ring",
            colourName(ring.colour) + ", radius " + percent(ring.radius) + ", " + percent(ring.around) + " of a full circle",
            0.3f + 0.6f * ring.around * clamp01(ring.strength * 3.0f), Basis::Observed);
    }
    const auto pieces = [&](const char* key, const char* label, const PieceGroup& g, const char* word) {
        if (g.count <= 0) {
            return;
        }
        std::string value = std::to_string(g.count) + " " + word + ", " + colourName(g.colour) + ", " +
                            (g.softness > 0.5f ? "soft" : "crisp");
        if (!g.shape.empty()) {
            value += ", like the " + g.shape + " shape";
        }
        if (g.spread < 170.0f) {
            value += ", mostly " + compass(g.heading);
        }
        float confidence = 0.45f + 0.35f * s.backdropEvenness;
        if (g.count < 4) {
            confidence -= 0.2f;
        }
        add(key, label, value, confidence, Basis::Observed,
            g.count < 4 ? "Very few were found; they may be specks in the picture rather than part of the effect." : "");
    };
    pieces("sparks", "Sparks", s.sparks, "small pieces");
    pieces("streaks", "Streaks", s.streaks, s.streaks.radial ? "streaks pointing outward" : "streaks leaning one way");
    pieces("bits", "Loose pieces", s.bits, "larger pieces");
    if (s.hasSmoke) {
        add("smoke", "Smoke", colourName(s.smokeColour) + ", soft, radius " + percent(s.smokeRadius), 0.45f,
            Basis::Inferred, "Grey and soft is read as smoke.");
    }
    add("depth", "What is in front of what", "Glow behind, shapes in the middle, sparks in front", 0.4f, Basis::Inferred,
        "A flat picture does not say which part is in front. The usual order is assumed, and real depth is not recovered.");
    a.uncertain.push_back("Depth and layer order: a flat picture cannot show them.");

    // ---- time
    if (moving && t.available) {
        a.methods.push_back("Movement: the effect is measured as a whole in every frame (size, brightness, colour, "
                            "position, turning, number of pieces). Single particles are not followed.");
        if (t.continuous) {
            add("timing", "Timing", "Keeps going for the whole clip (" + seconds(t.frames / t.framesPerSecond) + ")",
                0.85f, Basis::Observed);
        } else {
            add("timing", "Timing",
                "Appears at " + seconds(t.start) + ", fullest at " + seconds(t.peak) + ", gone by " + seconds(t.end),
                0.85f, Basis::Observed);
        }
        if (t.loops) {
            add("loop", "Loop", "Repeats every " + seconds(t.loopLength), t.loopConfidence, Basis::Observed);
        } else if (t.continuous) {
            add("loop", "Loop", "The end of the clip does not lead back into its start", 0.6f, Basis::Observed,
                "It will be made to loop anyway; the join may show.");
        }
        if (!t.bursts.empty()) {
            std::string value = std::to_string(t.bursts.size()) + (t.bursts.size() == 1 ? " burst, at " : " bursts, at ");
            for (std::size_t i = 0; i < t.bursts.size() && i < 4; ++i) {
                value += (i ? ", " : "") + seconds(t.bursts[i]);
            }
            add("bursts", "Bursts", value, 0.75f, Basis::Observed);
        }
        if (t.growth > 0) {
            add("spread", "Spreading",
                "Grows by " + number(t.growth) + " heights a second at first, " +
                    (t.slowing > 6.0f ? "then stops quickly" : (t.slowing > 2.0f ? "slowing steadily" : "slowing gently")),
                0.65f, Basis::Observed);
        }
        if (t.driftX != 0 || t.driftY != 0) {
            const float heading = std::atan2(t.driftY, t.driftX) * 180.0f / kPi;
            std::string value = "Travels " + compass(heading) + " at " +
                                number(std::hypot(t.driftX, t.driftY)) + " heights a second";
            if (t.pullX != 0 || t.pullY != 0) {
                value += ", curving " + compass(std::atan2(t.pullY, t.pullX) * 180.0f / kPi);
            }
            add("travel", "Travel", value, 0.7f, Basis::Observed);
        }
        if (t.turning != 0) {
            add("turning", "Turning",
                number(std::fabs(t.turning), 0) + " degrees a second, " +
                    (t.turning > 0 ? "counter-clockwise" : "clockwise"),
                t.turningConfidence, Basis::Observed,
                t.turningConfidence < 0.5f ? "The pattern changes too much from frame to frame to be sure." : "");
        }
        if (s.sparks.count > 0) {
            if (s.sparks.speed > 0) {
                add("sparkMotion", "How the sparks move",
                    "Outward at about " + number(s.sparks.speed) + " heights a second" +
                        (s.sparks.lifetime > 0 ? ", lasting about " + seconds(s.sparks.lifetime) : ""),
                    0.55f, Basis::Observed, "Measured from the group as a whole, not spark by spark.");
            } else {
                add("sparkMotion", "How the sparks move", "Could not be measured; a usual speed is assumed", 0.25f,
                    Basis::Inferred);
            }
            add("gravity", "Gravity on sparks", "Not measured; none is assumed", 0.2f, Basis::Inferred,
                "Following single sparks is needed to tell a fall from a spread.");
            a.uncertain.push_back("Whether sparks fall, rise or swirl: single sparks are not followed.");
        }
    } else {
        add("timing", "Timing", "Not in a still picture. A short burst that fades is assumed", 0.2f, Basis::Inferred,
            "Use a GIF or a video of the effect to have its timing measured.");
        if (s.sparks.count > 0 || s.streaks.count > 0) {
            add("sparkMotion", "How the pieces move", "Assumed to fly outward from the centre and slow down", 0.25f,
                Basis::Inferred);
        }
        a.uncertain.push_back("All movement and timing: a still picture shows one moment only.");
    }
    a.methods.push_back("Everything was worked out on this computer with plain image arithmetic. No AI model is "
                        "installed or needed; none was downloaded.");
}

}  // namespace vfx::editor::ref
