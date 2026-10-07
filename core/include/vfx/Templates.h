// VFX Forge core: starting points built from real modules.
#pragma once

#include <functional>
#include <string>

#include "vfx/Effect.h"
#include "vfx/Id.h"

namespace vfx {

using IdSource = std::function<Id()>;

// An empty effect with a fresh ID.
Effect makeEmptyEffect(const IdSource& newId, std::string name, bool threeD);

// A layer with one of every Phase 1 module at its default, plus the Simple
// controls bound to them: Size, Speed, Amount, Lifetime, Color, Glow, Spread,
// Direction, Shape and Blend.
Layer makeBasicEmitter(const IdSource& newId, std::string name);

}  // namespace vfx
