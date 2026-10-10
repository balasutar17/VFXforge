#include "vfx/editor/Audio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "vfx/DetMath.h"
#include "vfx/FileIO.h"
#include "vfx/Program.h"
#include "vfx/editor/Archive.h"
#include "vfx/editor/SoundLibrary.h"

namespace vfx::editor {

namespace {

constexpr double kMaxSeconds = 600.0;  // ten minutes is plenty for a sound effect

std::uint32_t le16(std::string_view s, std::size_t at) {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[at])) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[at + 1])) << 8);
}
std::uint32_t le32(std::string_view s, std::size_t at) {
    return le16(s, at) | (le16(s, at + 2) << 16);
}
std::uint32_t be16(std::string_view s, std::size_t at) {
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[at])) << 8) |
           static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[at + 1]));
}
std::uint32_t be32(std::string_view s, std::size_t at) { return (be16(s, at) << 16) | be16(s, at + 2); }

// One sample of `bits` bits at `p`, as -1 .. 1.
float readSample(const unsigned char* p, int bits, bool isFloat, bool bigEndian) {
    const int bytes = bits / 8;
    unsigned char b[8] = {};
    for (int i = 0; i < bytes; ++i) {
        b[i] = bigEndian ? p[bytes - 1 - i] : p[i];  // little-endian from here on
    }
    if (isFloat) {
        if (bits == 32) {
            float f;
            std::memcpy(&f, b, 4);
            return std::isfinite(f) ? std::clamp(f, -1.0f, 1.0f) : 0.0f;
        }
        double d;
        std::memcpy(&d, b, 8);
        return std::isfinite(d) ? static_cast<float>(std::clamp(d, -1.0, 1.0)) : 0.0f;
    }
    if (bits == 8) {
        // WAV's 8-bit samples are unsigned; AIFF's are signed.
        return bigEndian ? static_cast<float>(static_cast<std::int8_t>(b[0])) / 128.0f
                         : (static_cast<float>(b[0]) - 128.0f) / 128.0f;
    }
    std::int64_t v = 0;
    for (int i = bytes - 1; i >= 0; --i) {
        v = (v << 8) | b[i];
    }
    const std::int64_t sign = std::int64_t{1} << (bits - 1);
    if (v & sign) {
        v -= sign * 2;
    }
    return static_cast<float>(static_cast<double>(v) / static_cast<double>(sign));
}

Result<Sound> unpack(std::string_view data, int channels, int rate, int bits, bool isFloat, bool bigEndian) {
    if (channels < 1 || channels > 32) {
        return makeError("This sound has an unusual number of channels.");
    }
    if (rate < 2000 || rate > 384000) {
        return makeError("This sound has an unusual sample rate.");
    }
    const bool bitsOk = isFloat ? (bits == 32 || bits == 64) : (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    if (!bitsOk) {
        return makeError("This sound uses a sample format that can't be read.",
                         std::to_string(bits) + "-bit");
    }
    const std::size_t frameBytes = static_cast<std::size_t>(channels) * static_cast<std::size_t>(bits / 8);
    const std::size_t frames = data.size() / frameBytes;
    if (frames == 0) {
        return makeError("This sound is empty.");
    }
    if (static_cast<double>(frames) / rate > kMaxSeconds) {
        return makeError("This sound is too long: sounds can be at most ten minutes.");
    }
    Sound sound;
    sound.sampleRate = rate;
    sound.channels = channels >= 2 ? 2 : 1;
    sound.samples.resize(frames * static_cast<std::size_t>(sound.channels));
    const auto* p = reinterpret_cast<const unsigned char*>(data.data());
    for (std::size_t f = 0; f < frames; ++f) {
        const unsigned char* frame = p + f * frameBytes;
        for (int c = 0; c < sound.channels; ++c) {
            sound.samples[f * static_cast<std::size_t>(sound.channels) + static_cast<std::size_t>(c)] =
                readSample(frame + static_cast<std::size_t>(c) * static_cast<std::size_t>(bits / 8), bits,
                           isFloat, bigEndian);
        }
    }
    return sound;
}

// An 80-bit extended number, as AIFF stores its sample rate.
double extended(std::string_view s, std::size_t at) {
    const int exponent = static_cast<int>(be16(s, at) & 0x7fffu);
    const std::uint64_t mantissa = (static_cast<std::uint64_t>(be32(s, at + 2)) << 32) | be32(s, at + 6);
    if (exponent == 0 || mantissa == 0) {
        return 0.0;
    }
    return std::ldexp(static_cast<double>(mantissa), exponent - 16383 - 63);
}

// A small hash-based random number for sound randomness.
float unitFor(std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d) {
    std::uint64_t h = det::mix64(a ^ 0x534f554e44ull);
    h = det::mix64(h ^ b);
    h = det::mix64(h ^ c);
    h = det::mix64(h ^ d);
    return static_cast<float>(static_cast<double>(h >> 11) / static_cast<double>(1ull << 53));
}

const Module* soundModule(const Layer& layer) {
    for (const Module& m : layer.modules) {
        if (m.type == "sound" && m.known()) {
            return &m;
        }
    }
    return nullptr;
}

template <class T>
T get(const Module& m, const char* key, T fallback) {
    if (const Value* v = m.find(key)) {
        if (const T* typed = std::get_if<T>(v)) {
            return *typed;
        }
    }
    return fallback;
}

float softClip(float x) {
    const float a = std::fabs(x);
    if (a <= 0.8f) {
        return x;
    }
    const float y = 0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f);
    return x < 0.0f ? -y : y;
}

}  // namespace

