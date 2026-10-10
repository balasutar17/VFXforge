// VFX Forge editor: sound.
//
// A layer can carry a Sound module: a sound played when the layer starts, or
// at every burst of its particles, with a delay, volume, pitch, pan, fades,
// looping, trimming and a little randomness. When each sound plays is worked
// out from the compiled effect itself (the same layer start, duration and
// burst times the simulation uses), so sound and picture cannot drift apart.
//
// Sounds are kept as WAV files in the effect's project folder ("sounds/"),
// like pictures. The library's own sounds are made by code (see
// SoundLibrary.h) and written there when an effect uses them.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "vfx/Effect.h"
#include "vfx/Id.h"
#include "vfx/Result.h"

namespace vfx::editor {

// Decoded sound: samples from -1 to 1, interleaved when there are two channels.
struct Sound {
    int sampleRate = 48000;
    int channels = 1;  // 1 or 2
    std::vector<float> samples;

    std::size_t frames() const { return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0; }
    double seconds() const { return sampleRate > 0 ? static_cast<double>(frames()) / sampleRate : 0.0; }
};

// WAV (8, 16, 24 and 32-bit whole numbers, 32 and 64-bit floating point,
// plain or "extensible") and AIFF/AIFC (uncompressed). More than two
// channels are mixed down to two. Other formats are converted by the app.
Result<Sound> decodeWav(std::string_view bytes);
Result<Sound> decodeAiff(std::string_view bytes);
Result<Sound> decodeSound(std::string_view bytes);  // either, by its first bytes

// A 16-bit WAV file.
std::string encodeWav(const Sound& sound);

// Where the sounds of a project go, relative to its folder.
inline constexpr const char* kSoundsFolder = "sounds";

// "Big Boom.wav" becomes "sounds/big-boom-1a2b3c4d.wav".
std::string soundAssetPath(std::string_view originalName, std::string_view wavBytes);

// The loudest and quietest sample in each of `buckets` equal slices: what a
// waveform picture draws. Pairs of (low, high), -1 to 1.
std::vector<float> soundPeaks(const Sound& sound, int buckets);

// The decoded sounds of one effect, by asset ID. Library sounds that are
// missing from the project folder are made again and written there.
class SoundSet {
public:
    const Sound* find(Id asset) const;
    std::shared_ptr<const Sound> shared(Id asset) const;
    bool has(Id asset) const { return find(asset) != nullptr; }
    void put(Id asset, std::shared_ptr<const Sound> sound, std::string path);
    void clear() { sounds_.clear(); }
    std::size_t size() const { return sounds_.size(); }

    std::vector<std::string> sync(const Effect& effect, const std::filesystem::path& folder);
    void retryFailed() { failed_.clear(); }
    std::uint64_t revision() const { return revision_; }

private:
    struct Entry {
        std::shared_ptr<const Sound> sound;
        std::string path;
    };
    std::map<std::uint64_t, Entry> sounds_;
    std::map<std::uint64_t, std::pair<std::string, std::string>> failed_;
    std::uint64_t revision_ = 0;
};

// One playing of a sound, in the time of one pass of the effect.
struct SoundEvent {
    Id layer;
    Id asset;
    double time = 0;     // when it starts, seconds from the pass's start (may be negative)
    double stop = 0;     // when it is cut off (the layer's end for a loop), or a large number
    double trimStart = 0, length = 0;  // the part of the sound used; length 0 means to its end
    bool loop = false;
    float gain = 1;      // volume, randomness included
    float rate = 1;      // playback speed from pitch, randomness included
    float pan = 0;
    float fadeIn = 0, fadeOut = 0;
};

// Every sound event of one pass of the effect, in time order. Muted sounds,
// disabled layers and layers without a sound are left out. Randomness is a
// hash of the effect's seed, the layer, the pass and the event, so it is the
// same every time the effect plays.
std::vector<SoundEvent> soundEvents(const Effect& effect, std::int64_t pass = 0);

// Mixes the effect's sound into stereo samples (left, right interleaved) for
// `frames` samples starting at playback time `from` seconds (counting every
// pass of a looping effect: 3.5 s into a 2 s effect is 1.5 s into its second
// pass). `speed` is the playback speed: at 2, time runs twice as fast and
// sounds play an octave higher, like a tape. Adds nothing for sounds not in
// `sounds`. Writes, it does not add to, `out`.
void mixSound(const Effect& effect, const SoundSet& sounds, double from, double speed, int frames,
              int sampleRate, float* out);

// One pass of the effect's sound, mixed down, as a WAV file's bytes.
std::string mixdownWav(const Effect& effect, const SoundSet& sounds, int sampleRate = 48000);

// True when any enabled layer has a sound that is not muted.
bool hasSound(const Effect& effect);

}  // namespace vfx::editor
