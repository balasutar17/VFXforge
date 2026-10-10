#include "vfx/editor/SoundLibrary.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

namespace vfx::editor {

namespace {

constexpr double kTau = 6.283185307179586;

// A small fixed random sequence, so every sound is the same everywhere.
class Noise {
public:
    explicit Noise(std::uint64_t seed) : state_(seed * 0x9e3779b97f4a7c15ull + 1) {}
    double next() {  // -1 .. 1
        state_ ^= state_ << 13;
        state_ ^= state_ >> 7;
        state_ ^= state_ << 17;
        return static_cast<double>(state_ >> 11) / static_cast<double>(1ull << 52) - 1.0;
    }
    double unit() { return 0.5 * (next() + 1.0); }

private:
    std::uint64_t state_;
};

// A one-pole low-pass filter.
struct LowPass {
    double y = 0;
    double run(double x, double cutoff, double rate) {
        const double a = 1.0 - std::exp(-kTau * cutoff / rate);
        y += a * (x - y);
        return y;
    }
};

// A state-variable filter, band-pass output.
struct BandPass {
    double low = 0, band = 0;
    double run(double x, double centre, double q, double rate) {
        const double f = 2.0 * std::sin(3.141592653589793 * std::min(centre, rate * 0.24) / rate);
        const double high = x - low - band / q;
        band += f * high;
        low += f * band;
        return band;
    }
};

// A bell: a few decaying partials.
double bell(double t, double f, double decay) {
    if (t < 0.0) {
        return 0.0;
    }
    return std::sin(kTau * f * t) * std::exp(-t * decay) +
           0.4 * std::sin(kTau * f * 2.76 * t) * std::exp(-t * decay * 1.7) +
           0.2 * std::sin(kTau * f * 5.4 * t) * std::exp(-t * decay * 2.6);
}

using Maker = std::function<double(double t, int i, double rate, Noise& noise)>;

struct Recipe {
    LibrarySound info;
    double seconds;
    std::uint64_t seed;
    Maker make;
};

// Each recipe keeps its own running state in the lambda's captures.
std::vector<Recipe> recipes() {
    std::vector<Recipe> list;

    list.push_back({{"pop", "Candy Pop", "Casual and rewards", "A bright, juicy pop.", false}, 0.16, 1,
                    [phase = 0.0](double t, int, double rate, Noise& n) mutable {
                        const double f = 340.0 + 980.0 * std::exp(-t * 38.0);
                        phase += kTau * f / rate;
                        return std::sin(phase) * std::exp(-t * 30.0) + 0.5 * n.next() * std::exp(-t * 900.0);
                    }});
    list.push_back({{"bubble", "Bubble", "Casual and rewards", "A rising bubble blip.", false}, 0.14, 2,
                    [phase = 0.0](double t, int, double rate, Noise&) mutable {
                        const double f = 280.0 + 1500.0 * std::pow(std::min(t / 0.09, 1.0), 0.7);
                        phase += kTau * f / rate;
                        return std::sin(phase) * std::exp(-t * 26.0) * std::min(1.0, t * 400.0);
                    }});
    list.push_back({{"coin", "Coin", "Casual and rewards", "Two quick bright notes.", false}, 0.6, 3,
                    [](double t, int, double, Noise&) {
                        const bool second = t >= 0.075;
                        const double f = second ? 1318.5 : 987.8;
                        const double local = second ? t - 0.075 : t;
                        const double x = kTau * f * local;
                        const double tone = std::sin(x) + 0.3 * std::sin(3.0 * x) + 0.12 * std::sin(5.0 * x);
                        return tone * std::exp(-local * (second ? 7.0 : 2.0)) * 0.6;
                    }});
    list.push_back({{"collect", "Collect Chime", "Casual and rewards", "A soft two-bell chime.", false}, 0.9, 4,
                    [](double t, int, double, Noise&) {
                        return 0.6 * bell(t, 1046.5, 6.0) + 0.5 * bell(t - 0.06, 1568.0, 6.5);
                    }});
    list.push_back({{"sparkle", "Sparkle", "Casual and rewards", "A shower of tiny bells.", false}, 1.3, 5,
                    [hits = std::vector<std::pair<double, double>>{}](double t, int i, double, Noise& n) mutable {
                        if (i == 0) {
                            for (int k = 0; k < 9; ++k) {
                                hits.emplace_back(n.unit() * 0.7, 2400.0 + n.unit() * 3200.0);
                            }
                        }
                        double s = 0.0;
                        for (const auto& [at, f] : hits) {
                            s += 0.28 * bell(t - at, f, 9.0);
                        }
                        return s;
                    }});
    list.push_back({{"level-up", "Level Up", "Casual and rewards", "A rising four-note fanfare.", false}, 1.2, 6,
                    [](double t, int, double, Noise&) {
                        const double notes[4] = {523.25, 659.25, 783.99, 1046.5};
                        double s = 0.0;
                        for (int k = 0; k < 4; ++k) {
                            const double at = 0.09 * k;
                            const double local = t - at;
                            if (local < 0.0) {
                                continue;
                            }
                            const double decay = k == 3 ? 2.2 : 9.0;
                            const double ph = std::fmod(notes[k] * local, 1.0);
                            const double triangle = 4.0 * std::abs(ph - 0.5) - 1.0;
                            s += 0.45 * triangle * std::exp(-local * decay) * std::min(1.0, local * 300.0);
                        }
                        return s;
                    }});
    list.push_back({{"splat", "Jelly Splat", "Casual and rewards", "A wet, squishy splat.", false}, 0.35, 7,
                    [phase = 0.0, lp = LowPass{}, lp2 = LowPass{}](double t, int, double rate, Noise& n) mutable {
                        const double f = 70.0 + 180.0 * std::exp(-t * 18.0);
                        phase += kTau * f / rate;
                        const double body = std::sin(phase) * std::exp(-t * 14.0);
                        const double wet = lp2.run(lp.run(n.next(), 1100.0, rate), 1100.0, rate) *
                                           std::exp(-t * 22.0) * 5.0;
                        return 0.7 * body + wet;
                    }});

    list.push_back({{"shimmer", "Magic Shimmer", "Magic", "A bright chord that swells and fades.", false}, 1.6, 8,
                    [](double t, int, double, Noise&) {
                        const double chord[4] = {523.25, 659.25, 783.99, 1046.5};
                        const double env = std::min(1.0, t / 0.3) * std::exp(-std::max(0.0, t - 0.3) * 2.4);
                        const double trem = 0.75 + 0.25 * std::sin(kTau * 9.0 * t);
                        double s = 0.0;
                        for (const double f : chord) {
                            s += std::sin(kTau * f * t) + 0.2 * std::sin(kTau * f * 2.0 * t);
                        }
                        return 0.22 * s * env * trem;
                    }});

    list.push_back({{"whoosh", "Whoosh", "Combat and impacts", "Air rushing past.", false}, 0.55, 9,
                    [bp = BandPass{}](double t, int, double rate, Noise& n) mutable {
                        const double x = std::min(t / 0.55, 1.0);
                        const double centre = 300.0 + 2600.0 * std::sin(3.14159 * x);
                        const double env = std::pow(std::sin(3.14159 * x), 1.5);
                        return bp.run(n.next(), centre, 2.5, rate) * env * 2.2;
                    }});
    list.push_back({{"boom", "Boom", "Combat and impacts", "A deep, round boom.", false}, 1.6, 10,
                    [phase = 0.0, lp = LowPass{}, lp2 = LowPass{}, lp3 = LowPass{}](double t, int, double rate,
                                                                                   Noise& n) mutable {
                        const double f = 38.0 + 60.0 * std::exp(-t * 6.0);
                        phase += kTau * f / rate;
                        const double body = std::sin(phase) * std::exp(-t * 2.6);
                        const double rumble =
                            lp2.run(lp.run(n.next(), 180.0, rate), 180.0, rate) * std::exp(-t * 3.0) * 9.0;
                        const double click = lp3.run(n.next(), 1500.0, rate) * std::exp(-t * 120.0);
                        return std::tanh(1.2 * (body + rumble + 0.8 * click));
                    }});
    list.push_back({{"explosion", "Explosion", "Combat and impacts", "A boom with crackling debris.", false}, 2.2, 11,
                    [phase = 0.0, lp = LowPass{}, lp2 = LowPass{}, lp3 = LowPass{}, crackle = 0.0](
                        double t, int, double rate, Noise& n) mutable {
                        const double f = 34.0 + 70.0 * std::exp(-t * 5.0);
                        phase += kTau * f / rate;
                        const double body = std::sin(phase) * std::exp(-t * 2.0);
                        const double cutoff = 300.0 + 1200.0 * std::exp(-t * 4.0);
                        const double roar = lp2.run(lp.run(n.next(), cutoff, rate), cutoff, rate) *
                                            std::exp(-t * 2.2) * 6.0;
                        if (n.unit() < 0.0016 * std::exp(-t * 1.5)) {
                            crackle = 0.8 * n.next();
                        }
                        crackle *= 0.985;
                        return std::tanh(1.2 * (body + roar + lp3.run(crackle * n.next(), 3000.0, rate)));
                    }});
    list.push_back({{"hit", "Hit", "Combat and impacts", "A short punchy impact.", false}, 0.35, 12,
                    [phase = 0.0, lp = LowPass{}](double t, int, double rate, Noise& n) mutable {
                        const double f = 90.0 + 160.0 * std::exp(-t * 30.0);
                        phase += kTau * f / rate;
                        const double thump = std::sin(phase) * std::exp(-t * 16.0);
                        const double snap = lp.run(n.next(), 2500.0, rate) * std::exp(-t * 80.0) * 1.0;
                        return std::tanh(1.4 * (thump + snap));
                    }});

    list.push_back({{"zap", "Zap", "Energy and sci-fi", "An electric zap.", false}, 0.25, 13,
                    [phase = 0.0](double t, int, double rate, Noise& n) mutable {
                        const double f = 150.0 + 1700.0 * std::exp(-t * 18.0);
                        phase += f / rate;
                        const double saw = 2.0 * (phase - std::floor(phase)) - 1.0;
                        const double ring = std::sin(kTau * 63.0 * t);
                        return (0.6 * saw * ring + 0.25 * n.next()) * std::exp(-t * 12.0);
                    }});
    list.push_back({{"laser", "Laser", "Energy and sci-fi", "A falling laser shot.", false}, 0.32, 14,
                    [phase = 0.0](double t, int, double rate, Noise&) mutable {
                        const double f = (250.0 + 1300.0 * std::exp(-t * 10.0)) * (1.0 + 0.04 * std::sin(kTau * 35.0 * t));
                        phase += kTau * f / rate;
                        return std::sin(phase) * std::exp(-t * 7.0) * std::min(1.0, t * 500.0);
                    }});
    list.push_back({{"electric-loop", "Electric Hum", "Energy and sci-fi", "A crackling electric buzz that loops.", true}, 2.0, 15,
                    [lp = LowPass{}, lp2 = LowPass{}, lp3 = LowPass{}, crackle = 0.0](double t, int, double rate,
                                                                                      Noise& n) mutable {
                        const double ph = std::fmod(120.0 * t, 1.0);
                        const double buzz = lp2.run(lp.run(2.0 * ph - 1.0, 1200.0, rate), 1200.0, rate) * 0.8;
                        if (n.unit() < 0.0009) {
                            crackle = n.next();
                        }
                        crackle *= 0.97;
                        return buzz + 0.5 * lp3.run(crackle * n.next(), 5000.0, rate);
                    }});

    list.push_back({{"fire-loop", "Fire Crackle", "Environment", "A burning fire that loops.", true}, 3.0, 16,
                    [lp = LowPass{}, lp2 = LowPass{}, crackle = 0.0](double, int, double rate, Noise& n) mutable {
                        const double roar = lp.run(lp2.run(n.next(), 900.0, rate), 400.0, rate) * 2.4;
                        if (n.unit() < 0.0007) {
                            crackle = 0.9 * n.next();
                        }
                        crackle *= 0.93;
                        return roar + crackle * n.next();
                    }});
    list.push_back({{"rain-loop", "Rain", "Environment", "Steady rain that loops.", true}, 3.0, 17,
                    [hp = LowPass{}, drop = 0.0](double, int, double rate, Noise& n) mutable {
                        const double x = n.next();
                        const double hiss = (x - hp.run(x, 1200.0, rate)) * 0.35;
                        if (n.unit() < 0.004) {
                            drop = 0.5 * n.next();
                        }
                        drop *= 0.9;
                        return hiss + drop;
                    }});
    list.push_back({{"thunder", "Thunder", "Environment", "A crack of thunder rolling away.", false}, 3.0, 18,
                    [lp = LowPass{}, lp2 = LowPass{}, lp4 = LowPass{}](double t, int, double rate, Noise& n) mutable {
                        const double crack = lp2.run(n.next(), 3000.0, rate) * std::exp(-t * 14.0) * 1.5;
                        const double cutoff = 150.0 + 100.0 * std::sin(t * 7.0);
                        const double roll = lp4.run(lp.run(n.next(), cutoff, rate), cutoff, rate) * 14.0 *
                                            std::exp(-t * 1.1) * (0.6 + 0.4 * std::sin(t * 5.3));
                        return std::tanh(crack + roll);
                    }});
    return list;
}

const std::vector<Recipe>& allRecipes() {
    static const std::vector<Recipe> list = recipes();
    return list;
}

}  // namespace

const std::vector<LibrarySound>& librarySounds() {
    static const std::vector<LibrarySound> list = [] {
        std::vector<LibrarySound> out;
        for (const Recipe& r : allRecipes()) {
            out.push_back(r.info);
        }
        return out;
    }();
    return list;
}

const LibrarySound* findLibrarySound(std::string_view id) {
    for (const LibrarySound& s : librarySounds()) {
        if (s.id == id) {
            return &s;
        }
    }
    return nullptr;
}

Result<Sound> makeLibrarySound(std::string_view id, int sampleRate) {
    for (const Recipe& recipe : allRecipes()) {
        if (recipe.info.id != id) {
            continue;
        }
        Recipe r = recipe;  // a fresh copy of the filters' state
        Noise noise(r.seed);
        Sound sound;
        sound.sampleRate = sampleRate;
        sound.channels = 1;
        const int frames = static_cast<int>(r.seconds * sampleRate);
        sound.samples.resize(static_cast<std::size_t>(frames));
        double most = 0.0;
        std::vector<double> raw(static_cast<std::size_t>(frames));
        for (int i = 0; i < frames; ++i) {
            const double t = static_cast<double>(i) / sampleRate;
            raw[static_cast<std::size_t>(i)] = r.make(t, i, sampleRate, noise);
            most = std::max(most, std::abs(raw[static_cast<std::size_t>(i)]));
        }
        // A loop crossfades its end into its start, so it repeats seamlessly;
        // a one-shot fades its last 10 ms so it never ends with a click.
        const int edge = r.info.loops ? sampleRate / 5 : sampleRate / 100;
        if (r.info.loops && frames > 2 * edge) {
            for (int i = 0; i < edge; ++i) {
                const double w = static_cast<double>(i) / edge;
                auto& head = raw[static_cast<std::size_t>(i)];
                const double tail = raw[static_cast<std::size_t>(frames - edge + i)];
                head = head * w + tail * (1.0 - w);
            }
            raw.resize(static_cast<std::size_t>(frames - edge));
            sound.samples.resize(raw.size());
        } else {
            for (int i = 0; i < edge && i < frames; ++i) {
                raw[static_cast<std::size_t>(frames - 1 - i)] *= static_cast<double>(i) / edge;
            }
        }
        const double gain = most > 1e-9 ? 0.9 / most : 1.0;
        for (std::size_t i = 0; i < raw.size(); ++i) {
            sound.samples[i] = static_cast<float>(raw[i] * gain);
        }
        return sound;
    }
    return makeError("There is no library sound called \"" + std::string(id) + "\".");
}

std::string librarySoundPath(std::string_view id) {
    return std::string(kSoundsFolder) + "/lib-" + std::string(id) + ".wav";
}

std::string librarySoundId(std::string_view path) {
    const std::string prefix = std::string(kSoundsFolder) + "/lib-";
    if (path.size() <= prefix.size() + 4 || path.substr(0, prefix.size()) != prefix ||
        path.substr(path.size() - 4) != ".wav") {
        return {};
    }
    const std::string id(path.substr(prefix.size(), path.size() - prefix.size() - 4));
    return findLibrarySound(id) ? id : std::string();
}

}  // namespace vfx::editor