// ------------------------------------------------------------------ decode

Result<Sound> decodeWav(std::string_view bytes) {
    if (bytes.size() < 12 || bytes.substr(0, 4) != "RIFF" || bytes.substr(8, 4) != "WAVE") {
        return makeError("This is not a WAV sound.");
    }
    int format = 0, channels = 0, rate = 0, bits = 0;
    bool haveFormat = false;
    std::string_view data;
    std::size_t at = 12;
    while (at + 8 <= bytes.size()) {
        const std::string_view id = bytes.substr(at, 4);
        std::size_t size = le32(bytes, at + 4);
        const std::size_t body = at + 8;
        if (size > bytes.size() - body) {
            size = bytes.size() - body;  // a cut-short file: use what is there
        }
        if (id == "fmt " && size >= 16) {
            format = static_cast<int>(le16(bytes, body));
            channels = static_cast<int>(le16(bytes, body + 2));
            rate = static_cast<int>(le32(bytes, body + 4));
            bits = static_cast<int>(le16(bytes, body + 14));
            if (format == 0xFFFE && size >= 26) {
                format = static_cast<int>(le16(bytes, body + 24));  // the sub-format
            }
            haveFormat = true;
        } else if (id == "data") {
            data = bytes.substr(body, size);
        }
        at = body + size + (size & 1u);
    }
    if (!haveFormat || data.empty()) {
        return makeError("This WAV sound is damaged.", "no format or no data");
    }
    if (format != 1 && format != 3) {
        return makeError("This WAV sound is compressed in a way that can't be read.",
                         "format " + std::to_string(format));
    }
    return unpack(data, channels, rate, bits, format == 3, false);
}

Result<Sound> decodeAiff(std::string_view bytes) {
    if (bytes.size() < 12 || bytes.substr(0, 4) != "FORM" ||
        (bytes.substr(8, 4) != "AIFF" && bytes.substr(8, 4) != "AIFC")) {
        return makeError("This is not an AIFF sound.");
    }
    const bool aifc = bytes.substr(8, 4) == "AIFC";
    int channels = 0, bits = 0;
    double rate = 0.0;
    bool isFloat = false, littleEndian = false, haveFormat = false;
    std::string_view data;
    std::size_t at = 12;
    while (at + 8 <= bytes.size()) {
        const std::string_view id = bytes.substr(at, 4);
        std::size_t size = be32(bytes, at + 4);
        const std::size_t body = at + 8;
        if (size > bytes.size() - body) {
            size = bytes.size() - body;
        }
        if (id == "COMM" && size >= 18) {
            channels = static_cast<int>(be16(bytes, body));
            bits = static_cast<int>(be16(bytes, body + 6));
            rate = extended(bytes, body + 8);
            if (aifc && size >= 22) {
                const std::string_view kind = bytes.substr(body + 18, 4);
                if (kind == "sowt") {
                    littleEndian = true;
                } else if (kind == "fl32" || kind == "FL32") {
                    isFloat = true;
                    bits = 32;
                } else if (kind == "fl64" || kind == "FL64") {
                    isFloat = true;
                    bits = 64;
                } else if (kind != "NONE") {
                    return makeError("This AIFF sound is compressed in a way that can't be read.");
                }
            }
            haveFormat = true;
        } else if (id == "SSND" && size >= 8) {
            const std::size_t offset = be32(bytes, body);
            if (offset <= size - 8) {
                data = bytes.substr(body + 8 + offset, size - 8 - offset);
            }
        }
        at = body + size + (size & 1u);
    }
    if (!haveFormat || data.empty()) {
        return makeError("This AIFF sound is damaged.", "no format or no data");
    }
    // Some writers round odd bit depths up; read them in whole bytes.
    if (!isFloat) {
        bits = (bits + 7) / 8 * 8;
    }
    return unpack(data, channels, static_cast<int>(std::lround(rate)), bits, isFloat, !littleEndian);
}

