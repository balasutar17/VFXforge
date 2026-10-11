// VFX Forge editor: working on curves and colour gradients.
//
// The curve editor and the gradient editor in the app are thin: they hold a
// list of points, let the artist move them, and hand the list back. What a
// legal list is, what the ready-made curve shapes are, and what a curve or a
// gradient looks like between its points are all decided here, where they
// can be tested without a window.
#pragma once

#include <string_view>
#include <vector>

#include "vfx/Metadata.h"
#include "vfx/Value.h"

namespace vfx::editor {

// ------------------------------------------------------------------ curves

// The most points the editors will make. (A file may hold more.)
inline constexpr std::size_t kEditorCurvePoints = 32;
// Two points are never closer together in time than this.
inline constexpr double kCurveGap = 0.002;

// A ready-made curve: its points run from 0 to 1 in time, and its values
// are shares of "full": 1 is the property's own level, whatever that is.
struct CurveShape {
    const char* key;    // stable, for code and tests
    const char* label;  // for the artist
    const char* group;  // "Level", "Ease", "Life"
    const char* help;
    std::vector<CurveKey> keys;
};
const std::vector<CurveShape>& curveShapes();
const CurveShape* findCurveShape(std::string_view key);

// How high a value stands now: the number itself, the top of a random
// range, or the highest point of a curve (by size, so a spin of -90 gives
// 90). Zero when there is nothing to go by.
double levelOf(const Scalar& value);

// The value as a curve: a curve stays as it is; a number or a random range
// becomes a level line at that number (the middle of the range).
std::vector<CurveKey> curveFrom(const Scalar& value);

// A shape made to stand as high as the value does now (or as high as the
// property's ordinary value when it stands at nothing), within the
// property's limits.
Scalar shaped(const CurveShape& shape, const Scalar& now, const PropertyDesc& desc);

// Makes any list of points into a legal curve for the property: finite,
// in time order, no two at the same moment, each value within the limits,
// never empty and never more than the editors make.
std::vector<CurveKey> tidyCurve(std::vector<CurveKey> keys, const PropertyDesc& desc);

// The curve's height at one moment, bends included.
double curveAt(const std::vector<CurveKey>& keys, double t);

// --------------------------------------------------------------- gradients

// The same for a colour gradient's stops.
Gradient tidyGradient(Gradient gradient);

// The gradient's colour at one moment: a straight blend between the stops
// either side, as the simulation does it.
Color gradientAt(const Gradient& gradient, double t);

// ------------------------------------------------------------------ bursts

// In time order, counts and times within the limits, no more than 64.
BurstList tidyBursts(BurstList bursts, double layerLength);

}  // namespace vfx::editor
