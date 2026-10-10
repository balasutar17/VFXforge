// VFX Forge editor: small image tools shared by the reference analysis.
// Private to the editor library.
#pragma once

#include <cstddef>
#include <vector>

#include "vfx/editor/Reference.h"

namespace vfx::editor::ref {

inline constexpr float kPi = 3.14159265358979f;

inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float luminance(float r, float g, float b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }
inline float most(float r, float g, float b) { return r > g ? (r > b ? r : b) : (g > b ? g : b); }
inline float least(float r, float g, float b) { return r < g ? (r < b ? r : b) : (g < b ? g : b); }
// 0 for grey and white, 1 for a pure colour.
inline float saturation(float r, float g, float b) {
    const float hi = most(r, g, b);
    return hi > 1e-4f ? (hi - least(r, g, b)) / hi : 0.0f;
}

// A frame cropped and averaged down to the analysis grid. Colour is as
// painted (not multiplied by alpha); a is the see-through channel.
struct Grid {
    int width = 0, height = 0;
    std::vector<float> r, g, b, a;
};
Grid makeGrid(const Image& frame, const ReferenceOptions& options, int longestSide);

// What the background is taken to be.
struct BackdropRead {
    Backdrop kind = Backdrop::Dark;
    Swatch colour;
    float evenness = 1;
    float cutoff = 0.04f;
    bool additive = true;
};
BackdropRead readBackdrop(const std::vector<const Grid*>& frames, const ReferenceOptions& options);
Matte matteFromGrid(const Grid& grid, const BackdropRead& backdrop);
// The background as an analysis recorded it, to read further frames the same way.
BackdropRead backdropFrom(const StillAnalysis& still);

// Blurs in place with a square window, twice (close to a round blur).
void blur(std::vector<float>& values, int width, int height, int radius);
// Each point becomes the least (erode) or greatest (dilate) within radius.
void erode(std::vector<float>& values, int width, int height, int radius);
void dilate(std::vector<float>& values, int width, int height, int radius);

// A connected patch of points above a threshold.
struct Patch {
    int area = 0;
    float mass = 0;          // the sum of its values
    float x = 0, y = 0;      // its centre of mass
    int left = 0, top = 0, right = 0, bottom = 0;
    float major = 0, minor = 0;  // full length and width, from its spread
    float axisX = 1, axisY = 0;  // the long direction, y down
    float peak = 0;
    std::vector<int> points;     // indices into the grid
};
std::vector<Patch> findPatches(const std::vector<float>& values, int width, int height, float threshold);

// The basic measurements of one frame, cheap enough to take of every frame
// of a clip.
struct Basics {
    float total = 0;                 // the sum of the cover
    float fill = 0;                  // share of points with any effect
    float centreX = 0, centreY = 0;  // of mass, in grid points
    float hotX = 0, hotY = 0;
    float extent = 0, halfExtent = 0;  // in grid points, about the centre of mass
    float varXX = 0, varXY = 0, varYY = 0;
    float brightness = 0;            // the mean of the brightest twentieth
    Swatch colour;                   // the mean colour
};
Basics measureBasics(const Matte& matte);

// Bilinear reading of a grid of values; outside is zero.
float readAt(const std::vector<float>& values, int width, int height, float x, float y);

// The full reading of one frame, given its matte and what its background was.
StillAnalysis analyzeStill(const Matte& matte, const BackdropRead& backdrop);

// How many small loose pieces a frame has, and how far out they are on
// average (grid points from the centre given). Used per frame of a clip.
void countPieces(const Matte& matte, float centreX, float centreY, float extent, float& count,
                 float& reach);

// Fills in the report lines from the numbers.
void writeFindings(ReferenceAnalysis& analysis, bool moving);

// The clip part of the analysis.
TimeAnalysis analyzeTime(const std::vector<Matte>& mattes, double framesPerSecond, const Progress& progress,
                         bool& cancelled);

}  // namespace vfx::editor::ref