Result<Sound> decodeSound(std::string_view bytes) {
    if (bytes.size() >= 4 && bytes.substr(0, 4) == "FORM") {
        return decodeAiff(bytes);
    }
    return decodeWav(bytes);
}

std::string encodeWav(const Sound& sound) {
    const std::size_t channels = static_cast<std::size_t>(sound.channels > 0 ? sound.channels : 1);
    const std::size_t dataBytes = sound.samples.size() * 2;
    std::string out;
    out.reserve(44 + dataBytes);
    auto put16 = [&](std::uint32_t v) {
        out.push_back(static_cast<char>(v & 0xff));
        out.push_back(static_cast<char>((v >> 8) & 0xff));
    };
    auto put32 = [&](std::uint32_t v) {
        put16(v & 0xffff);
        put16(v >> 16);
    };
    out += "RIFF";
    put32(static_cast<std::uint32_t>(36 + dataBytes));
    out += "WAVEfmt ";
    put32(16);
    put16(1);
    put16(static_cast<std::uint32_t>(channels));
    put32(static_cast<std::uint32_t>(sound.sampleRate));
    put32(static_cast<std::uint32_t>(sound.sampleRate) * static_cast<std::uint32_t>(channels) * 2u);
    put16(static_cast<std::uint32_t>(channels * 2));
    put16(16);
    out += "data";
    put32(static_cast<std::uint32_t>(dataBytes));
    for (const float s : sound.samples) {
        const float c = std::isfinite(s) ? std::clamp(s, -1.0f, 1.0f) : 0.0f;
        const auto v = static_cast<std::int16_t>(std::lround(c * 32767.0f));
        put16(static_cast<std::uint16_t>(v));
    }
    return out;
}

std::string soundAssetPath(std::string_view originalName, std::string_view wavBytes) {
    std::string_view name = originalName;
    if (const auto slash = name.find_last_of("/\\"); slash != std::string_view::npos) {
        name.remove_prefix(slash + 1);
    }
    if (const auto dot = name.find_last_of('.'); dot != std::string_view::npos && dot > 0) {
        name = name.substr(0, dot);
    }
    std::string clean;
    for (const char ch : name) {
        const auto c = static_cast<unsigned char>(ch);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            clean.push_back(static_cast<char>(c));
        } else if (c >= 'A' && c <= 'Z') {
            clean.push_back(static_cast<char>(c - 'A' + 'a'));
        } else if (!clean.empty() && clean.back() != '-') {
            clean.push_back('-');
        }
        if (clean.size() >= 40) {
            break;
        }
    }
    while (!clean.empty() && clean.back() == '-') {
        clean.pop_back();
    }
    if (clean.empty() || clean.rfind("lib-", 0) == 0) {
        clean = "sound-" + clean;  // never mistaken for a library sound
        while (!clean.empty() && clean.back() == '-') {
            clean.pop_back();
        }
    }
    char hex[9];
    std::snprintf(hex, sizeof hex, "%08x", crc32(wavBytes) ^ static_cast<std::uint32_t>(wavBytes.size()));
    return std::string(kSoundsFolder) + "/" + clean + "-" + hex + ".wav";
}

std::vector<float> soundPeaks(const Sound& sound, int buckets) {
    std::vector<float> out;
    const std::size_t frames = sound.frames();
    if (buckets <= 0 || frames == 0) {
        return out;
    }
    out.reserve(static_cast<std::size_t>(buckets) * 2);
    const auto ch = static_cast<std::size_t>(sound.channels);
    for (int b = 0; b < buckets; ++b) {
        const std::size_t from = frames * static_cast<std::size_t>(b) / static_cast<std::size_t>(buckets);
        std::size_t to = frames * static_cast<std::size_t>(b + 1) / static_cast<std::size_t>(buckets);
        to = std::max(to, from + 1);
        float low = 0.0f, high = 0.0f;
        for (std::size_t f = from; f < to && f < frames; ++f) {
            for (std::size_t c = 0; c < ch; ++c) {
                const float s = sound.samples[f * ch + c];
                low = std::min(low, s);
                high = std::max(high, s);
            }
        }
        out.push_back(low);
        out.push_back(high);
    }
    return out;
}

