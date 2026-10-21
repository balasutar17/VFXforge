#include <string_view>
#include <vector>

#include "vfx/Program.h"

namespace vfx {

namespace {

struct BlendInfo {
    BlendMode mode;
    const char* name;
    const char* label;
    int group;
};

// In the order of Photoshop's menu.
const BlendInfo kBlends[] = {
    {BlendMode::Alpha, "alpha", "Normal", 0},
    {BlendMode::Dissolve, "dissolve", "Dissolve", 0},
    {BlendMode::Darken, "darken", "Darken", 1},
    {BlendMode::Multiply, "multiply", "Multiply", 1},
    {BlendMode::ColorBurn, "colorburn", "Color Burn", 1},
    {BlendMode::LinearBurn, "linearburn", "Linear Burn", 1},
    {BlendMode::DarkerColor, "darkercolor", "Darker Color", 1},
    {BlendMode::Lighten, "lighten", "Lighten", 2},
    {BlendMode::Screen, "screen", "Screen", 2},
    {BlendMode::ColorDodge, "colordodge", "Color Dodge", 2},
    {BlendMode::Additive, "additive", "Linear Dodge (Add)", 2},
    {BlendMode::LighterColor, "lightercolor", "Lighter Color", 2},
    {BlendMode::Overlay, "overlay", "Overlay", 3},
    {BlendMode::SoftLight, "softlight", "Soft Light", 3},
    {BlendMode::HardLight, "hardlight", "Hard Light", 3},
    {BlendMode::VividLight, "vividlight", "Vivid Light", 3},
    {BlendMode::LinearLight, "linearlight", "Linear Light", 3},
    {BlendMode::PinLight, "pinlight", "Pin Light", 3},
    {BlendMode::HardMix, "hardmix", "Hard Mix", 3},
    {BlendMode::Difference, "difference", "Difference", 4},
    {BlendMode::Exclusion, "exclusion", "Exclusion", 4},
    {BlendMode::Subtract, "subtract", "Subtract", 4},
    {BlendMode::Divide, "divide", "Divide", 4},
    {BlendMode::Hue, "hue", "Hue", 5},
    {BlendMode::Saturation, "saturation", "Saturation", 5},
    {BlendMode::Color, "color", "Color", 5},
    {BlendMode::Luminosity, "luminosity", "Luminosity", 5},
};

const BlendInfo& infoFor(BlendMode mode) {
    for (const BlendInfo& b : kBlends) {
        if (b.mode == mode) {
            return b;
        }
    }
    return kBlends[0];
}

}  // namespace

const std::vector<BlendMode>& blendModes() {
    static const std::vector<BlendMode> all = [] {
        std::vector<BlendMode> list;
        for (const BlendInfo& b : kBlends) {
            list.push_back(b.mode);
        }
        return list;
    }();
    return all;
}

const char* blendName(BlendMode mode) { return infoFor(mode).name; }

BlendMode blendFromName(std::string_view name) {
    for (const BlendInfo& b : kBlends) {
        if (name == b.name) {
            return b.mode;
        }
    }
    return BlendMode::Alpha;
}

const char* blendLabel(BlendMode mode) { return infoFor(mode).label; }

int blendGroup(BlendMode mode) { return infoFor(mode).group; }

bool blendByCard(BlendMode mode) {
    switch (mode) {
        case BlendMode::Alpha:
        case BlendMode::Additive:
        case BlendMode::Screen:
        case BlendMode::Multiply:
        case BlendMode::Exclusion:
            return true;
        default:
            return false;
    }
}

BlendMode blendForUnity(BlendMode mode) {
    switch (mode) {
        case BlendMode::Additive:
        case BlendMode::Lighten:
        case BlendMode::Screen:
        case BlendMode::ColorDodge:
        case BlendMode::LighterColor:
            return BlendMode::Additive;
        default:
            return BlendMode::Alpha;
    }
}

}  // namespace vfx
