#include "vfx/Templates.h"

#include <utility>

namespace vfx {

Effect makeEmptyEffect(const IdSource& newId, std::string name, bool threeD) {
    Effect effect;
    effect.id = newId();
    effect.name = std::move(name);
    effect.space = threeD ? "3d" : "2d";
    return effect;
}

Layer makeBasicEmitter(const IdSource& newId, std::string name) {
    const auto& registry = Registry::builtin();

    Layer layer;
    layer.id = newId();
    layer.name = std::move(name);

    auto add = [&](const char* type) {
        layer.modules.push_back(makeModule(*registry.findModule(type), newId()));
        return layer.modules.back().id;
    };
    const Id emission = add("emission");
    add("shape");
    const Id initial = add("initial");
    add("motion");
    add("overLife");
    const Id sprite = add("sprite");

    auto bind = [&](const char* label, Id module, const char* property) {
        SimpleControl control;
        control.id = newId();
        control.label = label;
        control.targets.push_back(ControlTarget{module, property});
        layer.controls.push_back(std::move(control));
    };
    bind("Size", initial, "size");
    bind("Speed", initial, "speed");
    bind("Amount", emission, "rate");
    bind("Lifetime", initial, "lifetime");
    bind("Color", initial, "color");
    bind("Glow", sprite, "glow");
    bind("Spread", initial, "spread");
    bind("Direction", initial, "direction");

    return layer;
}

}  // namespace vfx
