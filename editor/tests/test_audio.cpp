// Sound: decoding, the library, when sounds play, and the mixer.
#include <catch2/catch_amalgamated.hpp>

#include <cmath>
#include <filesystem>
#include <random>
#include <string>

#include "vfx/FileIO.h"
#include "vfx/Templates.h"
#include "vfx/editor/Audio.h"
#include "vfx/editor/Presets.h"
#include "vfx/editor/Session.h"
#include "vfx/editor/SoundLibrary.h"

using namespace vfx;
using namespace vfx::editor;

namespace {

std::string wavOf(int bits, int format, int channels, int rate, const std::vector<double>& samples) {
    std::string data;
    for (const double s : samples) {
        if (format == 3) {
            const float f = static_cast<float>(s);
            data.append(reinterpret_cast<const char*>(&f), 4);
        } else if (bits == 8) {
            data.push_back(static_cast<char>(static_cast<int>(std::lround(s * 127.0)) + 128));
        } else {
            const auto v = static_cast<std::int64_t>(std::lround(s * (std::pow(2.0, bits - 1) - 1)));
            for (int b = 0; b < bits / 8; ++b) {
                data.push_back(static_cast<char>((v >> (8 * b)) & 0xff));
            }
        }
    }
    std::string out = "RIFF";
    auto put32 = [&](std::uint32_t v) {
        for (int b = 0; b < 4; ++b) out.push_back(static_cast<char>((v >> (8 * b)) & 0xff));
    };
    auto put16 = [&](std::uint32_t v) {
        for (int b = 0; b < 2; ++b) out.push_back(static_cast<char>((v >> (8 * b)) & 0xff));
    };
    put32(static_cast<std::uint32_t>(36 + data.size()));
    out += "WAVEfmt ";
    put32(16);
    put16(static_cast<std::uint32_t>(format));
    put16(static_cast<std::uint32_t>(channels));
    put32(static_cast<std::uint32_t>(rate));
    put32(static_cast<std::uint32_t>(rate * channels * bits / 8));
    put16(static_cast<std::uint32_t>(channels * bits / 8));
    put16(static_cast<std::uint32_t>(bits));
    out += "data";
    put32(static_cast<std::uint32_t>(data.size()));
    return out + data;
}

std::string aiff16(int channels, const std::vector<double>& samples) {
    auto be32 = [](std::uint32_t v) {
        std::string s(4, '\0');
        for (int b = 0; b < 4; ++b) s[static_cast<std::size_t>(b)] = static_cast<char>((v >> (24 - 8 * b)) & 0xff);
        return s;
    };
    auto be16 = [](std::uint32_t v) {
        return std::string{static_cast<char>((v >> 8) & 0xff), static_cast<char>(v & 0xff)};
    };
    std::string data;
    for (const double s : samples) {
        data += be16(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(s * 32767.0))));
    }
    // 44100 as an 80-bit extended number.
    const std::string rate = std::string("\x40\x0e\xac\x44\x00\x00\x00\x00\x00\x00", 10);
    std::string comm = be16(static_cast<std::uint32_t>(channels)) +
                       be32(static_cast<std::uint32_t>(samples.size() / static_cast<std::size_t>(channels))) +
                       be16(16) + rate;
    std::string ssnd = be32(0) + be32(0) + data;
    std::string body = "AIFF" + std::string("COMM") + be32(static_cast<std::uint32_t>(comm.size())) + comm +
                       "SSND" + be32(static_cast<std::uint32_t>(ssnd.size())) + ssnd;
    return "FORM" + be32(static_cast<std::uint32_t>(body.size())) + body;
}

Module& moduleOf(Layer& layer, const char* type) {
    for (Module& m : layer.modules) {
        if (m.type == type) {
            return m;
        }
    }
    layer.modules.push_back(makeModule(*Registry::builtin().findModule(type), Id{layer.id.value + 777}));
    return layer.modules.back();
}

// One layer, 2 s long, starting at 0.5 s, in a 3 s effect, with a sound.
Effect soundEffect(bool loopEffect = false) {
    IdGenerator ids(3);
    const IdSource newId = [&ids]() { return ids.next(); };
    Effect effect = makeEmptyEffect(newId, "Sound test", false);
    effect.duration = 3.0;
    effect.loop = loopEffect ? "loop" : "once";
    Layer layer = makeBasicEmitter(newId, "Layer");
    layer.start = 0.5;
    layer.duration = 2.0;
    Asset asset;
    asset.id = Id{4242};
    asset.kind = "sound";
    asset.path = "sounds/tone.wav";
    effect.assets.push_back(asset);
    *moduleOf(layer, "sound").find("sound") = AssetRef{asset.id};
    effect.layers.push_back(layer);
    return effect;
}

