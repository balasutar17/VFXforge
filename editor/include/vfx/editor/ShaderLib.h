// VFX Forge editor: the shader library.
//
// A shader is a family (Hologram, Force Field ...), a preset of that family's settings, and an
// animation. Everything here is data: families list their parameters, presets are sets of
// values, and the maths lives in one shared source (editor/shaders/ShaderCore.hlsli) that the
// live preview runs as C++ and the Unity export carries over as HLSL.
//
// Parameters: every family has the common ones first (animation speed, pulse, flow ...), then
// its own. A value is four numbers: a number uses the first, a colour uses red green blue alpha.
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "vfx/Result.h"

namespace vfx::editor::shader {

inline constexpr int kMaxParams = 48;

enum class ParamKind { Number, Color, Choice };

struct ParamDef {
    std::string key;     // "scanDensity"
    std::string label;   // "Scanline density"
    std::string tip;
    ParamKind kind = ParamKind::Number;
    double min = 0, max = 1, def = 0;
    std::array<float, 4> colour{1, 1, 1, 1};
    std::vector<std::string> choices;
    bool advanced = false;
    bool common = false;  // one of the ones every family has
    std::string macro;    // "HG_scanDensity": the name the shader source reads it by
    int index = 0;        // position in the values
};

// How a family is drawn.
enum class BlendMode { Alpha, Additive };

struct Preset {
    std::string id, name, description;
    std::string shape;  // the preview shape that suits it
    // Only the values that differ from the family's defaults.
    std::vector<std::pair<std::string, std::array<float, 4>>> values;
    BlendMode blend = BlendMode::Additive;
};

struct Family {
    std::string id;       // "hologram"
    std::string code;     // "HG"
    int number = 0;       // 12: the number in the library
    std::string name;     // "Hologram / Energy"
    std::string summary;
    std::vector<std::string> tags;       // Casual, Stylized, Realistic, AAA, UI, VFX, Mobile
    std::vector<std::string> animations; // the modes it makes the most of
    std::vector<std::string> limits;     // what it does not do, stated plainly
    std::string function;                // "shade_HG"
    std::string previewShape;            // sphere, cube, plane, ring, sprite, particle
    bool cullOff = true;
    bool zwrite = false;
    bool isUi = false;
    bool isParticle = false;
    std::vector<ParamDef> params;        // common first, then its own
    std::vector<Preset> presets;
};

const std::vector<Family>& families();
const Family* findFamily(const std::string& id);
const Preset* findPreset(const Family& family, const std::string& id);
const ParamDef* findParam(const Family& family, const std::string& key);

// The common parameters, shared by every family.
inline constexpr int kCommonCount = 8;

// ---- animation: keyframes on number parameters
enum class Easing { Linear, Smooth, EaseIn, EaseOut };
enum class LoopMode { Loop, PingPong, Once };

struct Key {
    double time = 0;
    float value = 0;
    Easing easing = Easing::Smooth;
};
struct Track {
    std::string param;
    std::vector<Key> keys;  // in time order
};
struct Animation {
    double duration = 4.0;
    LoopMode loop = LoopMode::Loop;
    double speed = 1.0;  // playback speed
    std::vector<Track> tracks;
};

// Everything that makes one shader: which family and preset it began from, the values, the
// animation, and the quality profile (0 mobile, 1 standard, 2 high, 3 cinematic).
struct Settings {
    std::string family = "hologram";
    std::string preset;
    std::string name = "My shader";
    std::vector<std::array<float, 4>> values;  // by parameter index
    BlendMode blend = BlendMode::Additive;
    std::string shape = "sphere";
    int quality = 1;
    Animation animation;
};

// A family's own defaults, or one of its presets.
Settings defaultSettings(const Family& family);
Settings presetSettings(const Family& family, const Preset& preset);

// The playhead's time, folded into the animation's duration by its loop mode.
double animationTime(const Animation& a, double playhead);
// The values as the keyframes make them at a time on the timeline.
std::vector<std::array<float, 4>> valuesAt(const Settings& s, double time);

// Easing of the step between two keys, t from 0 to 1.
float ease(Easing e, float t);

// Saving. The file is JSON; reading checks every value against the family's limits and says what
// it changed.
std::string writeSettings(const Settings& s);
struct ReadSettings {
    Settings settings;
    std::vector<std::string> notes;  // what was repaired or ignored
};
Result<ReadSettings> readSettings(const std::string& text);

// Checks a shader for problems an artist should hear about before export: values out of range,
// features the chosen quality profile leaves out, a missing sprite.
std::vector<std::string> validateSettings(const Settings& s);

// The quality profiles: a name and what they change, in plain words.
struct Quality {
    std::string name, note;
    int octaves = 3;
};
const std::vector<Quality>& qualityProfiles();

}  // namespace vfx::editor::shader
