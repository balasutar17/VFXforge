#include "vfx/editor/Blend.h"

#include <algorithm>
#include <cmath>

namespace vfx::editor {

namespace {

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// The separable modes work on one channel at a time: d is what is behind,
// s the particle's own colour, both from 0 to 1.
float colorBurn(float d, float s) {
    if (d >= 1.0f) return 1.0f;
    if (s <= 0.0f) return 0.0f;
    return 1.0f - std::min(1.0f, (1.0f - d) / s);
}

float colorDodge(float d, float s) {
    if (d <= 0.0f) return 0.0f;
    if (s >= 1.0f) return 1.0f;
    return std::min(1.0f, d / (1.0f - s));
}

float overlay(float d, float s) {  // Hard Light with the two swapped
    return d <= 0.5f ? 2.0f * d * s : 1.0f - 2.0f * (1.0f - d) * (1.0f - s);
}

float softLight(float d, float s) {
    if (s <= 0.5f) {
        return d - (1.0f - 2.0f * s) * d * (1.0f - d);
    }
    const float lift = d <= 0.25f ? ((16.0f * d - 12.0f) * d + 4.0f) * d : std::sqrt(d);
    return d + (2.0f * s - 1.0f) * (lift - d);
}

float vividLight(float d, float s) {
    return s <= 0.5f ? colorBurn(d, 2.0f * s) : colorDodge(d, 2.0f * (s - 0.5f));
}

float pinLight(float d, float s) {
    return s < 0.5f ? std::min(d, 2.0f * s) : std::max(d, 2.0f * s - 1.0f);
}

float divide(float d, float s) {
    if (s <= 1e-6f) return d > 0.0f ? 1.0f : 0.0f;
    return std::min(1.0f, d / s);
}

float separable(BlendMode mode, float d, float s) {
    switch (mode) {
        case BlendMode::Darken: return std::min(d, s);
        case BlendMode::ColorBurn: return colorBurn(d, s);
        case BlendMode::LinearBurn: return clamp01(d + s - 1.0f);
        case BlendMode::Lighten: return std::max(d, s);
        case BlendMode::ColorDodge: return colorDodge(d, s);
        case BlendMode::Overlay: return overlay(d, s);
        case BlendMode::SoftLight: return softLight(d, s);
        case BlendMode::HardLight: return overlay(s, d);
        case BlendMode::VividLight: return vividLight(d, s);
        case BlendMode::LinearLight: return clamp01(d + 2.0f * s - 1.0f);
        case BlendMode::PinLight: return pinLight(d, s);
        case BlendMode::HardMix: return d + s >= 1.0f ? 1.0f : 0.0f;
        case BlendMode::Difference: return std::fabs(d - s);
        case BlendMode::Subtract: return std::max(0.0f, d - s);
        case BlendMode::Divide: return divide(d, s);
        default: return s;
    }
}

// The colour modes work on the whole colour, by hue, saturation and
// luminosity (the formulas of the W3C compositing specification, which are
// Photoshop's, too).
float lum(const float c[3]) { return 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2]; }

void clipColor(float c[3]) {
    const float l = lum(c);
    const float n = std::min({c[0], c[1], c[2]}), x = std::max({c[0], c[1], c[2]});
    if (n < 0.0f && l - n > 1e-9f) {
        for (int i = 0; i < 3; ++i) c[i] = l + (c[i] - l) * l / (l - n);
    }
    if (x > 1.0f && x - l > 1e-9f) {
        for (int i = 0; i < 3; ++i) c[i] = l + (c[i] - l) * (1.0f - l) / (x - l);
    }
}

void setLum(float c[3], float l) {
    const float shift = l - lum(c);
    for (int i = 0; i < 3; ++i) c[i] += shift;
    clipColor(c);
}

float sat(const float c[3]) { return std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]}); }