// ---------------------------------------------------------------- SoundSet

const Sound* SoundSet::find(Id asset) const {
    const auto it = sounds_.find(asset.value);
    return it == sounds_.end() ? nullptr : it->second.sound.get();
}

std::shared_ptr<const Sound> SoundSet::shared(Id asset) const {
    const auto it = sounds_.find(asset.value);
    return it == sounds_.end() ? nullptr : it->second.sound;
}

void SoundSet::put(Id asset, std::shared_ptr<const Sound> sound, std::string path) {
    sounds_[asset.value] = Entry{std::move(sound), std::move(path)};
    ++revision_;
}

std::vector<std::string> SoundSet::sync(const Effect& effect, const std::filesystem::path& folder) {
    std::vector<std::string> problems;
    std::map<std::uint64_t, Entry> next;
    for (const Asset& asset : effect.assets) {
        if (asset.kind != "sound") {
            continue;
        }
        const auto old = sounds_.find(asset.id.value);
        if (old != sounds_.end() && old->second.path == asset.path && old->second.sound) {
            next[asset.id.value] = old->second;
            continue;
        }
        const auto failed = failed_.find(asset.id.value);
        if (failed != failed_.end() && failed->second.first == asset.path) {
            problems.push_back(failed->second.second);
            continue;
        }
        auto fail = [&](std::string problem) {
            failed_[asset.id.value] = {asset.path, problem};
            problems.push_back(std::move(problem));
        };
        std::shared_ptr<const Sound> sound;
        auto bytes = folder.empty() ? Result<std::string>(makeError("no folder"))
                                    : readFile(folder / pathFromUtf8(asset.path));
        if (bytes.ok()) {
            auto decoded = decodeSound(bytes.value());
            if (!decoded.ok()) {
                fail("The sound \"" + asset.path + "\" can't be read: " + decoded.error().message);
                continue;
            }
            sound = std::make_shared<const Sound>(std::move(decoded.value()));
        } else if (const std::string id = librarySoundId(asset.path); !id.empty()) {
            // A library sound that is not on disk yet: make it, and keep a copy.
            auto made = makeLibrarySound(id);
            if (!made.ok()) {
                fail(made.error().message);
                continue;
            }
            if (!folder.empty()) {
                std::error_code error;
                const auto target = folder / pathFromUtf8(asset.path);
                std::filesystem::create_directories(target.parent_path(), error);
                (void)writeFileAtomic(target, encodeWav(made.value()));
            }
            sound = std::make_shared<const Sound>(std::move(made.value()));
        } else {
            fail("The sound \"" + asset.path + "\" can't be found next to the effect.");
            continue;
        }
        failed_.erase(asset.id.value);
        next[asset.id.value] = Entry{std::move(sound), asset.path};
        ++revision_;
    }
    if (next.size() != sounds_.size()) {
        ++revision_;
    }
    sounds_ = std::move(next);
    return problems;
}

// ------------------------------------------------------------------ events

bool hasSound(const Effect& effect) {
    for (const Layer& layer : effect.layers) {
        const Module* m = layer.enabled ? soundModule(layer) : nullptr;
        if (m && get<AssetRef>(*m, "sound", {}).id.valid() && !get<bool>(*m, "mute", false)) {
            return true;
        }
    }
    return false;
}