// A 0.2 s tone at 1 kHz, at 48 kHz.
std::shared_ptr<const Sound> tone(double seconds = 0.2) {
    auto s = std::make_shared<Sound>();
    s->sampleRate = 48000;
    s->channels = 1;
    for (int i = 0; i < static_cast<int>(seconds * 48000); ++i) {
        s->samples.push_back(static_cast<float>(0.5 * std::sin(6.283185307 * 1000.0 * i / 48000.0)));
    }
    return s;
}

double energy(const std::vector<float>& stereo, int fromFrame, int toFrame, int channel) {
    double e = 0.0;
    for (int i = fromFrame; i < toFrame; ++i) {
        const float v = stereo[static_cast<std::size_t>(2 * i + channel)];
        e += static_cast<double>(v) * v;
    }
    return e;
}

}  // namespace

TEST_CASE("WAV and AIFF sounds of every common kind decode", "[audio]") {
    const std::vector<double> stereo = {0.0, 0.5, -0.5, 0.25, 0.75, -0.75, 1.0, -1.0};
    for (const auto& [bits, format] : std::vector<std::pair<int, int>>{{8, 1}, {16, 1}, {24, 1}, {32, 1}, {32, 3}}) {
        INFO(bits << "-bit format " << format);
        auto sound = decodeWav(wavOf(bits, format, 2, 44100, stereo));
        REQUIRE(sound.ok());
        CHECK(sound.value().sampleRate == 44100);
        CHECK(sound.value().channels == 2);
        REQUIRE(sound.value().samples.size() == stereo.size());
        for (std::size_t i = 0; i < stereo.size(); ++i) {
            CHECK(sound.value().samples[i] == Catch::Approx(stereo[i]).margin(bits == 8 ? 0.02 : 1e-3));
        }
    }
    auto aiff = decodeSound(aiff16(1, {0.0, 0.5, -0.5}));
    REQUIRE(aiff.ok());
    CHECK(aiff.value().sampleRate == 44100);
    CHECK(aiff.value().samples[1] == Catch::Approx(0.5).margin(1e-3));

    // Our own writer reads back.
    Sound s;
    s.channels = 2;
    s.sampleRate = 22050;
    s.samples = {0.1f, -0.2f, 0.3f, -0.4f};
    auto back = decodeWav(encodeWav(s));
    REQUIRE(back.ok());
    CHECK(back.value().channels == 2);
    CHECK(back.value().samples[3] == Catch::Approx(-0.4).margin(1e-4));
}

TEST_CASE("Damaged sounds are refused, never read past the end", "[audio]") {
    CHECK_FALSE(decodeSound("").ok());
    CHECK_FALSE(decodeSound("RIFF....WAVE").ok());
    const std::string good = wavOf(16, 1, 1, 48000, std::vector<double>(500, 0.3));
    std::mt19937 rng(5);
    for (int round = 0; round < 3000; ++round) {
        std::string bad = good.substr(0, rng() % good.size());
        for (int k = 0; k < 3 && !bad.empty(); ++k) {
            bad[rng() % bad.size()] = static_cast<char>(rng() & 0xff);
        }
        auto sound = decodeSound(bad);
        if (sound.ok()) {
            CHECK(sound.value().samples.size() % static_cast<std::size_t>(sound.value().channels) == 0);
        }
    }
}

TEST_CASE("Every library sound is made, audible, and the same every time", "[audio][library]") {
    REQUIRE(librarySounds().size() >= 15);
    for (const LibrarySound& info : librarySounds()) {
        INFO(info.id);
        auto a = makeLibrarySound(info.id);
        auto b = makeLibrarySound(info.id);
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        CHECK(a.value().samples == b.value().samples);
        CHECK(a.value().seconds() > 0.1);
        float most = 0.0f;
        for (const float s : a.value().samples) {
            REQUIRE(std::isfinite(s));
            most = std::max(most, std::abs(s));
        }
        CHECK(most == Catch::Approx(0.9f).margin(0.01));
        CHECK(librarySoundId(librarySoundPath(info.id)) == info.id);
    }
    CHECK_FALSE(makeLibrarySound("no-such-sound").ok());
    CHECK(librarySoundId("sounds/boom-1234.wav").empty());
}

