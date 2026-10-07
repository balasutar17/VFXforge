#include "vfx/Templates.h"

#include <utility>
#include <vector>

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
    bind("Shape", sprite, "shape");
    bind("Blend", sprite, "blend");

    // The order an artist reads them in: what it looks like first, then how
    // much of it there is, then how it moves. The IDs above are handed out
    // in the older order so that effects made by earlier versions and by
    // this one get the same IDs for the same things.
    const char* order[] = {"Shape", "Color", "Blend", "Glow", "Size", "Amount", "Lifetime",
                           "Speed", "Spread", "Direction"};
    std::vector<SimpleControl> arranged;
    for (const char* label : order) {
        for (SimpleControl& control : layer.controls) {
            if (control.label == label) {
                arranged.push_back(std::move(control));
            }
        }
    }
    layer.controls = std::move(arranged);

    return layer;
}

}  // namespace vfx