void setSat(float c[3], float s) {
    int hi = 0, lo = 0;
    for (int i = 1; i < 3; ++i) {
        if (c[i] > c[hi]) hi = i;
        if (c[i] < c[lo]) lo = i;
    }
    if (hi == lo) {
        c[0] = c[1] = c[2] = 0.0f;
        return;
    }
    const int mid = 3 - hi - lo;
    const float range = c[hi] - c[lo];
    c[mid] = (c[mid] - c[lo]) * s / range;
    c[hi] = s;
    c[lo] = 0.0f;
}

void colorMode(BlendMode mode, const float d[3], const float s[3], float out[3]) {
    float c[3];
    switch (mode) {
        case BlendMode::Hue:
            std::copy(s, s + 3, c);
            setSat(c, sat(d));
            setLum(c, lum(d));
            break;
        case BlendMode::Saturation:
            std::copy(d, d + 3, c);
            setSat(c, sat(s));
            setLum(c, lum(d));
            break;
        case BlendMode::Color:
            std::copy(s, s + 3, c);
            setLum(c, lum(d));
            break;
        default:  // Luminosity
            std::copy(d, d + 3, c);
            setLum(c, lum(s));
            break;
    }
    for (int i = 0; i < 3; ++i) out[i] = clamp01(c[i]);
}

}  // namespace

float blendNoise(int x, int y) {
    const float n = 0.06711056f * static_cast<float>(x) + 0.00583715f * static_cast<float>(y);
    const float m = 52.9829189f * (n - std::floor(n));
    return m - std::floor(m);
}

void blendOver(BlendMode mode, float r, float g, float b, float a, float* pixel, float noise) {
    switch (mode) {
        case BlendMode::Alpha:
        case BlendMode::Additive: {
            // The one rule the graphics card follows: source + pixel * (1 - alpha).
            const float keep = 1.0f - a;
            pixel[0] = r + pixel[0] * keep;
            pixel[1] = g + pixel[1] * keep;
            pixel[2] = b + pixel[2] * keep;
            return;
        }
        case BlendMode::Screen:
        case BlendMode::Multiply:
        case BlendMode::Exclusion: {
            // What the card does with its blend factors, on a pixel and a
            // colour that it keeps between 0 and 1.
            const float p[3] = {clamp01(r), clamp01(g), clamp01(b)};
            for (int i = 0; i < 3; ++i) {
                const float d = clamp01(pixel[i]);
                if (mode == BlendMode::Screen) {
                    pixel[i] = p[i] + d * (1.0f - p[i]);
                } else if (mode == BlendMode::Multiply) {
                    pixel[i] = p[i] * d + d * (1.0f - a);
                } else {
                    pixel[i] = p[i] * (1.0f - d) + d * (1.0f - p[i]);
                }
            }
            return;
        }
        default:
            break;
    }
    // The rest read the pixel behind and do sums with it.
    if (!(a > 1e-6f)) {
        return;
    }
    const float alpha = std::min(a, 1.0f);
    const float s[3] = {clamp01(r / a), clamp01(g / a), clamp01(b / a)};
    const float d[3] = {clamp01(pixel[0]), clamp01(pixel[1]), clamp01(pixel[2])};
    float result[3];
    if (mode == BlendMode::Dissolve) {
        // Whole particle or nothing at each pixel, by chance.
        if (noise < alpha) {
            std::copy(s, s + 3, pixel);
        }
        return;
    }
    if (mode == BlendMode::DarkerColor || mode == BlendMode::LighterColor) {
        const bool darker = mode == BlendMode::DarkerColor;
        const bool sourceWins = darker ? lum(s) < lum(d) : lum(s) > lum(d);
        std::copy(sourceWins ? s : d, (sourceWins ? s : d) + 3, result);
    } else if (mode == BlendMode::Hue || mode == BlendMode::Saturation || mode == BlendMode::Color ||
               mode == BlendMode::Luminosity) {
        colorMode(mode, d, s, result);
    } else {
        for (int i = 0; i < 3; ++i) result[i] = clamp01(separable(mode, d[i], s[i]));
    }
    for (int i = 0; i < 3; ++i) {
        pixel[i] = d[i] * (1.0f - alpha) + result[i] * alpha;
    }
}

}  // namespace vfx::editor
