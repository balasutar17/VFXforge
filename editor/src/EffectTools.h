// VFX Forge editor: adjusting an effect as a whole.
//
// Small, exact changes to the ordinary properties of ordinary layers:
// "every size in this layer, 15% larger". Fitting to a reference and the
// Refine commands are both built from these. Private to the editor library.
#pragma once

#include <string>
#include <string_view>

#include "vfx/Effect.h"
#include "vfx/Value.h"

namespace vfx::editor::tools {

Module* moduleOf(Layer& layer, std::string_view type);
const Module* moduleOf(const Layer& layer, std::string_view type);

// A layer's property, or null.
template <class T>
T* property(Layer& layer, std::string_view type, std::string_view key) {
    Module* m = moduleOf(layer, type);
    Value* v = m ? m->find(key) : nullptr;
    return v ? std::get_if<T>(v) : nullptr;
}
template <class T>
const T* property(const Layer& layer, std::string_view type, std::string_view key) {
    const Module* m = moduleOf(layer, type);
    const Value* v = m ? m->find(key) : nullptr;
    return v ? std::get_if<T>(v) : nullptr;
}

// Multiplies a number, a random range or every key of a curve.
void scale(Scalar& value, double factor);
void scale(Vec3& value, double factor);

// How many particles the layer has alive at its busiest, roughly.
double busiest(const Layer& layer);
// Whether it sends out more than a couple of particles.
bool isParticles(const Layer& layer);
bool isAdditive(const Layer& layer);
std::string shapeOf(const Layer& layer);

// What the layer is for: its recorded role, or a guess from its settings
// for a layer built by hand ("glow", "sparks", "body"...).
std::string roleOf(const Layer& layer);

// ---- the adjustments. None touches a locked layer; callers check.
void scaleSize(Layer& layer, double factor);        // particle size only
void scaleReach(Layer& layer, double factor);       // speed, emitter size, pull: how far things go
void scaleEverything(Layer& layer, double factor);  // size and reach together
void scaleCount(Layer& layer, double factor, std::int64_t least = 1);
void scaleBrightness(Layer& layer, double factor);
// Makes the layer play `factor` times as fast.
void scaleTime(Layer& layer, double factor);
void setColour(Layer& layer, const Color& colour, bool keepOpacity);
void setShape(Layer& layer, const char* shape);

}  // namespace vfx::editor::tools