TEST_CASE("Sounds play when their layer starts, or at each burst", "[audio][events]") {
    Effect effect = soundEffect();
    Module& sound = moduleOf(effect.layers[0], "sound");
    *sound.find("delay") = 0.1;
    auto events = soundEvents(effect);
    REQUIRE(events.size() == 1);
    CHECK(events[0].time == Catch::Approx(0.6));
    CHECK(events[0].rate == Catch::Approx(1.0));
    CHECK(events[0].gain == Catch::Approx(1.0));

    // At every burst, a little earlier than each.
    *moduleOf(effect.layers[0], "emission").find("bursts") = BurstList{{{0.0, 10}, {0.7, 5}, {5.0, 5}}};
    *sound.find("play") = std::string("bursts");
    *sound.find("delay") = -0.05;
    events = soundEvents(effect);
    REQUIRE(events.size() == 2);  // the burst after the layer's end does not count
    CHECK(events[0].time == Catch::Approx(0.45));
    CHECK(events[1].time == Catch::Approx(1.15));

    // Randomness differs from play to play and pass to pass, but never from run to run.
    *sound.find("randomPitch") = 3.0;
    *sound.find("randomVolume") = 0.5;
    events = soundEvents(effect);
    CHECK(events[0].rate != events[1].rate);
    CHECK(events[0].rate == soundEvents(effect)[0].rate);
    CHECK(events[0].rate != soundEvents(effect, 1)[0].rate);
    for (const SoundEvent& e : events) {
        CHECK(e.rate >= std::pow(2.0, -3.0 / 12.0) - 1e-6);
        CHECK(e.rate <= std::pow(2.0, 3.0 / 12.0) + 1e-6);
        CHECK(e.gain >= 0.5f - 1e-6f);
        CHECK(e.gain <= 1.0f + 1e-6f);
    }

    // Muted, disabled or soundless layers play nothing.
    *sound.find("mute") = true;
    CHECK(soundEvents(effect).empty());
    CHECK_FALSE(hasSound(effect));
}

TEST_CASE("The mixer puts each sound exactly where it belongs", "[audio][mix]") {
    Effect effect = soundEffect();
    SoundSet sounds;
    sounds.put(Id{4242}, tone(), "sounds/tone.wav");
    const int rate = 48000;
    std::vector<float> out(static_cast<std::size_t>(rate) * 2 * 2);

    // Two seconds from the start: silence until 0.5 s, the tone for 0.2 s.
    mixSound(effect, sounds, 0.0, 1.0, 2 * rate, rate, out.data());
    CHECK(energy(out, 0, rate / 2 - 10, 0) == 0.0);
    CHECK(energy(out, rate / 2 + 100, rate / 2 + 9000, 0) > 100.0);
    CHECK(energy(out, rate * 7 / 10 + 100, 2 * rate, 0) == 0.0);
    // Left and right equal in the middle.
    CHECK(energy(out, 0, rate, 0) == Catch::Approx(energy(out, 0, rate, 1)).epsilon(1e-4));

    // Panned hard left, the right side is silent.
    *moduleOf(effect.layers[0], "sound").find("pan") = -1.0;
    mixSound(effect, sounds, 0.0, 1.0, rate, rate, out.data());
    CHECK(energy(out, 0, rate, 1) < 1e-6);
    CHECK(energy(out, 0, rate, 0) > 100.0);
    *moduleOf(effect.layers[0], "sound").find("pan") = 0.0;

    // Starting the mix part-way through hears the same samples.
    std::vector<float> part(static_cast<std::size_t>(rate) * 2);
    mixSound(effect, sounds, 0.55, 1.0, rate / 10, rate, part.data());
    mixSound(effect, sounds, 0.0, 1.0, rate, rate, out.data());
    const auto offset = static_cast<std::size_t>(0.55 * rate);
    for (int i = 0; i < 100; ++i) {
        CHECK(part[static_cast<std::size_t>(2 * i)] == Catch::Approx(out[2 * (offset + static_cast<std::size_t>(i))]).margin(1e-4));
    }

    // At double speed it comes twice as soon.
    mixSound(effect, sounds, 0.0, 2.0, rate, rate, out.data());
    CHECK(energy(out, 0, rate / 4 - 10, 0) == 0.0);
    CHECK(energy(out, rate / 4 + 50, rate / 4 + 2000, 0) > 10.0);

    // Paused, nothing plays.
    mixSound(effect, sounds, 0.6, 0.0, rate / 10, rate, out.data());
    CHECK(energy(out, 0, rate / 10, 0) == 0.0);
}

