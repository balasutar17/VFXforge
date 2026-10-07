// VFX Forge core: property paths.
//
// Every editable value has one address. Three forms exist:
//
//   effect/<field>                               effect/duration
//   layer/<layer id>/<field>                     layer/l-00c0ffee12345678/name
//   layer/<layer id>/module/<module id>/<prop>   layer/l-.../module/m-.../gravity
#pragma once

#include <string>
#include <string_view>

#include "vfx/Id.h"
#include "vfx/Result.h"

namespace vfx {

struct Path {
    Id layer;   // invalid for effect-level fields
    Id module;  // invalid for effect-level and layer-level fields
    std::string field;

    static Path effect(std::string field);
    static Path layerField(Id layer, std::string field);
    static Path property(Id layer, Id module, std::string field);

    static Result<Path> parse(std::string_view text);
    std::string str() const;

    bool isEffectField() const { return !layer.valid(); }
    bool isLayerField() const { return layer.valid() && !module.valid(); }
    bool isModuleProperty() const { return layer.valid() && module.valid(); }

    bool operator==(const Path&) const = default;
};

}  // namespace vfx