std::vector<SoundEvent> soundEvents(const Effect& effect, std::int64_t pass) {
    std::vector<SoundEvent> events;
    for (const Layer& layer : effect.layers) {
        if (!layer.enabled) {
            continue;
        }
        const Module* m = soundModule(layer);
        if (!m) {
            continue;
        }
        const Id asset = get<AssetRef>(*m, "sound", {}).id;
        if (!asset.valid() || get<bool>(*m, "mute", false)) {
            continue;
        }
        // The same layer timing and bursts the simulation uses.
        const auto program = compileLayer(effect, layer);
        std::vector<double> times;
        if (get<std::string>(*m, "play", "start") == "bursts") {
            for (const BurstF& b : program->bursts) {
                if (b.count > 0 && b.time <= program->duration) {
                    times.push_back(program->start + b.time);
                }
            }
        } else if (program->duration > 0.0) {
            times.push_back(program->start);
        }
        auto num = [&](const char* key, double fallback) {
            const double v = get<double>(*m, key, fallback);
            return std::isfinite(v) ? v : fallback;
        };
        const double delay = std::clamp(num("delay", 0.0), -10.0, 10.0);
        const double volume = std::clamp(num("volume", 1.0), 0.0, 4.0);
        const double pitch = std::clamp(num("pitch", 0.0), -24.0, 24.0);
        const double randomPitch = std::clamp(num("randomPitch", 0.0), 0.0, 12.0);
        const double randomVolume = std::clamp(num("randomVolume", 0.0), 0.0, 1.0);
        const bool loop = get<bool>(*m, "loop", false);
        const double layerEnd = program->start + program->duration;
        for (std::size_t i = 0; i < times.size(); ++i) {
            SoundEvent e;
            e.layer = layer.id;
            e.asset = asset;
            e.time = times[i] + delay;
            e.stop = loop ? layerEnd + std::max(0.0, delay) : std::numeric_limits<double>::max();
            e.trimStart = std::clamp(num("trimStart", 0.0), 0.0, 3600.0);
            e.length = std::clamp(num("length", 0.0), 0.0, 3600.0);
            e.loop = loop;
            const auto seed = static_cast<std::uint64_t>(effect.seed);
            const float up = unitFor(seed, layer.id.value, static_cast<std::uint64_t>(pass), i * 2);
            const float down = unitFor(seed, layer.id.value, static_cast<std::uint64_t>(pass), i * 2 + 1);
            const double semitones = pitch + randomPitch * (2.0 * up - 1.0);
            e.rate = static_cast<float>(std::pow(2.0, semitones / 12.0));
            e.gain = static_cast<float>(volume * (1.0 - randomVolume * down));
            e.pan = static_cast<float>(std::clamp(num("pan", 0.0), -1.0, 1.0));
            e.fadeIn = static_cast<float>(std::clamp(num("fadeIn", 0.0), 0.0, 60.0));
            e.fadeOut = static_cast<float>(std::clamp(num("fadeOut", 0.0), 0.0, 60.0));
            events.push_back(e);
        }
    }
    std::stable_sort(events.begin(), events.end(),
                     [](const SoundEvent& a, const SoundEvent& b) { return a.time < b.time; });
    return events;
}

// ------------------------------------------------------------------- mixer

namespace {

// How long an event can sound, from its start, in seconds.
double eventSpan(const SoundEvent& e, const Sound& sound) {
    const double available = std::max(0.0, sound.seconds() - e.trimStart);
    const double used = e.length > 0.0 ? std::min(e.length, available) : available;
    const double natural = e.rate > 0.0f ? used / e.rate : 0.0;
    if (e.loop) {
        return std::max(0.0, e.stop - e.time);
    }
    return std::min(natural, std::max(0.0, e.stop - e.time));
}

void mixEvent(const SoundEvent& e, const Sound& sound, double start, double from, double step, int frames,
              int sampleRate, float* out) {
    const double span = eventSpan(e, sound);
    if (span <= 0.0) {
        return;
    }
    const double clipRate = sound.sampleRate;
    const double trim = e.trimStart * clipRate;
    const double available = static_cast<double>(sound.frames()) - trim;
    if (available <= 1.0) {
        return;
    }
    const double used = e.length > 0.0 ? std::min(e.length * clipRate, available) : available;
    const double pan = (static_cast<double>(e.pan) + 1.0) * 0.785398163;  // 0 .. pi/2
    const bool mono = sound.channels == 1;
    // Equal power for a mono sound, kept at full level in the middle; a
    // simple balance for a stereo one.
    const float panL = mono ? static_cast<float>(1.41421356 * std::cos(pan)) : std::min(1.0f, 1.0f - e.pan);
    const float panR = mono ? static_cast<float>(1.41421356 * std::sin(pan)) : std::min(1.0f, 1.0f + e.pan);
    const auto ch = static_cast<std::size_t>(sound.channels);
    (void)sampleRate;
    for (int i = 0; i < frames; ++i) {
        const double local = from + step * i - start;  // seconds since the event began
        if (local < 0.0 || local >= span) {
            continue;
        }
        double position = local * e.rate * clipRate;
        if (e.loop) {
            position = std::fmod(position, used);
        } else if (position >= used - 1.0) {
            continue;
        }
        const double at = trim + position;
        const auto i0 = static_cast<std::size_t>(at);
        const std::size_t i1 = std::min(i0 + 1, sound.frames() - 1);
        const auto t = static_cast<float>(at - static_cast<double>(i0));
        float gain = e.gain;
        if (e.fadeIn > 0.0f && local < e.fadeIn) {
            gain *= static_cast<float>(local / e.fadeIn);
        }
        if (e.fadeOut > 0.0f && span - local < e.fadeOut) {
            gain *= static_cast<float>((span - local) / e.fadeOut);
        }
        // A very short fade at both ends, so nothing ever clicks.
        const double edge = std::min(local, span - local);
        if (edge < 0.002) {
            gain *= static_cast<float>(edge / 0.002);
        }
        const float l0 = sound.samples[i0 * ch], l1 = sound.samples[i1 * ch];
        const float left = l0 + (l1 - l0) * t;
        float right = left;
        if (!mono) {
            const float r0 = sound.samples[i0 * ch + 1], r1 = sound.samples[i1 * ch + 1];
            right = r0 + (r1 - r0) * t;
        }
        out[2 * i] += left * gain * panL;
        out[2 * i + 1] += right * gain * panR;
    }
}

}  // namespace