TEST_CASE("Loops, loop passes and fades", "[audio][mix]") {
    Effect effect = soundEffect(true);  // the effect repeats every 3 s
    Module& sound = moduleOf(effect.layers[0], "sound");
    *sound.find("loop") = true;  // the tone repeats until the layer ends at 2.5 s
    *sound.find("fadeOut") = 0.5;
    SoundSet sounds;
    sounds.put(Id{4242}, tone(), "sounds/tone.wav");
    const int rate = 8000;
    std::vector<float> out(static_cast<std::size_t>(rate) * 7 * 2);
    mixSound(effect, sounds, 0.0, 1.0, 7 * rate, rate, out.data());
    CHECK(energy(out, rate * 1, rate * 2, 0) > 100.0);          // still looping
    CHECK(energy(out, rate * 2 + rate / 4, rate * 5 / 2, 0) <
          energy(out, rate * 3 / 2, rate * 7 / 4, 0) * 0.6);    // fading out
    CHECK(energy(out, rate * 5 / 2 + 10, rate * 3 + rate / 2 - 10, 0) == 0.0);  // gap
    CHECK(energy(out, rate * 7 / 2 + 50, rate * 4, 0) > 100.0);  // the next pass
}

TEST_CASE("A layer can be given a library sound, saved and opened again", "[audio][session]") {
    const auto folder = std::filesystem::temp_directory_path() / "vfxforge_sound_session";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    {
        Session s;
        const Id layer = s.effect().layers[0].id;
        REQUIRE(s.useLibrarySound(layer, "pop").ok());
        bool hasModule = false;
        for (const Module& m : s.effect().layers[0].modules) {
            hasModule = hasModule || m.type == "sound";
        }
        CHECK(hasModule);
        REQUIRE(s.effect().assets.size() == 1);
        CHECK(s.effect().assets[0].path == "sounds/lib-pop.wav");
        s.tick(0.1);
        CHECK(s.sounds().size() == 1);
        CHECK(s.soundProblems().empty());

        // Undo takes the module away, redo brings it back.
        REQUIRE(s.undo().ok());
        CHECK(s.effect().assets.empty());
        REQUIRE(s.redo().ok());
        REQUIRE(s.saveAs(folder / "Pop.vfx").ok());
        CHECK(std::filesystem::exists(folder / "sounds" / "lib-pop.wav"));

        // A sound of the artist's own, replacing it.
        REQUIRE(s.useSound(layer, "My Boom.wav", encodeWav(*tone())).ok());
        CHECK(s.effect().assets.size() == 1);
        CHECK(s.effect().assets[0].path.rfind("sounds/my-boom-", 0) == 0);
        REQUIRE(s.save().ok());
    }
    Session again;
    REQUIRE(again.open(folder / "Pop.vfx").ok());
    again.tick(0.1);
    CHECK(again.sounds().size() == 1);
    CHECK(again.soundProblems().empty());
    REQUIRE(again.clearSound(again.effect().layers[0].id).ok());
    CHECK(again.effect().assets.empty());
    std::filesystem::remove_all(folder);
}

TEST_CASE("A mixdown is one pass of the effect's sound", "[audio][mix]") {
    Effect effect = soundEffect();
    SoundSet sounds;
    sounds.put(Id{4242}, tone(), "sounds/tone.wav");
    auto wav = decodeWav(mixdownWav(effect, sounds, 24000));
    REQUIRE(wav.ok());
    CHECK(wav.value().channels == 2);
    CHECK(wav.value().seconds() == Catch::Approx(3.0).margin(0.01));
    const auto peaks = soundPeaks(wav.value(), 30);
    REQUIRE(peaks.size() == 60);
    CHECK(peaks[2 * 2 + 1] == 0.0f);   // 0.2 s: quiet
    CHECK(peaks[2 * 5 + 1] > 0.3f);    // 0.5 s: the tone
}

TEST_CASE("Library presets come with library sounds that play", "[audio][presets]") {
    int withSound = 0;
    for (const PresetInfo& info : presets()) {
        IdGenerator ids(8);
        const IdSource newId = [&ids]() { return ids.next(); };
        Effect effect = makePreset(info.id, newId).value();
        for (const Asset& a : effect.assets) {
            if (a.kind == "sound") {
                INFO(info.id << " " << a.path);
                CHECK_FALSE(librarySoundId(a.path).empty());
            }
        }
        if (hasSound(effect)) {
            ++withSound;
            INFO(info.id);
            CHECK_FALSE(soundEvents(effect).empty());
        }
    }
    CHECK(withSound >= 25);

    // Adding a preset to an effect brings its sounds; adding it again
    // shares them.
    Session s;
    REQUIRE(s.addPreset("candy-pop").ok());
    const auto assets = s.effect().assets.size();
    CHECK(assets >= 1);
    REQUIRE(s.addPreset("candy-pop").ok());
    CHECK(s.effect().assets.size() == assets);
    s.tick(0.05);
    CHECK(s.soundProblems().empty());
    CHECK(s.sounds().size() == assets);
}
