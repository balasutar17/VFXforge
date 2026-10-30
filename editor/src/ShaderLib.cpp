#include "vfx/editor/ShaderLib.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include <nlohmann/json.hpp>

namespace vfx::editor::shader {

namespace {

using J = nlohmann::json;

// ------------------------------------------------------------ the parameter tables
ParamDef number(const std::string& code, const char* key, const char* label, const char* tip, double mn, double mx,
                double df, bool adv, bool common) {
    ParamDef p;
    p.key = key;
    p.label = label;
    p.tip = tip;
    p.kind = ParamKind::Number;
    p.min = mn;
    p.max = mx;
    p.def = df;
    p.advanced = adv;
    p.common = common;
    p.macro = code + "_" + key;
    return p;
}
ParamDef colour(const std::string& code, const char* key, const char* label, const char* tip, float r, float g, float b, float a) {
    ParamDef p;
    p.key = key;
    p.label = label;
    p.tip = tip;
    p.kind = ParamKind::Color;
    p.min = 0;
    p.max = 16;  // HDR: brighter than 1 blooms
    p.colour = {r, g, b, a};
    p.macro = code + "_" + key;
    return p;
}
ParamDef choice(const std::string& code, const char* key, const char* label, const char* tip, int df, const char* list) {
    ParamDef p;
    p.key = key;
    p.label = label;
    p.tip = tip;
    p.kind = ParamKind::Choice;
    p.def = df;
    std::stringstream in(list);
    std::string item;
    while (std::getline(in, item, '|')) p.choices.push_back(item);
    p.min = 0;
    p.max = static_cast<double>(p.choices.size()) - 1.0;
    p.macro = code + "_" + key;
    return p;
}

struct Tables {
    std::vector<ParamDef> common;
    std::vector<std::pair<std::string, std::vector<ParamDef>>> own;  // by family code
};

Tables buildTables() {
    Tables t;
    std::vector<ParamDef>* cur = nullptr;
#define VFX_COMMON(key, label, tip, mn, mx, df, adv) t.common.push_back(number("CM", #key, label, tip, mn, mx, df, adv != 0, true));
#define VFX_FAMILY(CODE) \
    t.own.push_back({#CODE, {}}); \
    cur = &t.own.back().second;
#define VFX_F(CODE, key, label, tip, mn, mx, df, adv) cur->push_back(number(#CODE, #key, label, tip, mn, mx, df, adv != 0, false));
#define VFX_C(CODE, key, label, tip, r, g, b, a) cur->push_back(colour(#CODE, #key, label, tip, r, g, b, a));
#define VFX_CH(CODE, key, label, tip, df, list) cur->push_back(choice(#CODE, #key, label, tip, df, list));
#include "../shaders/ShaderParams.def"
#undef VFX_COMMON
#undef VFX_FAMILY
#undef VFX_F
#undef VFX_C
#undef VFX_CH
    return t;
}

// ------------------------------------------------------------ presets
using Values = std::vector<std::pair<std::string, std::array<float, 4>>>;

struct PresetBuilder {
    Values v;
    PresetBuilder& n(const char* key, float a) {
        v.push_back({key, {a, 0, 0, 0}});
        return *this;
    }
    PresetBuilder& c(const char* key, float r, float g, float b) {
        v.push_back({key, {r, g, b, 1}});
        return *this;
    }
    PresetBuilder& ca(const std::string& key, float r, float g, float b, float a) {
        v.push_back({key, {r, g, b, a}});
        return *this;
    }
    PresetBuilder& nn(const std::string& key, float a) {
        v.push_back({key, {a, 0, 0, 0}});
        return *this;
    }
};

// Layer shaders: one layer is set with layer(), then tuned with the small setters.
struct LayerBuilder : PresetBuilder {
    LayerBuilder& layer(int i, int element, float ar, float ag, float ab, float br, float bg, float bb, int blend, float intensity,
                        float size, float thick, float soft, float count, float speed) {
        const std::string k = "L" + std::to_string(i) + "_";
        nn(k + "el", static_cast<float>(element));
        ca(k + "colA", ar, ag, ab, 1);
        ca(k + "colB", br, bg, bb, 1);
        nn(k + "blend", static_cast<float>(blend));
        nn(k + "intensity", intensity);
        nn(k + "size", size);
        nn(k + "thick", thick);
        nn(k + "soft", soft);
        nn(k + "count", count);
        nn(k + "speed", speed);
        return *this;
    }
    LayerBuilder& set(int i, const char* key, float value) {
        nn("L" + std::to_string(i) + "_" + key, value);
        return *this;
    }
    LayerBuilder& alphas(int i, float a, float b) {  // the opacity of colour A and colour B
        const std::string k = "L" + std::to_string(i) + "_";
        for (auto& kv : v) {
            if (kv.first == k + "colA") kv.second[3] = a;
            if (kv.first == k + "colB") kv.second[3] = b;
        }
        return *this;
    }
};

Preset preset(const char* id, const char* name, const char* description, const char* shape, BlendMode blend, const PresetBuilder& b) {
    Preset p;
    p.id = id;
    p.name = name;
    p.description = description;
    p.shape = shape;
    p.blend = blend;
    p.values = b.v;
    return p;
}

constexpr BlendMode kAdd = BlendMode::Additive;
constexpr BlendMode kAlpha = BlendMode::Alpha;

std::vector<Preset> emissivePresets() {
    return {
        preset("fire-energy", "Fire Energy", "Hot orange veins that flow upward", "sphere", kAdd,
               PresetBuilder{}.n("intensity", 1.25f).n("coreGlow", 0.2f).n("rimIntensity", 0.6f)),
        preset("arcane-energy", "Arcane Energy", "Violet magic that turns slowly", "sphere", kAdd,
               PresetBuilder{}.c("color1", 0.30f, 0.04f, 0.95f).c("color2", 0.80f, 0.30f, 1.0f).c("color3", 1.0f, 0.88f, 1.0f)
                   .n("rotateSpeed", 0.45f).n("flowSpeed", 0.5f).n("distortion", 0.55f)),
        preset("nature-energy", "Nature Energy", "Green life force in cells", "sphere", kAdd,
               PresetBuilder{}.c("color1", 0.04f, 0.55f, 0.08f).c("color2", 0.40f, 1.0f, 0.18f).c("color3", 0.92f, 1.0f, 0.60f)
                   .n("pattern", 1).n("flowSpeed", 0.25f).n("veins", 0.55f)),
        preset("ice-energy", "Ice Energy", "Cold blue crackling energy", "sphere", kAdd,
               PresetBuilder{}.c("color1", 0.03f, 0.25f, 1.0f).c("color2", 0.20f, 0.80f, 1.0f).c("color3", 0.92f, 1.0f, 1.0f)
                   .n("pattern", 0).n("flowSpeed", 0.6f).n("patternScale", 4.0f).n("flicker", 0.15f)),
        preset("plasma-orb", "Plasma Orb", "A bright orb of streaming plasma", "sphere", kAdd,
               PresetBuilder{}.c("color1", 0.85f, 0.05f, 0.75f).c("color2", 0.30f, 0.50f, 1.0f).c("color3", 1.0f, 1.0f, 1.0f)
                   .n("pattern", 2).n("rotateSpeed", 0.8f).n("distortion", 0.6f).n("intensity", 3.0f).n("coreGlow", 1.0f)),
        preset("magical-reward", "Magical Reward", "A golden, softly pulsing glow", "sphere", kAdd,
               PresetBuilder{}.c("color1", 1.0f, 0.62f, 0.05f).c("color2", 1.0f, 0.88f, 0.35f).c("color3", 1.0f, 1.0f, 0.90f)
                   .n("pattern", 1).n("pulseAmp", 0.35f).n("pulseFreq", 2.0f).n("coreGlow", 1.3f).n("rimIntensity", 1.2f)),
        preset("energy-core", "Energy Core", "A cyan-white core with a bright rim", "sphere", kAdd,
               PresetBuilder{}.c("color1", 0.0f, 0.45f, 1.0f).c("color2", 0.25f, 0.90f, 1.0f).c("color3", 1.0f, 1.0f, 1.0f)
                   .n("coreGlow", 1.6f).n("rimIntensity", 1.5f).n("rotateSpeed", 0.5f).n("pulseAmp", 0.3f)),
    };
}

std::vector<Preset> hologramPresets() {
    return {
        preset("hologram-globe", "Hologram Globe", "A glowing projected sphere", "sphere", kAdd, PresetBuilder{}),
        preset("hologram-object", "Hologram Object", "A projected block with a scan bar", "cube", kAdd,
               PresetBuilder{}.n("scanDensity", 70).n("gridScale", 14).n("gridStrength", 0.45f).n("sweep", 1.6f)),
        preset("teleport-portal", "Teleport Portal", "A rising ring of blue light", "ring", kAdd,
               PresetBuilder{}.c("color", 0.35f, 0.55f, 1.0f).c("edgeColor", 0.85f, 0.92f, 1.0f).n("scanSpeed", 2.2f)
                   .n("scanDensity", 36).n("sweep", 2.0f).n("heightFade", 0.9f).n("intensity", 2.6f).n("transparency", 0.3f)),
        preset("scan-effect", "Scan Effect", "A bar of light sweeping over a faint shape", "sphere", kAdd,
               PresetBuilder{}.n("sweep", 3.0f).n("scanIntensity", 0.35f).n("transparency", 0.2f).n("gridStrength", 0.1f).n("glitch", 0)),
        preset("projection-ring", "Projection Ring", "A wide ring of light for a projector base", "ring", kAdd,
               PresetBuilder{}.n("scanDensity", 18).n("gridStrength", 0).n("sweep", 0.5f).n("heightFade", 0.2f).n("transparency", 0.7f)),
        preset("sci-fi-display", "Sci-Fi Display", "A glitching green screen", "plane", kAdd,
               PresetBuilder{}.c("color", 0.15f, 1.0f, 0.55f).c("edgeColor", 0.8f, 1.0f, 0.85f).n("gridStrength", 0.8f)
                   .n("scanDensity", 100).n("scanWidth", 0.3f).n("glitch", 0.45f).n("glitchFreq", 2.5f).n("heightFade", 0)),
    };
}

std::vector<Preset> shieldPresets() {
    return {
        preset("energy-shield", "Energy Shield", "A violet dome with a glowing rim", "sphere", kAdd, PresetBuilder{}),
        preset("hex-shield", "Hex Shield", "A cyan honeycomb barrier", "sphere", kAdd,
               PresetBuilder{}.c("color", 0.10f, 0.65f, 1.0f).c("edgeColor", 0.70f, 0.97f, 1.0f).n("hexStrength", 1.0f).n("hexScale", 9.0f)
                   .n("transparency", 0.12f)),
        preset("magic-barrier", "Magic Barrier", "A rippling purple veil", "sphere", kAdd,
               PresetBuilder{}.c("color", 0.75f, 0.15f, 1.0f).c("edgeColor", 1.0f, 0.65f, 1.0f).n("ripple", 1.0f).n("hexStrength", 0.2f)
                   .n("distortion", 0.45f).n("transparency", 0.3f)),
        preset("impact-shield", "Impact Shield", "An orange shield that flares when hit", "sphere", kAdd,
               PresetBuilder{}.c("color", 1.0f, 0.45f, 0.05f).c("edgeColor", 1.0f, 0.85f, 0.45f).c("hitColor", 1.0f, 1.0f, 0.85f)
                   .n("hitIntensity", 2.2f).n("hitRadius", 0.45f).n("autoHit", 2.0f).n("hexStrength", 0.55f)),
        preset("electric-dome", "Electric Dome", "A flickering blue dome", "sphere", kAdd,
               PresetBuilder{}.c("color", 0.20f, 0.45f, 1.0f).c("edgeColor", 0.85f, 0.92f, 1.0f).c("hitColor", 1.0f, 1.0f, 1.0f)
                   .n("flicker", 0.35f).n("ripple", 0.8f).n("hexStrength", 0.3f).n("distortion", 0.35f)),
        preset("force-field-ring", "Force Field Ring", "A ring of fine hexagons", "ring", kAdd,
               PresetBuilder{}.n("transparency", 0.08f).n("hexScale", 14.0f).n("hexStrength", 0.9f).n("fresnel", 1.4f)),
    };
}

std::vector<Preset> particlePresets() {
    return {
        preset("fire", "Fire", "A flame that cools from yellow to ember", "particle", kAdd, PresetBuilder{}),
        preset("smoke", "Smoke", "Soft grey smoke", "particle", kAlpha,
               PresetBuilder{}.c("color1", 0.85f, 0.85f, 0.90f).c("color2", 0.60f, 0.60f, 0.66f).c("color3", 0.30f, 0.30f, 0.36f)
                   .n("pattern", 1).n("intensity", 1.6f).n("softness", 0.9f).n("fadeOut", 0.6f).n("noiseScale", 2.2f)),
        preset("sparks", "Sparks", "Hot flying sparks", "particle", kAdd,
               PresetBuilder{}.c("color1", 1.0f, 0.95f, 0.70f).c("color2", 1.0f, 0.55f, 0.10f).c("color3", 0.5f, 0.10f, 0.0f)
                   .n("pattern", 2).n("intensity", 3.0f).n("fadeOut", 0.4f)),
        preset("magic-trail", "Magic Trail", "A blue-violet swirl", "particle", kAdd,
               PresetBuilder{}.c("color1", 0.60f, 0.92f, 1.0f).c("color2", 0.50f, 0.30f, 1.0f).c("color3", 0.10f, 0.0f, 0.30f)
                   .n("pattern", 3).n("intensity", 2.4f)),
        preset("explosion", "Explosion", "A white-hot burst", "particle", kAdd,
               PresetBuilder{}.c("color1", 1.0f, 1.0f, 0.90f).c("color2", 1.0f, 0.50f, 0.08f).c("color3", 0.35f, 0.04f, 0.0f)
                   .n("intensity", 4.0f).n("turbulence", 1.4f).n("noiseScale", 2.0f).n("stretch", 1.0f).n("fadeIn", 0.05f)),
        preset("energy-swirl", "Energy Swirl", "A spinning ring of green energy", "particle", kAdd,
               PresetBuilder{}.c("color1", 0.80f, 1.0f, 0.70f).c("color2", 0.10f, 0.85f, 0.35f).c("color3", 0.0f, 0.20f, 0.10f)
                   .n("pattern", 3).n("turbulence", 0.4f).n("intensity", 2.8f)),
        preset("magical-dust", "Magical Dust", "Glittering pink and gold motes", "particle", kAdd,
               PresetBuilder{}.c("color1", 1.0f, 0.85f, 0.55f).c("color2", 1.0f, 0.40f, 0.80f).c("color3", 0.30f, 0.05f, 0.30f)
                   .n("pattern", 4).n("intensity", 2.4f).n("noiseScale", 3.5f)),
    };
}

std::vector<Preset> uiPresets() {
    return {
        preset("star-reward", "Star Reward", "A golden star with a sweeping shine", "star", kAlpha, PresetBuilder{}),
        preset("gem-shine", "Gem Shine", "A blue gem that glints", "gem", kAlpha,
               PresetBuilder{}.c("color1", 0.45f, 0.90f, 1.0f).c("color2", 0.10f, 0.30f, 0.95f).c("glowColor", 0.75f, 0.95f, 1.0f)
                   .c("outlineColor", 0.85f, 0.97f, 1.0f).n("shineIntensity", 2.2f).n("shineWidth", 0.15f).n("sparkle", 0.6f)),
        preset("button-glow", "Button Glow", "A green button with a soft glow", "button", kAlpha,
               PresetBuilder{}.c("color1", 0.45f, 0.95f, 0.25f).c("color2", 0.10f, 0.60f, 0.12f).c("glowColor", 0.70f, 1.0f, 0.55f)
                   .c("outlineColor", 0.85f, 1.0f, 0.75f).n("glowIntensity", 2.0f).n("pulseAmp", 0.15f).n("shineIntensity", 0.9f)),
        preset("animated-border", "Animated Border", "A rounded frame with a flowing light", "frame", kAlpha,
               PresetBuilder{}.n("frameMode", 1).c("color1", 0.25f, 0.85f, 1.0f).c("color2", 0.55f, 0.35f, 1.0f).c("glowColor", 0.8f, 0.95f, 1.0f)
                   .n("scrollV", 0.8f).n("shineAngle", 90).n("shineSpeed", 0.7f).n("shineInterval", 0.1f).n("sparkle", 0)),
        preset("rarity-frame", "Rarity Frame", "A purple-gold frame with a shifting gradient", "frame", kAlpha,
               PresetBuilder{}.n("frameMode", 1).c("color1", 1.0f, 0.75f, 0.15f).c("color2", 0.65f, 0.15f, 0.95f).c("glowColor", 1.0f, 0.90f, 0.55f)
                   .n("scrollV", 0.35f).n("frameWidth", 0.11f).n("glowIntensity", 2.0f).n("sparkle", 0.5f)),
        preset("selection-frame", "Selection Frame", "A pulsing white selection outline", "frame", kAlpha,
               PresetBuilder{}.n("frameMode", 1).c("color1", 1.0f, 1.0f, 1.0f).c("color2", 0.75f, 0.90f, 1.0f).c("glowColor", 0.7f, 0.9f, 1.0f)
                   .n("frameWidth", 0.05f).n("pulseAmp", 0.45f).n("pulseFreq", 1.6f).n("shineIntensity", 0.5f).n("sparkle", 0)),
        preset("reward-highlight", "Reward Highlight", "A glowing badge with rotating sparkles", "badge", kAlpha,
               PresetBuilder{}.c("color1", 1.0f, 0.95f, 0.55f).c("color2", 1.0f, 0.60f, 0.10f).n("glowIntensity", 2.8f).n("sparkle", 1.0f)
                   .n("pulseAmp", 0.25f).n("shineWidth", 0.3f)),
    };
}

std::vector<Preset> icePresets() {
    return {
        preset("ice-crystal", "Ice Crystal", "A clear blue crystal", "crystal", kAlpha, PresetBuilder{}),
        preset("frozen-cube", "Frozen Cube", "A cube of ice with cracks", "cube", kAlpha,
               PresetBuilder{}.n("cracks", 0.9f).n("crackScale", 5.0f).n("frost", 0.2f).n("transparency", 0.55f)),
        preset("icicles", "Icicles", "A spike of clear ice", "cone", kAlpha,
               PresetBuilder{}.n("smoothness", 1.0f).n("transparency", 0.45f).n("crystalScale", 7.0f).n("cracks", 0.15f).n("frost", 0.12f)),
        preset("frosted-glass", "Frosted Glass", "A pane covered in frost", "plane", kAlpha,
               PresetBuilder{}.n("frost", 0.75f).n("transparency", 0.3f).n("frostScale", 9.0f).n("cracks", 0.0f).n("refraction", 0.7f)),
        preset("snow-surface", "Snow Surface", "Snow settled on the top", "sphere", kAlpha,
               PresetBuilder{}.n("snow", 1.0f).n("frost", 0.55f).n("transparency", 0.8f).n("shimmer", 1.5f).n("cracks", 0.0f)),
        preset("frozen-gem", "Frozen Gem", "A gem sealed in ice", "crystal", kAlpha,
               PresetBuilder{}.c("iceColor", 0.70f, 0.90f, 1.0f).c("deepColor", 0.10f, 0.15f, 0.65f).n("crystalScale", 9.0f)
                   .n("edgeHighlight", 2.4f).n("shimmer", 1.8f).n("frost", 0.1f).n("cracks", 0.25f)),
    };
}

// Blend: 0 add, 1 alpha, 2 multiply, 3 screen, 4 mask, 5 erase.
// Elements: 1 circle 2 ring 3 segmented ring 4 spiral 5 star 6 polygon 7 hex grid 8 grid 9 stripes 10 speed lines
// 11 wave 12 beam 13 radar sweep 14 lightning 15 orbiting dots 16 shockwave 17 linear gradient 18 radial gradient
// 19 angular gradient 20 fresnel glow 21 height gradient 22 noise 23 cells 24 plasma 25 halftone 26 checker 27 solid
// Element numbers added for the sci-fi motion set.
constexpr int kArcRing = 28, kTickRing = 29, kLightColumn = 30, kFunnel = 31, kBrackets = 32;

// The sci-fi motion effects, built from layers. Each is a flat picture meant for a particle quad
// or a plane, and is tuned against the reference sheet's colours: violet, electric blue, cyan.
std::vector<Preset> sciFiPresets() {
    return {
        preset("sf-holo-ring", "Sci-Fi: Holographic Ring", "Tall violet ellipses and a flat orbit ring around a glowing eye", "particle", kAdd,
               LayerBuilder{}
                   .layer(0, 18, 0.25f, 0.08f, 0.7f, 0.55f, 0.3f, 1.0f, 0, 1.1f, 1.0f, 0.45f, 0.3f, 6, 0.5f).alphas(0, 0.0f, 0.45f)
                   .layer(1, 2, 0.4f, 0.15f, 1.0f, 0.7f, 0.45f, 1.0f, 0, 2.0f, 0.92f, 0.035f, 0.3f, 6, 0.5f).set(1, "aspect", 0.72f)
                   .layer(2, kArcRing, 0.5f, 0.25f, 1.0f, 0.8f, 0.6f, 1.0f, 0, 2.2f, 0.8f, 0.05f, 0.3f, 9, 0.5f).set(2, "aspect", 0.72f).set(2, "spin", 18)
                   .layer(3, kTickRing, 0.5f, 0.3f, 1.0f, 0.8f, 0.65f, 1.0f, 0, 1.3f, 0.62f, 0.1f, 0.3f, 60, 0.5f).set(3, "aspect", 0.72f)
                   .layer(4, 2, 0.45f, 0.2f, 1.0f, 0.75f, 0.5f, 1.0f, 0, 2.0f, 0.27f, 0.06f, 0.3f, 6, 0.5f).set(4, "aspect", 3.7f).set(4, "rot", -8)
                   .layer(5, kArcRing, 0.55f, 0.3f, 1.0f, 0.85f, 0.7f, 1.0f, 0, 2.2f, 0.27f, 0.06f, 0.3f, 6, 0.5f).set(5, "aspect", 3.7f).set(5, "rot", -8)
                   .layer(6, 18, 0.7f, 0.5f, 1.0f, 1.0f, 0.95f, 1.0f, 0, 3.0f, 0.14f, 0.3f, 0.3f, 6, 0.5f)
                   .layer(7, 1, 0.9f, 0.85f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 3.0f, 0.07f, 1.0f, 0.5f, 6, 0.5f)),
        preset("sf-scan-beam", "Sci-Fi: Scanning Beam", "A blue column of light between two glowing discs", "particle", kAdd,
               LayerBuilder{}
                   .layer(0, 18, 0.0f, 0.2f, 0.8f, 0.2f, 0.6f, 1.0f, 0, 0.9f, 1.0f, 0.4f, 0.3f, 6, 0.5f).alphas(0, 0.0f, 0.35f)
                   .layer(1, kLightColumn, 0.1f, 0.4f, 1.0f, 0.6f, 0.85f, 1.0f, 0, 2.0f, 1.0f, 0.22f, 0.3f, 22, 0.7f).set(1, "aspect", 0.45f)
                   .layer(2, 12, 0.3f, 0.6f, 1.0f, 0.9f, 0.97f, 1.0f, 0, 3.0f, 1.0f, 0.1f, 0.4f, 6, 0.5f)
                   .layer(3, 2, 0.15f, 0.5f, 1.0f, 0.7f, 0.9f, 1.0f, 0, 2.4f, 0.27f, 0.1f, 0.3f, 6, 0.5f).set(3, "aspect", 3.6f).set(3, "oy", -0.72f)
                   .layer(4, 2, 0.1f, 0.45f, 1.0f, 0.6f, 0.85f, 1.0f, 0, 2.0f, 0.16f, 0.12f, 0.3f, 6, 0.5f).set(4, "aspect", 3.6f).set(4, "oy", -0.72f)
                   .layer(5, 2, 0.1f, 0.4f, 1.0f, 0.6f, 0.85f, 1.0f, 0, 2.2f, 0.27f, 0.1f, 0.3f, 6, 0.5f).set(5, "aspect", 3.6f).set(5, "oy", 0.74f)
                   .layer(6, 16, 0.2f, 0.55f, 1.0f, 0.7f, 0.9f, 1.0f, 0, 2.0f, 0.3f, 0.2f, 0.5f, 6, 0.6f).set(6, "aspect", 3.3f).set(6, "oy", 0.74f)
                   .layer(7, 18, 0.5f, 0.8f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 3.0f, 0.2f, 0.3f, 0.3f, 6, 0.5f).set(7, "oy", 0.74f)),
        preset("sf-energy-wave", "Sci-Fi: Energy Wave", "Rippling blue rings spreading from a bright spike", "particle", kAdd,
               LayerBuilder{}
                   .layer(0, 18, 0.0f, 0.15f, 0.7f, 0.1f, 0.5f, 1.0f, 0, 0.9f, 1.0f, 0.5f, 0.3f, 6, 0.5f).alphas(0, 0.0f, 0.4f)
                   .layer(1, 2, 0.1f, 0.5f, 1.0f, 0.5f, 0.85f, 1.0f, 0, 2.6f, 0.5f, 0.07f, 0.3f, 6, 0.5f).set(1, "aspect", 1.9f).set(1, "oy", 0.1f)
                   .layer(2, kArcRing, 0.15f, 0.65f, 1.0f, 0.6f, 0.95f, 1.0f, 0, 2.6f, 0.4f, 0.06f, 0.3f, 5, 0.5f).set(2, "aspect", 1.9f).set(2, "oy", 0.1f).set(2, "spin", 25)
                   .layer(3, kArcRing, 0.2f, 0.7f, 1.0f, 0.8f, 1.0f, 1.0f, 0, 2.4f, 0.28f, 0.05f, 0.3f, 4, 0.5f).set(3, "aspect", 1.9f).set(3, "oy", 0.1f).set(3, "spin", -40)
                   .layer(4, 16, 0.2f, 0.6f, 1.0f, 0.7f, 0.95f, 1.0f, 0, 2.0f, 0.5f, 0.1f, 0.5f, 6, 0.5f).set(4, "aspect", 1.9f).set(4, "oy", 0.1f)
                   .layer(5, 12, 0.4f, 0.8f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 3.0f, 0.75f, 0.035f, 0.5f, 6, 0.5f).set(5, "oy", -0.15f)
                   .layer(6, 18, 0.4f, 0.8f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 3.2f, 0.22f, 0.3f, 0.3f, 6, 0.5f).set(6, "oy", 0.1f)),
        preset("sf-vortex-portal", "Sci-Fi: Vortex Portal", "A violet funnel of swirled rings", "particle", kAdd,
               LayerBuilder{}
                   .layer(0, 18, 0.15f, 0.05f, 0.6f, 0.45f, 0.2f, 1.0f, 0, 1.0f, 1.0f, 0.45f, 0.3f, 6, 0.5f).alphas(0, 0.0f, 0.4f)
                   .layer(1, kFunnel, 0.4f, 0.2f, 1.0f, 0.8f, 0.6f, 1.0f, 0, 1.7f, 1.0f, 0.45f, 0.25f, 12, 0.8f)
                   .layer(2, kFunnel, 0.5f, 0.3f, 1.0f, 0.9f, 0.75f, 1.0f, 0, 1.4f, 0.8f, 0.3f, 0.25f, 9, -0.6f).set(2, "oy", -0.05f)
                   .layer(3, 18, 0.7f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 2.2f, 0.12f, 0.3f, 0.3f, 6, 0.5f).set(3, "oy", 0.8f)
                   .layer(4, 2, 0.4f, 0.25f, 1.0f, 0.85f, 0.7f, 1.0f, 0, 2.0f, 0.25f, 0.08f, 0.3f, 6, 0.5f).set(4, "aspect", 3.6f).set(4, "oy", 0.8f)),
        preset("sf-electric-arc", "Sci-Fi: Electric Arc", "Branching blue-white lightning with a hot glow", "particle", kAdd,
               LayerBuilder{}
                   .layer(0, 18, 0.05f, 0.1f, 0.8f, 0.3f, 0.35f, 1.0f, 0, 1.1f, 1.0f, 0.4f, 0.3f, 6, 0.5f).alphas(0, 0.0f, 0.4f)
                   .layer(1, 14, 0.35f, 0.4f, 1.0f, 0.9f, 0.95f, 1.0f, 0, 3.4f, 1.0f, 0.3f, 0.3f, 5, 1.2f)
                   .layer(2, 14, 0.4f, 0.45f, 1.0f, 0.95f, 0.97f, 1.0f, 0, 2.6f, 0.8f, 0.25f, 0.3f, 7, 1.7f).set(2, "ox", 0.25f).set(2, "rot", 25)
                   .layer(3, 14, 0.4f, 0.5f, 1.0f, 0.9f, 0.95f, 1.0f, 0, 2.4f, 0.7f, 0.2f, 0.3f, 8, 2.1f).set(3, "ox", -0.3f).set(3, "rot", -35)
                   .layer(4, 14, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 2.0f, 0.5f, 0.2f, 0.3f, 9, 2.5f).set(4, "oy", 0.35f).set(4, "rot", 70)
                   .layer(5, 18, 0.6f, 0.6f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 3.0f, 0.2f, 0.3f, 0.3f, 6, 0.5f)),
        preset("sf-targeting", "Sci-Fi: Targeting Indicator", "A flat orbit ring and a reticle with a glowing centre", "particle", kAdd,
               LayerBuilder{}
                   .layer(0, 18, 0.05f, 0.2f, 0.9f, 0.25f, 0.6f, 1.0f, 0, 1.0f, 1.0f, 0.45f, 0.3f, 6, 0.5f).alphas(0, 0.0f, 0.35f)
                   .layer(1, 2, 0.2f, 0.55f, 1.0f, 0.7f, 0.9f, 1.0f, 0, 2.4f, 0.22f, 0.1f, 0.3f, 6, 0.5f).set(1, "aspect", 4.4f)
                   .layer(2, kArcRing, 0.3f, 0.65f, 1.0f, 0.85f, 0.95f, 1.0f, 0, 2.4f, 0.22f, 0.09f, 0.3f, 10, 0.5f).set(2, "aspect", 4.4f)
                   .layer(3, 2, 0.2f, 0.55f, 1.0f, 0.8f, 0.95f, 1.0f, 0, 2.4f, 0.5f, 0.045f, 0.3f, 6, 0.5f)
                   .layer(4, kTickRing, 0.3f, 0.65f, 1.0f, 0.9f, 0.97f, 1.0f, 0, 1.6f, 0.55f, 0.12f, 0.3f, 36, 0.5f).set(4, "spin", 12)
                   .layer(5, 14, 0.3f, 0.6f, 1.0f, 0.8f, 0.95f, 1.0f, 0, 2.4f, 0.4f, 0.3f, 0.2f, 6, 1.6f).set(5, "oy", -0.5f)
                   .layer(6, 14, 0.3f, 0.6f, 1.0f, 0.8f, 0.95f, 1.0f, 0, 2.4f, 0.4f, 0.3f, 0.2f, 6, 1.6f).set(6, "oy", 0.5f).set(6, "rot", 180)
                   .layer(7, 18, 0.6f, 0.85f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 3.4f, 0.16f, 0.3f, 0.3f, 6, 0.5f)),
        // ---- the small variants along the bottom of the sheet
        preset("sf-orb", "Sci-Fi: Blue Orb", "A glowing blue core inside two turning rings", "particle", kAdd,
               LayerBuilder{}
                   .layer(0, 18, 0.0f, 0.25f, 0.9f, 0.2f, 0.7f, 1.0f, 0, 1.3f, 1.0f, 0.4f, 0.3f, 6, 0.5f).alphas(0, 0.0f, 0.5f)
                   .layer(1, 1, 0.1f, 0.5f, 1.0f, 0.5f, 0.85f, 1.0f, 0, 2.0f, 0.42f, 1.0f, 0.5f, 6, 0.5f)
                   .layer(2, 2, 0.1f, 0.45f, 1.0f, 0.6f, 0.9f, 1.0f, 0, 2.2f, 0.62f, 0.07f, 0.3f, 6, 0.5f)
                   .layer(3, kArcRing, 0.2f, 0.6f, 1.0f, 0.7f, 0.95f, 1.0f, 0, 2.2f, 0.85f, 0.06f, 0.3f, 5, 0.8f)
                   .layer(4, 18, 0.7f, 0.9f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 3.0f, 0.18f, 0.3f, 0.3f, 6, 0.5f)),
        preset("sf-net-sphere", "Sci-Fi: Net Sphere", "A violet sphere of fine hexagonal net lines", "particle", kAdd,
               LayerBuilder{}
                   .layer(0, 18, 0.15f, 0.05f, 0.6f, 0.35f, 0.2f, 0.9f, 0, 1.0f, 1.0f, 0.4f, 0.3f, 6, 0.5f).alphas(0, 0.0f, 0.4f)
                   .layer(1, 7, 0.4f, 0.25f, 1.0f, 0.85f, 0.7f, 1.0f, 0, 2.0f, 0.9f, 0.12f, 0.2f, 14, 0.5f)
                   .layer(2, 1, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 4, 1.0f, 0.88f, 1.0f, 0.2f, 6, 0.5f)
                   .layer(3, 2, 0.5f, 0.3f, 1.0f, 0.9f, 0.8f, 1.0f, 0, 2.4f, 0.9f, 0.03f, 0.3f, 6, 0.5f)
                   .layer(4, 18, 0.8f, 0.7f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 3.0f, 0.15f, 0.3f, 0.3f, 6, 0.5f)),
        preset("sf-red-ring", "Sci-Fi: Red Ring", "A flat red and magenta ring with a rising spark", "particle", kAdd,
               LayerBuilder{}
                   .layer(0, 18, 0.5f, 0.0f, 0.15f, 1.0f, 0.2f, 0.5f, 0, 1.0f, 0.8f, 0.4f, 0.3f, 6, 0.5f).alphas(0, 0.0f, 0.4f).set(0, "aspect", 1.5f).set(0, "oy", -0.2f)
                   .layer(1, 2, 1.0f, 0.05f, 0.1f, 1.0f, 0.3f, 0.45f, 0, 2.4f, 0.28f, 0.09f, 0.3f, 6, 0.5f).set(1, "aspect", 3.4f).set(1, "oy", -0.2f)
                   .layer(2, kArcRing, 1.0f, 0.1f, 0.3f, 1.0f, 0.4f, 0.6f, 0, 2.2f, 0.22f, 0.08f, 0.3f, 7, 0.5f).set(2, "aspect", 3.4f).set(2, "oy", -0.2f)
                   .layer(3, 12, 1.0f, 0.2f, 0.2f, 1.0f, 0.85f, 0.7f, 0, 2.6f, 0.5f, 0.1f, 0.6f, 6, 0.5f).set(3, "oy", 0.2f)
                   .layer(4, 18, 1.0f, 0.5f, 0.6f, 1.0f, 1.0f, 1.0f, 0, 3.0f, 0.12f, 0.3f, 0.3f, 6, 0.5f).set(4, "oy", -0.2f)),
        preset("sf-pillar", "Sci-Fi: Light Pillar", "A thin cyan pillar standing on spreading ripples", "particle", kAdd,
               LayerBuilder{}
                   .layer(0, kLightColumn, 0.1f, 0.6f, 1.0f, 0.7f, 0.95f, 1.0f, 0, 1.8f, 1.0f, 0.2f, 0.3f, 14, 0.8f).set(0, "aspect", 0.25f)
                   .layer(1, 12, 0.3f, 0.8f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 3.0f, 1.0f, 0.05f, 0.5f, 6, 0.5f)
                   .layer(2, 16, 0.1f, 0.6f, 1.0f, 0.6f, 0.95f, 1.0f, 0, 2.0f, 0.3f, 0.12f, 0.5f, 6, 0.6f).set(2, "aspect", 3.3f).set(2, "oy", 0.75f)
                   .layer(3, 2, 0.1f, 0.6f, 1.0f, 0.6f, 0.95f, 1.0f, 0, 2.2f, 0.27f, 0.08f, 0.3f, 6, 0.5f).set(3, "aspect", 3.5f).set(3, "oy", 0.75f)
                   .layer(4, 18, 0.5f, 0.9f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 3.0f, 0.18f, 0.3f, 0.3f, 6, 0.5f).set(4, "oy", 0.75f)),
        preset("sf-ripples", "Sci-Fi: Concentric Rings", "Several flat blue rings, each turning at its own speed", "particle", kAdd,
               LayerBuilder{}
                   .layer(0, 18, 0.0f, 0.15f, 0.9f, 0.15f, 0.4f, 1.0f, 0, 1.0f, 1.0f, 0.4f, 0.3f, 6, 0.5f).alphas(0, 0.0f, 0.4f).set(0, "aspect", 2.0f)
                   .layer(1, kArcRing, 0.1f, 0.3f, 1.0f, 0.5f, 0.7f, 1.0f, 0, 2.2f, 0.5f, 0.07f, 0.3f, 6, 0.5f).set(1, "aspect", 2.0f).set(1, "spin", 20)
                   .layer(2, kArcRing, 0.15f, 0.4f, 1.0f, 0.6f, 0.8f, 1.0f, 0, 2.2f, 0.4f, 0.06f, 0.3f, 7, 0.5f).set(2, "aspect", 2.0f).set(2, "spin", -35)
                   .layer(3, kArcRing, 0.2f, 0.5f, 1.0f, 0.7f, 0.9f, 1.0f, 0, 2.2f, 0.3f, 0.05f, 0.3f, 5, 0.5f).set(3, "aspect", 2.0f).set(3, "spin", 50)
                   .layer(4, 2, 0.2f, 0.5f, 1.0f, 0.8f, 0.95f, 1.0f, 0, 2.4f, 0.5f, 0.03f, 0.3f, 6, 0.5f).set(4, "aspect", 2.0f)
                   .layer(5, 18, 0.5f, 0.8f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 3.0f, 0.14f, 0.3f, 0.3f, 6, 0.5f)),
        preset("sf-crown-blue", "Sci-Fi: Blue Crown", "A flat blue ring with spiky points rising round it", "particle", kAdd,
               LayerBuilder{}
                   .layer(0, 2, 0.1f, 0.4f, 1.0f, 0.5f, 0.8f, 1.0f, 0, 2.2f, 0.28f, 0.09f, 0.3f, 6, 0.5f).set(0, "aspect", 3.3f).set(0, "oy", -0.25f)
                   .layer(1, 14, 0.2f, 0.5f, 1.0f, 0.8f, 0.95f, 1.0f, 0, 2.2f, 0.4f, 0.2f, 0.3f, 4, 1.4f).set(1, "ox", -0.4f).set(1, "oy", 0.1f)
                   .layer(2, 14, 0.2f, 0.5f, 1.0f, 0.8f, 0.95f, 1.0f, 0, 2.2f, 0.4f, 0.2f, 0.3f, 4, 1.8f).set(2, "ox", 0.4f).set(2, "oy", 0.1f)
                   .layer(3, 12, 0.3f, 0.7f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 2.8f, 0.5f, 0.06f, 0.5f, 6, 0.5f).set(3, "oy", 0.05f)
                   .layer(4, 18, 0.4f, 0.8f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 3.0f, 0.16f, 0.3f, 0.3f, 6, 0.5f).set(4, "oy", -0.25f)),
        preset("sf-crown-orange", "Sci-Fi: Orange Crown", "A flat orange ring with flame-like points and a bright triangle core", "particle", kAdd,
               LayerBuilder{}
                   .layer(0, 2, 1.0f, 0.4f, 0.05f, 1.0f, 0.8f, 0.4f, 0, 2.4f, 0.28f, 0.09f, 0.3f, 6, 0.5f).set(0, "aspect", 3.3f).set(0, "oy", -0.25f)
                   .layer(1, kArcRing, 1.0f, 0.5f, 0.1f, 1.0f, 0.85f, 0.5f, 0, 2.2f, 0.22f, 0.07f, 0.3f, 6, 0.5f).set(1, "aspect", 3.3f).set(1, "oy", -0.25f)
                   .layer(2, 6, 1.0f, 0.6f, 0.1f, 1.0f, 0.95f, 0.6f, 0, 2.6f, 0.14f, 0.5f, 0.3f, 3, 0.5f)
                   .layer(3, 14, 1.0f, 0.5f, 0.1f, 1.0f, 0.9f, 0.5f, 0, 2.2f, 0.4f, 0.25f, 0.3f, 5, 1.6f).set(3, "ox", -0.45f).set(3, "oy", 0.05f)
                   .layer(4, 14, 1.0f, 0.5f, 0.1f, 1.0f, 0.9f, 0.5f, 0, 2.2f, 0.4f, 0.25f, 0.3f, 5, 2.0f).set(4, "ox", 0.45f).set(4, "oy", 0.05f)
                   .layer(5, 18, 1.0f, 0.6f, 0.2f, 1.0f, 1.0f, 0.8f, 0, 3.0f, 0.2f, 0.3f, 0.3f, 6, 0.5f).set(5, "oy", -0.1f)),
        preset("sf-crown-cyan", "Sci-Fi: Cyan Crown", "A cyan ring and spikes around a bright blue core", "particle", kAdd,
               LayerBuilder{}
                   .layer(0, 2, 0.0f, 0.7f, 1.0f, 0.6f, 0.95f, 1.0f, 0, 2.2f, 0.28f, 0.09f, 0.3f, 6, 0.5f).set(0, "aspect", 3.3f).set(0, "oy", -0.25f)
                   .layer(1, 14, 0.1f, 0.7f, 1.0f, 0.8f, 1.0f, 1.0f, 0, 2.2f, 0.4f, 0.2f, 0.3f, 4, 1.4f).set(1, "ox", -0.4f).set(1, "oy", 0.05f)
                   .layer(2, 14, 0.1f, 0.7f, 1.0f, 0.8f, 1.0f, 1.0f, 0, 2.2f, 0.4f, 0.2f, 0.3f, 4, 1.8f).set(2, "ox", 0.4f).set(2, "oy", 0.05f)
                   .layer(3, 1, 0.3f, 0.8f, 1.0f, 0.9f, 1.0f, 1.0f, 0, 2.6f, 0.17f, 1.0f, 0.5f, 6, 0.5f).set(3, "oy", -0.1f)
                   .layer(4, 18, 0.4f, 0.9f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 3.0f, 0.22f, 0.3f, 0.3f, 6, 0.5f).set(4, "oy", -0.1f)),
        preset("sf-ring-core", "Sci-Fi: Ring and Core", "A bright blue disc with a thin flat ring through it", "particle", kAdd,
               LayerBuilder{}
                   .layer(0, 18, 0.0f, 0.3f, 1.0f, 0.3f, 0.7f, 1.0f, 0, 1.2f, 1.0f, 0.4f, 0.3f, 6, 0.5f).alphas(0, 0.0f, 0.5f)
                   .layer(1, 2, 0.1f, 0.5f, 1.0f, 0.6f, 0.9f, 1.0f, 0, 2.2f, 0.28f, 0.08f, 0.3f, 6, 0.5f).set(1, "aspect", 3.3f)
                   .layer(2, 1, 0.1f, 0.5f, 1.0f, 0.6f, 0.9f, 1.0f, 0, 2.4f, 0.4f, 1.0f, 0.5f, 6, 0.5f)
                   .layer(3, 2, 0.5f, 0.85f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 2.6f, 0.45f, 0.04f, 0.3f, 6, 0.5f)
                   .layer(4, 18, 0.8f, 0.95f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 3.0f, 0.2f, 0.3f, 0.3f, 6, 0.5f)),
        preset("sf-sun-reticle", "Sci-Fi: Sun Reticle", "A golden-orange burst of rays inside a thin circle", "particle", kAdd,
               LayerBuilder{}
                   .layer(0, 18, 1.0f, 0.4f, 0.0f, 1.0f, 0.8f, 0.3f, 0, 1.2f, 1.0f, 0.45f, 0.3f, 6, 0.5f).alphas(0, 0.0f, 0.5f)
                   .layer(1, 2, 1.0f, 0.6f, 0.1f, 1.0f, 0.9f, 0.5f, 0, 2.2f, 0.95f, 0.03f, 0.3f, 6, 0.5f)
                   .layer(2, 5, 1.0f, 0.55f, 0.1f, 1.0f, 0.9f, 0.5f, 0, 2.6f, 0.6f, 0.9f, 0.3f, 14, 0.5f).set(2, "spin", 12)
                   .layer(3, 10, 1.0f, 0.6f, 0.15f, 1.0f, 0.95f, 0.6f, 0, 2.0f, 0.9f, 0.25f, 0.4f, 28, 1.4f)
                   .layer(4, 18, 1.0f, 0.8f, 0.4f, 1.0f, 1.0f, 0.9f, 0, 3.2f, 0.2f, 0.3f, 0.3f, 6, 0.5f)),
    };
}

std::vector<Preset> layerBasePresets();
std::vector<Preset> layerPresets() {
    std::vector<Preset> all = layerBasePresets();
    for (Preset& p : sciFiPresets()) all.push_back(std::move(p));
    return all;
}

std::vector<Preset> layerBasePresets() {
    return {
        preset("blank", "Start from a circle", "One circle to build on: add layers, change them, animate them", "plane", kAdd,
               LayerBuilder{}.layer(0, 1, 0.25f, 0.65f, 1.0f, 0.9f, 1.0f, 1.0f, 0, 1.6f, 0.7f, 1.0f, 0.5f, 6, 0.5f)),
        preset("energy-field", "Energy Field", "A glowing field: gradient, moving noise, a sweep, streaks, orbiting orbs and a halo", "sphere", kAdd,
               LayerBuilder{}
                   .layer(0, 18, 0.0f, 0.25f, 0.9f, 0.4f, 0.95f, 1.0f, 0, 0.8f, 1.0f, 0.3f, 0.3f, 6, 0.5f).set(0, "space", 1)
                   .layer(1, 22, 0.1f, 0.5f, 1.0f, 0.7f, 1.0f, 1.0f, 3, 0.9f, 1.0f, 0.55f, 0.5f, 5, 0.5f).set(1, "space", 1).set(1, "warp", 0.4f)
                   .layer(2, 13, 0.3f, 0.9f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 1.6f, 0.95f, 0.45f, 0.15f, 6, 0.8f).set(2, "space", 1)
                   .layer(3, 10, 0.4f, 0.9f, 1.0f, 0.9f, 1.0f, 1.0f, 0, 1.2f, 1.0f, 0.2f, 0.4f, 18, 1.2f).set(3, "space", 1)
                   .layer(4, 15, 0.7f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 2.5f, 0.85f, 0.12f, 0.6f, 5, 0.9f).set(4, "space", 1)
                   .layer(5, 20, 0.1f, 0.4f, 1.0f, 0.5f, 0.9f, 1.0f, 0, 1.4f, 1.0f, 0.3f, 0.3f, 5, 0.5f)
                   .n("brightness", 1.0f)),
        preset("magic-circle", "Magic Circle", "Rings, runes and a turning star: a spell on the ground", "plane", kAdd,
               LayerBuilder{}
                   .layer(0, 2, 0.5f, 0.2f, 1.0f, 0.9f, 0.6f, 1.0f, 0, 2.0f, 0.92f, 0.04f, 0.3f, 6, 0.5f)
                   .layer(1, 3, 0.6f, 0.3f, 1.0f, 1.0f, 0.8f, 1.0f, 0, 2.2f, 0.78f, 0.09f, 0.2f, 12, 0.35f).set(1, "spin", -25)
                   .layer(2, 5, 0.4f, 0.2f, 1.0f, 0.9f, 0.7f, 1.0f, 0, 1.6f, 0.62f, 0.12f, 0.15f, 6, 0.5f).set(2, "spin", 30)
                   .layer(3, 6, 0.7f, 0.4f, 1.0f, 1.0f, 0.9f, 1.0f, 0, 1.8f, 0.45f, 0.08f, 0.2f, 3, 0.5f).set(3, "spin", -40)
                   .layer(4, 15, 0.9f, 0.8f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 3.0f, 0.92f, 0.1f, 0.5f, 8, 0.7f)
                   .layer(5, 18, 0.3f, 0.1f, 0.8f, 0.6f, 0.3f, 1.0f, 0, 1.0f, 1.0f, 0.4f, 0.3f, 6, 0.5f).alphas(5, 0.35f, 0.0f)),
        preset("radar-scanner", "Radar Scanner", "A grid with rings and a sweeping beam", "plane", kAdd,
               LayerBuilder{}
                   .layer(0, 8, 0.0f, 0.5f, 0.35f, 0.2f, 1.0f, 0.6f, 0, 1.0f, 1.0f, 0.12f, 0.2f, 8, 0.5f)
                   .layer(1, 2, 0.1f, 0.9f, 0.5f, 0.5f, 1.0f, 0.8f, 0, 1.4f, 0.95f, 0.025f, 0.3f, 6, 0.5f)
                   .layer(2, 2, 0.1f, 0.9f, 0.5f, 0.5f, 1.0f, 0.8f, 0, 1.2f, 0.55f, 0.025f, 0.3f, 6, 0.5f)
                   .layer(3, 13, 0.1f, 1.0f, 0.5f, 0.7f, 1.0f, 0.9f, 0, 2.4f, 0.95f, 0.55f, 0.15f, 6, 0.9f)
                   .layer(4, 15, 0.6f, 1.0f, 0.8f, 1.0f, 1.0f, 1.0f, 0, 3.0f, 0.7f, 0.08f, 0.5f, 3, 0.4f)),
        preset("neon-outline", "Neon Outline", "A bright glowing edge round an object", "sphere", kAdd,
               LayerBuilder{}
                   .layer(0, 20, 1.0f, 0.1f, 0.7f, 1.0f, 0.5f, 0.9f, 0, 2.8f, 1.0f, 0.3f, 0.3f, 5, 0.5f)
                   .layer(1, 20, 0.3f, 0.1f, 1.0f, 0.9f, 0.3f, 1.0f, 0, 1.4f, 1.0f, 0.3f, 0.3f, 2, 0.5f)
                   .n("rimIntensity", 0.6f)),
        preset("shockwave-blast", "Shockwave Blast", "Rings that burst outward from a bright flash", "plane", kAdd,
               LayerBuilder{}
                   .layer(0, 16, 1.0f, 0.55f, 0.15f, 1.0f, 0.95f, 0.7f, 0, 2.8f, 1.0f, 0.16f, 0.5f, 6, 0.9f)
                   .layer(1, 16, 1.0f, 0.4f, 0.1f, 1.0f, 0.85f, 0.5f, 0, 2.0f, 1.0f, 0.1f, 0.5f, 6, 0.9f).set(1, "ox", 0.0f)
                   .layer(2, 18, 1.0f, 0.5f, 0.1f, 1.0f, 1.0f, 0.9f, 0, 2.6f, 0.45f, 0.5f, 0.3f, 6, 0.5f)
                   .layer(3, 10, 1.0f, 0.7f, 0.3f, 1.0f, 1.0f, 0.8f, 0, 2.0f, 1.0f, 0.2f, 0.4f, 20, 1.6f)),
        preset("electric-arc", "Electric Arc", "Jagged lightning with a hot glow", "plane", kAdd,
               LayerBuilder{}
                   .layer(0, 12, 0.2f, 0.4f, 1.0f, 0.5f, 0.8f, 1.0f, 0, 1.4f, 1.0f, 0.5f, 0.6f, 6, 0.5f)
                   .layer(1, 14, 0.4f, 0.7f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 3.2f, 1.0f, 0.2f, 0.3f, 3, 1.4f)
                   .layer(2, 14, 0.5f, 0.8f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 2.2f, 1.0f, 0.15f, 0.3f, 4, 2.0f).set(2, "ox", 0.12f)),
        preset("slot-win-burst", "Slot Win Burst", "A jackpot burst: rays, a star and sparkles", "plane", kAdd,
               LayerBuilder{}
                   .layer(0, 18, 1.0f, 0.55f, 0.05f, 1.0f, 0.95f, 0.5f, 0, 2.0f, 0.9f, 0.35f, 0.3f, 6, 0.5f)
                   .layer(1, 10, 1.0f, 0.8f, 0.2f, 1.0f, 1.0f, 0.8f, 0, 2.4f, 1.0f, 0.3f, 0.4f, 28, 1.4f)
                   .layer(2, 5, 1.0f, 0.7f, 0.1f, 1.0f, 1.0f, 0.7f, 0, 2.6f, 0.6f, 0.6f, 0.2f, 8, 0.5f).set(2, "spin", 25)
                   .layer(3, 15, 1.0f, 0.95f, 0.6f, 1.0f, 1.0f, 1.0f, 0, 3.0f, 0.9f, 0.1f, 0.6f, 10, 0.6f)
                   .layer(4, 25, 1.0f, 0.9f, 0.5f, 1.0f, 1.0f, 1.0f, 3, 1.5f, 1.0f, 0.25f, 0.4f, 14, 0.5f)),
        preset("hologram-grid", "Hologram Grid", "A hex grid with scan stripes and a glowing rim", "sphere", kAdd,
               LayerBuilder{}
                   .layer(0, 7, 0.1f, 0.7f, 1.0f, 0.5f, 1.0f, 1.0f, 0, 1.5f, 1.0f, 0.3f, 0.3f, 8, 0.5f).set(0, "space", 1)
                   .layer(1, 9, 0.2f, 0.8f, 1.0f, 0.6f, 1.0f, 1.0f, 3, 1.2f, 1.0f, 0.3f, 0.4f, 30, 1.0f).set(1, "rot", 90).set(1, "space", 1)
                   .layer(2, 20, 0.2f, 0.9f, 1.0f, 0.8f, 1.0f, 1.0f, 0, 2.0f, 1.0f, 0.3f, 0.3f, 3, 0.5f)),
    };
}

// What each element of a layer is called, which settings it uses and what they mean for it.
const std::vector<ElementInfo>& buildElements() {
    static const std::vector<ElementInfo> all = {
        {"None", "", "An empty layer", {}, {}, {}},
        {"Circle", "Shape", "A disc, or an outline of one", {"count", "speed"}, {{"thick", "Fill / outline (1 is filled)"}}, {{"size", 0.7f}, {"thick", 1.0f}, {"soft", 0.25f}}},
        {"Ring", "Shape", "A thin ring", {"count", "speed"}, {{"thick", "Ring width"}}, {{"size", 0.8f}, {"thick", 0.12f}, {"soft", 0.3f}}},
        {"Segmented ring", "Shape", "A ring broken into segments", {}, {{"thick", "Ring width"}, {"count", "Segments"}, {"speed", "Segment turning"}}, {{"size", 0.85f}, {"thick", 0.15f}, {"soft", 0.2f}, {"count", 8}, {"speed", 0.5f}}},
        {"Spiral", "Shape", "A spiral with several arms", {}, {{"thick", "Line width"}, {"count", "Arms"}, {"speed", "Travel speed"}}, {{"size", 0.9f}, {"thick", 0.35f}, {"count", 3}, {"speed", 0.6f}}},
        {"Star", "Shape", "A star with points", {"speed"}, {{"thick", "Sharpness"}, {"count", "Points"}}, {{"size", 0.8f}, {"thick", 0.6f}, {"count", 5}}},
        {"Polygon", "Shape", "A triangle, square, hexagon ...", {"speed"}, {{"thick", "Fill / outline (1 is filled)"}, {"count", "Sides"}}, {{"size", 0.8f}, {"thick", 0.12f}, {"count", 6}}},
        {"Hex grid", "Shape", "A honeycomb of lines", {"speed"}, {{"thick", "Line width"}, {"count", "Cells"}}, {{"size", 1.0f}, {"thick", 0.3f}, {"soft", 0.3f}, {"count", 10}}},
        {"Grid", "Shape", "Square grid lines", {"speed"}, {{"thick", "Line width"}, {"count", "Lines"}}, {{"size", 1.0f}, {"thick", 0.15f}, {"count", 10}}},
        {"Stripes", "Shape", "Bars that can scroll", {}, {{"thick", "Stripe width"}, {"count", "Stripes"}, {"speed", "Scroll speed"}}, {{"size", 1.0f}, {"thick", 0.4f}, {"count", 12}, {"speed", 0.5f}}},
        {"Speed lines", "Shape", "Streaks flying outward", {}, {{"thick", "Line width"}, {"count", "Lines"}, {"speed", "Speed"}}, {{"size", 1.0f}, {"thick", 0.25f}, {"soft", 0.4f}, {"count", 24}, {"speed", 1.5f}}},
        {"Wave", "Shape", "A rippling ribbon", {}, {{"thick", "Line width"}, {"count", "Waves"}, {"speed", "Wave speed"}}, {{"size", 1.0f}, {"thick", 0.08f}, {"soft", 0.3f}, {"count", 3}, {"speed", 1.0f}}},
        {"Beam", "Shape", "A vertical beam of light", {"count", "speed"}, {{"thick", "Beam width"}, {"soft", "Fade at the ends"}}, {{"size", 1.0f}, {"thick", 0.2f}, {"soft", 0.5f}}},
        {"Radar sweep", "Shape", "A turning wedge with a trail", {"count"}, {{"thick", "Trail length"}, {"speed", "Sweep speed"}}, {{"size", 0.95f}, {"thick", 0.5f}, {"soft", 0.15f}, {"speed", 0.8f}}},
        {"Lightning", "Shape", "A jagged bolt", {}, {{"thick", "Bolt width"}, {"count", "Jaggedness"}, {"speed", "Flicker speed"}}, {{"size", 1.0f}, {"thick", 0.2f}, {"count", 3}, {"speed", 1.4f}}},
        {"Orbiting dots", "Shape", "Dots circling the middle", {}, {{"thick", "Dot size"}, {"soft", "Trail glow"}, {"count", "Dots"}, {"speed", "Orbit speed"}}, {{"size", 0.85f}, {"thick", 0.15f}, {"soft", 0.5f}, {"count", 6}, {"speed", 0.9f}}},
        {"Shockwave", "Shape", "A ring that expands and fades, again and again", {"count"}, {{"thick", "Ring width"}, {"speed", "Expand speed"}}, {{"size", 1.0f}, {"thick", 0.15f}, {"soft", 0.5f}, {"speed", 0.9f}}},
        {"Linear gradient", "Gradient", "Colour A to colour B across the layer", {"thick", "soft", "count"}, {{"speed", "Scroll speed"}}, {{"size", 1.0f}, {"speed", 0.0f}}},
        {"Radial gradient", "Gradient", "Colour B in the middle fading to colour A", {"soft", "count", "speed"}, {{"thick", "Falloff"}}, {{"size", 1.0f}, {"thick", 0.3f}}},
        {"Angular gradient", "Gradient", "Colour going round the middle", {"thick", "soft", "count"}, {{"speed", "Scroll speed"}}, {{"size", 1.0f}, {"speed", 0.5f}}},
        {"Fresnel glow", "Gradient", "Glow that gathers at the edge of the object", {"thick", "soft", "size", "aspect", "rot", "spin", "speed", "warp", "twist", "ox", "oy", "space"}, {{"count", "Edge sharpness"}}, {{"count", 4}}},
        {"Height gradient", "Gradient", "Colour changes from the bottom of the object to the top", {"thick", "soft", "count", "aspect", "rot", "spin", "warp", "twist", "ox", "space"}, {{"size", "Height range"}, {"oy", "Height offset"}, {"speed", "Scroll speed"}}, {{"size", 1.0f}, {"speed", 0.0f}}},
        {"Noise", "Pattern", "Moving cloudy noise", {}, {{"thick", "Amount shown"}, {"soft", "Edge softness"}, {"count", "Scale"}, {"speed", "Flow speed"}}, {{"size", 1.0f}, {"thick", 0.6f}, {"soft", 0.4f}, {"count", 6}, {"speed", 0.5f}}},
        {"Cells", "Pattern", "Cell lines, like cracked ground", {}, {{"thick", "Line width"}, {"count", "Cells"}, {"speed", "Drift speed"}}, {{"size", 1.0f}, {"thick", 0.4f}, {"count", 6}, {"speed", 0.3f}}},
        {"Plasma", "Pattern", "Slow rolling colour", {"thick", "soft"}, {{"count", "Scale"}, {"speed", "Flow speed"}}, {{"size", 1.0f}, {"count", 6}, {"speed", 1.0f}}},
        {"Halftone dots", "Pattern", "A grid of dots", {"speed"}, {{"thick", "Dot size"}, {"count", "Dots across"}}, {{"size", 1.0f}, {"thick", 0.6f}, {"count", 12}}},
        {"Checker", "Pattern", "A chequerboard", {"thick", "soft", "speed"}, {{"count", "Squares"}}, {{"size", 1.0f}, {"count", 8}}},
        {"Solid colour", "Pattern", "A flat colour over the whole object", {"thick", "soft", "count", "speed", "size", "aspect", "rot", "spin", "warp", "twist", "ox", "oy", "space"}, {}, {}},
        {"Arc ring", "Sci-fi", "A ring broken into arcs of different lengths", {}, {{"thick", "Ring width"}, {"count", "Arcs"}, {"speed", "Turning"}}, {{"size", 0.9f}, {"thick", 0.08f}, {"soft", 0.3f}, {"count", 7}, {"speed", 0.6f}}},
        {"Tick ring", "Sci-fi", "Short tick marks standing on a ring, like a dial", {"speed"}, {{"thick", "Tick length"}, {"count", "Ticks"}}, {{"size", 0.95f}, {"thick", 0.1f}, {"soft", 0.2f}, {"count", 48}}},
        {"Light column", "Sci-fi", "A column of fine vertical streaks that climb", {}, {{"thick", "Streak width"}, {"count", "Streaks"}, {"speed", "Climb speed"}}, {{"size", 1.0f}, {"thick", 0.25f}, {"soft", 0.3f}, {"count", 20}, {"speed", 0.8f}}},
        {"Funnel", "Sci-fi", "A funnel seen from the side: swirled rings narrowing to a point", {}, {{"thick", "Ring width"}, {"count", "Rings"}, {"speed", "Swirl speed"}}, {{"size", 1.0f}, {"thick", 1.0f}, {"soft", 0.4f}, {"count", 10}, {"speed", 0.8f}}},
        {"Corner brackets", "Sci-fi", "Four corner brackets of a square frame", {"speed"}, {{"thick", "Line width"}, {"count", "Bracket length (0 to 9)"}}, {{"size", 0.9f}, {"thick", 0.05f}, {"soft", 0.2f}, {"count", 6}}},
    };
    return all;
}

std::vector<Family> buildFamilies() {
    const Tables t = buildTables();
    struct Info {
        const char* id;
        const char* code;
        int number;
        const char* name;
        const char* summary;
        std::vector<std::string> tags;
        std::vector<std::string> animations;
        std::vector<std::string> limits;
        const char* shape;
        bool cullOff, zwrite, ui, particle;
        std::vector<Preset> presets;
    };
    const std::vector<Info> infos = {
        {"emissive", "EM", 11, "Emissive / Glow", "Pulsing glow, rim light and flowing energy veins",
         {"Casual", "Stylized", "VFX", "Mobile"},
         {"Pulse", "Flow", "Rotate", "Flicker", "Dissolve", "Noise / turbulence"},
         {"Bloom comes from your camera's post-processing; the shader only makes the emission bright (HDR).",
          "The pattern is procedural (no texture), so it follows the object's own coordinates."},
         "sphere", false, false, false, false, emissivePresets()},
        {"hologram", "HG", 12, "Hologram / Energy", "Scanlines, fresnel, projection and glitch for sci-fi effects",
         {"Stylized", "VFX", "AAA", "Mobile"},
         {"Scroll", "Flow", "Rotate", "Flicker", "Dissolve", "Expand"},
         {"Scanlines follow the object's own height, so the object should be about as tall as it is wide for the bar to cross it evenly.",
          "The glitch moves the pattern sideways; it does not move the mesh."},
         "sphere", true, false, false, false, hologramPresets()},
        {"shield", "FF", 14, "Force Field / Shield", "A dome with a rim, hexagon pattern, ripples and impact reactions",
         {"Stylized", "VFX", "AAA"},
         {"Pulse", "Expand", "Hit reaction", "Scroll", "Dissolve", "Noise / turbulence"},
         {"The surface wobble is a colour effect; the mesh itself does not move.",
          "There is no refraction of what is behind the shield.",
          "An impact needs a script call (VFXForgeShaderAnimator.Hit); the demo scene repeats one for you."},
         "sphere", true, false, false, false, shieldPresets()},
        {"particle", "PV", 15, "Particle / VFX material", "Fire, smoke, sparks, swirls and dust drawn on a particle quad",
         {"Casual", "Stylized", "VFX", "Mobile"},
         {"Flow", "Rotate", "Dissolve", "Noise / turbulence", "Flicker"},
         {"This is the material only. It does not spawn, move or time particles: use Unity's Particle System (VFX Forge's own export makes those).",
          "For lifetime colours, add the AgePercent custom vertex stream to the Particle System (see the setup notes).",
          "No flipbook sheet is read: the patterns are drawn by the shader."},
         "particle", true, false, false, true, particlePresets()},
        {"ui", "UI", 16, "UI / Sprite", "Glow, a sweeping shine, outline and frames for Canvas sprites",
         {"Casual", "UI", "Mobile"},
         {"Scroll", "Pulse", "Rotate", "Dissolve", "Flicker"},
         {"The preview draws a stand-in shape; in Unity the shader uses your sprite's own alpha (or draws a rounded border).",
          "Glow and outline come from sampling the sprite around each pixel, so a sprite needs transparent space round it.",
          "Works on a Canvas (masking, clip rect, stencil); it does not draw in a world-space mesh renderer."},
         "star", true, false, true, false, uiPresets()},
        {"ice", "IC", 18, "Ice / Snow", "Ice with facets, frost, cracks, snow and melting",
         {"Stylized", "Realistic", "AAA"},
         {"Dissolve", "Noise / turbulence", "Pulse", "Flow"},
         {"Refraction is a look made with the facets and a moving pattern; it does not bend what is behind the ice.",
          "Cracks are drawn as a fixed pattern; they do not grow over time.",
          "Melting fades the object from the top down; it does not deform the mesh.",
          "Light is a fixed direction, not Unity's lights."},
         "crystal", false, false, false, false, icePresets()},
        {"layers", "LY", 20, "Layer Shader", "Build your own: stack shapes, gradients and patterns, then animate and export",
         {"Casual", "Stylized", "VFX", "Mobile", "AAA"},
         {"Pulse", "Flow", "Rotate", "Dissolve", "Expand", "Keyframes"},
         {"Up to eight layers; no imported textures, masks or flipbooks yet: every element is drawn by the shader.",
          "Layers cannot be grouped or nested, and there is no node graph yet.",
          "Each layer draws in one 2D space laid on the object (mesh UV, front, top or wrap); it is not a per-vertex deformation.",
          "Bloom comes from your camera's post-processing; the shader only makes the colours bright (HDR)."},
         "sphere", true, false, false, false, layerPresets()},
    };
    std::vector<Family> out;
    for (const Info& i : infos) {
        Family f;
        f.id = i.id;
        f.code = i.code;
        f.number = i.number;
        f.name = i.name;
        f.summary = i.summary;
        f.tags = i.tags;
        f.animations = i.animations;
        f.limits = i.limits;
        f.function = std::string("shade_") + i.code;
        f.previewShape = i.shape;
        f.cullOff = i.cullOff;
        f.zwrite = i.zwrite;
        f.isUi = i.ui;
        f.isParticle = i.particle;
        f.presets = i.presets;
        f.params = t.common;
        for (const auto& own : t.own) {
            if (own.first == i.code) f.params.insert(f.params.end(), own.second.begin(), own.second.end());
        }
        for (std::size_t n = 0; n < f.params.size(); ++n) f.params[n].index = static_cast<int>(n);
        out.push_back(std::move(f));
    }
    return out;
}

float clampValue(const ParamDef& p, float v) {
    if (!std::isfinite(v)) return static_cast<float>(p.def);
    if (p.kind == ParamKind::Choice) return std::clamp(std::round(v), 0.0f, static_cast<float>(p.choices.size() - 1));
    return std::clamp(v, static_cast<float>(p.min), static_cast<float>(p.max));
}

const char* loopName(LoopMode m) { return m == LoopMode::Loop ? "loop" : m == LoopMode::PingPong ? "pingpong" : "once"; }
const char* easeName(Easing e) {
    return e == Easing::Linear ? "linear" : e == Easing::Smooth ? "smooth" : e == Easing::EaseIn ? "easeIn" : "easeOut";
}

}  // namespace

// ---------------------------------------------------------------- layer operations
namespace {

int layerBase(const Family& f) {
    const ParamDef* p = findParam(f, "L0_el");
    return p ? p->index : -1;
}

// "L3_size" -> 3, anything else -> -1.
int trackLayerOf(const std::string& key) {
    if (key.size() < 3 || key[0] != 'L' || key[1] < '0' || key[1] > '9' || key[2] != '_') return -1;
    return key[1] - '0';
}

std::string withLayer(const std::string& key, int layer) { return "L" + std::to_string(layer) + key.substr(2); }

void resetLayer(Settings& s, const Family& f, int slot) {
    const int base = layerBase(f) + slot * kLayerStride;
    const Settings d = defaultSettings(f);
    for (int k = 0; k < kLayerStride; ++k) s.values[static_cast<std::size_t>(base + k)] = d.values[static_cast<std::size_t>(base + k)];
    if (static_cast<std::size_t>(slot) < s.layerNames.size()) s.layerNames[static_cast<std::size_t>(slot)].clear();
}

void copyLayer(Settings& s, const Family& f, int from, int to) {
    const int a = layerBase(f) + from * kLayerStride, b = layerBase(f) + to * kLayerStride;
    for (int k = 0; k < kLayerStride; ++k) s.values[static_cast<std::size_t>(b + k)] = s.values[static_cast<std::size_t>(a + k)];
    if (s.layerNames.size() < static_cast<std::size_t>(kLayerCount)) s.layerNames.resize(static_cast<std::size_t>(kLayerCount));
    s.layerNames[static_cast<std::size_t>(to)] = s.layerNames[static_cast<std::size_t>(from)];
}

// Renames the tracks of layers by a map from old slot to new slot (-1 drops the track).
template <class Map>
void remapTracks(Settings& s, Map map) {
    std::vector<Track> out;
    for (Track& t : s.animation.tracks) {
        const int layer = trackLayerOf(t.param);
        if (layer < 0) {
            out.push_back(std::move(t));
            continue;
        }
        const int to = map(layer);
        if (to < 0) continue;
        t.param = withLayer(t.param, to);
        out.push_back(std::move(t));
    }
    s.animation.tracks = std::move(out);
}

}  // namespace

int layerTop(const Settings& s) {
    const Family* f = findFamily(s.family);
    if (!f || f->id != "layers") return 0;
    const int base = layerBase(*f);
    int top = 0;
    for (int i = 0; i < kLayerCount; ++i) {
        const std::size_t at = static_cast<std::size_t>(base + i * kLayerStride);
        if (at < s.values.size() && s.values[at][0] > 0.5f) top = i + 1;
    }
    return top;
}

std::string layerName(const Settings& s, int slot) {
    if (slot < 0 || slot >= kLayerCount) return {};
    if (static_cast<std::size_t>(slot) < s.layerNames.size() && !s.layerNames[static_cast<std::size_t>(slot)].empty()) return s.layerNames[static_cast<std::size_t>(slot)];
    const Family* f = findFamily(s.family);
    if (!f) return {};
    const int el = static_cast<int>(s.values[static_cast<std::size_t>(layerBase(*f) + slot * kLayerStride)][0] + 0.5f);
    const auto& list = layerElements();
    return el >= 0 && static_cast<std::size_t>(el) < list.size() ? list[static_cast<std::size_t>(el)].name : std::string();
}

bool layerAdd(Settings& s, int element) {
    const Family* f = findFamily(s.family);
    if (!f || f->id != "layers" || element < 1 || static_cast<std::size_t>(element) >= layerElements().size()) return false;
    const int slot = layerTop(s);
    if (slot >= kLayerCount) return false;
    resetLayer(s, *f, slot);
    const auto set = [&](const char* key, float v) {
        const int at = layerParamIndex(*f, slot, key);
        if (at >= 0) s.values[static_cast<std::size_t>(at)][0] = v;
    };
    set("el", static_cast<float>(element));
    for (const auto& [key, value] : layerElements()[static_cast<std::size_t>(element)].defaults) set(key.c_str(), value);
    return true;
}

bool layerRemove(Settings& s, int slot) {
    const Family* f = findFamily(s.family);
    const int top = layerTop(s);
    if (!f || slot < 0 || slot >= top) return false;
    for (int i = slot; i + 1 < kLayerCount; ++i) copyLayer(s, *f, i + 1, i);
    resetLayer(s, *f, kLayerCount - 1);
    remapTracks(s, [&](int l) { return l == slot ? -1 : (l > slot ? l - 1 : l); });
    return true;
}

bool layerDuplicate(Settings& s, int slot) {
    const Family* f = findFamily(s.family);
    const int top = layerTop(s);
    if (!f || slot < 0 || slot >= top || top >= kLayerCount) return false;
    // Tracks of the copied layer are copied too, before anything moves.
    std::vector<Track> copies;
    for (const Track& t : s.animation.tracks) {
        if (trackLayerOf(t.param) == slot) {
            Track c = t;
            c.param = withLayer(t.param, slot + 1);
            copies.push_back(std::move(c));
        }
    }
    for (int i = top - 1; i > slot; --i) copyLayer(s, *f, i, i + 1);
    copyLayer(s, *f, slot, slot + 1);
    remapTracks(s, [&](int l) { return l > slot ? l + 1 : l; });
    for (Track& c : copies) s.animation.tracks.push_back(std::move(c));
    if (static_cast<std::size_t>(slot + 1) < s.layerNames.size() && !s.layerNames[static_cast<std::size_t>(slot + 1)].empty()) s.layerNames[static_cast<std::size_t>(slot + 1)] += " copy";
    return true;
}

bool layerMove(Settings& s, int slot, int delta) {
    const Family* f = findFamily(s.family);
    const int top = layerTop(s);
    const int other = slot + delta;
    if (!f || slot < 0 || slot >= top || other < 0 || other >= top || delta == 0) return false;
    const int scratch = kLayerCount - 1;
    if (top >= kLayerCount) {
        // No spare slot: swap through a copy of one layer's values.
        std::vector<std::array<float, 4>> hold;
        const int a = layerBase(*f) + slot * kLayerStride;
        for (int k = 0; k < kLayerStride; ++k) hold.push_back(s.values[static_cast<std::size_t>(a + k)]);
        const std::string name = static_cast<std::size_t>(slot) < s.layerNames.size() ? s.layerNames[static_cast<std::size_t>(slot)] : std::string();
        copyLayer(s, *f, other, slot);
        const int b = layerBase(*f) + other * kLayerStride;
        for (int k = 0; k < kLayerStride; ++k) s.values[static_cast<std::size_t>(b + k)] = hold[static_cast<std::size_t>(k)];
        s.layerNames[static_cast<std::size_t>(other)] = name;
    } else {
        copyLayer(s, *f, slot, scratch);
        copyLayer(s, *f, other, slot);
        copyLayer(s, *f, scratch, other);
        resetLayer(s, *f, scratch);
    }
    remapTracks(s, [&](int l) { return l == slot ? other : (l == other ? slot : l); });
    return true;
}

const std::vector<ElementInfo>& layerElements() { return buildElements(); }

const std::vector<std::string>& layerBlendNames() {
    static const std::vector<std::string> names = {"Add", "Alpha", "Multiply", "Screen", "Mask", "Erase"};
    return names;
}

int layerParamIndex(const Family& family, int layer, const std::string& key) {
    if (layer < 0 || layer >= kLayerCount) return -1;
    const ParamDef* p = findParam(family, "L" + std::to_string(layer) + "_" + key);
    return p ? p->index : -1;
}

const std::vector<Family>& families() {
    static const std::vector<Family> all = buildFamilies();
    return all;
}

const Family* findFamily(const std::string& id) {
    for (const Family& f : families()) {
        if (f.id == id) return &f;
    }
    return nullptr;
}
const Preset* findPreset(const Family& family, const std::string& id) {
    for (const Preset& p : family.presets) {
        if (p.id == id) return &p;
    }
    return nullptr;
}
const ParamDef* findParam(const Family& family, const std::string& key) {
    for (const ParamDef& p : family.params) {
        if (p.key == key) return &p;
    }
    return nullptr;
}

Settings defaultSettings(const Family& family) {
    Settings s;
    s.family = family.id;
    s.name = family.name;
    s.values.assign(family.params.size(), std::array<float, 4>{0, 0, 0, 0});
    for (const ParamDef& p : family.params) {
        s.values[static_cast<std::size_t>(p.index)] =
            p.kind == ParamKind::Color ? p.colour : std::array<float, 4>{static_cast<float>(p.def), 0, 0, 0};
    }
    s.shape = family.previewShape;
    s.blend = family.presets.empty() ? BlendMode::Additive : family.presets.front().blend;
    return s;
}

Settings presetSettings(const Family& family, const Preset& preset) {
    Settings s = defaultSettings(family);
    s.preset = preset.id;
    s.name = preset.name;
    s.shape = preset.shape;
    s.blend = preset.blend;
    for (const auto& [key, value] : preset.values) {
        if (const ParamDef* p = findParam(family, key)) {
            std::array<float, 4> v = value;
            if (p->kind != ParamKind::Color) v = {clampValue(*p, value[0]), 0, 0, 0};
            s.values[static_cast<std::size_t>(p->index)] = v;
        }
    }
    return s;
}

float ease(Easing e, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (e) {
        case Easing::Linear: return t;
        case Easing::Smooth: return t * t * (3.0f - 2.0f * t);
        case Easing::EaseIn: return t * t;
        case Easing::EaseOut: return 1.0f - (1.0f - t) * (1.0f - t);
    }
    return t;
}

double animationTime(const Animation& a, double playhead) {
    const double d = std::max(a.duration, 0.01);
    playhead = std::max(playhead, 0.0);
    switch (a.loop) {
        case LoopMode::Loop: return playhead - d * std::floor(playhead / d);
        case LoopMode::PingPong: {
            const double m = playhead - 2.0 * d * std::floor(playhead / (2.0 * d));
            return m <= d ? m : 2.0 * d - m;
        }
        case LoopMode::Once: return std::min(playhead, d);
    }
    return playhead;
}

std::vector<std::array<float, 4>> valuesAt(const Settings& s, double time) {
    std::vector<std::array<float, 4>> out = s.values;
    const Family* family = findFamily(s.family);
    if (!family) return out;
    for (const Track& track : s.animation.tracks) {
        const ParamDef* p = findParam(*family, track.param);
        if (!p || p->kind == ParamKind::Color || track.keys.empty()) continue;
        const auto& keys = track.keys;
        float v = keys.front().value;
        if (time >= keys.back().time) {
            v = keys.back().value;
        } else if (time > keys.front().time) {
            for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
                if (time >= keys[i].time && time < keys[i + 1].time) {
                    const double span = std::max(keys[i + 1].time - keys[i].time, 1e-9);
                    const float k = ease(keys[i].easing, static_cast<float>((time - keys[i].time) / span));
                    v = keys[i].value + (keys[i + 1].value - keys[i].value) * k;
                    break;
                }
            }
        }
        out[static_cast<std::size_t>(p->index)][0] = clampValue(*p, v);
    }
    return out;
}

std::string writeSettings(const Settings& s) {
    J j;
    j["format"] = "vfxforge.shader";
    j["formatVersion"] = 1;
    j["name"] = s.name;
    j["family"] = s.family;
    j["preset"] = s.preset;
    j["blend"] = s.blend == BlendMode::Additive ? "additive" : "alpha";
    j["shape"] = s.shape;
    j["quality"] = s.quality;
    const Family* f = findFamily(s.family);
    J values = J::object();
    if (f) {
        for (const ParamDef& p : f->params) {
            if (static_cast<std::size_t>(p.index) >= s.values.size()) continue;
            const auto& v = s.values[static_cast<std::size_t>(p.index)];
            if (p.kind == ParamKind::Color) {
                values[p.key] = J::array({v[0], v[1], v[2], v[3]});
            } else {
                values[p.key] = v[0];
            }
        }
    }
    j["values"] = values;
    if (!s.layerNames.empty()) j["layerNames"] = s.layerNames;
    J anim;
    anim["duration"] = s.animation.duration;
    anim["loop"] = loopName(s.animation.loop);
    anim["speed"] = s.animation.speed;
    J tracks = J::array();
    for (const Track& t : s.animation.tracks) {
        J keys = J::array();
        for (const Key& k : t.keys) keys.push_back({{"time", k.time}, {"value", k.value}, {"ease", easeName(k.easing)}});
        tracks.push_back({{"param", t.param}, {"keys", keys}});
    }
    anim["tracks"] = tracks;
    j["animation"] = anim;
    return j.dump(2) + "\n";
}

Result<ReadSettings> readSettings(const std::string& text) {
    J j = J::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return makeError("That file is not a VFX Forge shader.", "not json");
    if (j.value("format", "") != "vfxforge.shader") return makeError("That file is not a VFX Forge shader.", "wrong format");
    const Family* f = findFamily(j.value("family", ""));
    if (!f) return makeError("That shader uses a family this version does not know: " + j.value("family", "") + ".", "unknown family");
    ReadSettings out;
    Settings s = defaultSettings(*f);
    s.name = j.value("name", s.name);
    s.preset = j.value("preset", "");
    s.shape = j.value("shape", s.shape);
    s.blend = j.value("blend", "additive") == "alpha" ? BlendMode::Alpha : BlendMode::Additive;
    s.quality = std::clamp(j.value("quality", 1), 0, 3);
    if (j.contains("values") && j["values"].is_object()) {
        for (auto it = j["values"].begin(); it != j["values"].end(); ++it) {
            const ParamDef* p = findParam(*f, it.key());
            if (!p) {
                out.notes.push_back("Ignored a setting this version does not have: " + it.key() + ".");
                continue;
            }
            std::array<float, 4> v = s.values[static_cast<std::size_t>(p->index)];
            if (p->kind == ParamKind::Color && it.value().is_array() && it.value().size() >= 3) {
                for (std::size_t i = 0; i < 4 && i < it.value().size(); ++i) v[i] = std::clamp(it.value()[i].get<float>(), 0.0f, 16.0f);
            } else if (p->kind != ParamKind::Color && it.value().is_number()) {
                const float raw = it.value().get<float>();
                v = {clampValue(*p, raw), 0, 0, 0};
                if (v[0] != raw) out.notes.push_back(p->label + " was out of range and was brought back to its limit.");
            } else {
                out.notes.push_back(p->label + " could not be read; its default was kept.");
            }
            s.values[static_cast<std::size_t>(p->index)] = v;
        }
    }
    if (j.contains("layerNames") && j["layerNames"].is_array()) {
        for (const J& n : j["layerNames"]) {
            if (n.is_string() && s.layerNames.size() < static_cast<std::size_t>(kLayerCount)) s.layerNames.push_back(n.get<std::string>().substr(0, 40));
        }
    }
    if (j.contains("animation") && j["animation"].is_object()) {
        const J& a = j["animation"];
        s.animation.duration = std::clamp(a.value("duration", 4.0), 0.1, 600.0);
        const std::string loop = a.value("loop", "loop");
        s.animation.loop = loop == "pingpong" ? LoopMode::PingPong : loop == "once" ? LoopMode::Once : LoopMode::Loop;
        s.animation.speed = std::clamp(a.value("speed", 1.0), 0.05, 8.0);
        if (a.contains("tracks") && a["tracks"].is_array()) {
            for (const J& t : a["tracks"]) {
                const ParamDef* p = findParam(*f, t.value("param", ""));
                if (!p || p->kind == ParamKind::Color || !t.contains("keys") || !t["keys"].is_array()) {
                    out.notes.push_back("Ignored an animation track that could not be used.");
                    continue;
                }
                Track track;
                track.param = p->key;
                for (const J& k : t["keys"]) {
                    Key key;
                    key.time = std::clamp(k.value("time", 0.0), 0.0, s.animation.duration);
                    key.value = clampValue(*p, k.value("value", static_cast<float>(p->def)));
                    const std::string e = k.value("ease", "smooth");
                    key.easing = e == "linear" ? Easing::Linear : e == "easeIn" ? Easing::EaseIn : e == "easeOut" ? Easing::EaseOut : Easing::Smooth;
                    track.keys.push_back(key);
                }
                std::stable_sort(track.keys.begin(), track.keys.end(), [](const Key& l, const Key& r) { return l.time < r.time; });
                if (!track.keys.empty()) s.animation.tracks.push_back(std::move(track));
            }
        }
    }
    out.settings = std::move(s);
    return out;
}

std::vector<std::string> validateSettings(const Settings& s) {
    std::vector<std::string> out;
    const Family* f = findFamily(s.family);
    if (!f) {
        out.push_back("The shader family is not known.");
        return out;
    }
    if (s.values.size() != f->params.size()) out.push_back("The settings do not match the family's parameter list.");
    for (const ParamDef& p : f->params) {
        if (static_cast<std::size_t>(p.index) >= s.values.size()) continue;
        const auto& v = s.values[static_cast<std::size_t>(p.index)];
        if (p.kind != ParamKind::Color && (v[0] < p.min - 1e-4 || v[0] > p.max + 1e-4 || !std::isfinite(v[0]))) {
            out.push_back(p.label + " is outside its limits (" + std::to_string(p.min) + " to " + std::to_string(p.max) + ").");
        }
    }
    if (s.quality == 0) {
        if (f->id == "ice") out.push_back("Mobile profile: Ice leaves out the cracks.");
        if (f->id == "shield") out.push_back("Mobile profile: the Force Field draws one hexagon projection instead of three blended ones.");
        out.push_back("Mobile profile: the noise has fewer layers, so patterns look a little simpler.");
    }
    if (f->isUi && s.values.size() > 0) {
        out.push_back("UI: assign your sprite to the material's Sprite slot in Unity (or set Shape from: Border).");
    }
    if (f->isParticle) {
        out.push_back("Particle: add the AgePercent custom vertex stream to the Particle System for the colour over life to follow each particle.");
    }
    return out;
}

const std::vector<Quality>& qualityProfiles() {
    static const std::vector<Quality> q = {
        {"Mobile", "Fewest noise layers; leaves out the most expensive parts (Ice cracks, extra hexagon projections).", 2},
        {"Standard", "The balanced profile for most games.", 3},
        {"High", "Full noise detail.", 4},
        {"Cinematic", "Full detail; use for close-ups and capture.", 4},
    };
    return q;
}

}  // namespace vfx::editor::shader