void mixSound(const Effect& effect, const SoundSet& sounds, double from, double speed, int frames,
              int sampleRate, float* out) {
    if (frames <= 0) {
        return;
    }
    std::fill(out, out + 2 * frames, 0.0f);
    if (!(speed > 0.0) || sampleRate <= 0 || !hasSound(effect)) {
        return;
    }
    const double step = speed / sampleRate;
    const double to = from + step * frames;
    const double duration = effect.duration > 0.01 ? effect.duration : 0.01;
    const bool loop = effect.loop == "loop";

    // How far before and after its pass an event can sound.
    const std::vector<SoundEvent> first = soundEvents(effect, 0);
    double before = 0.0, after = 0.0;
    for (const SoundEvent& e : first) {
        before = std::max(before, -e.time);
        if (const Sound* s = sounds.find(e.asset)) {
            // Randomness can slow a sound down by up to an octave.
            const double longest = e.loop ? e.stop - e.time : s->seconds() * 2.0 / std::max(0.05f, e.rate);
            after = std::max(after, e.time + longest - duration);
        }
    }
    std::int64_t firstPass = 0, lastPass = 0;
    if (loop) {
        firstPass = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor((from - after) / duration)) - 1);
        lastPass = static_cast<std::int64_t>(std::floor((to + before) / duration)) + 1;
        if (lastPass - firstPass > 4000) {
            firstPass = lastPass - 4000;  // an absurdly short loop: keep the work bounded
        }
    }
    for (std::int64_t pass = firstPass; pass <= lastPass; ++pass) {
        const double base = static_cast<double>(pass) * duration;
        for (const SoundEvent& e : pass == 0 ? first : soundEvents(effect, pass)) {
            const Sound* s = sounds.find(e.asset);
            if (!s) {
                continue;
            }
            const double start = base + e.time;
            if (start > to || start + eventSpan(e, *s) < from) {
                continue;
            }
            mixEvent(e, *s, start, from, step, frames, sampleRate, out);
        }
    }
    for (int i = 0; i < 2 * frames; ++i) {
        out[i] = softClip(out[i]);
    }
}

std::string mixdownWav(const Effect& effect, const SoundSet& sounds, int sampleRate) {
    const double duration = effect.duration > 0.01 ? effect.duration : 0.01;
    double from = 0.0, seconds = duration;
    if (effect.loop == "loop") {
        // The second pass, as the frame export records it, so sounds that
        // carry over from the end of one pass into the next are heard.
        from = duration;
    } else {
        for (const SoundEvent& e : soundEvents(effect, 0)) {
            if (const Sound* s = sounds.find(e.asset)) {
                seconds = std::max(seconds, e.time + (e.loop ? e.stop - e.time : s->seconds() / e.rate));
            }
        }
        seconds = std::min(seconds, duration + 30.0);
    }
    const int frames = static_cast<int>(std::ceil(seconds * sampleRate));
    Sound mix;
    mix.sampleRate = sampleRate;
    mix.channels = 2;
    mix.samples.resize(static_cast<std::size_t>(frames) * 2);
    mixSound(effect, sounds, from, 1.0, frames, sampleRate, mix.samples.data());
    return encodeWav(mix);
}

}  // namespace vfx::editor
