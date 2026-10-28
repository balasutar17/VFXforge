#include "vfx/editor/Presets.h"
#include "vfx/editor/Silhouette.h"

#include <cmath>
#include <initializer_list>
#include <utility>

#include "vfx/Metadata.h"
#include "vfx/Value.h"
#include "vfx/editor/Session.h"
#include "vfx/editor/SoundLibrary.h"

#include "EffectTools.h"
#include "LayerMake.h"

namespace vfx::editor {

namespace {

using namespace make;

// A gradient that means "white-hot, then your colour, then dark and gone".
// Because it multiplies the layer's colour, changing that one colour re-hues
// the whole fire.
#define VFX_HOT_TO_DARK \
    {{0.0, 2.6, 2.2, 1.9, 1.0}, {0.25, 1.0, 1.0, 1.0, 1.0}, {0.7, 0.55, 0.3, 0.3, 0.8}, {1.0, 0.15, 0.08, 0.08, 0.0}}

// Shown as hand-drawn animation is: twelve drawings a second, each held
// until the next ("on twos").
Effect onTwos(Effect e) {
    for (Layer& layer : e.layers) {
        if (double* hold = tools::property<double>(layer, "sprite", "hold")) {
            *hold = 12.0;
        }
    }
    return e;
}

// ------------------------------------------------------------------ toon
// Hard-edged shapes with a lighter inner tone, in the manner of hand-drawn
// 2D effects, and shown on twos as hand-drawn effects are.

Effect toonPuff(const IdSource& id) {
    Effect e = begin(id, "Toon Puff", 1.6);
    add(e, Make(id, "Puffs").burst(9).circle(0.2).life(0.5, 0.85).speed(2.6, 5.2).aim(90, 180)
               .size(1.1, 1.7).turn(-30, 30).color(0xff6fcb).drag(4.6).gravity(0, 0)
               .sizeOver({{0, 0.35}, {0.2, 1.0}, {0.65, 0.85}, {1, 0}}).solid().look("puff").sound("pop", true, 0.0, 0.9, 1.0));
    add(e, Make(id, "Small puffs").burst(9).circle(0.2).life(0.4, 0.7).speed(4.5, 7.5).aim(90, 180)
               .size(0.45, 0.8).turn(-40, 40).color(0xff9fdd).drag(4.2).gravity(0, 0)
               .sizeOver({{0, 0.4}, {0.25, 1.0}, {1, 0}}).solid().look("puff"));
    add(e, Make(id, "Dots").burst(14).life(0.35, 0.65).speed(6, 10).aim(90, 180).size(0.1, 0.2)
               .color(0xffc4ea).drag(3.6).gravity(0, 0).sizeOver({{0, 1}, {0.6, 0.8}, {1, 0}}).solid()
               .look("disc"));
    add(e, Make(id, "Glint").burst(1).life(0.24, 0.24).speed(0, 0).size(3.0, 3.0).color(0xffffff)
               .gravity(0, 0).sizeOver({{0, 0.25}, {0.35, 1.0}, {1, 0}}).solid().look("glint"));
    return onTwos(std::move(e));
}

Effect toonHit(const IdSource& id) {
    Effect e = begin(id, "Toon Hit", 0.9);
    add(e, Make(id, "Ring").burst(1).life(0.2, 0.2).speed(0, 0).size(4.6, 4.6).color(0xfff2c4)
               .gravity(0, 0).sizeOver({{0, 0.2}, {0.6, 0.85}, {1, 1}}).fade({{0, 1}, {0.6, 0.8}, {1, 0}})
               .look("ring"));
    add(e, Make(id, "Spikes").burst(1).life(0.24, 0.24).speed(0, 0).size(4.4, 4.4).turn(0, 36)
               .color(0xff8a14).gravity(0, 0).sizeOver({{0, 0.3}, {0.25, 1.0}, {0.6, 0.9}, {1, 0}})
               .solid().look("burst").sound("hit", true, 0.0, 1.0, 1.0));
    add(e, Make(id, "Shards").burst(10).life(0.2, 0.36).speed(8, 15).aim(90, 180).size(0.26, 0.42)
               .color(0xffd23f).drag(5).gravity(0, 0).sizeOver({{0, 1}, {0.5, 0.9}, {1, 0}}).solid()
               .look("diamond").streak(0.0));
    add(e, Make(id, "Dots").burst(10).life(0.25, 0.45).speed(5, 10).aim(90, 180).size(0.1, 0.18)
               .color(0xfff0b0).drag(4).gravity(0, 0).sizeOver({{0, 1}, {1, 0}}).solid().look("disc"));
    return onTwos(std::move(e));
}

Effect toonExplosion(const IdSource& id) {
    Effect e = begin(id, "Toon Explosion", 1.8);
    add(e, Make(id, "Smoke").burst(8, 0.06).circle(0.3).life(0.9, 1.3).speed(1.6, 3.6).aim(90, 180)
               .size(1.2, 1.9).turn(-30, 30).color(0x574b66).drag(2.8).gravity(0, 0.8)
               .sizeOver({{0, 0.4}, {0.3, 1.0}, {1, 0.8}}).solid().look("puff").dissolve(0.6, 0.0, 0.02));
    add(e, Make(id, "Fire").burst(10).circle(0.2).life(0.45, 0.78).speed(2.2, 5.2).aim(90, 180)
               .size(1.2, 1.9).turn(-30, 30).color(0xff7a14).drag(4.2).gravity(0, 0.5)
               .sizeOver({{0, 0.4}, {0.2, 1.0}, {0.6, 0.85}, {1, 0}}).solid().look("puff"));
    add(e, Make(id, "Hot core").burst(6).circle(0.1).life(0.3, 0.5).speed(1.0, 3.0).aim(90, 180)
               .size(0.9, 1.4).turn(-30, 30).color(0xffd23f).drag(4.5).gravity(0, 0.5)
               .sizeOver({{0, 0.5}, {0.25, 1.0}, {1, 0}}).solid().look("puff"));
    add(e, Make(id, "Flash").burst(1).life(0.16, 0.16).speed(0, 0).size(5.6, 5.6).turn(0, 36)
               .color(0xfff0b4).gravity(0, 0).sizeOver({{0, 0.3}, {0.4, 1.0}, {1, 0.6}})
               .fade({{0, 1}, {0.6, 1}, {1, 0}}).look("burst").sound("explosion", true));
    add(e, Make(id, "Bits").burst(14).life(0.5, 0.9).speed(5, 10).aim(90, 180).size(0.12, 0.22)
               .color(0xffb347).drag(2.6).gravity(0, -6).sizeOver({{0, 1}, {0.7, 0.8}, {1, 0}}).solid()
               .look("disc"));
    return onTwos(std::move(e));
}

Effect slash(const IdSource& id) {
    Effect e = begin(id, "Slash", 0.9);
    add(e, Make(id, "Glow").burst(1).life(0.22, 0.22).speed(7, 7).aim(20, 0).size(4.6, 4.6)
               .color(0x38c8ff).drag(7).gravity(0, 0).sizeOver({{0, 0.5}, {0.3, 1.0}, {1, 1.1}})
               .fade({{0, 0.5}, {0.5, 0.3}, {1, 0}}).look("crescent").additive().streak(0.0));
    add(e, Make(id, "Slash").burst(1).life(0.22, 0.22).speed(7, 7).aim(20, 0).size(4.0, 4.0)
               .color(0x9fe9ff).drag(7).gravity(0, 0).sizeOver({{0, 0.5}, {0.3, 1.0}, {1, 1.1}})
               .fade({{0, 1}, {0.55, 1}, {1, 0}}).look("crescent").streak(0.0));
    add(e, Make(id, "Lines").burst(8).circle(1.0).life(0.14, 0.26).speed(9, 16).aim(20, 12)
               .size(0.08, 0.14).color(0xe6fbff).drag(5).gravity(0, 0).fade({{0, 1}, {0.5, 1}, {1, 0}})
               .look("streak").additive().glow(1.6).streak(0.06));
    add(e, Make(id, "Glints").burst(6, 0.04).circle(1.3).life(0.3, 0.55).speed(1, 4).aim(20, 40)
               .size(0.3, 0.6).color(0xffffff).drag(3).gravity(0, 0)
               .sizeOver({{0, 0.2}, {0.3, 1.0}, {1, 0}}).solid().look("glint"));
    return onTwos(std::move(e));
}

Effect magicOrb(const IdSource& id) {
    Effect e = begin(id, "Magic Orb", 4.0);
    add(e, Make(id, "Halo").rate(3).life(1.4, 1.4).speed(0, 0).size(4.4, 5.0).color(0x7a3cff)
               .gravity(0, 0).sizeOver({{0, 0.7}, {1, 1.1}}).fade({{0, 0}, {0.4, 0.3}, {1, 0}}).additive());
    add(e, Make(id, "Orb").burst(1).life(4.0, 4.0).speed(0, 0).size(2.3, 2.3).color(0x9b5cff)
               .gravity(0, 0).sizeOver({{0, 1.0}, {0.25, 1.06}, {0.5, 1.0}, {0.75, 1.06}, {1, 1.0}})
               .solid().look("orb"));
    add(e, Make(id, "Glints").rate(7).circle(1.7, true).life(0.5, 0.9).speed(0.1, 0.5).aim(90, 180)
               .size(0.3, 0.6).color(0xf0e2ff).gravity(0, 0)
               .sizeOver({{0, 0.1}, {0.4, 1.0}, {1, 0}}).solid().look("glint"));
    add(e, Make(id, "Motes").rate(14).circle(1.3).life(0.8, 1.4).speed(0.8, 1.8).aim(90, 20)
               .size(0.08, 0.16).color(0xc9a6ff).gravity(0, 0.6)
               .sizeOver({{0, 0}, {0.3, 1.0}, {1, 0}}).solid().look("disc"));
    return e;
}

Effect toonFire(const IdSource& id) {
    Effect e = begin(id, "Toon Fire", 4.0);
    add(e, Make(id, "Flames").rate(13).circle(0.45).life(0.55, 0.9).speed(1.4, 2.6).aim(90, 8)
               .size(1.1, 1.8).turn(-14, 14).color(0xff6a14).gravity(0, 1.2)
               .sizeOver({{0, 0.35}, {0.25, 1.0}, {0.7, 0.75}, {1, 0}}).solid().look("blaze"));
    add(e, Make(id, "Hot flames").rate(11).circle(0.3).life(0.4, 0.65).speed(1.0, 1.9).aim(90, 6)
               .size(0.7, 1.1).turn(-12, 12).color(0xffc531).gravity(0, 1.0)
               .sizeOver({{0, 0.4}, {0.25, 1.0}, {1, 0}}).solid().look("blaze"));
    add(e, Make(id, "Bits").rate(9).circle(0.5).life(0.7, 1.2).speed(2.2, 3.8).aim(90, 22)
               .size(0.08, 0.16).color(0xffb347).gravity(0, 0.6)
               .sizeOver({{0, 1}, {0.6, 0.8}, {1, 0}}).solid().look("disc"));
    return onTwos(std::move(e));
}

Effect toonSparkles(const IdSource& id) {
    Effect e = begin(id, "Toon Sparkles", 4.0);
    add(e, Make(id, "Big glints").rate(5).circle(1.8).life(0.6, 1.0).speed(0, 0.3).aim(90, 180)
               .size(0.6, 1.1).color(0xffd23f).gravity(0, 0)
               .sizeOver({{0, 0}, {0.3, 1.0}, {0.6, 0.9}, {1, 0}}).solid().look("glint"));
    add(e, Make(id, "Small glints").rate(9).circle(2.0).life(0.5, 0.9).speed(0, 0.4).aim(90, 180)
               .size(0.25, 0.5).color(0xfff1a8).gravity(0, 0)
               .sizeOver({{0, 0}, {0.3, 1.0}, {1, 0}}).solid().look("glint"));
    add(e, Make(id, "Dots").rate(12).circle(2.0).life(0.6, 1.1).speed(0.2, 0.8).aim(90, 180)
               .size(0.06, 0.13).color(0xffffff).gravity(0, 0.3)
               .sizeOver({{0, 0}, {0.3, 1.0}, {1, 0}}).solid().look("disc"));
    return e;
}

// --------------------------------------------------------------- blasts

Effect fireBlast(const IdSource& id) {
    Effect e = begin(id, "Fire Blast", 2.0);
    add(e, Make(id, "Smoke").burst(14, 0.04).circle(0.3).life(0.9, 1.5).speed(1.2, 3.0).aim(90, 180)
               .size(1.0, 1.7).turn(0, 360).spin(-40, 40).color(0x4a4340, 0.75).drag(2.4).gravity(0, 1.0)
               .sizeOver({{0, 0.45}, {0.3, 1.0}, {1, 1.5}}).fade({{0, 0}, {0.12, 1}, {0.5, 0.7}, {1, 0}})
               .look("smoke").sound("explosion", true));
    add(e, Make(id, "Shockwave").burst(1).life(0.4, 0.4).speed(0, 0).size(6.2, 6.2).color(0xffb45c)
               .gravity(0, 0).sizeOver({{0, 0.1}, {0.35, 0.72}, {1, 1}}).fade({{0, 0.8}, {0.4, 0.3}, {1, 0}})
               .look("ring").additive().glow(1.2));
    add(e, Make(id, "Fireball").burst(34).circle(0.15).life(0.4, 0.85).speed(2.5, 7.5).aim(90, 180)
               .size(0.9, 1.7).color(0xff6a14).drag(3.6).gravity(0, 1.5)
               .sizeOver({{0, 0.5}, {0.2, 1.0}, {1, 0.3}}).solid().tint(VFX_HOT_TO_DARK)
               .additive().glow(1.1));
    add(e, Make(id, "Flash").burst(1).life(0.16, 0.16).speed(0, 0).size(5.0, 5.0).color(0xfff0c8)
               .gravity(0, 0).sizeOver({{0, 0.35}, {1, 1}}).fade({{0, 1}, {1, 0}}).additive().glow(1.4));
    add(e, Make(id, "Sparks").burst(36).life(0.5, 1.0).speed(5, 12).aim(90, 180).size(0.09, 0.16)
               .color(0xffd98a).drag(1.3).gravity(0, -9).fade({{0, 1}, {0.7, 1}, {1, 0}})
               .look("streak").additive().glow(2.0).streak(0.045));
    return e;
}

Effect cartoonPop(const IdSource& id) {
    Effect e = begin(id, "Cartoon Pop", 1.4);
    add(e, Make(id, "Ring").burst(1).life(0.32, 0.32).speed(0, 0).size(4.2, 4.2).color(0xffffff)
               .gravity(0, 0).sizeOver({{0, 0.15}, {0.5, 0.85}, {1, 1}}).fade({{0, 1}, {0.6, 0.7}, {1, 0}})
               .look("ring"));
    add(e, Make(id, "Puffs").burst(10).circle(0.2).life(0.42, 0.68).speed(3.2, 5.5).aim(90, 180)
               .size(0.7, 1.15).color(0xfff4dc).drag(4.5).gravity(0, 0)
               .sizeOver({{0, 0.5}, {0.25, 1.0}, {1, 0.0}}).solid().look("disc"));
    add(e, Make(id, "Dots").burst(16).life(0.4, 0.7).speed(5, 9).aim(90, 180).size(0.12, 0.2)
               .color(0xff9f1c).drag(3).gravity(0, -3).sizeOver({{0, 1}, {0.6, 1}, {1, 0}}).solid()
               .look("disc"));
    add(e, Make(id, "Stars").burst(7).life(0.6, 0.95).speed(4, 7.5).aim(90, 180).size(0.42, 0.66)
               .turn(0, 360).spin(-260, 260).color(0xffd23f).drag(2.6).gravity(0, -2)
               .sizeOver({{0, 0.2}, {0.2, 1.0}, {0.75, 1.0}, {1, 0}}).solid().look("star"));
    return onTwos(std::move(e));
}

Effect arcaneBurst(const IdSource& id) {
    Effect e = begin(id, "Arcane Burst", 2.0);
    add(e, Make(id, "Outer ring").from(0.06, 1.0).burst(1).life(0.6, 0.6).speed(0, 0).size(6.4, 6.4)
               .color(0x6a5cff).gravity(0, 0).sizeOver({{0, 0.1}, {0.4, 0.75}, {1, 1}})
               .fade({{0, 0.8}, {0.5, 0.35}, {1, 0}}).look("ring").additive().glow(1.3));
    add(e, Make(id, "Inner ring").burst(1).life(0.38, 0.38).speed(0, 0).size(4.0, 4.0).color(0x7df9ff)
               .gravity(0, 0).sizeOver({{0, 0.05}, {0.4, 0.8}, {1, 1}}).fade({{0, 1}, {0.6, 0.5}, {1, 0}})
               .look("ring").additive().glow(1.5));
    add(e, Make(id, "Core").burst(1).life(0.3, 0.3).speed(0, 0).size(4.4, 4.4).color(0xc9b6ff)
               .gravity(0, 0).sizeOver({{0, 0.3}, {1, 1}}).fade({{0, 1}, {1, 0}}).additive().glow(1.5));
    add(e, Make(id, "Rays").burst(22).life(0.25, 0.45).speed(7, 15).aim(90, 180).size(0.14, 0.24)
               .color(0x9a7bff).drag(4).gravity(0, 0).fade({{0, 1}, {0.5, 0.8}, {1, 0}})
               .look("streak").additive().glow(2.2).streak(0.08));
    add(e, Make(id, "Motes").burst(26, 0.03).circle(0.4).life(0.9, 1.5).speed(1.2, 4.2).aim(90, 180)
               .size(0.08, 0.16).color(0x7df9ff).drag(2.8).gravity(0, 0.6)
               .fade({{0, 1}, {0.5, 1}, {1, 0}}).additive().glow(2.0));
    add(e, Make(id, "Glints").burst(12, 0.05).circle(1.3).life(0.5, 0.9).speed(0.2, 1.2).aim(90, 180)
               .size(0.4, 0.8).color(0xe8dcff).gravity(0, 0).sizeOver({{0, 0.1}, {0.3, 1}, {1, 0.1}})
               .fade({{0, 0}, {0.25, 1}, {1, 0}}).look("sparkle").additive().glow(1.6));
    return e;
}

Effect smokePuff(const IdSource& id) {
    Effect e = begin(id, "Smoke Puff", 1.8);
    add(e, Make(id, "Puff").burst(13).circle(0.25).life(0.8, 1.3).speed(1.4, 3.6).aim(90, 180)
               .size(0.9, 1.5).turn(0, 360).spin(-30, 30).color(0xe9e4dc, 0.9).drag(3.4)
               .gravity(0, 0.8).sizeOver({{0, 0.4}, {0.3, 1.0}, {1, 1.25}})
               .fade({{0, 0.95}, {0.4, 0.8}, {1, 0.35}}).look("smoke").turbulence(0.5, 1.3, 0.5)
               .dissolve(0.65, 0.0, 0.55));
    add(e, Make(id, "Dust").burst(12).life(0.4, 0.8).speed(3, 6).aim(90, 180).size(0.1, 0.18)
               .color(0xd8d0c4).drag(3).gravity(0, -1).sizeOver({{0, 1}, {1, 0}}).solid().look("disc"));
    return e;
}

// ----------------------------------------------------------------- fire

Effect campfire(const IdSource& id) {
    Effect e = begin(id, "Campfire", 4.0);
    add(e, Make(id, "Smoke").rate(6).circle(0.25).life(2.2, 3.0).speed(0.9, 1.5).aim(90, 10)
               .size(0.9, 1.4).turn(0, 360).spin(-25, 25).color(0x38322f, 0.6).gravity(0, 0.5)
               .sizeOver({{0, 0.5}, {1, 1.7}}).fade({{0, 0}, {0.3, 0}, {0.55, 0.5}, {1, 0.2}}).look("smoke")
               .turbulence(0.6, 1.4, 0.5).dissolve(0.45, 0.0, 0.6).sound("fire-loop", false, 0.0, 0.8).soundLoop());
    add(e, Make(id, "Flames").rate(42).circle(0.38).life(0.6, 1.05).speed(1.3, 2.5).aim(90, 9)
               .size(0.75, 1.25).color(0xff6412).gravity(0, 1.6).drag(0.4)
               .sizeOver({{0, 0.55}, {0.2, 1.0}, {1, 0.12}}).solid().tint(VFX_HOT_TO_DARK)
               .look("flame").additive().glow(0.7));
    add(e, Make(id, "Glow").rate(5).life(0.9, 1.2).speed(0, 0).size(3.2, 3.8).color(0xff7a1f)
               .gravity(0, 0).fade({{0, 0}, {0.5, 0.2}, {1, 0}}).additive());
    add(e, Make(id, "Embers").rate(11).circle(0.3).life(1.2, 2.2).speed(1.6, 3.2).aim(90, 22)
               .size(0.05, 0.1).color(0xffb44a).gravity(0, 0.5).drag(0.3)
               .fade({{0, 1}, {0.6, 1}, {1, 0}}).additive().glow(2.2).turbulence(1.3, 0.8, 0.9));
    return e;
}

Effect spiritFlame(const IdSource& id) {
    Effect e = begin(id, "Spirit Flame", 4.0);
    add(e, Make(id, "Flames").rate(48).circle(0.3).life(0.7, 1.2).speed(1.4, 2.6).aim(90, 7)
               .size(0.7, 1.1).color(0x16c8ff).gravity(0, 2.0).drag(0.5)
               .sizeOver({{0, 0.5}, {0.2, 1.0}, {1, 0.08}}).solid().tint(VFX_HOT_TO_DARK)
               .look("flame").additive().glow(0.75));
    add(e, Make(id, "Glow").rate(5).life(0.9, 1.2).speed(0, 0).size(3.0, 3.6).color(0x1f9dff)
               .gravity(0, 0).fade({{0, 0}, {0.5, 0.22}, {1, 0}}).additive());
    add(e, Make(id, "Wisps").rate(9).circle(0.5).life(1.0, 1.8).speed(1.2, 2.4).aim(90, 26)
               .size(0.22, 0.42).color(0xb8f4ff).gravity(0, 0.8)
               .sizeOver({{0, 0.2}, {0.3, 1}, {1, 0.1}}).fade({{0, 0}, {0.3, 1}, {1, 0}})
               .look("sparkle").additive().glow(1.4).turbulence(1.0, 0.9, 0.8));
    return e;
}

Effect risingEmbers(const IdSource& id) {
    Effect e = begin(id, "Rising Embers", 5.0);
    add(e, Make(id, "Embers").rate(26).rect(8, 0.6).life(2.2, 3.6).speed(0.9, 2.2).aim(90, 16)
               .size(0.05, 0.12).color(0xff9d3c).gravity(0.25, 0.35).drag(0.2)
               .fade({{0, 0}, {0.15, 1}, {0.7, 1}, {1, 0}}).additive().glow(2.4).turbulence(1.2, 1.5, 0.5));
    add(e, Make(id, "Haze").rate(7).rect(8, 0.6).life(2.0, 3.0).speed(0.5, 1.0).aim(90, 12)
               .size(2.4, 3.6).color(0xff5a1a).gravity(0, 0.2)
               .fade({{0, 0}, {0.4, 0.07}, {1, 0}}).additive());
    return e;
}

// -------------------------------------------------------- water, bubbles

Effect bubbles(const IdSource& id) {
    Effect e = begin(id, "Bubbles", 5.0);
    add(e, Make(id, "Bubbles").rate(9).rect(5, 0.4).life(2.4, 3.6).speed(0.6, 1.4).aim(90, 12)
               .size(0.22, 0.62).color(0xcdeeff, 0.95).gravity(0, 0.45).drag(0.3)
               .sizeOver({{0, 0.5}, {0.2, 1.0}, {0.94, 1.05}, {1, 1.45}})
               .fade({{0, 0}, {0.1, 1}, {0.94, 1}, {1, 0}}).look("bubble"));
    add(e, Make(id, "Tiny bubbles").rate(14).rect(5, 0.4).life(1.6, 2.6).speed(0.9, 1.8).aim(90, 10)
               .size(0.06, 0.13).color(0xe6f7ff, 0.8).gravity(0, 0.5)
               .fade({{0, 0}, {0.15, 1}, {0.8, 1}, {1, 0}}).look("ring"));
    return e;
}

Effect bubblePop(const IdSource& id) {
    Effect e = begin(id, "Bubble Pop", 1.4);
    add(e, Make(id, "Pop ring").burst(1).life(0.28, 0.28).speed(0, 0).size(3.0, 3.0).color(0xdff4ff)
               .gravity(0, 0).sizeOver({{0, 0.3}, {1, 1}}).fade({{0, 0.9}, {1, 0}}).look("ring").sound("bubble", true));
    add(e, Make(id, "Droplets").burst(14).circle(0.5, true).life(0.35, 0.6).speed(3, 6).aim(90, 180)
               .size(0.1, 0.18).color(0xbfe9ff).drag(3).gravity(0, -6)
               .sizeOver({{0, 1}, {0.6, 0.8}, {1, 0}}).solid().look("disc"));
    add(e, Make(id, "Small bubbles").burst(8).circle(0.4).life(0.6, 1.0).speed(1.5, 3.5).aim(90, 180)
               .size(0.2, 0.4).color(0xd8f2ff, 0.95).drag(2.5).gravity(0, 1)
               .fade({{0, 1}, {0.7, 1}, {1, 0}}).look("bubble"));
    return e;
}

Effect waterSplash(const IdSource& id) {
    Effect e = begin(id, "Water Splash", 1.8);
    add(e, Make(id, "Mist").burst(10).rect(1.2, 0.1).life(0.5, 0.9).speed(1.5, 3.5).aim(90, 50)
               .size(0.8, 1.4).color(0xbfe4ff, 0.35).drag(2.5).gravity(0, -1)
               .sizeOver({{0, 0.5}, {1, 1.4}}).fade({{0, 1}, {1, 0}}));
    add(e, Make(id, "Drops").burst(34).rect(0.8, 0.1).life(0.7, 1.2).speed(4.5, 9).aim(90, 32)
               .size(0.1, 0.22).color(0x8fd3ff).gravity(0, -13).drag(0.3)
               .fade({{0, 1}, {0.8, 1}, {1, 0}}).look("streak").streak(0.03));
    add(e, Make(id, "Highlights").burst(14).rect(0.8, 0.1).life(0.6, 1.0).speed(5, 9.5).aim(90, 28)
               .size(0.06, 0.1).color(0xffffff).gravity(0, -13).drag(0.3)
               .fade({{0, 1}, {0.7, 1}, {1, 0}}).look("disc"));
    return e;
}

// ---------------------------------------------------------- glow, magic

Effect softGlow(const IdSource& id) {
    Effect e = begin(id, "Soft Glow", 4.0);
    add(e, Make(id, "Glow").rate(2.5).life(1.6, 1.6).speed(0, 0).size(3.4, 3.8).color(0xffd27a)
               .gravity(0, 0).sizeOver({{0, 0.65}, {0.5, 1.0}, {1, 1.2}})
               .fade({{0, 0}, {0.35, 0.42}, {1, 0}}).additive());
    add(e, Make(id, "Core").rate(2.5).life(1.6, 1.6).speed(0, 0).size(1.3, 1.5).color(0xfff3cf)
               .gravity(0, 0).sizeOver({{0, 0.7}, {0.5, 1.0}, {1, 1.1}})
               .fade({{0, 0}, {0.35, 0.6}, {1, 0}}).additive());
    add(e, Make(id, "Twinkles").rate(8).circle(1.5).life(0.7, 1.2).speed(0, 0.25).aim(90, 180)
               .size(0.3, 0.62).color(0xfff0b8).gravity(0, 0)
               .sizeOver({{0, 0.1}, {0.35, 1}, {1, 0.1}}).fade({{0, 0}, {0.3, 1}, {1, 0}})
               .look("sparkle").additive().glow(1.4));
    return e;
}

Effect magicSparkles(const IdSource& id) {
    Effect e = begin(id, "Magic Sparkles", 4.0);
    const unsigned colors[3] = {0xff8ad8, 0x7df9ff, 0xfff6c2};
    const char* names[3] = {"Pink", "Cyan", "Gold"};
    for (int i = 0; i < 3; ++i) {
        add(e, Make(id, names[i]).rate(9).circle(2.0).life(0.6, 1.2).speed(0, 0.3).aim(90, 180)
                   .size(0.4, 1.0).color(colors[i]).gravity(0, 0.15)
                   .sizeOver({{0, 0.05}, {0.35, 1}, {1, 0.05}}).fade({{0, 0}, {0.3, 1}, {1, 0}})
                   .look("sparkle").additive().glow(1.5));
    }
    add(e, Make(id, "Dust").rate(22).circle(2.1).life(1.0, 1.8).speed(0.1, 0.5).aim(90, 180)
               .size(0.04, 0.09).color(0xffffff).gravity(0, 0.2)
               .fade({{0, 0}, {0.3, 0.9}, {1, 0}}).additive().glow(1.6));
    return e;
}

Effect healingAura(const IdSource& id) {
    Effect e = begin(id, "Healing Aura", 4.0);
    add(e, Make(id, "Ground ring").rate(1.25).life(1.3, 1.3).speed(0, 0).size(4.2, 4.2).color(0x4dff9a)
               .gravity(0, 0).sizeOver({{0, 0.2}, {1, 1}}).fade({{0, 0}, {0.2, 0.5}, {1, 0}})
               .look("ring").additive());
    add(e, Make(id, "Glow").rate(3).life(1.4, 1.4).speed(0.3, 0.5).aim(90, 0).size(2.6, 3.2)
               .color(0x2fe37a).gravity(0, 0).fade({{0, 0}, {0.4, 0.22}, {1, 0}}).additive());
    add(e, Make(id, "Orbs").rate(16).rect(2.6, 0.3).life(1.1, 1.8).speed(1.0, 2.2).aim(90, 6)
               .size(0.12, 0.26).color(0x8dffb8).gravity(0, 0.8)
               .sizeOver({{0, 0.3}, {0.3, 1}, {1, 0.2}}).fade({{0, 0}, {0.2, 1}, {0.7, 1}, {1, 0}})
               .additive().glow(1.8));
    add(e, Make(id, "Crosses").rate(4).rect(2.4, 0.4).life(1.0, 1.5).speed(1.2, 2.0).aim(90, 4)
               .size(0.24, 0.4).color(0xb6ffcf).gravity(0, 0)
               .sizeOver({{0, 0.2}, {0.2, 1.1, true}, {0.35, 1.0, true}, {1, 0.8}})
               .fade({{0, 0}, {0.15, 1}, {0.7, 1}, {1, 0}}).look("plus"));
    add(e, Make(id, "Glints").rate(7).rect(2.8, 2.0).life(0.6, 1.0).speed(0.4, 1.0).aim(90, 10)
               .size(0.3, 0.6).color(0xe6ffe9).gravity(0, 0)
               .sizeOver({{0, 0.1}, {0.35, 1}, {1, 0.1}}).fade({{0, 0}, {0.3, 1}, {1, 0}})
               .look("sparkle").additive().glow(1.4));
    return e;
}

Effect levelUp(const IdSource& id) {
    Effect e = begin(id, "Level Up", 2.4);
    add(e, Make(id, "Ring").burst(1).life(0.7, 0.7).speed(0, 0).size(5.4, 5.4).color(0xffd95a)
               .gravity(0, 0).sizeOver({{0, 0.1}, {0.4, 0.75}, {1, 1}}).fade({{0, 1}, {0.5, 0.5}, {1, 0}})
               .look("ring").additive().glow(1.3).sound("level-up", true));
    add(e, Make(id, "Column glow").from(0, 1.2).rate(10).rect(1.2, 0.2).life(0.7, 1.0).speed(2.5, 4.0)
               .aim(90, 0).size(1.6, 2.2).color(0xffc233).gravity(0, 0)
               .fade({{0, 0}, {0.3, 0.3}, {1, 0}}).additive());
    add(e, Make(id, "Rising light").from(0, 1.3).rate(60).rect(1.8, 0.2).life(0.5, 0.9).speed(4, 8.5)
               .aim(90, 0).size(0.12, 0.22).color(0xffe9a0).gravity(0, 0)
               .fade({{0, 0}, {0.2, 1}, {0.7, 1}, {1, 0}}).look("streak").additive().glow(2.0).streak(0.09));
    add(e, Make(id, "Arrows").from(0.05, 0.7).rate(9).rect(2.0, 0.2).life(0.7, 1.0).speed(3.0, 4.5).aim(90, 0)
               .size(0.5, 0.8).turn(90, 90).color(0xffe07a).gravity(0, 0)
               .pace({{0, 2.6}, {0.3, 0.5, true}, {1, 0.05, true}})
               .fade({{0, 0}, {0.15, 1}, {0.7, 1}, {1, 0}}).look("chevron"));
    add(e, Make(id, "Stars").burst(9, 0.1).life(0.9, 1.3).speed(3.5, 6.5).aim(90, 70).size(0.36, 0.6)
               .turn(0, 360).spin(-200, 200).color(0xffd23f).drag(1.8).gravity(0, -4)
               .sizeOver({{0, 0.2}, {0.2, 1}, {0.8, 1}, {1, 0}}).solid().look("star"));
    add(e, Make(id, "Glints").from(0.1, 1.4).rate(14).rect(2.4, 3.2).life(0.5, 0.9).speed(0.5, 1.5)
               .aim(90, 20).size(0.3, 0.65).color(0xfff6cf).gravity(0, 0)
               .sizeOver({{0, 0.1}, {0.35, 1}, {1, 0.1}}).fade({{0, 0}, {0.3, 1}, {1, 0}})
               .look("sparkle").additive().glow(1.5));
    return e;
}

Effect fairyDust(const IdSource& id) {
    Effect e = begin(id, "Fairy Dust", 4.0);
    add(e, Make(id, "Dust").rate(46).circle(0.25).life(1.0, 1.9).speed(0.5, 2.2).aim(270, 55)
               .size(0.05, 0.11).color(0xffe9a8).gravity(0, -1.6).drag(0.8)
               .fade({{0, 1}, {0.6, 1}, {1, 0}}).additive().glow(2.0).turbulence(0.8, 0.8, 0.8));
    add(e, Make(id, "Glints").rate(11).circle(0.3).life(0.8, 1.5).speed(0.4, 1.8).aim(270, 60)
               .size(0.3, 0.62).color(0xfff7d6).gravity(0, -1.4).drag(0.8)
               .sizeOver({{0, 0.1}, {0.3, 1}, {1, 0.1}}).fade({{0, 0}, {0.2, 1}, {1, 0}})
               .look("sparkle").additive().glow(1.5));
    add(e, Make(id, "Source").rate(4).life(0.8, 0.8).speed(0, 0).size(1.2, 1.4).color(0xffdc8a)
               .gravity(0, 0).fade({{0, 0}, {0.5, 0.5}, {1, 0}}).additive());
    return e;
}

// ---------------------------------------------------------------- frames

Effect frameSparkle(const IdSource& id) {
    Effect e = begin(id, "Frame Sparkle", 4.0);
    add(e, Make(id, "Outline").rate(700).rect(5.2, 3.2, true).life(0.5, 0.8).speed(0, 0)
               .size(0.24, 0.34).color(0xffc94a).gravity(0, 0)
               .fade({{0, 0}, {0.5, 0.14}, {1, 0}}).additive());
    add(e, Make(id, "Twinkles").rate(22).rect(5.2, 3.2, true).life(0.45, 0.9).speed(0, 0.2).aim(90, 180)
               .size(0.35, 0.8).color(0xfff4c4).gravity(0, 0)
               .sizeOver({{0, 0.05}, {0.35, 1}, {1, 0.05}}).fade({{0, 0}, {0.3, 1}, {1, 0}})
               .look("sparkle").additive().glow(1.5));
    add(e, Make(id, "Dust").rate(30).rect(5.2, 3.2, true).life(0.6, 1.2).speed(0.1, 0.5).aim(90, 180)
               .size(0.04, 0.09).color(0xffffff).gravity(0, -0.3)
               .fade({{0, 1}, {0.6, 0.8}, {1, 0}}).additive().glow(1.6));
    return e;
}

Effect neonFrame(const IdSource& id) {
    Effect e = begin(id, "Neon Frame", 4.0);
    add(e, Make(id, "Halo").rate(420).rect(5.2, 3.2, true).life(0.6, 0.9).speed(0, 0)
               .size(0.75, 0.95).color(0x19d3ff).gravity(0, 0)
               .fade({{0, 0}, {0.5, 0.035}, {1, 0}}).additive());
    add(e, Make(id, "Tube").rate(1500).rect(5.2, 3.2, true).life(0.4, 0.55).speed(0, 0)
               .size(0.15, 0.18).color(0x9ff1ff).gravity(0, 0)
               .fade({{0, 0}, {0.5, 0.42}, {1, 0}}).additive().glow(1.3));
    add(e, Make(id, "Flicker").rate(5).rect(5.2, 3.2, true).life(0.25, 0.5).speed(0, 0)
               .size(0.5, 0.9).color(0xffffff).gravity(0, 0)
               .sizeOver({{0, 0.1}, {0.3, 1}, {1, 0.1}}).fade({{0, 0}, {0.3, 1}, {1, 0}})
               .look("sparkle").additive().glow(1.5));
    return e;
}

Effect burningFrame(const IdSource& id) {
    Effect e = begin(id, "Burning Frame", 4.0);
    add(e, Make(id, "Flames").rate(520).rect(5.2, 3.2, true).life(0.35, 0.7).speed(0.4, 1.2).aim(90, 14)
               .size(0.3, 0.6).color(0xff6412).gravity(0, 2.2)
               .sizeOver({{0, 0.5}, {0.2, 1.0}, {1, 0.1}}).solid().tint(VFX_HOT_TO_DARK)
               .look("flame").additive().glow(0.5));
    add(e, Make(id, "Embers").rate(24).rect(5.2, 3.2, true).life(0.8, 1.5).speed(0.8, 2.0).aim(90, 24)
               .size(0.04, 0.08).color(0xffb44a).gravity(0, 0.8)
               .fade({{0, 1}, {0.6, 1}, {1, 0}}).additive().glow(2.2).turbulence(1.0, 0.8, 0.9));
    return e;
}

// -------------------------------------------------------------- shooting

Effect muzzleFlash(const IdSource& id) {
    Effect e = begin(id, "Muzzle Flash", 0.7);
    add(e, Make(id, "Smoke").burst(5, 0.02).life(0.4, 0.65).speed(1.0, 2.6).aim(0, 24)
               .size(0.6, 1.0).turn(0, 360).color(0x8a8580, 0.45).drag(3).gravity(0, 0.8)
               .sizeOver({{0, 0.5}, {1, 1.5}}).fade({{0, 1}, {1, 0}}).look("smoke"));
    add(e, Make(id, "Flash").burst(1).life(0.09, 0.09).speed(0, 0).size(2.6, 2.6).color(0xffe9b0)
               .gravity(0, 0).sizeOver({{0, 0.6}, {1, 1}}).fade({{0, 1}, {1, 0}}).additive().glow(1.5));
    add(e, Make(id, "Flare").burst(1).life(0.08, 0.08).speed(0, 0).size(3.4, 3.4).color(0xfff2cc)
               .gravity(0, 0).sizeOver({{0, 0.6}, {1, 1}}).fade({{0, 1}, {1, 0}})
               .look("sparkle").additive().glow(1.0));
    add(e, Make(id, "Flame").burst(7).life(0.07, 0.13).speed(9, 18).aim(0, 9).size(0.5, 0.8)
               .color(0xffa52e).gravity(0, 0).drag(6).fade({{0, 1}, {1, 0}})
               .look("streak").additive().glow(1.6).streak(0.07));
    add(e, Make(id, "Sparks").burst(12).life(0.15, 0.32).speed(8, 18).aim(0, 20).size(0.05, 0.09)
               .color(0xffe2a0).gravity(0, -4).drag(3).fade({{0, 1}, {0.6, 1}, {1, 0}})
               .look("streak").additive().glow(2.2).streak(0.035));
    return e;
}

Effect laserBolts(const IdSource& id) {
    Effect e = begin(id, "Laser Bolts", 2.0);
    add(e, Make(id, "Glow").rate(6).life(0.55, 0.55).speed(13, 13).aim(0, 0).size(0.6, 0.6)
               .color(0xff2e4d).gravity(0, 0).fade({{0, 0.9}, {0.85, 0.9}, {1, 0}})
               .look("streak").additive().glow(1.2).streak(0.1));
    add(e, Make(id, "Core").rate(6).life(0.55, 0.55).speed(13, 13).aim(0, 0).size(0.2, 0.2)
               .color(0xffd9df).gravity(0, 0).fade({{0, 1}, {0.85, 1}, {1, 0}})
               .look("streak").additive().glow(2.0).streak(0.1));
    add(e, Make(id, "Muzzle").rate(6).life(0.1, 0.1).speed(0, 0).size(1.3, 1.3).color(0xff6a80)
               .gravity(0, 0).fade({{0, 1}, {1, 0}}).additive().glow(1.3));
    return e;
}

Effect hitImpact(const IdSource& id) {
    Effect e = begin(id, "Hit Impact", 0.9);
    add(e, Make(id, "Ring").burst(1).life(0.22, 0.22).speed(0, 0).size(3.2, 3.2).color(0xfff1c2)
               .gravity(0, 0).sizeOver({{0, 0.15}, {0.5, 0.8}, {1, 1}}).fade({{0, 1}, {1, 0}})
               .look("ring").additive());
    add(e, Make(id, "Flash").burst(1).life(0.12, 0.12).speed(0, 0).size(2.4, 2.4).color(0xfff6dc)
               .gravity(0, 0).sizeOver({{0, 0.5}, {1, 1}}).fade({{0, 1}, {1, 0}}).additive().glow(1.2));
    add(e, Make(id, "Lines").burst(14).life(0.14, 0.26).speed(9, 17).aim(90, 180).size(0.1, 0.17)
               .color(0xffe08a).gravity(0, 0).drag(5).fade({{0, 1}, {0.5, 1}, {1, 0}})
               .look("streak").additive().glow(2.0).streak(0.06));
    add(e, Make(id, "Star").burst(1).life(0.2, 0.2).speed(0, 0).size(4.2, 4.2).turn(0, 45).color(0xffffff)
               .gravity(0, 0).sizeOver({{0, 0.3}, {0.4, 1}, {1, 0.4}}).fade({{0, 1}, {1, 0}})
               .look("sparkle").additive().glow(1.0));
    return e;
}

Effect shootingStars(const IdSource& id) {
    Effect e = begin(id, "Shooting Stars", 4.0);
    add(e, Make(id, "Night stars").rate(12).rect(9, 5.5).life(0.8, 1.6).speed(0, 0).size(0.25, 0.55)
               .color(0xcfe2ff).gravity(0, 0).sizeOver({{0, 0.1}, {0.4, 1}, {1, 0.1}})
               .fade({{0, 0}, {0.4, 0.9}, {1, 0}}).look("sparkle").additive());
    add(e, Make(id, "Trails").rate(3.5).rect(9, 4).life(0.7, 1.0).speed(8, 12).aim(212, 4)
               .size(0.16, 0.26).color(0x9fd0ff).gravity(0, 0)
               .fade({{0, 0}, {0.2, 1}, {0.7, 1}, {1, 0}}).look("streak").additive().glow(1.8).streak(0.16));
    return e;
}

// ---------------------------------------------------------------- rewards

Effect coinBurst(const IdSource& id) {
    Effect e = begin(id, "Coin Burst", 2.2);
    add(e, Make(id, "Coins").burst(18).rect(0.5, 0.1).life(1.2, 1.7).speed(6.5, 10).aim(90, 26)
               .size(0.44, 0.58).color(0xffc531).gravity(0, -15)
               .fade({{0, 1}, {0.85, 1}, {1, 0}}).look("coin").sound("coin", true, 0.0, 0.9, 1.0));
    add(e, Make(id, "Glints").burst(12, 0.05).alsoRate(10).circle(1.2).life(0.4, 0.8).speed(1, 3)
               .aim(90, 60).size(0.3, 0.7).color(0xfff7cf).gravity(0, 0)
               .sizeOver({{0, 0.1}, {0.35, 1}, {1, 0.1}}).fade({{0, 0}, {0.3, 1}, {1, 0}})
               .look("sparkle").additive().glow(1.5).from(0, 1.0));
    add(e, Make(id, "Pop").burst(1).life(0.25, 0.25).speed(0, 0).size(3.4, 3.4).color(0xffdf7a)
               .gravity(0, 0).sizeOver({{0, 0.2}, {1, 1}}).fade({{0, 0.8}, {1, 0}}).look("ring").additive());
    return e;
}

Effect confetti(const IdSource& id) {
    Effect e = begin(id, "Confetti", 3.2);
    const unsigned colors[5] = {0xff4d6d, 0xffc531, 0x3ddc97, 0x3aa0ff, 0xb46bff};
    const char* names[5] = {"Red", "Yellow", "Green", "Blue", "Purple"};
    for (int i = 0; i < 5; ++i) {
        add(e, Make(id, names[i]).burst(16).rect(0.6, 0.1).life(1.9, 2.8).speed(6, 12).aim(90, 30)
                   .size(0.28, 0.46).turn(0, 360).spin(-420, 420).color(colors[i]).gravity(0, -6)
                   .drag(1.7).fade({{0, 1}, {0.85, 1}, {1, 0}}).look(i % 2 == 0 ? "confetti" : "ribbon")
                   .turbulence(1.1, 1.3, 0.6));
    }
    return e;
}

Effect starBurst(const IdSource& id) {
    Effect e = begin(id, "Star Burst", 1.8);
    add(e, Make(id, "Glow").burst(1).life(0.4, 0.4).speed(0, 0).size(4.4, 4.4).color(0xffd95a)
               .gravity(0, 0).sizeOver({{0, 0.4}, {1, 1}}).fade({{0, 0.8}, {1, 0}}).additive());
    add(e, Make(id, "Ring").burst(1).life(0.4, 0.4).speed(0, 0).size(4.6, 4.6).color(0xfff0b0)
               .gravity(0, 0).sizeOver({{0, 0.1}, {0.5, 0.8}, {1, 1}}).fade({{0, 1}, {1, 0}})
               .look("ring").additive());
    add(e, Make(id, "Stars").burst(12).life(0.8, 1.2).speed(3.5, 7.5).aim(90, 180).size(0.36, 0.7)
               .turn(0, 360).spin(-220, 220).color(0xffd23f).drag(2.6).gravity(0, -1.5)
               .sizeOver({{0, 0.2}, {0.15, 1.1}, {0.3, 1}, {0.8, 1}, {1, 0}}).solid().look("star"));
    add(e, Make(id, "Glints").burst(16, 0.04).circle(0.8).life(0.5, 1.0).speed(1.5, 5).aim(90, 180)
               .size(0.25, 0.6).color(0xfff7cf).drag(2.5).gravity(0, 0)
               .sizeOver({{0, 0.1}, {0.35, 1}, {1, 0.1}}).fade({{0, 0}, {0.3, 1}, {1, 0}})
               .look("sparkle").additive().glow(1.5));
    return e;
}

Effect hearts(const IdSource& id) {
    Effect e = begin(id, "Hearts", 4.0);
    add(e, Make(id, "Hearts").rate(5).circle(0.5).life(1.6, 2.3).speed(1.0, 2.0).aim(90, 24)
               .size(0.4, 0.8).turn(-18, 18).color(0xff4f86).gravity(0, 0.3).drag(0.3)
               .sizeOver({{0, 0.0}, {0.12, 1.15}, {0.22, 1.0}, {1, 0.9}})
               .fade({{0, 1}, {0.75, 1}, {1, 0}}).look("heart"));
    add(e, Make(id, "Small hearts").rate(5).circle(0.7).life(1.2, 1.8).speed(1.3, 2.4).aim(90, 30)
               .size(0.18, 0.3).turn(-25, 25).color(0xff9dbb).gravity(0, 0.3)
               .sizeOver({{0, 0.0}, {0.15, 1.1}, {0.25, 1.0}, {1, 0.9}})
               .fade({{0, 1}, {0.7, 1}, {1, 0}}).look("heart"));
    add(e, Make(id, "Glints").rate(7).circle(1.0).life(0.5, 0.9).speed(0.5, 1.5).aim(90, 40)
               .size(0.25, 0.5).color(0xffe3ec).gravity(0, 0)
               .sizeOver({{0, 0.1}, {0.35, 1}, {1, 0.1}}).fade({{0, 0}, {0.3, 1}, {1, 0}})
               .look("sparkle").additive().glow(1.4));
    return e;
}

Effect gemShatter(const IdSource& id) {
    Effect e = begin(id, "Gem Shatter", 1.8);
    add(e, Make(id, "Flash").burst(1).life(0.18, 0.18).speed(0, 0).size(3.8, 3.8).color(0xbfe8ff)
               .gravity(0, 0).sizeOver({{0, 0.4}, {1, 1}}).fade({{0, 1}, {1, 0}}).additive().glow(1.3));
    add(e, Make(id, "Shards").burst(16).circle(0.3).life(0.8, 1.3).speed(3.5, 8).aim(90, 180)
               .size(0.22, 0.46).turn(0, 360).spin(-360, 360).color(0x36b8ff).drag(1.6).gravity(0, -10)
               .sizeOver({{0, 1}, {0.7, 1}, {1, 0}}).solid().look("glass"));
    add(e, Make(id, "Small shards").burst(10).circle(0.3).life(0.7, 1.1).speed(4, 8.5).aim(90, 180)
               .size(0.12, 0.22).turn(0, 360).spin(-360, 360).color(0xd9f3ff).drag(1.6).gravity(0, -10)
               .sizeOver({{0, 1}, {0.7, 1}, {1, 0}}).solid().look("diamond"));
    add(e, Make(id, "Glints").burst(14, 0.03).circle(0.9).life(0.4, 0.9).speed(1, 4).aim(90, 180)
               .size(0.3, 0.7).color(0xffffff).drag(2).gravity(0, 0)
               .sizeOver({{0, 0.1}, {0.35, 1}, {1, 0.1}}).fade({{0, 0}, {0.3, 1}, {1, 0}})
               .look("sparkle").additive().glow(1.5));
    return e;
}

// ---------------------------------------------------------------- weather

Effect rain(const IdSource& id) {
    Effect e = begin(id, "Rain", 3.0);
    add(e, Make(id, "Far rain").rate(240).rect(12, 8).life(0.3, 0.45).speed(11, 14).aim(262, 1)
               .size(0.05, 0.08).color(0x9fc0ee, 0.8).gravity(0, 0)
               .fade({{0, 0}, {0.2, 1}, {0.8, 1}, {1, 0}}).look("streak").streak(0.04).sound("rain-loop", false, 0.0, 0.7).soundLoop());
    add(e, Make(id, "Near rain").rate(100).rect(12, 8).life(0.28, 0.4).speed(16, 20).aim(262, 1)
               .size(0.08, 0.13).color(0xdbe9ff, 1.0).gravity(0, 0)
               .fade({{0, 0}, {0.2, 1}, {0.8, 1}, {1, 0}}).look("streak").streak(0.045));
    return e;
}

Effect snow(const IdSource& id) {
    Effect e = begin(id, "Snow", 6.0);
    add(e, Make(id, "Far flakes").rate(60).rect(12, 8).life(3.0, 4.5).speed(0.35, 0.7).aim(265, 14)
               .size(0.06, 0.11).color(0xdfeaff, 0.7).gravity(0, 0)
               .fade({{0, 0}, {0.15, 1}, {0.85, 1}, {1, 0}}).turbulence(0.35, 2.4, 0.3));
    add(e, Make(id, "Near flakes").rate(22).rect(12, 8).life(2.6, 3.8).speed(0.7, 1.3).aim(262, 16)
               .size(0.2, 0.38).turn(0, 60).spin(-40, 40).color(0xffffff, 0.9).gravity(0, 0)
               .fade({{0, 0}, {0.15, 1}, {0.85, 1}, {1, 0}}).look("snowflake").turbulence(0.55, 2.0, 0.3));
    return e;
}

Effect fireflies(const IdSource& id) {
    Effect e = begin(id, "Fireflies", 6.0);
    add(e, Make(id, "Fireflies").rate(5).rect(9, 5).life(2.6, 4.0).speed(0.15, 0.45).aim(90, 180)
               .size(0.3, 0.46).color(0xa8f03c).gravity(0, 0)
               .fade({{0, 0}, {0.25, 1}, {0.5, 0.2}, {0.75, 1}, {1, 0}}).additive().glow(2.1)
               .turbulence(0.7, 1.4, 0.5));
    return e;
}

// --------------------------------------------------------------- match-3
// Juicy, glossy candy effects in the manner of the big match-3 games: short,
// front-loaded, saturated, with white gloss, white flashes and twinkles.
// One board tile is about 1.2 units across.

const unsigned kCandyColors[6] = {0xff2d55, 0xff8a1f, 0xffd21f, 0x2ed46b, 0x2b8cff, 0xa04dff};

Effect candyPop(const IdSource& id) {
    Effect e = begin(id, "Candy Pop", 1.0);
    add(e, Make(id, "Flash").burst(1).life(0.16, 0.16).speed(0, 0).size(2.6, 2.6).color(0xffffff)
               .gravity(0, 0).sizeOver({{0, 0.4}, {0.3, 1.0}, {1, 1.1}}).fade({{0, 0.9}, {1, 0}})
               .additive().glow(1.4));
    add(e, Make(id, "Pop ring").burst(1).life(0.28, 0.28).speed(0, 0).size(2.8, 2.8).color(0xffd1dc)
               .gravity(0, 0).sizeOver({{0, 0.25}, {1, 1}}).fade({{0, 1}, {0.6, 0.6}, {1, 0}})
               .look("ring").additive().glow(1.2));
    add(e, Make(id, "Candy").burst(7).circle(0.3).life(0.5, 0.75).speed(4.5, 7.5).aim(90, 180)
               .size(0.32, 0.48).turn(0, 360).spin(-500, 500).color(0xff2d55).gravity(0, -11).drag(0.8)
               .sizeOver({{0, 0.6}, {0.1, 1}, {0.75, 1}, {1, 0}}).solid().look("candy").sound("pop", false, 0.0, 1.0, 2.0));
    add(e, Make(id, "Shards").burst(6).circle(0.3).life(0.45, 0.7).speed(5, 9).aim(90, 180)
               .size(0.22, 0.34).turn(0, 360).spin(-600, 600).color(0xff6b86).gravity(0, -11).drag(0.8)
               .sizeOver({{0, 1}, {0.75, 1}, {1, 0}}).solid().look("shard"));
    add(e, Make(id, "Twinkles").burst(4).circle(0.7).life(0.35, 0.55).speed(0.3, 1.2).aim(90, 180)
               .size(0.7, 1.1).turn(0, 45).color(0xfff2c8).gravity(0, 0)
               .sizeOver({{0, 0}, {0.3, 1}, {1, 0}}).solid().look("twinkle").additive().glow(1.3));
    add(e, Make(id, "Sugar").burst(12).life(0.4, 0.7).speed(3, 7).aim(90, 180).size(0.05, 0.1)
               .color(0xffffff, 0.9).gravity(0, -6).drag(1).sizeOver({{0, 1}, {1, 0}}).solid().look("disc"));
    return e;
}

Effect jellySplat(const IdSource& id) {
    Effect e = begin(id, "Jelly Splat", 1.2);
    add(e, Make(id, "Splat").burst(1).life(0.6, 0.6).speed(0, 0).size(2.8, 2.8).turn(0, 360)
               .color(0xff4fa0).gravity(0, 0).sizeOver({{0, 0.2}, {0.12, 1.08}, {0.25, 1}, {1, 0.95}})
               .fade({{0, 1}, {0.55, 1}, {1, 0}}).look("splat").sound("splat", false, 0.0, 1.0, 1.5));
    add(e, Make(id, "Drops").burst(10).circle(0.4).life(0.4, 0.65).speed(5, 9).aim(90, 180)
               .size(0.32, 0.55).color(0xff6fb5).gravity(0, -12)
               .sizeOver({{0, 1}, {0.8, 0.9}, {1, 0}}).solid().look("drop").streak(0.0));
    add(e, Make(id, "Droplets").burst(12).circle(0.4).life(0.35, 0.6).speed(3, 8).aim(90, 180)
               .size(0.07, 0.14).color(0xffa6d2).gravity(0, -10).sizeOver({{0, 1}, {1, 0}}).solid()
               .look("disc"));
    add(e, Make(id, "Shine").burst(1, 0.05).circle(0.4).life(0.3, 0.3).speed(0, 0).size(1.4, 1.4)
               .color(0xffffff).gravity(0, 0).sizeOver({{0, 0}, {0.3, 1}, {1, 0}}).solid()
               .look("twinkle").additive().glow(1.4));
    return e;
}

Effect lineBlast(const IdSource& id, bool down) {
    Effect e = begin(id, down ? "Line Blast Down" : "Line Blast Across", 1.1);
    const double a = down ? 90.0 : 0.0;
    add(e, Make(id, "Charge").burst(1).life(0.2, 0.2).speed(0, 0).size(3.0, 3.0).color(0xffffff)
               .gravity(0, 0).sizeOver({{0, 0.3}, {0.3, 1}, {1, 0.8}}).fade({{0, 1}, {1, 0}})
               .additive().glow(1.6).sound("laser", true, 0.0, 0.8, 1.0));
    for (const double heading : {a, a + 180.0}) {
        add(e, Make(id, "Glow beam").burst(1).life(0.42, 0.42).speed(30, 30).aim(heading, 0)
                   .size(2.4, 2.4).color(0x4fc3ff).gravity(0, 0).fade({{0, 0.8}, {1, 0}})
                   .look("streak").additive().glow(1.4).streak(0.34));
        add(e, Make(id, "Core beam").burst(1).life(0.4, 0.4).speed(30, 30).aim(heading, 0)
                   .size(1.0, 1.0).color(0xfff6d8).gravity(0, 0).fade({{0, 1}, {0.6, 1}, {1, 0}})
                   .look("streak").additive().glow(2.2).streak(0.30));
    }
    Make twinkles(id, "Twinkles");
    twinkles.from(0.04, 0.3).rate(70).life(0.25, 0.45).speed(0, 0.6).aim(90, 180).size(0.4, 0.8)
        .turn(0, 45).color(0xfff1b8).gravity(0, 0).sizeOver({{0, 0}, {0.3, 1}, {1, 0}}).solid()
        .look("twinkle").additive().glow(1.3);
    down ? twinkles.rect(0.25, 12) : twinkles.rect(12, 0.25);
    add(e, twinkles);
    const char* looks[3] = {"candy", "bean", "swirl"};
    for (int i = 0; i < 3; ++i) {
        Make pieces(id, "Candy");
        pieces.burst(5, 0.06).life(0.5, 0.8).speed(2, 5).aim(down ? 0 : 90, down ? 180 : 60)
            .size(0.45, 0.65).turn(0, 360).spin(-400, 400).color(kCandyColors[i * 2]).gravity(0, -9)
            .sizeOver({{0, 0.5}, {0.15, 1}, {0.8, 1}, {1, 0}}).solid().look(looks[i]);
        down ? pieces.rect(0.4, 10) : pieces.rect(10, 0.4);
        add(e, pieces);
    }
    return e;
}

Effect lineBlastAcross(const IdSource& id) { return lineBlast(id, false); }
Effect lineBlastDown(const IdSource& id) { return lineBlast(id, true); }

Effect candyBomb(const IdSource& id) {
    Effect e = begin(id, "Candy Bomb", 1.4);
    for (const double at : {0.0, 0.35}) {
        const double s = at > 0.0 ? 1.25 : 1.0;  // the second blast is bigger
        add(e, Make(id, "Flash").burst(1, at).life(0.14, 0.14).speed(0, 0).size(3.2 * s, 3.2 * s)
                   .color(0xffffff).gravity(0, 0).sizeOver({{0, 0.3}, {0.3, 1}, {1, 1}})
                   .fade({{0, 1}, {1, 0}}).additive().glow(1.5));
        add(e, Make(id, "Shockwave").burst(1, at).life(0.32, 0.32).speed(0, 0).size(5.5 * s, 5.5 * s)
                   .color(0xffd86b).gravity(0, 0).sizeOver({{0, 0.2}, {1, 1}})
                   .fade({{0, 1}, {0.4, 0.8}, {1, 0}}).look("shockwave").additive().glow(1.5));
    }
    add(e, Make(id, "Burst").burst(1).life(0.3, 0.3).speed(0, 0).size(4.8, 4.8).turn(0, 36)
               .color(0xffd23f).gravity(0, 0).sizeOver({{0, 0.3}, {0.25, 1}, {0.6, 0.9}, {1, 0}}).solid()
               .look("burst").sound("hit"));
    add(e, Make(id, "Puffs").burst(8, 0.35).circle(0.4).life(0.5, 0.8).speed(2.5, 5).aim(90, 180)
               .size(1.1, 1.7).turn(-30, 30).color(0xff9ac8).drag(4).gravity(0, 0)
               .sizeOver({{0, 0.35}, {0.2, 1}, {0.65, 0.85}, {1, 0}}).solid().look("puff").sound("boom", true, 0.0, 0.9));
    const char* looks[4] = {"candy", "bean", "swirl", "star"};
    const unsigned colors[4] = {0xff2d55, 0x2ed46b, 0xff2d55, 0xffd21f};
    for (int i = 0; i < 4; ++i) {
        add(e, Make(id, "Candy").burst(4, 0.35).circle(0.5).life(0.6, 0.9).speed(6, 11).aim(90, 180)
                   .size(0.35, 0.55).turn(0, 360).spin(-450, 450).color(colors[i]).gravity(0, -12).drag(0.6)
                   .sizeOver({{0, 0.5}, {0.1, 1}, {0.8, 1}, {1, 0}}).solid().look(looks[i]));
    }
    add(e, Make(id, "Twinkles").burst(5, 0.38).circle(1.6).life(0.35, 0.6).speed(0.5, 2).aim(90, 180)
               .size(0.6, 1.1).turn(0, 45).color(0xfff2c8).gravity(0, 0)
               .sizeOver({{0, 0}, {0.3, 1}, {1, 0}}).solid().look("twinkle").additive().glow(1.4));
    return e;
}

Effect rainbowBurst(const IdSource& id) {
    Effect e = begin(id, "Rainbow Burst", 1.6);
    add(e, Make(id, "Rays").burst(1).life(0.8, 0.8).speed(0, 0).size(6.5, 6.5).turn(0, 36)
               .spin(120, 120).color(0xfff2a8).gravity(0, 0).sizeOver({{0, 0.4}, {0.3, 1}, {1, 1.1}})
               .fade({{0, 0}, {0.2, 0.9}, {1, 0}}).look("rays").additive().glow(1.2));
    add(e, Make(id, "Glow").burst(1).life(0.6, 0.6).speed(0, 0).size(4, 4).color(0xd7b8ff)
               .gravity(0, 0).sizeOver({{0, 0.5}, {0.3, 1.1}, {1, 1}}).fade({{0, 0.8}, {1, 0}})
               .additive());
    add(e, Make(id, "Orb").burst(1).life(0.55, 0.55).speed(0, 0).size(1.6, 1.6).color(0x8a4bff)
               .gravity(0, 0).sizeOver({{0, 0.2}, {0.3, 1.15}, {0.6, 1}, {1, 0}}).solid().look("orb").sound("shimmer"));
    add(e, Make(id, "Flare").burst(1, 0.1).life(0.4, 0.4).speed(0, 0).size(3.5, 3.5).turn(0, 30)
               .color(0xffffff).gravity(0, 0).sizeOver({{0, 0.3}, {0.25, 1}, {1, 0.7}})
               .fade({{0, 1}, {1, 0}}).look("flare").additive().glow(1.6).sound("sparkle", true));
    for (const unsigned c : kCandyColors) {
        add(e, Make(id, "Beams").burst(3, 0.12).life(0.3, 0.45).speed(18, 26).aim(90, 180).size(0.5, 0.7)
                   .color(c).gravity(0, 0).fade({{0, 1}, {0.6, 1}, {1, 0}}).look("streak").additive()
                   .glow(1.8).streak(0.12));
        add(e, Make(id, "Candy").burst(3, 0.15).circle(0.4).life(0.6, 0.9).speed(5, 9).aim(90, 180)
                   .size(0.28, 0.42).turn(0, 360).spin(-400, 400).color(c).gravity(0, -10)
                   .sizeOver({{0, 0.5}, {0.1, 1}, {0.8, 1}, {1, 0}}).solid().look("candy"));
    }
    return e;
}

Effect sweetCelebration(const IdSource& id) {
    Effect e = begin(id, "Sweet Celebration", 3.0);
    const char* looks[5] = {"candy", "bean", "swirl", "star", "heart"};
    const unsigned colors[5] = {0xff2d55, 0x2ed46b, 0xff8a1f, 0xffd21f, 0xff6fb5};
    for (int i = 0; i < 5; ++i) {
        add(e, Make(id, "Candy").burst(8).rect(10, 0.2).life(2.0, 2.8).speed(9, 14).aim(90, 25)
                   .size(0.55, 0.85).turn(0, 360).spin(-300, 300).color(colors[i]).gravity(0, -9).drag(1)
                   .fade({{0, 1}, {0.85, 1}, {1, 0}}).look(looks[i]));
    }
    add(e, Make(id, "Twinkles").rate(12).rect(10, 6).life(0.4, 0.7).speed(0, 0.3).aim(90, 180)
               .size(0.5, 1.0).turn(0, 45).color(0xfff2c8).gravity(0, 0)
               .sizeOver({{0, 0}, {0.3, 1}, {1, 0}}).solid().look("twinkle").additive().glow(1.4).sound("level-up", false, 0.0, 0.9));
    for (const unsigned c : {0xff6fb5u, 0x4fc3ffu, 0xffd21fu}) {
        add(e, Make(id, "Flares").rate(1.2).rect(8, 5).life(0.35, 0.5).speed(0, 0).size(1.6, 2.6)
                   .turn(0, 30).color(c).gravity(0, 0).sizeOver({{0, 0.2}, {0.25, 1}, {1, 0.6}})
                   .fade({{0, 1}, {1, 0}}).look("flare").additive().glow(1.6));
    }
    return e;
}

Effect collectSparkle(const IdSource& id) {
    Effect e = begin(id, "Collect Sparkle", 0.9);
    add(e, Make(id, "Ring").burst(1).life(0.3, 0.3).speed(0, 0).size(2.2, 2.2).color(0xfff3b0)
               .gravity(0, 0).sizeOver({{0, 0.3}, {1, 1}}).fade({{0, 1}, {1, 0}}).look("ring").additive()
               .glow(1.4));
    add(e, Make(id, "Glow").burst(1).life(0.35, 0.35).speed(0, 0).size(2.4, 2.4).color(0xffe27a)
               .gravity(0, 0).sizeOver({{0, 0.4}, {0.3, 1}, {1, 0.9}}).fade({{0, 0.8}, {1, 0}}).additive());
    add(e, Make(id, "Twinkle").burst(1).life(0.4, 0.4).speed(0, 0).size(2.6, 2.6).color(0xffffff)
               .gravity(0, 0).sizeOver({{0, 0}, {0.25, 1.15}, {0.5, 1}, {1, 0}}).solid().look("twinkle")
               .additive().glow(1.6).sound("collect", false, 0.0, 1.0, 1.0));
    add(e, Make(id, "Sparkles").burst(8).life(0.35, 0.55).speed(2.5, 4.5).aim(90, 180).size(0.35, 0.6)
               .turn(0, 45).color(0xffe27a).drag(3).gravity(0, 0).sizeOver({{0, 1}, {1, 0}}).solid()
               .look("twinkle").additive().glow(1.3));
    return e;
}

Effect hintGlow(const IdSource& id) {
    Effect e = begin(id, "Hint Glow", 1.2);
    add(e, Make(id, "Pulse").burst(1).life(1.2, 1.2).speed(0, 0).size(2.2, 2.2).color(0xffd86b)
               .gravity(0, 0).sizeOver({{0, 0.85}, {0.5, 1.05}, {1, 0.85}})
               .fade({{0, 0.15}, {0.5, 0.45}, {1, 0.15}}).additive());
    add(e, Make(id, "Ring").burst(1).life(0.6, 0.6).speed(0, 0).size(2.6, 2.6).color(0xfff2b0)
               .gravity(0, 0).sizeOver({{0, 0.55}, {1, 1}}).fade({{0, 0}, {0.2, 0.9}, {1, 0}})
               .look("ring").additive().glow(1.3));
    add(e, Make(id, "Twinkles").rate(3).circle(0.8, true).life(0.5, 0.7).speed(0, 0.2).aim(90, 180)
               .size(0.4, 0.7).turn(0, 45).color(0xffffff).gravity(0, 0)
               .sizeOver({{0, 0}, {0.4, 1}, {1, 0}}).solid().look("twinkle").additive().glow(1.3));
    return e;
}

// ------------------------------------------------------- light and energy

Effect starburstFlare(const IdSource& id) {
    Effect e = begin(id, "Starburst Flare", 1.2);
    add(e, Make(id, "Halo").burst(1).life(0.5, 0.5).speed(0, 0).size(5, 5).color(0x4fa8ff)
               .gravity(0, 0).sizeOver({{0, 0.3}, {0.2, 1}, {1, 1.1}}).fade({{0, 0.9}, {1, 0}}).additive());
    add(e, Make(id, "Flare").burst(1).life(0.5, 0.5).speed(0, 0).size(5, 5).turn(0, 30).color(0xbfe4ff)
               .gravity(0, 0).sizeOver({{0, 0.3}, {0.2, 1}, {1, 0.8}}).fade({{0, 1}, {0.4, 0.9}, {1, 0}})
               .look("flare").additive().glow(1.6).sound("sparkle", true, 0.0, 0.8));
    add(e, Make(id, "Twinkles").burst(6).circle(1.5).life(0.35, 0.6).speed(0.5, 2).aim(90, 180)
               .size(0.5, 0.9).turn(0, 45).color(0xdff1ff).gravity(0, 0)
               .sizeOver({{0, 0}, {0.3, 1}, {1, 0}}).solid().look("twinkle").additive().glow(1.4));
    return e;
}

Effect rewardRays(const IdSource& id) {
    Effect e = begin(id, "Reward Rays", 4.0);
    add(e, Make(id, "Glow").burst(1).life(4, 4).speed(0, 0).size(4.5, 4.5).color(0xffc94a)
               .gravity(0, 0).fade({{0, 0}, {0.1, 0.7}, {0.9, 0.7}, {1, 0}}).additive().sound("shimmer", false, 0.0, 0.8));
    add(e, Make(id, "Rays").burst(1).life(4, 4).speed(0, 0).size(7, 7).color(0xffd75e).spin(20, 20)
               .gravity(0, 0).fade({{0, 0}, {0.15, 0.8}, {0.85, 0.8}, {1, 0}}).look("rays").additive()
               .glow(1.1));
    add(e, Make(id, "Inner rays").burst(1).life(4, 4).speed(0, 0).size(6, 6).turn(18, 18)
               .color(0xfff3c4).spin(-14, -14).gravity(0, 0).fade({{0, 0}, {0.15, 0.7}, {0.85, 0.7}, {1, 0}})
               .look("rays").additive());
    add(e, Make(id, "Twinkles").rate(5).circle(2.2).life(0.5, 0.8).speed(0, 0.3).aim(90, 180)
               .size(0.5, 0.9).turn(0, 45).color(0xffffff).gravity(0, 0)
               .sizeOver({{0, 0}, {0.3, 1}, {1, 0}}).solid().look("twinkle").additive().glow(1.4));
    return e;
}

Effect meteorShower(const IdSource& id) {
    Effect e = begin(id, "Meteor Shower", 3.0);
    for (const unsigned c : {0x4fb6ffu, 0xc44dffu, 0xff7a1au}) {
        add(e, Make(id, "Comets").rate(0.9).rect(12, 0.2).life(1.1, 1.4).speed(8, 10).aim(240, 3)
                   .size(0.75, 0.95).color(c).gravity(0, 0).fade({{0, 0}, {0.08, 1}, {0.85, 1}, {1, 0}})
                   .look("drop").additive().glow(1.8).streak(0.0).trail(0.45, 0.7));
    }
    add(e, Make(id, "Stars").rate(10).rect(16, 12).life(0.6, 1.2).speed(0, 0).size(0.15, 0.3)
               .color(0xffffff).gravity(0, 0).fade({{0, 0}, {0.5, 1}, {1, 0}}).look("twinkle").additive());
    return e;
}

Effect stylizedComets(const IdSource& id) {
    Effect e = begin(id, "Stylized Comets", 2.4);
    const unsigned colors[4] = {0xff8a1a, 0xb44dff, 0x3aa8ff, 0xff4a2a};
    const double where[4][2] = {{-2.6, 2.4}, {0.2, 3.2}, {2.8, 3.8}, {-0.8, 5.2}};
    for (int i = 0; i < 4; ++i) {
        const double at = 0.3 * i;
        const double ox = where[i][0], oy = where[i][1];
        // Each comet: a coloured glow, a white-hot head with a long ribbon,
        // and thinner wisps flowing beside it. Same place, same speed, so
        // they travel together.
        add(e, Make(id, "Glow").burst(1, at).at(ox, oy).life(1.5, 1.5).speed(7, 7).aim(235, 0)
                   .size(2.0, 2.0).color(colors[i], 0.6).gravity(0, 0)
                   .fade({{0, 0}, {0.06, 0.6}, {0.8, 0.6}, {1, 0}}).additive());
        add(e, Make(id, "Comet").burst(1, at).at(ox, oy).life(1.5, 1.5).speed(7, 7).aim(235, 0)
                   .size(0.9, 0.9).color(colors[i]).gravity(0, 0).fade({{0, 0}, {0.06, 1}, {0.8, 1}, {1, 0}})
                   .look("drop").additive().glow(1.7).streak(0.0).trail(0.8, 0.85).sound("whoosh", true, -0.05, 0.7, 2.0));
        add(e, Make(id, "Wisps").burst(3, at).at(ox, oy).circle(0.25).life(1.5, 1.5).speed(6.3, 6.9)
                   .aim(235, 2).size(0.3, 0.5).color(colors[i], 0.8).gravity(0, 0)
                   .fade({{0, 0}, {0.1, 0.8}, {0.7, 0.5}, {1, 0}}).look("soft").additive().glow(1.2)
                   .trail(0.55, 0.6));
    }
    add(e, Make(id, "Sparkles").rate(9).rect(12, 9).life(0.5, 0.9).speed(0, 0.2).aim(90, 180)
               .size(0.3, 0.6).turn(0, 45).color(0xdff1ff).gravity(0, 0)
               .sizeOver({{0, 0}, {0.3, 1}, {1, 0}}).solid().look("twinkle").additive().glow(1.4));
    return e;
}

Effect fireworkSparks(const IdSource& id) {
    Effect e = begin(id, "Firework Sparks", 2.0);
    add(e, Make(id, "Flash").burst(1).life(0.18, 0.18).speed(0, 0).size(3.5, 3.5).turn(0, 30)
               .color(0xfff0c8).gravity(0, 0).sizeOver({{0, 0.3}, {0.3, 1}, {1, 0.8}}).fade({{0, 1}, {1, 0}})
               .look("flare").additive().glow(1.6).sound("boom", true, 0.0, 0.6));
    for (const unsigned c : {0xffb347u, 0xff5ab4u, 0x6fd0ffu}) {
        add(e, Make(id, "Sparks").burst(14).life(0.9, 1.4).speed(6, 10).aim(90, 180).size(0.14, 0.22)
                   .color(c).gravity(0, -5).drag(1.4).fade({{0, 1}, {0.7, 0.8}, {1, 0}}).look("soft")
                   .additive().glow(2.0).trail(0.35, 1.2));
    }
    return e;
}

Effect lightningStrike(const IdSource& id) {
    Effect e = begin(id, "Lightning Strike", 1.2);
    // The bolt flickers: several short strikes, each turned a little.
    for (const double at : {0.0, 0.07, 0.15, 0.3}) {
        add(e, Make(id, "Bolt").burst(1, at).life(0.09, 0.12).speed(0, 0).size(6, 6.8).turn(82, 98)
                   .color(0x7fc8ff).gravity(0, 0).fade({{0, 1}, {1, 0.3}}).look("bolt").additive()
                   .glow(2.2));
    }
    add(e, Make(id, "Glow").burst(1).life(0.4, 0.4).speed(0, 0).size(5, 5).color(0x2f7dff)
               .gravity(0, 0).fade({{0, 0.9}, {0.3, 0.4}, {0.4, 0.8}, {1, 0}}).additive().sound("thunder", true, 0.0, 1.0));
    add(e, Make(id, "Sparks").burst(14).rect(0.3, 5).life(0.2, 0.4).speed(1, 3).aim(90, 180)
               .size(0.3, 0.55).turn(0, 45).color(0xbfe6ff).gravity(0, 0)
               .sizeOver({{0, 1}, {1, 0}}).solid().look("twinkle").additive().glow(1.6));
    return e;
}

Effect electricOrb(const IdSource& id) {
    Effect e = begin(id, "Electric Orb", 2.0);
    add(e, Make(id, "Glow").rate(2).life(1.0, 1.0).speed(0, 0).size(3.6, 4.2).color(0x2f7dff)
               .gravity(0, 0).fade({{0, 0}, {0.5, 0.6}, {1, 0}}).additive().sound("electric-loop", false, 0.0, 0.6).soundLoop());
    add(e, Make(id, "Core").rate(4).life(0.5, 0.5).speed(0, 0).size(1.6, 1.9).turn(0, 30)
               .color(0xd8f0ff).gravity(0, 0).fade({{0, 0}, {0.5, 1}, {1, 0}}).look("flare").additive()
               .glow(1.6));
    add(e, Make(id, "Arcs").rate(16).circle(0.2).life(0.08, 0.14).speed(0, 0).size(2.2, 3.0)
               .turn(0, 360).color(0x8fd8ff).gravity(0, 0).fade({{0, 1}, {1, 0.2}}).look("bolt")
               .additive().glow(2.0));
    return e;
}

// ---------------------------------------------------------------- flashes
// Bursts of light: hard white spikes in a coloured halo, slivers of light
// flying out, and for some a ring breaking apart.

Effect flash(const IdSource& id, const char* name, unsigned halo, unsigned tint, bool ring) {
    Effect e = begin(id, name, 1.0);
    add(e, Make(id, "Halo").burst(1).life(0.45, 0.45).speed(0, 0).size(5.2, 5.2).color(halo)
               .gravity(0, 0).sizeOver({{0, 0.4}, {0.2, 1}, {1, 1.1}}).fade({{0, 0.9}, {1, 0}}).additive());
    add(e, Make(id, "Flash").burst(1).life(0.35, 0.35).speed(0, 0).size(4.2, 4.2).turn(0, 360)
               .color(tint).gravity(0, 0).sizeOver({{0, 0.3}, {0.15, 1.05}, {0.5, 1}, {1, 0.6}})
               .fade({{0, 1}, {0.5, 0.9}, {1, 0}}).look("starflash").additive().glow(1.6).sound("hit", true, 0.0, 0.8, 1.5));
    add(e, Make(id, "Slivers").burst(12).circle(0.3).life(0.35, 0.6).speed(6, 11).aim(90, 180)
               .size(0.5, 0.9).color(tint).drag(4).gravity(0, 0).sizeOver({{0, 1}, {1, 0}}).solid()
               .look("sliver").additive().glow(1.6).streak(0.0).sound("sparkle", true, 0.03, 0.5, 2.0));
    add(e, Make(id, "Specks").burst(10).circle(0.6).life(0.3, 0.6).speed(2, 5).aim(90, 180)
               .size(0.25, 0.45).turn(0, 45).color(tint).drag(3).gravity(0, 0)
               .sizeOver({{0, 1}, {1, 0}}).solid().look("twinkle").additive().glow(1.3));
    if (ring) {
        add(e, Make(id, "Ring").burst(1).life(0.4, 0.4).speed(0, 0).size(4.6, 4.6).turn(0, 360)
                   .color(halo).gravity(0, 0).sizeOver({{0, 0.5}, {1, 1.15}}).fade({{0, 1}, {0.6, 0.8}, {1, 0}})
                   .look("shardring").additive().glow(1.2));
    }
    return e;
}

Effect goldFlash(const IdSource& id) { return flash(id, "Gold Flash", 0xff8a2a, 0xffd9a0, false); }
Effect iceFlash(const IdSource& id) { return flash(id, "Ice Flash", 0x2f7dff, 0xbfe4ff, false); }
Effect roseFlash(const IdSource& id) { return flash(id, "Rose Flash", 0xff3a8a, 0xffc4dc, true); }
Effect violetFlash(const IdSource& id) { return flash(id, "Violet Flash", 0x7a3cff, 0xe2d0ff, false); }
Effect emberFlash(const IdSource& id) { return flash(id, "Ember Flash", 0xc0401a, 0xffc890, true); }

// ----------------------------------------------------------------- table

// ---------------------------------------------------------------- ground
// Effects that lie on the floor and are seen from an angle: a shape laid
// flat and turning in its own plane, light rising out of it. The way a
// game engine builds them: a flat disc with a picture on it, upright
// streaks of light, and small sparks drifting up.

// A sprite that is there for the whole of a looping effect must hand over
// to its next self without a blink; see keepWholePass.
Effect seamless(Effect e) {
    tools::smoothLoop(e);
    return e;
}

// Thin shafts of light standing on the ground, each fading in and out.
void risingBeams(Effect& e, const IdSource& id, unsigned colour, double width, double height, double perSecond) {
    Make m(id, "Beams");
    m.rate(perSecond).rect(width, 0.25 * width).at(0.0, 0.5 * height)
        .life(0.9, 1.7).speed(0.25, 0.6).aim(90, 0).size(0.1, 0.22).color(colour).gravity(0, 0)
        .sizeOver({{0, 0.6}, {0.3, 1.0}, {1, 0.7}}).fade({{0, 0}, {0.25, 1}, {0.6, 0.8}, {1, 0}})
        .look("beam").additive().glow(1.3).streak(height / 0.42);
    add(e, m);
}

void risingMotes(Effect& e, const IdSource& id, unsigned colour, double width, double perSecond) {
    Make m(id, "Motes");
    m.rate(perSecond).rect(width, 0.3 * width).at(0.0, 0.2)
        .life(0.9, 1.8).speed(0.5, 1.5).aim(90, 12).size(0.05, 0.12).color(colour).gravity(0, 0.4)
        .sizeOver({{0, 0}, {0.25, 1.0}, {1, 0}}).solid().look("disc").additive().glow(1.4)
        .turbulence(0.6, 0.9, 0.7);
    add(e, m);
}

Effect magicCircle(const IdSource& id) {
    const double tilt = 70.0;
    Effect e = begin(id, "Magic Circle", 6.0);
    add(e, Make(id, "Ground glow").burst(1).life(6.0, 6.0).speed(0, 0).size(8.2, 8.2).color(0x1583ff, 0.6)
               .gravity(0, 0).sizeOver({{0, 1.0}, {0.5, 1.06}, {1, 1.0}}).solid().look("soft").additive()
               .lieFlat(tilt).sound("shimmer", false, 0.0, 0.7));
    add(e, Make(id, "Circle").burst(1).life(6.0, 6.0).speed(0, 0).size(6.4, 6.4).color(0x35c8ff)
               .gravity(0, 0).spin(30, 30).solid().look("sigil").additive().glow(1.0).lieFlat(tilt));
    add(e, Make(id, "Inner circle").burst(1).life(6.0, 6.0).speed(0, 0).size(3.3, 3.3).color(0x7fe6ff)
               .gravity(0, 0).spin(-60, -60).solid().look("sigil").additive().glow(1.0).lieFlat(tilt));
    add(e, Make(id, "Pulse").rate(0.5).life(2.0, 2.0).speed(0, 0).size(7.0, 7.0).color(0x8fe9ff)
               .gravity(0, 0).sizeOver({{0, 0.25}, {1, 1.0}}).fade({{0, 0}, {0.15, 0.7}, {1, 0}})
               .look("ring").additive().lieFlat(tilt));
    add(e, Make(id, "Core").burst(1).life(6.0, 6.0).speed(0, 0).size(2.4, 2.4).color(0xbff3ff, 0.7)
               .gravity(0, 0).sizeOver({{0, 1.0}, {0.25, 1.12}, {0.5, 1.0}, {0.75, 1.12}, {1, 1.0}})
               .solid().look("soft").additive().lieFlat(tilt));
    risingBeams(e, id, 0x9fe8ff, 3.6, 3.2, 7.0);
    risingMotes(e, id, 0xd8f8ff, 4.4, 12.0);
    return seamless(e);
}

Effect vortexPortal(const IdSource& id) {
    const double tilt = 66.0;
    Effect e = begin(id, "Vortex Portal", 6.0);
    add(e, Make(id, "Ground glow").burst(1).life(6.0, 6.0).speed(0, 0).size(8.6, 8.6).color(0x2b35ff, 0.55)
               .gravity(0, 0).sizeOver({{0, 1.0}, {0.5, 1.05}, {1, 1.0}}).solid().look("soft").additive()
               .lieFlat(tilt).sound("shimmer", false, 0.0, 0.7));
    add(e, Make(id, "Vortex").burst(1).life(6.0, 6.0).speed(0, 0).size(7.4, 7.4).color(0x3560ff)
               .gravity(0, 0).spin(60, 60).solid().look("vortex").additive().glow(1.25).lieFlat(tilt));
    add(e, Make(id, "Inner vortex").burst(1).life(6.0, 6.0).speed(0, 0).size(4.8, 4.8).turn(50, 50)
               .color(0x8f4dff).gravity(0, 0).spin(120, 120).solid().look("vortex").additive().glow(1.2)
               .lieFlat(tilt));
    add(e, Make(id, "Wisps").rate(1.5).life(2.0, 2.0).speed(0, 0).size(5.0, 7.0).turn(0, 360)
               .color(0x4fd0ff, 0.6).gravity(0, 0).spin(80, 110).sizeOver({{0, 0.7}, {1, 1.1}})
               .fade({{0, 0}, {0.35, 1}, {1, 0}}).look("vortex").additive().lieFlat(tilt));
    add(e, Make(id, "Core").burst(1).life(6.0, 6.0).speed(0, 0).size(2.1, 2.1).color(0xf3e9ff)
               .gravity(0, 0).sizeOver({{0, 1.0}, {0.25, 1.14}, {0.5, 1.0}, {0.75, 1.14}, {1, 1.0}})
               .solid().look("soft").additive().glow(1.5).lieFlat(tilt));
    risingBeams(e, id, 0x8fc4ff, 2.2, 3.4, 5.0);
    risingMotes(e, id, 0xe2d8ff, 3.6, 9.0);
    return seamless(e);
}

Effect groundSlam(const IdSource& id) {
    const double tilt = 68.0;
    Effect e = begin(id, "Ground Slam", 1.4);
    add(e, Make(id, "Flash").burst(1).life(0.22, 0.22).speed(0, 0).size(7.0, 7.0).color(0xfff1d0)
               .gravity(0, 0).sizeOver({{0, 0.4}, {1, 1.0}}).fade({{0, 1}, {1, 0}}).look("soft").additive()
               .glow(1.6).lieFlat(tilt).sound("boom", true));
    add(e, Make(id, "Shockwave").burst(1).life(0.6, 0.6).speed(0, 0).size(9.0, 9.0).color(0xffd9a0)
               .gravity(0, 0).sizeOver({{0, 0.12}, {0.35, 0.75}, {1, 1.0}}).fade({{0, 1}, {0.5, 0.8}, {1, 0}})
               .look("shockwave").additive().glow(1.2).lieFlat(tilt));
    add(e, Make(id, "Second ring").from(0.08, 1.0).burst(1).life(0.7, 0.7).speed(0, 0).size(6.0, 6.0)
               .color(0xffb066).gravity(0, 0).sizeOver({{0, 0.1}, {0.4, 0.8}, {1, 1.0}})
               .fade({{0, 0.9}, {1, 0}}).look("ring").additive().lieFlat(tilt));
    add(e, Make(id, "Dust").burst(14).rect(3.0, 0.5).life(0.6, 1.1).speed(0.6, 2.4).aim(90, 80)
               .size(0.9, 1.7).color(0xb9a48c, 0.8).gravity(0, 0.3).drag(2.4)
               .sizeOver({{0, 0.5}, {1, 1.5}}).fade({{0, 0.8}, {0.4, 0.6}, {1, 0.25}}).look("smoke")
               .turbulence(0.4, 1.2, 0.5).dissolve(0.6, 0.0, 0.5));
    add(e, Make(id, "Chips").burst(16).rect(2.2, 0.3).life(0.5, 0.9).speed(3.0, 7.0).aim(90, 50)
               .size(0.1, 0.24).turn(0, 360).color(0xe8cfa4).gravity(0, -12).spin(-400, 400)
               .fade({{0, 1}, {0.8, 1}, {1, 0}}).look("shard"));
    return e;
}

// ------------------------------------------------------- smoke and wind
// What turbulence and dissolve are for: things carried about by moving air,
// winding as they go, and things eaten away in patches as they die.

Effect risingSmoke(const IdSource& id) {
    Effect e = begin(id, "Rising Smoke", 6.0);
    add(e, Make(id, "Smoke").rate(9).circle(0.2).at(0.0, -2.2).life(3.0, 4.0).speed(1.0, 1.5).aim(90, 8)
               .size(0.8, 1.3).turn(0, 360).spin(-20, 20).color(0x8f8a84, 0.6).gravity(0, 0.15)
               .sizeOver({{0, 0.4}, {1, 2.0}}).fade({{0, 0}, {0.12, 0.75}, {0.6, 0.6}, {1, 0.25}})
               .look("smoke").turbulence(0.9, 1.6, 0.4).dissolve(0.5, 0.0, 0.7));
    add(e, Make(id, "Thin smoke").rate(6).circle(0.15).at(0.0, -2.2).life(2.4, 3.4).speed(1.2, 1.9).aim(90, 10)
               .size(0.4, 0.75).turn(0, 360).spin(-30, 30).color(0xcfcac3, 0.4).gravity(0, 0.2)
               .sizeOver({{0, 0.4}, {1, 2.2}}).fade({{0, 0}, {0.15, 0.7}, {0.6, 0.5}, {1, 0.2}})
               .look("smoke").turbulence(1.3, 1.1, 0.5).dissolve(0.5, 0.0, 0.8));
    return e;
}

Effect groundMist(const IdSource& id) {
    Effect e = begin(id, "Ground Mist", 8.0);
    add(e, Make(id, "Mist").rate(5).rect(11, 0.8).at(0.0, -1.4).life(4.5, 6.5).speed(0.2, 0.5).aim(0, 14)
               .size(2.8, 4.2).turn(0, 360).spin(-6, 6).color(0xcfdcea, 0.3).gravity(0, 0)
               .sizeOver({{0, 0.7}, {1, 1.3}}).fade({{0, 0}, {0.25, 1}, {0.6, 1}, {1, 0.35}})
               .look("smoke").turbulence(0.35, 2.5, 0.25).dissolve(0.5, 0.0, 0.9));
    add(e, Make(id, "Low mist").rate(4).rect(11, 0.4).at(0.0, -2.1).life(4.0, 6.0).speed(0.3, 0.7).aim(0, 8)
               .size(1.8, 2.8).turn(0, 360).color(0xe6eef7, 0.26).gravity(0, 0)
               .sizeOver({{0, 0.8}, {1, 1.25}}).fade({{0, 0}, {0.25, 1}, {0.6, 1}, {1, 0.35}})
               .look("smoke").turbulence(0.3, 2.0, 0.25).dissolve(0.5, 0.0, 0.9));
    return e;
}

Effect spiritWisps(const IdSource& id) {
    Effect e = begin(id, "Spirit Wisps", 6.0);
    add(e, Make(id, "Wisps").rate(12).rect(6, 0.2).at(0.0, -2.4).life(3.0, 3.6).speed(1.2, 1.6).aim(90, 4)
               .size(0.14, 0.2).color(0x8fd8ff).gravity(0, 0)
               .fade({{0, 0}, {0.1, 1}, {0.8, 1}, {1, 0}}).additive().glow(1.6).turbulence(1.8, 1.4, 0.5)
               .trail(1.2, 0.5).sound("shimmer", false, 0.0, 0.6));
    add(e, Make(id, "Motes").rate(16).rect(6.5, 3.0).at(0.0, -0.6).life(1.6, 2.6).speed(0.2, 0.7).aim(90, 30)
               .size(0.04, 0.09).color(0xd6f3ff).gravity(0, 0.2)
               .fade({{0, 0}, {0.3, 1}, {0.7, 1}, {1, 0}}).additive().glow(1.8).turbulence(1.2, 1.2, 0.6));
    return e;
}

Effect burnAway(const IdSource& id) {
    Effect e = begin(id, "Burn Away", 2.8);
    // A sheet that burns through from many places at once, its eaten edges
    // white-hot. Swap its Shape, or give it a picture, to burn anything.
    add(e, Make(id, "Sheet").burst(1).life(2.0, 2.0).speed(0, 0).size(4.0, 4.0).color(0xc79a5e)
               .gravity(0, 0).solid()
               .tint({{0.0, 1.0, 1.0, 1.0, 1.0}, {0.45, 0.85, 0.66, 0.46, 1.0}, {1.0, 0.3, 0.18, 0.12, 1.0}})
               .look("square").dissolve(0.85, 1.0, 0.03).sound("fire-loop", false, 0.0, 0.7));
    add(e, Make(id, "Fire glow").burst(1).life(2.0, 2.0).speed(0, 0).size(6.4, 6.4).color(0xff6a1a)
               .gravity(0, 0).fade({{0, 0}, {0.3, 0.3}, {0.75, 0.3}, {1, 0}}).additive());
    add(e, Make(id, "Embers").from(0.3, 1.7).rate(46).rect(3.4, 3.4).life(0.8, 1.6).speed(0.4, 1.8).aim(90, 40)
               .size(0.05, 0.1).color(0xffa040).gravity(0, 1.2).drag(0.3)
               .fade({{0, 1}, {0.6, 1}, {1, 0}}).additive().glow(2.4).turbulence(1.6, 0.8, 0.9));
    add(e, Make(id, "Smoke").from(0.4, 1.7).rate(7).rect(3.0, 3.0).life(1.2, 1.9).speed(0.6, 1.2).aim(90, 14)
               .size(0.9, 1.5).turn(0, 360).spin(-25, 25).color(0x3a3431, 0.5).gravity(0, 0.5)
               .sizeOver({{0, 0.5}, {1, 1.6}}).fade({{0, 0}, {0.25, 0.7}, {1, 0.2}}).look("smoke")
               .turbulence(0.7, 1.3, 0.5).dissolve(0.5, 0.0, 0.7));
    return e;
}

Effect vanish(const IdSource& id) {
    Effect e = begin(id, "Vanish", 1.8);
    // Something magical breaking up and blowing away: for a pickup taken, a
    // character teleporting out, a spell ending.
    add(e, Make(id, "Body").burst(1).life(1.1, 1.1).speed(0, 0).size(3.0, 3.0).color(0x58d6ff)
               .gravity(0, 0).sizeOver({{0, 1.0}, {1, 1.2}}).solid().look("orb").additive().glow(1.1)
               .dissolve(0.9, 1.0, 0.04).sound("whoosh", true, 0.0, 0.8));
    add(e, Make(id, "Ring").burst(1).life(0.45, 0.45).speed(0, 0).size(5.2, 5.2).color(0xbdf0ff)
               .gravity(0, 0).sizeOver({{0, 0.3}, {1, 1.0}}).fade({{0, 0.9}, {1, 0}}).look("ring").additive());
    add(e, Make(id, "Dust").from(0.1, 0.8).rate(110).circle(1.1).life(0.7, 1.3).speed(0.3, 1.6).aim(90, 180)
               .size(0.05, 0.11).color(0xa8ecff).gravity(0, 0.6).drag(0.6)
               .fade({{0, 0}, {0.15, 1}, {0.6, 1}, {1, 0}}).additive().glow(2.0).turbulence(2.2, 0.8, 1.0));
    add(e, Make(id, "Glints").from(0.1, 0.7).rate(14).circle(1.2).life(0.4, 0.8).speed(0.2, 1.0).aim(90, 180)
               .size(0.3, 0.6).color(0xeafaff).gravity(0, 0).sizeOver({{0, 0.1}, {0.3, 1}, {1, 0.1}})
               .fade({{0, 0}, {0.25, 1}, {1, 0}}).look("sparkle").additive().glow(1.6));
    return e;
}

// -------------------------------------------------------- cartoon motion
// The animation principles at work: squash and stretch, anticipation,
// overshoot and settle, arcs, and drawings held "on twos".

Effect bouncyPop(const IdSource& id) {
    Effect e = begin(id, "Bouncy Pop", 1.5);
    // The blob arrives stretched tall, lands flat, wobbles and settles, then
    // stretches away again as it goes.
    add(e, Make(id, "Blob").burst(1).life(1.05, 1.05).speed(0, 0).size(2.5, 2.5).color(0xff5fa2).gravity(0, 0)
               .sizeOver({{0, 0.15}, {0.1, 1.1, true}, {0.22, 0.96, true}, {0.8, 1.0, true}, {0.9, 1.08, true}, {1, 0}})
               .squash({{0, 0.75}, {0.1, -0.42, true}, {0.2, 0.26, true}, {0.31, -0.14, true}, {0.43, 0.06, true},
                        {0.55, 0, true}, {0.82, 0, true}, {0.9, -0.3, true}, {1, 0.9}})
               .solid().look("candy").hold(12).sound("pop", true, 0.0, 0.9, 1.0));
    add(e, Make(id, "Ring").burst(1, 0.08).life(0.3, 0.3).speed(0, 0).size(4.6, 4.6).color(0xffd1e6).gravity(0, 0)
               .sizeOver({{0, 0.35}, {0.5, 0.9, true}, {1, 1.0, true}}).fade({{0, 1}, {0.5, 0.8}, {1, 0}})
               .look("shockwave").hold(12));
    // Drops thrown out: long and thin while they are fast, round as they slow.
    add(e, Make(id, "Drops").burst(9, 0.08).life(0.45, 0.75).speed(4.5, 8.5).aim(90, 150).size(0.24, 0.42)
               .color(0xff8fc0).drag(2.4).gravity(0, -9).sizeOver({{0, 1}, {0.7, 0.9}, {1, 0}})
               .squash({{0, 0.9}, {0.5, 0.25, true}, {1, -0.1, true}}).solid().look("disc").streak(0.0).hold(12));
    add(e, Make(id, "Twinkles").burst(5, 0.1).circle(1.5).life(0.3, 0.5).speed(0.3, 1.2).aim(90, 180)
               .size(0.4, 0.7).color(0xffffff).gravity(0, 0).sizeOver({{0, 0.1}, {0.35, 1.0}, {1, 0}}).solid()
               .look("glint").hold(12));
    return e;
}

Effect windUpBlast(const IdSource& id) {
    Effect e = begin(id, "Wind-up Blast", 1.9);
    // Anticipation: motes are thrown out and drawn back in, and the core
    // shrinks and flattens, before anything goes off.
    add(e, Make(id, "Gather").burst(26).life(0.56, 0.62).speed(7.5, 12.0).aim(90, 180).size(0.14, 0.26)
               .color(0x9fe8ff).gravity(0, 0)
               .pace({{0, 2.2}, {0.3, 0.0, true}, {0.55, -0.25, true}, {1, -0.75}})
               .sizeOver({{0, 0.4}, {0.3, 1.0}, {1, 0.5}}).fade({{0, 0}, {0.15, 1}, {0.9, 1}, {1, 0}})
               .look("disc").additive().glow(1.8).trail(0.12, 0.8));
    add(e, Make(id, "Core").burst(1).life(1.0, 1.0).speed(0, 0).size(1.9, 1.9).color(0x4fc3ff).gravity(0, 0)
               .sizeOver({{0, 0.0}, {0.14, 0.7, true}, {0.42, 0.62, true}, {0.56, 0.34, true}, {0.62, 1.7}, {0.75, 1.15, true}, {1, 0}})
               .squash({{0, 0}, {0.4, 0, true}, {0.56, -0.5, true}, {0.62, 0.55}, {0.72, -0.12, true}, {0.82, 0, true}, {1, 0}})
               .solid().look("orb").hold(12));
    add(e, Make(id, "Flash").burst(1, 0.6).life(0.2, 0.2).speed(0, 0).size(6.4, 6.4).turn(0, 30).color(0xe9fbff)
               .gravity(0, 0).sizeOver({{0, 0.3}, {0.35, 1.0}, {1, 0.7}}).fade({{0, 1}, {0.55, 1}, {1, 0}})
               .look("starflash").additive().glow(1.6).hold(12).sound("zap", true, 0.0, 0.9));
    add(e, Make(id, "Ring").burst(1, 0.62).life(0.42, 0.42).speed(0, 0).size(7.2, 7.2).color(0xbfefff)
               .gravity(0, 0).sizeOver({{0, 0.15}, {0.4, 0.8, true}, {1, 1.0, true}}).fade({{0, 1}, {0.6, 0.7}, {1, 0}})
               .look("shockwave").additive().hold(12));
    // The blast itself: off at once, then coasting to a stop (slow in).
    add(e, Make(id, "Shards").burst(14, 0.62).life(0.45, 0.75).speed(6, 11).aim(90, 180).size(0.42, 0.78)
               .color(0x7fd8ff).gravity(0, 0).pace({{0, 2.6}, {0.3, 0.5, true}, {1, 0.05, true}})
               .squash({{0, 1.2}, {0.4, 0.3, true}, {1, 0, true}})
               .sizeOver({{0, 1}, {0.6, 0.9}, {1, 0}}).solid().look("shard").streak(0.0).hold(12));
    return e;
}

Effect spiralBurst(const IdSource& id) {
    Effect e = begin(id, "Spiral Burst", 1.8);
    // Arcs: the same burst twice, turned opposite ways about the centre.
    add(e, Make(id, "Glow").burst(1).life(0.5, 0.5).speed(0, 0).size(4.2, 4.2).color(0xffb347).gravity(0, 0)
               .sizeOver({{0, 0.3}, {0.3, 1.0}, {1, 1.2}}).fade({{0, 0.8}, {1, 0}}).additive());
    add(e, Make(id, "Arms").burst(12).life(0.9, 1.2).speed(2.6, 3.4).aim(90, 180).size(0.26, 0.4)
               .color(0xffd76a).gravity(0, 0).orbit(210).pace({{0, 2.2}, {0.4, 0.7, true}, {1, 0.2, true}})
               .sizeOver({{0, 0.6}, {0.2, 1.0}, {1, 0}}).solid().look("glint").additive().glow(1.8)
               .trail(0.42, 0.9));
    add(e, Make(id, "Counter arms").burst(8, 0.05).life(0.7, 1.0).speed(1.6, 2.4).aim(90, 180).size(0.16, 0.26)
               .color(0xff8a5c).gravity(0, 0).orbit(-260).pace({{0, 2.2}, {0.4, 0.7, true}, {1, 0.2, true}})
               .sizeOver({{0, 0.6}, {0.2, 1.0}, {1, 0}}).solid().look("disc").additive().glow(1.6)
               .trail(0.34, 0.9));
    add(e, Make(id, "Star").burst(1).life(0.34, 0.34).speed(0, 0).size(3.4, 3.4).color(0xfff3c2).gravity(0, 0)
               .spin(180, 180).sizeOver({{0, 0.2}, {0.3, 1.0}, {1, 0}}).solid().look("starflash").additive()
               .glow(1.5).sound("sparkle", true, 0.0, 0.8));
    return e;
}

Effect springyPoof(const IdSource& id) {
    Effect e = begin(id, "Springy Poof", 1.6);
    // Overshoot and settle: the puffs fly past where they will rest, come
    // back a little, and hang there before they tear away.
    add(e, Make(id, "Puffs").burst(7).circle(0.15).life(0.9, 1.15).speed(4.2, 6.4).aim(90, 180).size(1.0, 1.5)
               .turn(-30, 30).color(0xe9e4ff).gravity(0, 0.25)
               .pace({{0, 3.0}, {0.14, 0.9, true}, {0.26, -0.75, true}, {0.42, 0.12, true}, {0.6, 0.02, true}, {1, 0.1}})
               .sizeOver({{0, 0.3}, {0.14, 1.12, true}, {0.3, 0.94, true}, {0.6, 1.0, true}, {1, 0.85}})
               .squash({{0, 0.5}, {0.14, -0.3, true}, {0.28, 0.14, true}, {0.42, 0, true}, {1, 0}})
               .solid().look("puff").dissolve(0.45, 0.0, 0.05).hold(12).sound("pop", true, 0.0, 0.8, 1.0));
    add(e, Make(id, "Small puffs").burst(8).circle(0.15).life(0.6, 0.9).speed(6.5, 9.5).aim(90, 180)
               .size(0.45, 0.8).turn(-40, 40).color(0xc9bfff).gravity(0, 0.25)
               .pace({{0, 3.0}, {0.16, 0.8, true}, {0.3, -0.6, true}, {0.46, 0.08, true}, {1, 0.05}})
               .sizeOver({{0, 0.4}, {0.2, 1.0}, {1, 0.6}}).solid().look("puff").dissolve(0.5, 0.0, 0.05).hold(12));
    add(e, Make(id, "Lines").burst(10).circle(0.6).life(0.16, 0.26).speed(9, 14).aim(90, 180).size(0.1, 0.16)
               .color(0xffffff).drag(5).gravity(0, 0).fade({{0, 1}, {0.5, 1}, {1, 0}}).look("streak")
               .streak(0.05).hold(12));
    add(e, Make(id, "Stars").burst(6, 0.05).life(0.55, 0.85).speed(9, 13).aim(90, 180).size(0.36, 0.6)
               .color(0xfff0a8).gravity(0, 0).spin(-200, 200)
               .pace({{0, 2.6}, {0.2, 0.6, true}, {0.36, -0.4, true}, {0.55, 0, true}, {1, 0}})
               .sizeOver({{0, 0.3}, {0.2, 1.1, true}, {0.4, 0.95, true}, {0.8, 1.0}, {1, 0}}).solid().look("star")
               .hold(12));
    return e;
}

// ------------------------------------------------- with the shape library
// Effects built on shapes from the library: leaves, trophies, runes, and
// the marks that float over a character's head.

Effect fallingLeaves(const IdSource& id) {
    Effect e = begin(id, "Falling Leaves", 6.0);
    const unsigned colors[3] = {0xe8742a, 0xd9a321, 0xb5452a};
    const char* names[3] = {"Orange leaves", "Yellow leaves", "Red leaves"};
    for (int i = 0; i < 3; ++i) {
        // Each leaf turns over and back as it falls: squashed thin, then
        // full again, twice on the way down.
        add(e, Make(id, names[i]).rate(4).rect(12, 6).at(0, 3).life(4.0, 5.4).speed(0.6, 1.2).aim(264, 18)
                   .size(0.4, 0.7).turn(0, 360).spin(-90, 90).color(colors[i]).gravity(0, -0.2)
                   .squash({{0, 0, true}, {0.2, -0.6, true}, {0.4, 0, true}, {0.6, -0.6, true}, {0.8, 0, true}, {1, -0.4}})
                   .fade({{0, 0}, {0.1, 1}, {0.88, 1}, {1, 0}}).look("leaf").turbulence(0.9, 2.2, 0.4));
    }
    return e;
}

Effect trophyWin(const IdSource& id) {
    Effect e = begin(id, "Trophy Win", 3.0);
    add(e, Make(id, "Rays").burst(1).life(2.6, 2.6).speed(0, 0).size(7.6, 7.6).color(0xffc531).gravity(0, 0)
               .spin(24, 24).sizeOver({{0, 0.2}, {0.15, 1.0, true}, {1, 1.05}})
               .fade({{0, 0}, {0.12, 0.6}, {0.8, 0.5}, {1, 0}}).look("rays").additive());
    add(e, Make(id, "Trophy").burst(1).life(2.6, 2.6).speed(0, 0).size(3.2, 3.2).color(0xffc531).gravity(0, 0)
               .sizeOver({{0, 0.0}, {0.08, 1.15, true}, {0.16, 0.95, true}, {0.3, 1.0, true}, {0.92, 1.0}, {1, 0}})
               .squash({{0, 0.6}, {0.08, -0.35, true}, {0.16, 0.2, true}, {0.24, -0.08, true}, {0.34, 0, true}, {1, 0}})
               .solid().look("trophy").hold(12).sound("level-up", true, 0.0, 0.9));
    const unsigned colors[3] = {0xff4d6d, 0x3ddc97, 0x3aa0ff};
    const char* names[3] = {"Red paper", "Green paper", "Blue paper"};
    for (int i = 0; i < 3; ++i) {
        add(e, Make(id, names[i]).burst(12, 0.1).rect(1.2, 0.1).at(0, -1.2).life(1.8, 2.5).speed(7, 12).aim(90, 34)
                   .size(0.28, 0.44).turn(0, 360).spin(-420, 420).color(colors[i]).gravity(0, -6).drag(1.8)
                   .fade({{0, 1}, {0.85, 1}, {1, 0}}).look(i == 1 ? "ribbon" : "confetti").turbulence(1.0, 1.3, 0.6));
    }
    add(e, Make(id, "Stars").burst(8, 0.08).life(0.9, 1.3).speed(6, 9).aim(90, 180).size(0.36, 0.6)
               .turn(-20, 20).color(0xfff1a8).gravity(0, 0)
               .pace({{0, 3.0}, {0.14, 0.9, true}, {0.26, -0.75, true}, {0.42, 0.12, true}, {0.6, 0, true}, {1, 0}})
               .sizeOver({{0, 0.2}, {0.2, 1.1, true}, {0.4, 1.0, true}, {0.8, 1.0}, {1, 0}}).solid().look("starfat")
               .hold(12));
    return e;
}

Effect runeWard(const IdSource& id) {
    // Eighteen seconds a pass: the outer circle turns once in it and the
    // inner one twice the other way, so the loop has no seam.
    Effect e = begin(id, "Rune Ward", 18.0);
    add(e, Make(id, "Glow").rate(2).life(1.6, 1.6).speed(0, 0).size(6.4, 6.4).color(0x7a4dff).gravity(0, 0)
               .fade({{0, 0}, {0.5, 0.22}, {1, 0}}).additive().lieFlat(64));
    add(e, Make(id, "Outer circle").burst(1).life(18.0, 18.0).speed(0, 0).size(5.6, 5.6).color(0xb79cff).gravity(0, 0)
               .spin(20, 20).solid().look("triad").additive().glow(1.5).lieFlat(64));
    add(e, Make(id, "Inner circle").burst(1).life(18.0, 18.0).speed(0, 0).size(2.9, 2.9).color(0xe2d6ff).gravity(0, 0)
               .spin(-40, -40).solid().look("pentacle").additive().glow(1.5).lieFlat(64));
    const char* runes[3] = {"runea", "runeb", "runec"};
    const char* names[3] = {"Runes of arms", "Runes of the lozenge", "Runes of the stroke"};
    for (int i = 0; i < 3; ++i) {
        add(e, Make(id, names[i]).rate(1.1).rect(4.2, 0.6).at(0, 0.3).life(1.5, 2.0).speed(0.7, 1.2).aim(90, 0)
                   .size(0.62, 0.92).color(0xd9c8ff).gravity(0, 0)
                   .sizeOver({{0, 0.6}, {0.2, 1.0, true}, {1, 1.0}}).fade({{0, 0}, {0.2, 1}, {0.7, 1}, {1, 0}})
                   .look(runes[i]).additive().glow(1.4));
    }
    add(e, Make(id, "Wall").rate(0.75).at(0, 1.5).life(1.3, 1.3).speed(0, 0).size(4.4, 4.4).color(0x9f86ff)
               .gravity(0, 0).sizeOver({{0, 0.85}, {1, 1.05}}).fade({{0, 0}, {0.2, 0.42}, {1, 0}})
               .look("hexshield").additive());
    add(e, Make(id, "Motes").rate(14).rect(4.4, 0.6).life(0.9, 1.5).speed(0.8, 1.8).aim(90, 6).size(0.06, 0.12)
               .color(0xcbb8ff).gravity(0, 0).fade({{0, 0}, {0.2, 1}, {0.7, 1}, {1, 0}}).additive().glow(2.0)
               .turbulence(0.6, 1.2, 0.6));
    tools::smoothLoop(e);
    return e;
}

Effect buffUp(const IdSource& id) {
    Effect e = begin(id, "Buff Up", 1.8);
    add(e, Make(id, "Ground ring").burst(1).life(0.6, 0.6).speed(0, 0).size(4.4, 4.4).color(0x5dff8a).gravity(0, 0)
               .sizeOver({{0, 0.2}, {0.5, 0.85, true}, {1, 1.0, true}}).fade({{0, 0.9}, {0.5, 0.6}, {1, 0}})
               .look("shockwave").additive().lieFlat(66));
    add(e, Make(id, "Column").from(0, 0.8).rate(10).rect(1.6, 0.2).life(0.6, 0.9).speed(2.5, 3.5).aim(90, 0)
               .size(1.6, 2.2).color(0x2fe37a).gravity(0, 0).fade({{0, 0}, {0.3, 0.25}, {1, 0}}).additive());
    // The arrows shoot up and coast: off at once, slowing to a stop.
    add(e, Make(id, "Arrows").from(0.05, 0.75).rate(12).rect(2.0, 0.3).life(0.8, 1.1).speed(3.0, 4.4).aim(90, 0)
               .size(0.55, 0.85).turn(90, 90).color(0x8dffb0).gravity(0, 0)
               .pace({{0, 2.6}, {0.3, 0.5, true}, {1, 0.05, true}})
               .squash({{0, 0.5}, {0.3, 0.0, true}, {1, 0}})
               .fade({{0, 0}, {0.12, 1}, {0.7, 1}, {1, 0}}).look("chevron").hold(12)
               .sound("level-up", false, 0.0, 0.7));
    add(e, Make(id, "Plus signs").burst(7, 0.1).rect(2.4, 1.2).at(0, 0.8).life(0.8, 1.2).speed(0.8, 1.6).aim(90, 20)
               .size(0.2, 0.36).color(0xd6ffe2).gravity(0, 0)
               .sizeOver({{0, 0.0}, {0.2, 1.15, true}, {0.35, 1.0, true}, {1, 0.7}})
               .fade({{0, 1}, {0.7, 1}, {1, 0}}).look("plus").hold(12));
    return e;
}

Effect sleepZs(const IdSource& id) {
    Effect e = begin(id, "Sleep", 3.0);
    add(e, Make(id, "Zs").rate(1.1).at(0.2, 0.2).life(2.2, 2.4).speed(0.8, 1.0).aim(64, 6).size(0.9, 1.1)
               .turn(-8, 8).color(0xbfd4ff).gravity(0, 0)
               .sizeOver({{0, 0.25}, {0.3, 0.8, true}, {1, 1.2, true}})
               .squash({{0, -0.12, true}, {0.25, 0.12, true}, {0.5, -0.12, true}, {0.75, 0.12, true}, {1, -0.12, true}})
               .fade({{0, 0}, {0.15, 1}, {0.7, 1}, {1, 0}}).look("zed").hold(8).turbulence(0.25, 1.6, 0.3));
    add(e, Make(id, "Bubble").rate(0.5).at(-0.5, -0.2).life(1.8, 1.8).speed(0.2, 0.3).aim(110, 5).size(0.5, 0.6)
               .color(0xdbe7ff).gravity(0, 0).sizeOver({{0, 0.2}, {0.8, 1.0, true}, {0.9, 1.25}, {1, 0}})
               .fade({{0, 0}, {0.2, 0.9}, {0.9, 0.9}, {1, 0}}).look("bubble"));
    return e;
}

Effect poisoned(const IdSource& id) {
    Effect e = begin(id, "Poisoned", 3.0);
    add(e, Make(id, "Fumes").rate(5).circle(0.6).life(1.2, 1.8).speed(0.5, 1.1).aim(90, 25).size(0.9, 1.4)
               .turn(-30, 30).color(0x5a9a2a, 0.75).gravity(0, 0.3).sizeOver({{0, 0.4}, {0.4, 1.0, true}, {1, 1.15}})
               .fade({{0, 0}, {0.2, 1}, {1, 0.4}}).look("puffball").dissolve(0.6, 0.0, 0.5).turbulence(0.5, 1.4, 0.5)
               .hold(12));
    add(e, Make(id, "Skull").rate(0.75).at(0, 0.3).life(1.2, 1.2).speed(0.5, 0.6).aim(90, 0).size(1.3, 1.5)
               .color(0xb9f25e).gravity(0, 0)
               .sizeOver({{0, 0.0}, {0.14, 1.15, true}, {0.26, 0.96, true}, {0.4, 1.0, true}, {1, 1.0}})
               .squash({{0, 0.4}, {0.14, -0.25, true}, {0.26, 0.1, true}, {0.4, 0, true}, {1, 0}})
               .fade({{0, 1}, {0.7, 1}, {1, 0}}).look("skull").hold(12));
    add(e, Make(id, "Bubbles").rate(9).circle(0.8).life(0.7, 1.2).speed(0.8, 1.8).aim(90, 14).size(0.14, 0.3)
               .color(0xcaf78a).gravity(0, 0.4).sizeOver({{0, 0.3}, {0.85, 1.0}, {0.9, 1.3}, {1, 0}}).solid()
               .look("bubble").turbulence(0.4, 0.9, 0.6));
    add(e, Make(id, "Drips").rate(4).rect(1.6, 0.2).at(0, 0.2).life(0.5, 0.8).speed(0.2, 0.6).aim(270, 6)
               .size(0.2, 0.32).color(0x8fdc3c).gravity(0, -7).sizeOver({{0, 0.4}, {0.3, 1.0}, {1, 0.8}})
               .squash({{0, 0.0}, {1, 0.5}}).fade({{0, 1}, {0.8, 1}, {1, 0}}).look("droplet"));
    return e;
}

Effect stunned(const IdSource& id) {
    Effect e = begin(id, "Stunned", 2.4);
    // Five stars going round and round. Each pops out of the middle, is
    // carried once round by Orbit and is gone as the next one arrives, so
    // they sit a fifth of a turn apart however long the effect plays.
    add(e, Make(id, "Stars").burst(1, 0.0).burst(1, 0.48).burst(1, 0.96).burst(1, 1.44).burst(1, 1.92)
               .life(2.4, 2.4).speed(2.3, 2.3).aim(0, 0).size(0.6, 0.6).color(0xffd84a).gravity(0, 0)
               .orbit(150).spin(-180, -180)
               .pace({{0, 14}, {0.07, -1.2, true}, {0.14, 0, true}, {1, 0}})
               .sizeOver({{0, 0.0}, {0.06, 1.2, true}, {0.14, 1.0, true}, {0.92, 1.0}, {1, 0}})
               .squash({{0, 0.15, true}, {0.125, -0.15, true}, {0.25, 0.15, true}, {0.375, -0.15, true},
                        {0.5, 0.15, true}, {0.625, -0.15, true}, {0.75, 0.15, true}, {0.875, -0.15, true}, {1, 0.15, true}})
               .solid().look("starfat"));
    add(e, Make(id, "Dizzy").burst(1).life(2.4, 2.4).speed(0, 0).size(1.5, 1.5).color(0xfff2b0).gravity(0, 0)
               .spin(300, 300).solid().look("spiral").additive().glow(1.3));
    add(e, Make(id, "Trail dots").rate(22).circle(1.25, true).life(0.35, 0.55).speed(0, 0).size(0.08, 0.16)
               .color(0xfff6cf).gravity(0, 0).orbit(150).sizeOver({{0, 1}, {1, 0}}).solid().look("disc"));
    tools::smoothLoop(e);
    return e;
}

Effect alert(const IdSource& id) {
    Effect e = begin(id, "Alert", 1.5);
    add(e, Make(id, "Lines").burst(1).life(0.24, 0.24).speed(0, 0).size(5.4, 5.4).turn(0, 30).color(0xfff0c4)
               .gravity(0, 0).sizeOver({{0, 0.5}, {1, 1.0}}).fade({{0, 1}, {0.6, 1}, {1, 0}}).look("impact")
               .hold(12));
    add(e, Make(id, "Bang").burst(1).life(0.3, 0.3).speed(0, 0).size(4.2, 4.2).color(0xffd23f).gravity(0, 0)
               .sizeOver({{0, 0.3}, {0.4, 1.0, true}, {1, 1.1}}).fade({{0, 1}, {0.6, 1}, {1, 0}}).look("pow")
               .hold(12));
    // The mark lands flat, springs up past itself and settles.
    add(e, Make(id, "Mark").burst(1, 0.04).life(1.2, 1.2).speed(0, 0).size(2.6, 2.6).color(0xff4d4d).gravity(0, 0)
               .sizeOver({{0, 0.2}, {0.1, 1.15, true}, {0.2, 0.95, true}, {0.32, 1.0, true}, {0.88, 1.0}, {1, 0}})
               .squash({{0, -0.5}, {0.1, 0.35, true}, {0.2, -0.15, true}, {0.3, 0.06, true}, {0.4, 0, true}, {1, 0}})
               .solid().look("exclaim").hold(12).sound("hit", true, 0.0, 0.8, 1.0));
    return e;
}

// ---------------------------------------------------------------------- 3D
// Effects with depth, made to be seen from any side: round a character,
// on the ground, rising past the camera.

Effect aura3d(const IdSource& id) {
    Effect e = begin3d(id, "Aura 3D", 2.0);
    // A ring lying on the ground round the feet, breathing.
    add(e, Make(id, "Ground ring").rate(1).life(2.0, 2.0).speed(0, 0).size(2.6, 2.6).color(0x5dff8a).gravity(0, 0)
               .sizeOver({{0, 0.85}, {0.5, 1.0, true}, {1, 0.85, true}}).fade({{0, 0}, {0.25, 0.8}, {0.75, 0.8}, {1, 0}})
               .look("softring").additive().ground().flatOnPlane());
    add(e, Make(id, "Ground glow").rate(1).life(2.0, 2.0).speed(0, 0).size(3.4, 3.4).color(0x2fe37a).gravity(0, 0)
               .fade({{0, 0}, {0.3, 0.35}, {0.7, 0.35}, {1, 0}}).additive().ground().flatOnPlane());
    // Motes rising from the ring, turning round the character as they go.
    add(e, Make(id, "Rising motes").rate(36).circle(1.05, true).life(1.2, 1.8).speed(0.8, 1.3).toward(0, 1, 0, 4)
               .size(0.07, 0.13).color(0x9dffbe).gravity(0, 0).orbit(70).ground()
               .fade({{0, 0}, {0.2, 1}, {0.8, 1}, {1, 0}}).additive().glow(2.0));
    add(e, Make(id, "Crosses").rate(4).circle(0.9).life(1.2, 1.6).speed(0.9, 1.3).toward(0, 1, 0, 8).size(0.16, 0.24)
               .color(0xd6ffe2).gravity(0, 0).ground().sizeOver({{0, 0}, {0.2, 1.1, true}, {0.35, 1, true}, {1, 0.6}})
               .fade({{0, 1}, {0.75, 1}, {1, 0}}).look("plus").hold(12));
    tools::smoothLoop(e);
    return e;
}

Effect levelUp3d(const IdSource& id) {
    Effect e = begin3d(id, "Level-up Pillar 3D", 2.0);
    add(e, Make(id, "Shockwave").burst(1).life(0.6, 0.6).speed(0, 0).size(4.0, 4.0).color(0xffd76a).gravity(0, 0)
               .sizeOver({{0, 0.1}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("shockwave").additive().ground()
               .flatOnPlane());
    add(e, Make(id, "Column").rate(40).circle(0.7, true).life(0.5, 0.8).speed(5, 7).toward(0, 1, 0, 0).size(0.07, 0.11)
               .color(0xffe08a).gravity(0, 0).ground().additive().glow(1.6).streak(0.07));
    add(e, Make(id, "Glow").rate(1).life(2.0, 2.0).speed(0, 0).at3(0, 1.4, 0).size(3.2, 3.2).color(0xffb02e)
               .gravity(0, 0).fade({{0, 0}, {0.2, 0.3}, {0.8, 0.3}, {1, 0}}).additive());
    add(e, Make(id, "Stars").rate(9).circle(0.9, true).life(1.1, 1.5).speed(1.4, 2.2).toward(0, 1, 0, 10)
               .size(0.22, 0.32).color(0xfff1a8).gravity(0, -0.6).orbit(-90).ground().spin(-90, 90)
               .sizeOver({{0, 0}, {0.15, 1.15, true}, {0.3, 1, true}, {0.85, 1}, {1, 0}}).solid().look("starfat")
               .hold(12));
    tools::smoothLoop(e);
    return e;
}

Effect portal3d(const IdSource& id) {
    // Four seconds a pass, so the whirl and the rim each turn a whole number
    // of times in it and nothing jumps when the loop starts again.
    Effect e = begin3d(id, "Portal 3D", 4.0);
    add(e, Make(id, "Vortex").burst(1).life(4.0, 4.0).speed(0, 0).size(3.2, 3.2).color(0x9f6bff).gravity(0, 0)
               .spin(180, 180).solid().look("vortex").additive().glow(1.4).ground().flatOnPlane());
    add(e, Make(id, "Rim").burst(1).life(4.0, 4.0).speed(0, 0).size(3.3, 3.3).color(0xd6c2ff).gravity(0, 0)
               .spin(-45, -45).solid().look("portalring").additive().ground().flatOnPlane());
    add(e, Make(id, "Beams").rate(10).circle(1.2, true).life(0.5, 0.8).speed(3, 4.5).toward(0, 1, 0, 0)
               .size(0.12, 0.2).color(0xb48cff).gravity(0, 0).ground().additive().streak(0.18)
               .fade({{0, 0}, {0.2, 1}, {1, 0}}));
    add(e, Make(id, "Motes").rate(40).circle(1.6, true).life(1.0, 1.6).speed(0.2, 0.6).toward(0, 1, 0, 20)
               .size(0.06, 0.12).color(0xe9dcff).gravity(0, 0).orbit(160).ground()
               .fade({{0, 0}, {0.2, 1}, {0.8, 1}, {1, 0}}).additive().glow(2.2));
    tools::smoothLoop(e);
    return e;
}

Effect groundSlam3d(const IdSource& id) {
    Effect e = begin3d(id, "Ground Slam 3D", 1.6);
    add(e, Make(id, "Shockwave").burst(1).life(0.5, 0.5).speed(0, 0).size(5.0, 5.0).color(0xfff0c4).gravity(0, 0)
               .sizeOver({{0, 0.1}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("shockwave").additive().ground()
               .flatOnPlane());
    add(e, Make(id, "Crack").burst(1).life(1.2, 1.2).speed(0, 0).size(2.4, 2.4).color(0x3a2c22, 0.9).gravity(0, 0)
               .fade({{0, 1}, {0.7, 1}, {1, 0}}).look("crack").ground().flatOnPlane());
    add(e, Make(id, "Dust").burst(26).circle(1.0, true).life(0.8, 1.3).speed(1.5, 3.0).toward(0, 1, 0, 75)
               .size(0.5, 0.9).color(0xc9b79a).gravity(0, -1.0).drag(2.5).spin(-60, 60).ground()
               .sizeOver({{0, 0.4}, {0.3, 1.0, true}, {1, 1.3}}).fade({{0, 0.9}, {0.6, 0.7}, {1, 0}}).look("puff"));
    add(e, Make(id, "Rocks").burst(16).circle(0.6).life(0.6, 1.0).speed(4, 7).toward(0, 1, 0, 40).size(0.12, 0.22)
               .color(0x8a7a6a).gravity(0, -14).spin(-400, 400).ground().solid().look("rock"));
    return e;
}

Effect fire3d(const IdSource& id) {
    Effect e = begin3d(id, "Campfire 3D", 2.0);
    add(e, Make(id, "Smoke").rate(6).sphere(0.3).at3(0, 0.9, 0).life(1.8, 2.4).speed(0.5, 0.8).toward(0, 1, 0, 15)
               .size(0.5, 0.8).color(0x6b6f7a, 0.8).gravity(0, 0.2).spin(-30, 30)
               .sizeOver({{0, 0.5}, {1, 1.6}}).fade({{0, 0}, {0.2, 0.7}, {1, 0}}).look("puff").turbulence(0.4, 1.5, 0.5));
    add(e, Make(id, "Flames").rate(30).sphere(0.32).at3(0, 0.15, 0).life(0.5, 0.8).speed(1.0, 1.7).toward(0, 1, 0, 8)
               .size(0.45, 0.75).color(0xff7a22).gravity(0, 0).sizeOver({{0, 0.5}, {0.3, 1}, {1, 0.15}})
               .look("flame").additive().glow(1.3));
    add(e, Make(id, "Core").rate(20).sphere(0.18).at3(0, 0.12, 0).life(0.3, 0.5).speed(0.6, 1.0).toward(0, 1, 0, 6)
               .size(0.3, 0.45).color(0xffe08a).gravity(0, 0).sizeOver({{0, 0.6}, {1, 0.1}}).look("flame").additive()
               .glow(1.6));
    add(e, Make(id, "Embers").rate(10).sphere(0.3).at3(0, 0.3, 0).life(1.0, 1.8).speed(1.0, 2.0).toward(0, 1, 0, 20)
               .size(0.04, 0.07).color(0xffb05a).gravity(0, 0.3).additive().glow(2.0).turbulence(0.8, 1.0, 0.8));
    add(e, Make(id, "Logs").burst(1).life(2.0, 2.0).speed(0, 0).size(1.3, 1.3).color(0x5a3a22).gravity(0, 0).solid()
               .look("bone").ground().flatOnPlane());
    tools::smoothLoop(e);
    return e;
}

Effect hitSparks3d(const IdSource& id) {
    Effect e = begin3d(id, "Hit Sparks 3D", 0.8);
    add(e, Make(id, "Flash").burst(1).life(0.14, 0.14).speed(0, 0).at3(0, 1.5, 0).size(1.8, 1.8).color(0xffffff)
               .gravity(0, 0).sizeOver({{0, 0.5}, {1, 1.2}}).fade({{0, 1}, {1, 0}}).look("starflash").additive());
    add(e, Make(id, "Sparks").burst(46).sphere(0.08).at3(0, 1.5, 0).life(0.25, 0.55).speed(6, 11).toward(0, 1, 0, 180)
               .size(0.05, 0.09).color(0xffc060).gravity(0, -8).drag(3).additive().glow(1.8).streak(0.035));
    add(e, Make(id, "Ring").burst(1).life(0.25, 0.25).speed(0, 0).at3(0, 1.5, 0).size(1.6, 1.6).color(0xffe2a0)
               .gravity(0, 0).sizeOver({{0, 0.2}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("ring").additive());
    return e;
}

Effect smokeColumn3d(const IdSource& id) {
    Effect e = begin3d(id, "Smoke Column 3D", 3.0);
    add(e, Make(id, "Smoke").rate(14).sphere(0.4).life(2.2, 3.0).speed(0.6, 1.0).toward(0, 1, 0, 18).size(0.7, 1.1)
               .color(0xc2c6d0).gravity(0, 0.25).spin(-25, 25).sizeOver({{0, 0.4}, {1, 1.7}})
               .fade({{0, 0}, {0.15, 1}, {0.7, 0.8}, {1, 0}}).look("puff").turbulence(0.5, 1.6, 0.5));
    tools::smoothLoop(e);
    return e;
}

Effect guardianOrbs3d(const IdSource& id) {
    Effect e = begin3d(id, "Guardian Orbs 3D", 2.0);
    // Three orbs going round a character at chest height, each leaving a
    // trail. Each pops out from the middle and is carried once round by
    // Orbit, a third of a turn after the one before, so they stay evenly
    // spaced however long it plays.
    add(e, Make(id, "Orbs").burst(1, 0.0).burst(1, 2.0 / 3.0).burst(1, 4.0 / 3.0).at3(0, 1.6, 0).life(2.0, 2.0)
               .speed(2.9, 2.9).toward(1, 0, 0, 0).size(0.32, 0.32).color(0x6fd6ff).gravity(0, 0).orbit(180).ground()
               .pace({{0, 14}, {0.07, -1.2, true}, {0.14, 0, true}, {1, 0}})
               .sizeOver({{0, 0}, {0.08, 1.2, true}, {0.16, 1, true}, {0.94, 1}, {1, 0}}).solid().look("orb")
               .additive().glow(1.6).trail(0.45, 0.18));
    add(e, Make(id, "Sparkles").rate(30).circle(1.3, true).at3(0, 1.6, 0).life(0.4, 0.7).speed(0.1, 0.4)
               .toward(0, 1, 0, 180).size(0.06, 0.12).color(0xd8f6ff).gravity(0, -0.5).orbit(180).ground()
               .fade({{0, 1}, {1, 0}}).look("glint").additive().glow(2.0));
    tools::smoothLoop(e);
    return e;
}

// ---- 3D game effects: the staples of a 3D action or casual game, each
// made to be seen from any side. (0.21)

// A tint key from a screen colour: the colour the particle is multiplied by
// at that moment of its life (so give the layer a white Color).
Tint hexTint(double t, unsigned rgb) {
    const Color c = hex(rgb, 1.0);
    return Tint{t, c.r, c.g, c.b, 1.0};
}

Effect laserBeam3d(const IdSource& id) {
    Effect e = begin3d(id, "Laser Beam 3D", 2.0);
    // Charging at the gun end, then a beam that lasts and an impact where it lands.
    add(e, Make(id, "Charge").from(0.0, 0.5).rate(40).sphere(0.9, true).at3(-3, 1.5, 0).life(0.25, 0.35)
               .speed(0, 0).size(0.05, 0.09).color(0x9fd8ff).gravity(0, 0).sizeOver({{0, 0}, {0.5, 1}, {1, 0}})
               .additive().glow(2.0));
    add(e, Make(id, "Charge glow").from(0.0, 1.7).burst(1).life(1.7, 1.7).at3(-3, 1.5, 0).speed(0, 0).size(1.3, 1.3)
               .color(0x3fa9ff).gravity(0, 0).sizeOver({{0, 0.1}, {0.25, 1.0, true}, {0.35, 0.8}, {0.9, 0.8}, {1, 0}})
               .fade({{0, 0.4}, {0.25, 1}, {0.9, 0.9}, {1, 0}}).additive().glow(1.6));
    add(e, Make(id, "Beam glow").from(0.45, 1.25).rate(90).at3(-3, 1.5, 0).life(0.2, 0.2).speed(30, 30)
               .toward(1, 0, 0, 0).size(0.75, 0.75).color(0x2f8cff, 0.6).gravity(0, 0).fade({{0, 1}, {0.85, 1}, {1, 0}})
               .look("beam").additive().glow(1.2).streak(0.05));
    add(e, Make(id, "Beam core").from(0.45, 1.25).rate(110).at3(-3, 1.5, 0).life(0.2, 0.2).speed(30, 30)
               .toward(1, 0, 0, 0).size(0.22, 0.26).color(0xe9f7ff).gravity(0, 0).look("beam").additive().glow(2.2)
               .streak(0.05));
    add(e, Make(id, "Beam sparks").from(0.45, 1.25).rate(70).sphere(0.18).at3(-3, 1.5, 0).life(0.15, 0.25)
               .speed(20, 28).toward(1, 0, 0, 2).size(0.04, 0.07).color(0xbfe6ff).gravity(0, 0).additive().glow(2.0)
               .streak(0.01).turbulence(3.0, 0.4, 2.0));
    add(e, Make(id, "Impact flash").from(0.6, 1.1).rate(22).at3(3, 1.5, 0).life(0.08, 0.14).speed(0, 0)
               .size(1.2, 1.8).turn(0, 360).color(0xdff2ff).gravity(0, 0).fade({{0, 1}, {1, 0}}).look("starflash")
               .additive().glow(1.6));
    add(e, Make(id, "Impact sparks").from(0.6, 1.1).rate(70).at3(3, 1.5, 0).life(0.25, 0.5).speed(4, 8)
               .toward(-1, 0.3, 0, 70).size(0.04, 0.08).color(0x7cc8ff).gravity(0, -9).drag(1.5).additive().glow(2.0)
               .streak(0.03));
    add(e, Make(id, "Scorch").from(0.7, 1.3).burst(1).life(1.3, 1.3).at3(3, 1.5, 0).speed(0, 0).size(0.9, 0.9)
               .color(0x1b2a44, 0.7).gravity(0, 0).fade({{0, 0}, {0.1, 0.8}, {0.7, 0.8}, {1, 0}}).look("impact"));
    tools::smoothLoop(e);
    return e;
}

Effect tornado3d(const IdSource& id) {
    Effect e = begin3d(id, "Tornado 3D", 3.0);
    // Dust drawn in at the foot and carried round and up a widening funnel.
    add(e, Make(id, "Funnel dust").rate(55).circle(0.35, true).life(1.6, 2.1).speed(2.0, 2.6).toward(0, 1, 0, 20)
               .size(0.35, 0.55).turn(0, 360).color(0xbca78a, 0.75).gravity(0, 0).orbit(420).ground().spin(-90, 90)
               .sizeOver({{0, 0.5}, {1, 2.2}}).fade({{0, 0}, {0.15, 0.8}, {0.75, 0.6}, {1, 0}}).look("dustcloud"));
    add(e, Make(id, "Wind streaks").rate(70).circle(0.45, true).life(1.0, 1.4).speed(2.6, 3.4).toward(0, 1, 0, 22)
               .size(0.05, 0.08).color(0xffffff, 0.7).gravity(0, 0).orbit(560).ground().additive()
               .fade({{0, 0}, {0.2, 1}, {0.8, 0.8}, {1, 0}}).streak(0.06));
    add(e, Make(id, "Debris").rate(10).circle(0.5, true).life(1.5, 2.0).speed(1.6, 2.4).toward(0, 1, 0, 25)
               .size(0.12, 0.22).turn(0, 360).color(0x6f5b44).gravity(0, 0).orbit(380).ground().spin(-400, 400)
               .fade({{0, 0}, {0.1, 1}, {0.85, 1}, {1, 0}}).solid().look("rock"));
    add(e, Make(id, "Leaves").rate(6).circle(0.6, true).life(1.6, 2.0).speed(1.8, 2.6).toward(0, 1, 0, 28)
               .size(0.16, 0.24).turn(0, 360).color(0x7fae4a).gravity(0, 0).orbit(330).ground().spin(-300, 300)
               .fade({{0, 0}, {0.1, 1}, {0.85, 1}, {1, 0}}).solid().look("leaf"));
    add(e, Make(id, "Ground dust").rate(14).circle(1.1, true).life(1.0, 1.4).speed(0.2, 0.5).toward(0, 1, 0, 60)
               .size(0.6, 0.9).turn(0, 360).color(0xa8977d, 0.55).gravity(0, 0).orbit(200).ground()
               .sizeOver({{0, 0.6}, {1, 1.4}}).fade({{0, 0}, {0.3, 0.6}, {1, 0}}).look("puff"));
    add(e, Make(id, "Ground ring").rate(2).life(1.5, 1.5).speed(0, 0).size(2.8, 2.8).color(0x8f7c62, 0.5)
               .gravity(0, 0).spin(240, 240).fade({{0, 0}, {0.5, 0.5}, {1, 0}}).look("smokering").ground().flatOnPlane());
    tools::smoothLoop(e);
    return e;
}

Effect standingPortal3d(const IdSource& id) {
    Effect e = begin3d(id, "Standing Portal 3D", 4.0);
    // A gateway standing upright: a whirl inside a turning rim, motes drawn round it.
    add(e, Make(id, "Back glow").burst(1).life(4.0, 4.0).at3(0, 1.8, 0).speed(0, 0).size(4.4, 4.4).color(0x2f9cff, 0.45)
               .gravity(0, 0).additive().flatOnPlane());
    add(e, Make(id, "Whirl").burst(1).life(4.0, 4.0).at3(0, 1.8, 0).speed(0, 0).size(3.0, 3.0).color(0x47c6ff)
               .gravity(0, 0).spin(-180, -180).solid().look("vortex").additive().glow(1.4).flatOnPlane());
    add(e, Make(id, "Rim").burst(1).life(4.0, 4.0).at3(0, 1.8, 0).speed(0, 0).size(3.3, 3.3).color(0xc8f2ff)
               .gravity(0, 0).spin(45, 45).solid().look("portalring").additive().glow(1.3).flatOnPlane());
    add(e, Make(id, "Runes").burst(1).life(4.0, 4.0).at3(0, 1.8, 0).speed(0, 0).size(3.9, 3.9).color(0x8fe0ff, 0.6)
               .gravity(0, 0).spin(-22.5, -22.5).solid().look("dashring").additive().flatOnPlane());
    add(e, Make(id, "Drawn motes").rate(40).circle(1.75, true).at3(0, 1.8, 0).life(0.9, 1.3).speed(0.1, 0.3)
               .toward(0, 0, 1, 40).size(0.05, 0.1).color(0xd8f6ff).gravity(0, 0).orbit(-160)
               .sizeOver({{0, 1}, {1, 0.2}}).fade({{0, 0}, {0.2, 1}, {1, 0}}).additive().glow(2.2));
    add(e, Make(id, "Edge sparks").rate(16).circle(1.6, true).at3(0, 1.8, 0).life(0.4, 0.7).speed(0.8, 1.6)
               .toward(0, 1, 0, 180).size(0.04, 0.07).color(0x9ce8ff).gravity(0, -1.5).additive().glow(2.0)
               .streak(0.04));
    add(e, Make(id, "Floor light").burst(1).life(4.0, 4.0).speed(0, 0).size(3.4, 3.4).color(0x2f9cff, 0.35).gravity(0, 0)
               .fade({{0, 0}, {0.15, 1}, {0.85, 1}, {1, 0}}).additive().ground().flatOnPlane());
    tools::smoothLoop(e);
    return e;
}

Effect gunfire3d(const IdSource& id) {
    Effect e = begin3d(id, "Gunfire 3D", 1.0);
    // Four shots a second from the left, each with its flash, tracer, shell and hit.
    const double shots[] = {0.0, 0.25, 0.5, 0.75};
    Make flash(id, "Muzzle flash"), glow(id, "Muzzle glow"), tracer(id, "Tracers"), shell(id, "Shells"),
        smoke(id, "Smoke"), hit(id, "Hit sparks"), puff(id, "Hit dust");
    for (const double t : shots) {
        flash.burst(1, t);
        glow.burst(1, t);
        tracer.burst(1, t + 0.01);
        shell.burst(1, t + 0.02);
        smoke.burst(2, t + 0.02);
        hit.burst(22, t + 0.12);
        puff.burst(2, t + 0.12);
    }
    add(e, flash.at3(-2.0, 1.4, 0).life(0.07, 0.07).speed(0, 0).size(1.4, 1.7).turn(0, 360).color(0xfff2c2)
               .gravity(0, 0).look("crossflare").additive().glow(1.8));
    add(e, glow.at3(-2.0, 1.4, 0).life(0.12, 0.12).speed(0, 0).size(2.0, 2.0).color(0xff9a2e, 0.8).gravity(0, 0)
               .fade({{0, 1}, {1, 0}}).additive());
    add(e, tracer.at3(-1.9, 1.4, 0).life(0.11, 0.11).speed(40, 40).toward(1, 0, 0, 0.6).size(0.24, 0.24)
               .color(0xffd27a).gravity(0, 0).look("beam").additive().glow(2.4).streak(0.05));
    Make hitFlash(id, "Hit flash");
    for (const double t : shots) {
        hitFlash.burst(1, t + 0.12);
    }
    add(e, hitFlash.at3(2.4, 1.4, 0).life(0.08, 0.08).speed(0, 0).size(0.9, 1.2).turn(0, 360).color(0xffe6b0)
               .gravity(0, 0).look("starflash").additive().glow(1.6));
    add(e, shell.at3(-2.2, 1.5, 0).life(0.7, 0.8).speed(2.5, 3.2).toward(-0.3, 1, -0.6, 15).size(0.11, 0.12)
               .turn(0, 360).color(0xd8a640).gravity(0, -12).spin(-900, 900).solid().look("capsule"));
    add(e, smoke.at3(-1.8, 1.4, 0).life(0.5, 0.8).speed(0.4, 0.9).toward(1, 0.4, 0, 30).size(0.25, 0.4)
               .turn(0, 360).color(0xc9c9c9, 0.35).gravity(0, 0.4).drag(2).sizeOver({{0, 0.4}, {1, 1.6}})
               .fade({{0, 0.8}, {1, 0}}).look("puff").turbulence(0.3, 0.8, 0.6));
    add(e, hit.at3(2.4, 1.4, 0).life(0.15, 0.35).speed(3, 7).toward(-1, 0.4, 0, 60).size(0.05, 0.09)
               .color(0xffc062).gravity(0, -10).drag(2).additive().glow(2.0).streak(0.03));
    add(e, puff.at3(2.4, 1.4, 0).life(0.4, 0.6).speed(0.3, 0.8).toward(-1, 0.3, 0, 50).size(0.25, 0.35)
               .turn(0, 360).color(0xb7a68f, 0.5).gravity(0, 0.2).drag(2).sizeOver({{0, 0.5}, {1, 1.4}})
               .fade({{0, 0.8}, {1, 0}}).look("dustcloud"));
    return e;
}

Effect lightRays3d(const IdSource& id) {
    Effect e = begin3d(id, "Holy Light 3D", 4.0);
    // Shafts of light falling on a spot, dust glinting in them.
    add(e, Make(id, "Shafts").rate(7).circle(0.8).at3(0, 5.2, 0).life(1.6, 2.2).speed(0.6, 0.9).toward(0, -1, 0, 0)
               .size(0.9, 1.4).color(0xfff1b8, 0.4).gravity(0, 0).ground().fade({{0, 0}, {0.35, 1}, {0.7, 1}, {1, 0}})
               .look("beam").additive().streak(4.5));
    add(e, Make(id, "Column").rate(2).at3(0, 2.4, 0).life(2.0, 2.0).speed(0, 0).size(2.6, 2.6)
               .color(0xffe08a, 0.4).gravity(0, 0).fade({{0, 0}, {0.5, 1}, {1, 0}}).look("lightcone").additive());
    add(e, Make(id, "Floor halo").burst(1).life(4.0, 4.0).speed(0, 0).size(2.6, 2.6).color(0xffe39a, 0.7).gravity(0, 0)
               .fade({{0, 0}, {0.2, 1}, {0.8, 1}, {1, 0}}).look("halo").additive().ground().flatOnPlane());
    add(e, Make(id, "Floor glow").burst(1).life(4.0, 4.0).speed(0, 0).size(3.4, 3.4).color(0xffc85a, 0.35)
               .gravity(0, 0).fade({{0, 0}, {0.2, 1}, {0.8, 1}, {1, 0}}).additive().ground().flatOnPlane());
    add(e, Make(id, "Dust").rate(18).sphere(1.0).at3(0, 2.0, 0).life(1.6, 2.6).speed(0.05, 0.2).toward(0, -1, 0, 90)
               .size(0.04, 0.08).color(0xfff7d6).gravity(0, -0.05).fade({{0, 0}, {0.3, 1}, {0.7, 1}, {1, 0}})
               .look("glint").additive().glow(1.8).turbulence(0.15, 1.0, 0.3));
    add(e, Make(id, "Feathers").rate(1.5).circle(0.8).at3(0, 4.5, 0).life(2.4, 3.0).speed(0.4, 0.6)
               .toward(0, -1, 0, 20).size(0.22, 0.3).turn(0, 360).color(0xfffbef).gravity(0, 0).spin(-60, 60).ground()
               .fade({{0, 0}, {0.2, 1}, {0.8, 1}, {1, 0}}).solid().look("feather").turbulence(0.3, 1.2, 0.4));
    tools::smoothLoop(e);
    return e;
}

Effect waterSplash3d(const IdSource& id) {
    Effect e = begin3d(id, "Water Splash 3D", 1.6);
    // Something drops into water: a crown of drops, spray and widening ripples.
    add(e, Make(id, "Crown").burst(36).circle(0.35, true).life(0.6, 0.9).speed(4, 6).toward(0, 1, 0, 30)
               .size(0.08, 0.14).color(0x7fd2ff).gravity(0, -14).ground().solid().look("droplet").streak(0.02));
    add(e, Make(id, "Column").burst(14).circle(0.12).life(0.5, 0.7).speed(5, 7.5).toward(0, 1, 0, 6)
               .size(0.1, 0.16).color(0xd6f2ff).gravity(0, -14).ground().solid().look("droplet").streak(0.02));
    add(e, Make(id, "Splash").burst(1).life(0.35, 0.35).at3(0, 0.45, 0).speed(0, 0).size(1.5, 1.5)
               .color(0xb9e8ff, 0.9).gravity(0, 0).sizeOver({{0, 0.3}, {0.4, 1.1, true}, {1, 1.2}})
               .fade({{0, 1}, {0.6, 0.8}, {1, 0}}).look("splash"));
    add(e, Make(id, "Spray").burst(16).circle(0.4).life(0.5, 0.9).speed(0.8, 1.8).toward(0, 1, 0, 60)
               .size(0.3, 0.5).turn(0, 360).color(0xffffff, 0.45).gravity(0, -1).drag(2).ground()
               .sizeOver({{0, 0.5}, {1, 1.5}}).fade({{0, 0.8}, {1, 0}}).look("puff"));
    add(e, Make(id, "Ripples").burst(1, 0.0).burst(1, 0.22).burst(1, 0.45).life(1.0, 1.0).speed(0, 0).size(3.2, 3.2)
               .color(0xa8e2ff, 0.8).gravity(0, 0).sizeOver({{0, 0.1}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}})
               .look("ripple").additive().ground().flatOnPlane());
    add(e, Make(id, "Drips").burst(10, 0.5).circle(0.9, true).life(0.3, 0.5).speed(1.2, 2.0).toward(0, 1, 0, 25)
               .size(0.05, 0.08).color(0x9fdcff).gravity(0, -12).ground().solid().look("droplet"));
    return e;
}

Effect fireball3d(const IdSource& id) {
    Effect e = begin3d(id, "Fireball 3D", 2.2);
    // Flies from left to right, trailing flame and smoke, and bursts where it lands.
    add(e, Make(id, "Fireball").burst(1).at3(-3, 1.5, 0).life(1.5, 1.5).speed(4, 4).toward(1, 0, 0, 0)
               .size(0.9, 0.9).color(0xffa13a).gravity(0, 0).look("fireball").additive().glow(1.6).streak(0.0)
               .trail(0.35, 0.55));
    add(e, Make(id, "Core").burst(1).at3(-3, 1.5, 0).life(1.5, 1.5).speed(4, 4).toward(1, 0, 0, 0).size(0.5, 0.5)
               .color(0xfff1b0).gravity(0, 0).additive().glow(2.0));
    add(e, Make(id, "Flame tail").burst(70).sphere(0.15).at3(-3, 1.5, 0).life(1.5, 1.5).speed(2.6, 3.95)
               .toward(1, 0, 0, 3).size(0.25, 0.5).turn(0, 360).color(0xffffff).gravity(0, 0.3).spin(-180, 180)
               .tint({hexTint(0, 0xffd27a), hexTint(0.5, 0xff7a22), hexTint(1, 0x8a2a10)}).sizeOver({{0, 1}, {1, 0.3}})
               .fade({{0, 0.9}, {0.8, 0.5}, {1, 0}}).look("flame").additive().glow(1.2).turbulence(0.3, 0.6, 1.0));
    add(e, Make(id, "Smoke tail").burst(30).sphere(0.2).at3(-3, 1.5, 0).life(1.5, 1.5).speed(1.6, 3.4)
               .toward(1, 0.05, 0, 6).size(0.3, 0.5).turn(0, 360).color(0x4f4a48, 0.45).gravity(0, 0.4).spin(-60, 60)
               .sizeOver({{0, 0.5}, {1, 1.6}}).fade({{0, 0.6}, {1, 0}}).look("puff").turbulence(0.3, 0.8, 0.6));
    add(e, Make(id, "Blast").from(1.5, 0.7).burst(1).at3(3, 1.5, 0).life(0.45, 0.45).speed(0, 0).size(2.4, 2.4)
               .color(0xffb347).gravity(0, 0).sizeOver({{0, 0.2}, {0.3, 1.1, true}, {1, 1.2}})
               .fade({{0, 1}, {0.5, 0.8}, {1, 0}}).look("pow").additive().glow(1.4));
    add(e, Make(id, "Embers").from(1.5, 0.7).burst(30).at3(3, 1.5, 0).life(0.3, 0.6).speed(3, 7).toward(-1, 0.5, 0, 80)
               .size(0.04, 0.08).color(0xffc062).gravity(0, -6).drag(2).additive().glow(2.0).streak(0.03));
    add(e, Make(id, "Blast smoke").from(1.5, 0.7).burst(8).sphere(0.3).at3(3, 1.5, 0).life(0.5, 0.7).speed(0.5, 1.2)
               .toward(0, 1, 0, 120).size(0.5, 0.8).turn(0, 360).color(0x5a5250, 0.6).gravity(0, 0.6).drag(2)
               .sizeOver({{0, 0.5}, {1, 1.5}}).fade({{0, 0.8}, {1, 0}}).look("puff"));
    return e;
}

Effect explosion3d(const IdSource& id) {
    Effect e = begin3d(id, "Explosion 3D", 2.0);
    add(e, Make(id, "Flash").burst(1).at3(0, 1.0, 0).life(0.12, 0.12).speed(0, 0).size(4.0, 4.0).color(0xfff6d6)
               .gravity(0, 0).fade({{0, 1}, {1, 0}}).additive().glow(1.5));
    add(e, Make(id, "Fireball").burst(14).sphere(0.4).at3(0, 1.0, 0).life(0.5, 0.8).speed(1.5, 3.0)
               .toward(0, 1, 0, 120).size(0.9, 1.3).turn(0, 360).color(0xffffff).gravity(0, 1).drag(3).spin(-90, 90)
               .tint({hexTint(0, 0xfff1b0), hexTint(0.3, 0xff8a2a), hexTint(1, 0x6a2410)}).sizeOver({{0, 0.4}, {0.3, 1.1, true}, {1, 1.3}})
               .fade({{0, 1}, {0.6, 0.8}, {1, 0}}).look("fireball").additive().glow(1.3));
    add(e, Make(id, "Smoke").burst(16).sphere(0.6).at3(0, 1.2, 0).life(1.2, 1.8).speed(0.8, 1.8).toward(0, 1, 0, 80)
               .size(0.9, 1.3).turn(0, 360).color(0x3d3a3a, 0.7).gravity(0, 0.8).drag(2.5).spin(-40, 40)
               .sizeOver({{0, 0.5}, {1, 1.8}}).fade({{0, 0}, {0.15, 0.8}, {1, 0}}).look("puff")
               .turbulence(0.3, 1.2, 0.5));
    add(e, Make(id, "Debris").burst(18).sphere(0.3).at3(0, 0.8, 0).life(0.9, 1.3).speed(5, 9).toward(0, 1, 0, 70)
               .size(0.1, 0.2).turn(0, 360).color(0x4c3a30).gravity(0, -16).spin(-700, 700).solid().look("rock"));
    add(e, Make(id, "Sparks").burst(50).at3(0, 1.0, 0).life(0.3, 0.7).speed(6, 12).toward(0, 1, 0, 100)
               .size(0.03, 0.06).color(0xffc062).gravity(0, -8).drag(1.5).additive().glow(2.0).streak(0.03));
    add(e, Make(id, "Shockwave").burst(1).life(0.45, 0.45).speed(0, 0).size(6.0, 6.0).color(0xffe0b0).gravity(0, 0)
               .sizeOver({{0, 0.1}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("shockwave").additive().ground()
               .flatOnPlane());
    add(e, Make(id, "Scorch").burst(1).life(2.0, 2.0).speed(0, 0).size(3.0, 3.0).color(0x1a1412, 0.85).gravity(0, 0)
               .fade({{0, 0}, {0.05, 1}, {0.75, 1}, {1, 0}}).look("crack").ground().flatOnPlane());
    return e;
}

Effect lightning3d(const IdSource& id) {
    Effect e = begin3d(id, "Lightning Strike 3D", 1.5);
    // A bolt from the sky, flickering three times, then crackling on the ground.
    add(e, Make(id, "Bolt").burst(1, 0.0).burst(1, 0.09).burst(1, 0.2).at3(0, 3.2, 0).life(0.07, 0.07).speed(0, 0)
               .size(6.5, 6.5).turn(82, 98).color(0xe6f3ff).gravity(0, 0).look("bolt").additive().glow(2.2));
    add(e, Make(id, "Sky flash").burst(1, 0.0).burst(1, 0.2).at3(0, 3.5, 0).life(0.12, 0.12).speed(0, 0).size(8, 8)
               .color(0x9fc6ff, 0.5).gravity(0, 0).fade({{0, 1}, {1, 0}}).additive());
    add(e, Make(id, "Ground flash").burst(1).life(0.3, 0.3).speed(0, 0).size(3.4, 3.4).color(0xbfe0ff).gravity(0, 0)
               .fade({{0, 1}, {1, 0}}).additive().glow(1.5).ground().flatOnPlane());
    add(e, Make(id, "Arcs").rate(18).circle(0.9).life(0.06, 0.1).speed(0, 0).size(0.5, 0.9).turn(0, 360)
               .color(0xcfe6ff).gravity(0, 0).from(0.05, 0.9).look("zigzag").additive().glow(2.0).ground());
    add(e, Make(id, "Sparks").burst(40).life(0.3, 0.7).speed(3, 7).toward(0, 1, 0, 75).size(0.03, 0.06)
               .color(0xa8d4ff).gravity(0, -9).drag(1.5).additive().glow(2.0).streak(0.03));
    add(e, Make(id, "Scorch").burst(1).life(1.5, 1.5).speed(0, 0).size(2.0, 2.0).color(0x161b26, 0.85).gravity(0, 0)
               .fade({{0, 0}, {0.05, 1}, {0.7, 1}, {1, 0}}).look("crack").ground().flatOnPlane());
    add(e, Make(id, "Smoke").burst(6).circle(0.4).life(0.8, 1.2).speed(0.4, 0.9).toward(0, 1, 0, 30).size(0.5, 0.8)
               .turn(0, 360).color(0x6b7280, 0.5).gravity(0, 0.4).ground().sizeOver({{0, 0.5}, {1, 1.6}})
               .fade({{0, 0}, {0.2, 0.7}, {1, 0}}).look("puff"));
    return e;
}

Effect shieldBubble3d(const IdSource& id) {
    Effect e = begin3d(id, "Shield Bubble 3D", 2.0);
    // A hexagon-patterned bubble round a character, rippling, sparks running over it.
    add(e, Make(id, "Bubble").burst(1).at3(0, 1.4, 0).life(2.0, 2.0).speed(0, 0).size(3.6, 3.6).color(0x5fd0ff, 0.55)
               .gravity(0, 0).sizeOver({{0, 0.97}, {0.5, 1.0, true}, {1, 0.97, true}})
               .fade({{0, 0.7}, {0.5, 1}, {1, 0.7}}).look("hexshield").additive());
    add(e, Make(id, "Rim glow").burst(1).at3(0, 1.4, 0).life(2.0, 2.0).speed(0, 0).size(3.8, 3.8).color(0x2fa8ff, 0.6)
               .gravity(0, 0).look("softring").additive().glow(1.3));
    add(e, Make(id, "Ripples").burst(1, 0.0).burst(1, 1.0).at3(0, 1.4, 0).life(0.8, 0.8).speed(0, 0).size(3.6, 3.6)
               .color(0xbff0ff).gravity(0, 0).sizeOver({{0, 0.6}, {1, 1.0, true}}).fade({{0, 0.8}, {1, 0}})
               .look("ring").additive());
    add(e, Make(id, "Sparks").rate(30).sphere(1.75, true).at3(0, 1.4, 0).life(0.4, 0.7).speed(0, 0.1).size(0.04, 0.08)
               .color(0xe0f8ff).gravity(0, 0).orbit(120).ground().fade({{0, 0}, {0.3, 1}, {1, 0}}).additive().glow(2.2));
    add(e, Make(id, "Floor ring").burst(1).life(2.0, 2.0).speed(0, 0).size(3.2, 3.2).color(0x5fd0ff, 0.6).gravity(0, 0)
               .fade({{0, 0.8}, {0.5, 1}, {1, 0.8}}).solid().look("softring").additive().ground()
               .flatOnPlane());
    tools::smoothLoop(e);
    return e;
}

Effect teleport3d(const IdSource& id) {
    Effect e = begin3d(id, "Teleport 3D", 2.0);
    // A ring closes in on the floor, light pours up, a flash, and sparkles fall away.
    add(e, Make(id, "Closing ring").burst(1).life(0.8, 0.8).speed(0, 0).size(3.2, 3.2).color(0x9b8cff).gravity(0, 0)
               .sizeOver({{0, 1}, {1, 0.15, true}}).fade({{0, 0}, {0.2, 1}, {1, 0.6}}).look("doublering").additive()
               .glow(1.5).ground().flatOnPlane());
    add(e, Make(id, "Up-rush").from(0.3, 0.8).rate(80).circle(0.6, true).life(0.4, 0.6).speed(6, 9).toward(0, 1, 0, 2)
               .size(0.06, 0.1).color(0xd9d2ff).gravity(0, 0).ground().additive().glow(1.8).streak(0.07));
    add(e, Make(id, "Column").from(0.4, 0.8).burst(1).at3(0, 1.6, 0).life(0.8, 0.8).speed(0, 0).size(3.4, 3.4)
               .color(0x8f7bff, 0.7).gravity(0, 0).sizeOver({{0, 0.4}, {0.4, 1.0, true}, {1, 0.6}})
               .fade({{0, 0}, {0.3, 1}, {1, 0}}).look("lightcone").additive());
    add(e, Make(id, "Flash").from(0.8, 0.4).burst(1).at3(0, 1.4, 0).life(0.2, 0.2).speed(0, 0).size(3.0, 3.0)
               .color(0xf2efff).gravity(0, 0).fade({{0, 1}, {1, 0}}).look("starflash").additive().glow(1.6));
    add(e, Make(id, "Sparkles").from(0.8, 1.2).burst(30).sphere(0.9).at3(0, 1.4, 0).life(0.6, 1.1).speed(0.4, 1.4)
               .toward(0, 1, 0, 180).size(0.08, 0.16).turn(0, 45).color(0xe6e0ff).gravity(0, -1.2).drag(1)
               .sizeOver({{0, 1}, {1, 0}}).look("twinkle").additive().glow(1.8));
    return e;
}

Effect frostNova3d(const IdSource& id) {
    Effect e = begin3d(id, "Frost Nova 3D", 2.0);
    add(e, Make(id, "Frost wave").burst(1).life(0.6, 0.6).speed(0, 0).size(6.0, 6.0).color(0xbfeeff).gravity(0, 0)
               .sizeOver({{0, 0.1}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("shockwave").additive().ground()
               .flatOnPlane());
    add(e, Make(id, "Ice floor").burst(1).life(2.0, 2.0).speed(0, 0).size(4.2, 4.2).color(0x8fd8ff, 0.3).gravity(0, 0)
               .sizeOver({{0, 0.2}, {0.3, 1.0, true}}).fade({{0, 0}, {0.1, 1}, {0.75, 1}, {1, 0}}).look("snowflake")
               .additive().ground().flatOnPlane());
    add(e, Make(id, "Shards").burst(16).circle(1.6, true).life(1.4, 1.6).speed(0, 0).size(0.5, 0.9).turn(-20, 20)
               .color(0xd8f4ff).gravity(0, 0).ground().sizeOver({{0, 0}, {0.1, 1.15, true}, {0.2, 1, true}, {0.8, 1}, {1, 0}})
               .solid().look("crystal"));
    add(e, Make(id, "Ice chips").burst(30).circle(0.5).life(0.5, 0.9).speed(3, 6).toward(0, 1, 0, 70).size(0.06, 0.12)
               .turn(0, 360).color(0xe8f8ff).gravity(0, -12).spin(-600, 600).ground().solid().look("shard"));
    add(e, Make(id, "Cold mist").burst(14).circle(1.6).life(1.0, 1.6).speed(0.2, 0.5).toward(0, 1, 0, 40)
               .size(0.7, 1.0).turn(0, 360).color(0xd6f1ff, 0.4).gravity(0, 0).ground().sizeOver({{0, 0.5}, {1, 1.4}})
               .fade({{0, 0}, {0.2, 0.6}, {1, 0}}).look("puff"));
    add(e, Make(id, "Snowflakes").rate(10).circle(1.8).at3(0, 2.5, 0).life(1.5, 2.0).speed(0.3, 0.6)
               .toward(0, -1, 0, 20).size(0.08, 0.14).turn(0, 360).color(0xffffff).gravity(0, 0).spin(-60, 60).ground()
               .fade({{0, 0}, {0.2, 1}, {0.8, 1}, {1, 0}}).look("snowflake").additive());
    return e;
}

Effect poisonCloud3d(const IdSource& id) {
    Effect e = begin3d(id, "Poison Cloud 3D", 3.0);
    add(e, Make(id, "Cloud").rate(12).circle(1.4).at3(0, 0.3, 0).life(2.0, 2.6).speed(0.1, 0.3).toward(0, 1, 0, 60)
               .size(1.0, 1.4).turn(0, 360).color(0x5bc44a, 0.5).gravity(0, 0).spin(-25, 25).ground()
               .sizeOver({{0, 0.6}, {1, 1.3}}).fade({{0, 0}, {0.25, 0.7}, {0.75, 0.6}, {1, 0}}).look("puff")
               .turbulence(0.25, 1.4, 0.4));
    add(e, Make(id, "Dark core").rate(5).circle(0.9).at3(0, 0.2, 0).life(2.0, 2.6).speed(0.05, 0.15)
               .toward(0, 1, 0, 60).size(0.9, 1.2).turn(0, 360).color(0x2d6b2a, 0.55).gravity(0, 0).ground()
               .sizeOver({{0, 0.6}, {1, 1.3}}).fade({{0, 0}, {0.3, 0.7}, {1, 0}}).look("puff"));
    add(e, Make(id, "Bubbles").rate(8).circle(1.2).at3(0, 0.1, 0).life(0.8, 1.4).speed(0.5, 1.0).toward(0, 1, 0, 10)
               .size(0.12, 0.22).color(0xb6ff7a).gravity(0, 0).ground()
               .sizeOver({{0, 0.3}, {0.8, 1.0}, {0.9, 1.4, true}, {1, 0}}).look("bubble").additive());
    add(e, Make(id, "Skulls").rate(1).circle(0.8).at3(0, 0.6, 0).life(1.4, 1.4).speed(0.5, 0.7).toward(0, 1, 0, 10)
               .size(0.3, 0.36).color(0xc8ff9a).gravity(0, 0).ground()
               .sizeOver({{0, 0}, {0.15, 1.15, true}, {0.3, 1, true}, {1, 0.9}}).fade({{0, 1}, {0.75, 1}, {1, 0}})
               .solid().look("skull").hold(12));
    add(e, Make(id, "Floor stain").burst(1).life(3.0, 3.0).speed(0, 0).size(3.6, 3.6).color(0x3f9b2f, 0.5).gravity(0, 0)
               .fade({{0, 0}, {0.2, 1}, {0.8, 1}, {1, 0}}).look("splat").ground().flatOnPlane());
    tools::smoothLoop(e);
    return e;
}

Effect meteor3d(const IdSource& id) {
    Effect e = begin3d(id, "Meteor Strike 3D", 2.4);
    // From high on the left, down in a second, and a crater-making blast.
    add(e, Make(id, "Meteor").burst(1).at3(-3.5, 7, 0).life(1.0, 1.0).speed(7.83, 7.83).toward(0.447, -0.894, 0, 0)
               .size(1.0, 1.0).color(0xffa24a).gravity(0, 0).look("rock").solid().spin(-360, 360));
    add(e, Make(id, "Burning").burst(1).at3(-3.5, 7, 0).life(1.0, 1.0).speed(7.83, 7.83).toward(0.447, -0.894, 0, 0)
               .size(1.6, 1.6).color(0xff7a22).gravity(0, 0).look("fireball").additive().glow(1.5).streak(0.0)
               .trail(0.3, 0.7));
    add(e, Make(id, "Fire tail").burst(60).sphere(0.25).at3(-3.5, 7, 0).life(1.0, 1.0).speed(4.5, 7.7)
               .toward(0.447, -0.894, 0, 3).size(0.3, 0.6).turn(0, 360).color(0xffffff).gravity(0, 0).spin(-200, 200)
               .tint({hexTint(0, 0xffd27a), hexTint(0.6, 0xff6a1a), hexTint(1, 0x5a1a0a)}).sizeOver({{0, 1}, {1, 0.3}})
               .fade({{0, 0.9}, {1, 0}}).look("flame").additive().glow(1.2));
    add(e, Make(id, "Impact flash").from(1.0, 0.4).burst(1).at3(0, 0.5, 0).life(0.15, 0.15).speed(0, 0).size(5, 5)
               .color(0xfff1c2).gravity(0, 0).fade({{0, 1}, {1, 0}}).additive().glow(1.5));
    add(e, Make(id, "Blast").from(1.0, 1.0).burst(12).sphere(0.4).at3(0, 0.5, 0).life(0.5, 0.8).speed(1.5, 3)
               .toward(0, 1, 0, 110).size(1.0, 1.4).turn(0, 360).color(0xffffff).gravity(0, 1).drag(3)
               .tint({hexTint(0, 0xfff1b0), hexTint(0.3, 0xff8a2a), hexTint(1, 0x5a1e0c)}).sizeOver({{0, 0.4}, {0.3, 1.1, true}, {1, 1.3}})
               .fade({{0, 1}, {1, 0}}).look("fireball").additive());
    add(e, Make(id, "Rocks").from(1.0, 1.4).burst(20).circle(0.6).life(0.9, 1.3).speed(5, 9).toward(0, 1, 0, 55)
               .size(0.12, 0.24).turn(0, 360).color(0x4a3a2e).gravity(0, -16).spin(-600, 600).ground().solid()
               .look("rock"));
    add(e, Make(id, "Dust").from(1.0, 1.4).burst(18).circle(1.0, true).life(0.9, 1.4).speed(1.5, 3)
               .toward(0, 1, 0, 80).size(0.7, 1.1).turn(0, 360).color(0x8f7c62, 0.7).gravity(0, -0.5).drag(2.5).ground()
               .sizeOver({{0, 0.5}, {1, 1.6}}).fade({{0, 0.8}, {1, 0}}).look("dustcloud"));
    add(e, Make(id, "Shockwave").from(1.0, 0.6).burst(1).life(0.5, 0.5).speed(0, 0).size(7, 7).color(0xffd9a0)
               .gravity(0, 0).sizeOver({{0, 0.1}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("shockwave").additive()
               .ground().flatOnPlane());
    add(e, Make(id, "Crater").from(1.0, 1.4).burst(1).life(1.4, 1.4).speed(0, 0).size(3.2, 3.2).color(0x1a1412, 0.9)
               .gravity(0, 0).fade({{0, 1}, {0.7, 1}, {1, 0}}).look("crack").ground().flatOnPlane());
    return e;
}

Effect lootBeam3d(const IdSource& id) {
    Effect e = begin3d(id, "Loot Beam 3D", 2.0);
    // A dropped treasure: a tall beam of its colour, a ring on the floor, a gem turning above.
    add(e, Make(id, "Beam").rate(30).circle(0.25).life(0.8, 1.0).speed(5, 6).toward(0, 1, 0, 0).size(0.35, 0.5)
               .color(0xffb340, 0.5).gravity(0, 0).ground().fade({{0, 0}, {0.2, 1}, {0.7, 0.8}, {1, 0}}).look("beam")
               .additive().streak(0.12));
    add(e, Make(id, "Beam core").rate(30).circle(0.08).life(0.7, 0.9).speed(5, 6).toward(0, 1, 0, 0).size(0.12, 0.16)
               .color(0xfff4d0).gravity(0, 0).ground().additive().glow(1.8).streak(0.12));
    add(e, Make(id, "Floor ring").burst(1).life(2.0, 2.0).speed(0, 0).size(1.6, 1.6).color(0xffc85a).gravity(0, 0)
               .spin(180, 180).fade({{0, 0.8}, {0.5, 1}, {1, 0.8}}).look("dashring").additive().ground().flatOnPlane());
    add(e, Make(id, "Floor glow").burst(1).life(2.0, 2.0).speed(0, 0).size(2.4, 2.4).color(0xff9f2a, 0.5).gravity(0, 0)
               .additive().ground().flatOnPlane());
    add(e, Make(id, "Gem").burst(1).at3(0, 0.6, 0).life(2.0, 2.0).speed(0, 0).size(0.55, 0.55).color(0xffd36a)
               .gravity(0, 0).solid().look("gem"));
    add(e, Make(id, "Twinkles").rate(8).sphere(0.5).at3(0, 0.7, 0).life(0.4, 0.7).speed(0, 0.2).size(0.12, 0.2)
               .turn(0, 45).color(0xffffff).gravity(0, 0).sizeOver({{0, 0}, {0.3, 1}, {1, 0}}).look("twinkle").additive()
               .glow(1.6));
    add(e, Make(id, "Rising motes").rate(14).circle(0.5, true).life(1.0, 1.4).speed(0.8, 1.4).toward(0, 1, 0, 6)
               .size(0.04, 0.07).color(0xffe39a).gravity(0, 0).orbit(90).ground().fade({{0, 0}, {0.2, 1}, {1, 0}})
               .additive().glow(2.0));
    tools::smoothLoop(e);
    return e;
}

Effect swordSlash3d(const IdSource& id) {
    Effect e = begin3d(id, "Sword Slash 3D", 1.2);
    // Two quick strokes, one down to the right and one back across, with sparks.
    // (Turn the group with the handles to slant it along any swing.)
    add(e, Make(id, "Slash").burst(1, 0.0).burst(1, 0.4).at3(0, 1.5, 0).life(0.22, 0.22).speed(0, 0).size(3.0, 3.2)
               .turn(-35, 35).color(0xe6f4ff).gravity(0, 0)
               .sizeOver({{0, 0.7}, {0.3, 1.05, true}, {1, 1.1}}).fade({{0, 1}, {0.5, 0.9}, {1, 0}}).look("slash")
               .additive().glow(1.8).flatOnPlane());
    add(e, Make(id, "Afterglow").burst(1, 0.0).burst(1, 0.4).at3(0, 1.5, 0).life(0.3, 0.3).speed(0, 0).size(3.4, 3.6)
               .turn(-35, 35).color(0x3f8cff, 0.7).gravity(0, 0).sizeOver({{0, 0.75}, {0.3, 1.1, true}, {1, 1.2}})
               .fade({{0, 0.9}, {1, 0}}).look("slash").additive().glow(1.2).flatOnPlane());
    add(e, Make(id, "Sparks").burst(20, 0.05).burst(20, 0.45).sphere(0.4).at3(0.6, 1.4, 0).life(0.2, 0.45).speed(4, 8)
               .toward(1, 0.2, 0, 70).size(0.03, 0.06).color(0xd6ecff).gravity(0, -8).drag(2).additive().glow(2.0)
               .streak(0.03));
    add(e, Make(id, "Speed lines").burst(1, 0.0).burst(1, 0.4).at3(0, 1.5, 0).life(0.15, 0.15).speed(0, 0)
               .size(3.2, 3.2).turn(0, 360).color(0xbfe0ff, 0.18).gravity(0, 0).fade({{0, 1}, {1, 0}}).look("speedlines")
               .additive());
    return e;
}

Effect rain3d(const IdSource& id) {
    Effect e = begin3d(id, "Rain 3D", 2.0);
    // Rain over an area, splashing and rippling where it lands.
    add(e, Make(id, "Rain").rate(260).rect(8, 8).at3(0, 7, 0).life(0.7, 0.7).speed(10, 10).toward(0.08, -1, 0, 1)
               .size(0.09, 0.11).color(0xd6e8ff, 0.9).gravity(0, 0).ground().look("raindash").additive().glow(1.2).streak(0.06));
    add(e, Make(id, "Splashes").rate(60).rect(8, 8).life(0.12, 0.2).speed(0, 0).at3(0, 0.08, 0).size(0.3, 0.45)
               .color(0xe6f2ff, 0.9).gravity(0, 0).ground().sizeOver({{0, 0.4}, {1, 1}}).fade({{0, 1}, {1, 0}})
               .look("splash"));
    add(e, Make(id, "Ripples").rate(50).rect(8, 8).life(0.5, 0.7).speed(0, 0).size(0.5, 0.8).color(0xb8d4ff, 0.8)
               .gravity(0, 0).sizeOver({{0, 0.2}, {1, 1, true}}).fade({{0, 1}, {1, 0}}).look("ring").ground()
               .flatOnPlane());
    return e;
}

// ---- Slot-game symbol and frame effects (0.22), built on the Path shape.
// Outlines follow the effect's target picture when there is one (Fit to
// target), and a rounded square the size of a symbol when there is not.

PointList symbolSquare() { return roundedRect(2.4, 2.4, 0.35, 6); }

// A bright line along a path, `thickness` wide: soft dots close together,
// each living a moment, so it shimmers. length is the path's, to keep the
// dots as close on a long path as on a short one.
Make& glowLine(Make& m, unsigned colour, double thickness, double length, double alpha = 1.0) {
    const double life = 0.12;
    const double alive = length / (thickness * 0.5);
    return m.rate(std::round(alive / life)).life(life * 0.8, life * 1.2).speed(0, 0)
        .size(thickness * 4.0, thickness * 4.4).color(colour, alpha * 0.6).gravity(0, 0).additive().glow(1.6);
}

Effect goldOutline(const IdSource& id) {
    Effect e = begin(id, "Gold Outline", 2.0);
    Make line(id, "Gold line");
    add(e, glowLine(line.path(symbolSquare()).fitTarget(), 0xffd36a, 0.06, 9.0, 0.9));
    add(e, Make(id, "Halo").path(symbolSquare()).fitTarget().rate(500).life(0.3, 0.4).speed(0, 0).size(0.35, 0.45)
               .color(0xffa21f, 0.12).gravity(0, 0).fade({{0, 0}, {0.5, 1}, {1, 0}}).additive());
    add(e, Make(id, "Running sparks").path(symbolSquare()).fitTarget().travel(0.5, 0.06, 2).rate(220).life(0.25, 0.45)
               .speed(0.1, 0.4).sendOut().size(0.05, 0.1).color(0xfff0b0).gravity(0, -0.4).fade({{0, 1}, {1, 0}})
               .additive().glow(2.2).streak(0.04));
    add(e, Make(id, "Heads").path(symbolSquare()).fitTarget().travel(0.5, 0.01, 2).rate(40).life(0.12, 0.16).speed(0, 0)
               .size(0.5, 0.6).turn(0, 360).color(0xfff6d8).gravity(0, 0).look("starflash").additive().glow(1.8));
    add(e, Make(id, "Glints").path(symbolSquare()).fitTarget().rate(6).life(0.4, 0.6).speed(0, 0).size(0.18, 0.3)
               .turn(0, 45).color(0xffffff).gravity(0, 0).sizeOver({{0, 0}, {0.4, 1, true}, {1, 0}}).look("twinkle")
               .additive().glow(1.8));
    tools::smoothLoop(e);
    return e;
}

Effect rareOutline(const IdSource& id) {
    Effect e = begin(id, "Rare Outline", 2.0);
    Make line(id, "Blue line");
    add(e, glowLine(line.path(symbolSquare()).fitTarget(), 0x6fc6ff, 0.05, 9.0, 0.85));
    add(e, Make(id, "Crackle").path(symbolSquare()).fitTarget().band(0.06).rate(90).life(0.05, 0.09).speed(0, 0)
               .size(0.35, 0.55).color(0xd6f0ff).gravity(0, 0).alongPath().look("zigzag").additive().glow(2.0).hold(15));
    add(e, Make(id, "Arcs").path(symbolSquare()).fitTarget().padding(0.06).rate(14).life(0.06, 0.1).speed(0, 0)
               .size(0.4, 0.7).turn(-30, 30).color(0x9fe0ff).gravity(0, 0).alongPath().look("bolt").additive().glow(2.0));
    add(e, Make(id, "Halo").path(symbolSquare()).fitTarget().rate(500).life(0.3, 0.4).speed(0, 0).size(0.35, 0.45)
               .color(0x1f7bff, 0.12).gravity(0, 0).fade({{0, 0}, {0.5, 1}, {1, 0}}).additive());
    return e;
}

Effect epicOutline(const IdSource& id) {
    Effect e = begin(id, "Epic Outline", 2.0);
    Make line(id, "Violet line");
    add(e, glowLine(line.path(symbolSquare()).fitTarget(), 0xd9a6ff, 0.05, 9.0, 0.85));
    add(e, Make(id, "Smoke").path(symbolSquare()).fitTarget().rate(120).life(0.5, 0.9).speed(0.1, 0.3).sendOut()
               .size(0.25, 0.4).turn(0, 360).color(0x9b4dff, 0.5).gravity(0, 0).spin(-60, 60)
               .sizeOver({{0, 0.5}, {1, 1.5}}).fade({{0, 0}, {0.2, 0.7}, {1, 0}}).look("smoke").additive()
               .turbulence(0.4, 0.5, 0.8));
    add(e, Make(id, "Motes").path(symbolSquare()).fitTarget().rate(30).life(0.6, 1.0).speed(0.2, 0.5).sendOut()
               .size(0.03, 0.06).color(0xf2dcff).gravity(0, 0.2).fade({{0, 1}, {1, 0}}).additive().glow(2.2));
    return e;
}

Effect legendaryOutline(const IdSource& id) {
    Effect e = begin(id, "Legendary Outline", 2.0);
    Make line(id, "Hot line");
    add(e, glowLine(line.path(symbolSquare()).fitTarget(), 0xffe9a8, 0.06, 9.0));
    add(e, Make(id, "Flames").path(symbolSquare()).fitTarget().rate(420).life(0.3, 0.55).speed(0.4, 0.9)
               .toward(0, 1, 0, 25).size(0.25, 0.45).turn(-10, 10).color(0xffffff).gravity(0, 0.6)
               .tint({hexTint(0, 0xfff1b0), hexTint(0.4, 0xff8a1f), hexTint(1, 0xa8200a)})
               .sizeOver({{0, 0.6}, {0.3, 1}, {1, 0.2}}).fade({{0, 0.9}, {1, 0}}).look("flame").additive().glow(1.3)
               .turbulence(0.4, 0.4, 1.2));
    add(e, Make(id, "Comets").path(symbolSquare()).fitTarget().travel(0.75, 0.08, 2).rate(160).life(0.2, 0.35)
               .speed(0, 0.2).size(0.18, 0.28).color(0xfff6d8).gravity(0, 0).sizeOver({{0, 1}, {1, 0}}).additive()
               .glow(2.2));
    add(e, Make(id, "Embers").path(symbolSquare()).fitTarget().rate(30).life(0.6, 1.2).speed(0.5, 1.2).toward(0, 1, 0, 40)
               .size(0.03, 0.06).color(0xffc062).gravity(0, 0.4).additive().glow(2.0).turbulence(0.6, 0.6, 1.0));
    tools::smoothLoop(e);
    return e;
}

Effect outlineDrawOn(const IdSource& id) {
    Effect e = begin(id, "Outline Trace", 2.0);
    // The outline is drawn round once, a spark leading, then glows and fades.
    add(e, Make(id, "Traced line").path(symbolSquare()).fitTarget().drawOn(1.0).from(0, 1.0).rate(900).life(1.0, 1.0)
               .speed(0, 0).size(0.16, 0.18).color(0xffd36a).gravity(0, 0).alongPath()
               .fade({{0, 1}, {0.6, 1}, {1, 0}}).look("glowbar").additive().glow(1.6));
    add(e, Make(id, "Spark").path(symbolSquare()).fitTarget().travel(1.0, 0.004).from(0, 1.0).rate(80).life(0.08, 0.1)
               .speed(0, 0).size(0.6, 0.7).turn(0, 360).color(0xffffff).gravity(0, 0).look("starflash").additive()
               .glow(2.0));
    add(e, Make(id, "Spark trail").path(symbolSquare()).fitTarget().travel(1.0, 0.02).from(0, 1.0).rate(200)
               .life(0.2, 0.4).speed(0.2, 0.6).toward(0, 1, 0, 180).size(0.03, 0.06).color(0xffe39a).gravity(0, -1)
               .additive().glow(2.0));
    add(e, Make(id, "Finish glow").from(1.0, 0.9).path(symbolSquare()).fitTarget().burst(120).life(0.6, 0.9).speed(0, 0)
               .size(0.5, 0.7).color(0xffa21f, 0.35).gravity(0, 0).fade({{0, 1}, {1, 0}}).additive());
    return e;
}

Effect symbolGlitter(const IdSource& id) {
    Effect e = begin(id, "Symbol Glitter", 2.0);
    // Glitter all over the symbol, inside its silhouette.
    add(e, Make(id, "Glitter").path(symbolSquare()).fitTarget().fillPath().rate(70).life(0.3, 0.6).speed(0, 0)
               .size(0.1, 0.22).turn(0, 45).color(0xffffff).gravity(0, 0).sizeOver({{0, 0}, {0.3, 1, true}, {1, 0}})
               .look("twinkle").additive().glow(1.8));
    add(e, Make(id, "Shimmer").path(symbolSquare()).fitTarget().fillPath().rate(160).life(0.5, 0.8).speed(0, 0)
               .size(0.04, 0.08).color(0xffe9a8, 0.9).gravity(0, 0).fade({{0, 0}, {0.5, 1}, {1, 0}}).additive());
    return e;
}

// ---- Energy frames: reel and symbol frames, columns and pillars, after
// the frame sheet Bala sent.

Effect fireSparkFrame(const IdSource& id) {
    Effect e = begin(id, "Fire Spark Frame", 2.0);
    const PointList frame = roundedRect(1.6, 3.2, 0.04, 1);
    Make line(id, "Fire line");
    add(e, glowLine(line.path(frame), 0xff7a1f, 0.07, 9.6));
    Make core(id, "Hot core");
    add(e, glowLine(core.path(frame), 0xffe2a8, 0.025, 9.6));
    add(e, Make(id, "Sparkles").path(frame).band(0.12).rate(60).life(0.2, 0.5).speed(0, 0.15).size(0.06, 0.14)
               .turn(0, 45).color(0xffd27a).gravity(0, 0).sizeOver({{0, 0}, {0.3, 1, true}, {1, 0}}).look("twinkle")
               .additive().glow(2.0));
    add(e, Make(id, "Embers").path(frame).rate(40).life(0.4, 0.8).speed(0.1, 0.4).sendOut().size(0.02, 0.04)
               .color(0xffa04a).gravity(0, 0.3).additive().glow(2.0));
    add(e, Make(id, "Inner glow").path(frame).padding(-0.1).rate(400).life(0.3, 0.4).speed(0, 0).size(0.25, 0.35)
               .color(0xff5a1a, 0.1).gravity(0, 0).fade({{0, 0}, {0.5, 1}, {1, 0}}).additive());
    return e;
}

Effect cornerFireFrame(const IdSource& id) {
    Effect e = begin(id, "Corner Fire Frame", 2.0);
    const PointList frame = roundedRect(1.6, 3.2, 0.0, 1);
    add(e, Make(id, "Fire edges").path(frame).band(0.06).rate(300).life(0.15, 0.3).speed(0.05, 0.2).sendOut()
               .size(0.08, 0.16).turn(0, 360).color(0xffffff).gravity(0, 0)
               .tint({hexTint(0, 0xffe0a0), hexTint(0.5, 0xff6a1a), hexTint(1, 0x801808)}).look("flame").additive()
               .glow(1.4).turbulence(0.3, 0.3, 1.5));
    Make edge(id, "Edge line");
    add(e, glowLine(edge.path(frame), 0xff4a12, 0.04, 9.6, 0.8));
    for (int c = 0; c < 4; ++c) {
        const double x = c % 2 == 0 ? -0.8 : 0.8, y = c < 2 ? -1.6 : 1.6;
        const char* names[4] = {"Fireball bottom left", "Fireball bottom right", "Fireball top left", "Fireball top right"};
        add(e, Make(id, names[c]).at(x, y).rate(40).life(0.25, 0.4).speed(0, 0.2).toward(0, 1, 0, 180).size(0.25, 0.4)
                   .turn(0, 360).color(0xffffff).gravity(0, 0.4).spin(-120, 120)
                   .tint({hexTint(0, 0xfff1b0), hexTint(0.5, 0xff7a1f), hexTint(1, 0x801a08)})
                   .sizeOver({{0, 1}, {1, 0.3}}).look("fireball").additive().glow(1.5));
    }
    add(e, Make(id, "Running sparks").path(frame).travel(0.4, 0.05, 2).rate(120).life(0.3, 0.5).speed(0.2, 0.6)
               .toward(0, 1, 0, 180).size(0.02, 0.04).color(0xffc062).gravity(0, -0.5).additive().glow(2.0));
    tools::smoothLoop(e);
    return e;
}

// Crackling electricity on a square frame, in a colour.
Effect electricFrame(const IdSource& id, const char* name, unsigned line, unsigned crackle, unsigned halo) {
    Effect e = begin(id, name, 2.0);
    const PointList frame = roundedRect(2.2, 2.2, 0.06, 2);
    Make l(id, "Line");
    add(e, glowLine(l.path(frame), line, 0.04, 8.8, 0.9));
    add(e, Make(id, "Crackle").path(frame).band(0.1).rate(240).life(0.05, 0.1).speed(0, 0).size(0.35, 0.6)
               .color(crackle).gravity(0, 0).alongPath().look("zigzag").additive().glow(2.0).hold(15));
    add(e, Make(id, "Arcs").path(frame).padding(0.08).rate(10).life(0.06, 0.1).speed(0, 0).size(0.4, 0.8)
               .turn(-35, 35).color(crackle).gravity(0, 0).alongPath().look("bolt").additive().glow(2.2));
    add(e, Make(id, "Halo").path(frame).rate(500).life(0.3, 0.4).speed(0, 0).size(0.3, 0.4).color(halo, 0.12)
               .gravity(0, 0).fade({{0, 0}, {0.5, 1}, {1, 0}}).additive());
    return e;
}
Effect goldElectricFrame(const IdSource& id) { return electricFrame(id, "Gold Electric Frame", 0xffc23a, 0xfff0b0, 0xff8a1a); }
Effect cyanElectricFrame(const IdSource& id) { return electricFrame(id, "Neon Electric Frame", 0x4fe8ff, 0xd8fbff, 0x1fb8ff); }

// A frame of drifting smoke, in a colour.
Effect smokeFrame(const IdSource& id, const char* name, unsigned line, unsigned smoke) {
    Effect e = begin(id, name, 3.0);
    const PointList frame = roundedRect(2.2, 2.2, 0.08, 2);
    Make l(id, "Line");
    add(e, glowLine(l.path(frame), line, 0.035, 8.8, 0.9));
    add(e, Make(id, "Smoke").path(frame).band(0.12).rate(220).life(0.8, 1.4).speed(0.03, 0.15).sendOut().size(0.22, 0.38)
               .turn(0, 360).color(smoke, 0.3).gravity(0, 0).spin(-40, 40).sizeOver({{0, 0.5}, {1, 1.5}})
               .fade({{0, 0}, {0.3, 0.6}, {1, 0}}).look("smoke").additive().turbulence(0.25, 0.4, 0.5));
    add(e, Make(id, "Inner glow").path(frame).padding(-0.06).rate(400).life(0.3, 0.4).speed(0, 0).size(0.2, 0.3)
               .color(smoke, 0.15).gravity(0, 0).fade({{0, 0}, {0.5, 1}, {1, 0}}).additive());
    tools::smoothLoop(e);
    return e;
}
Effect blueSmokeFrame(const IdSource& id) { return smokeFrame(id, "Blue Smoke Frame", 0x7fb8ff, 0x3f7fff); }
Effect violetSmokeFrame(const IdSource& id) { return smokeFrame(id, "Violet Smoke Frame", 0xd6b8ff, 0x8f4dff); }

Effect frameDrawOn(const IdSource& id) {
    Effect e = begin(id, "Frame Trace", 2.0);
    // The frame is drawn on from a corner in the first second, flashes, holds, fades.
    const PointList frame{{{-1.1, 1.1}, {-1.1, -1.1}, {1.1, -1.1}, {1.1, 1.1}}};
    add(e, Make(id, "Drawn line").path(frame).drawOn(1.0).from(0, 2.0).rate(700).life(2.0, 2.0).speed(0, 0)
               .size(0.1, 0.12).color(0xffc85a).gravity(0, 0).alongPath()
               .fade({{0, 1}, {0.5, 1}, {1, 0}}).look("glowbar").additive().glow(1.6));
    add(e, Make(id, "Pen").path(frame).travel(1.0, 0.004).from(0, 1.0).rate(60).life(0.08, 0.1).speed(0, 0)
               .size(0.45, 0.55).turn(0, 360).color(0xffffff).gravity(0, 0).look("starflash").additive().glow(2.0));
    add(e, Make(id, "Shavings").path(frame).travel(1.0, 0.015).from(0, 1.0).rate(160).life(0.3, 0.5).speed(0.2, 0.6)
               .toward(0, 1, 0, 180).size(0.02, 0.05).color(0xffe39a).gravity(0, -1.5).additive().glow(2.0));
    add(e, Make(id, "Flash").from(1.0, 0.5).path(frame).burst(160).life(0.25, 0.35).speed(0, 0).size(0.35, 0.45)
               .color(0xfff1c2, 0.6).gravity(0, 0).alongPath().fade({{0, 1}, {1, 0}}).look("glowbar").additive());
    return e;
}

// A tall reel column lit from below, lightning rising through it.
Effect lightningColumn(const IdSource& id, const char* name, unsigned fill, unsigned bolt, unsigned edge) {
    Effect e = begin(id, name, 2.0);
    add(e, Make(id, "Column glow").rect(1.3, 3.4).rate(500).life(0.4, 0.6).speed(0, 0.3).aim(90, 5)
               .size(0.35, 0.45).color(fill, 0.12).gravity(0, 0).fade({{0, 0}, {0.3, 1}, {1, 0}}).additive());
    add(e, Make(id, "Low glow").rect(1.3, 1.2).at(0, -1.2).rate(300).life(0.4, 0.6).speed(0, 0.4).aim(90, 5)
               .size(0.35, 0.45).color(fill, 0.1).gravity(0, 0).fade({{0, 0}, {0.3, 1}, {1, 0}}).additive());
    add(e, Make(id, "Base light").at(0, -1.7).rate(6).life(0.4, 0.6).speed(0, 0).size(1.6, 2.0).color(fill, 0.45)
               .gravity(0, 0).fade({{0, 0}, {0.3, 1}, {1, 0}}).additive().glow(1.2));
    add(e, Make(id, "Bolts").rect(0.9, 2.0).rate(16).life(0.07, 0.12).speed(0, 0).size(2.2, 3.2).turn(80, 100)
               .color(bolt).gravity(0, 0).look("bolt").additive().glow(2.0).hold(15));
    add(e, Make(id, "Left edge").line(-0.7, -1.8, -0.7, 1.8).band(0.06).rate(60).life(0.06, 0.1).speed(0, 0)
               .size(0.4, 0.7).color(edge).gravity(0, 0).alongPath().look("zigzag").additive().glow(2.0).hold(15));
    add(e, Make(id, "Right edge").line(0.7, -1.8, 0.7, 1.8).band(0.06).rate(60).life(0.06, 0.1).speed(0, 0)
               .size(0.4, 0.7).color(edge).gravity(0, 0).alongPath().look("zigzag").additive().glow(2.0).hold(15));
    add(e, Make(id, "Sparks").rect(1.0, 0.3).at(0, -1.7).rate(50).life(0.3, 0.6).speed(1.0, 2.5).aim(90, 40)
               .size(0.02, 0.05).color(edge).gravity(0, -1.0).additive().glow(2.0).streak(0.03));
    return e;
}
Effect cyanLightningColumn(const IdSource& id) { return lightningColumn(id, "Cyan Lightning Column", 0x1fc8d8, 0xd6fbff, 0x7ff0ff); }
Effect goldLightningColumn(const IdSource& id) { return lightningColumn(id, "Gold Lightning Column", 0xff7a12, 0xfff2b0, 0xffd34a); }

Effect lightPillars(const IdSource& id) {
    Effect e = begin(id, "Light Pillars", 2.0);
    // Two upright beams of light, as at the sides of a winning reel.
    for (const double x : {-0.8, 0.8}) {
        Make beam(id, x < 0 ? "Left beam" : "Right beam");
        add(e, glowLine(beam.line(x, -1.8, x, 1.8), 0xfff2a0, 0.05, 3.6));
        add(e, Make(id, x < 0 ? "Left glow" : "Right glow").line(x, -1.8, x, 1.8).rate(120).life(0.3, 0.4).speed(0, 0)
                   .size(0.5, 0.7).color(0xffc21a, 0.25).gravity(0, 0).fade({{0, 0}, {0.5, 1}, {1, 0}}).additive());
        add(e, Make(id, x < 0 ? "Left swirls" : "Right swirls").line(x, -1.8, x, 1.8).rate(20).life(0.4, 0.7)
                   .speed(0.1, 0.3).toward(0, 1, 0, 180).size(0.1, 0.18).turn(0, 360).color(0xfff0b0, 0.6).gravity(0, 0)
                   .spin(-200, 200).fade({{0, 1}, {1, 0}}).look("swirl").additive().glow(1.6));
    }
    return e;
}

Effect portalGate(const IdSource& id) {
    Effect e = begin(id, "Portal Gate", 2.0);
    const PointList gate = roundedRect(1.4, 3.0, 0.5, 6);
    Make l(id, "Gate line");
    add(e, glowLine(l.path(gate), 0x6fb8ff, 0.07, 8.0));
    Make c(id, "White core");
    add(e, glowLine(c.path(gate), 0xf0f8ff, 0.025, 8.0));
    add(e, Make(id, "Crackle").path(gate).band(0.1).rate(70).life(0.05, 0.09).speed(0, 0).size(0.35, 0.6)
               .color(0xbfe6ff).gravity(0, 0).alongPath().look("zigzag").additive().glow(2.0).hold(15));
    add(e, Make(id, "Inner haze").path(gate).fillPath().rate(16).life(0.6, 0.9).speed(0, 0.1).size(0.6, 0.9)
               .color(0x1f5bff, 0.12).gravity(0, 0).fade({{0, 0}, {0.5, 1}, {1, 0}}).additive());
    add(e, Make(id, "Running light").path(gate).travel(0.6, 0.08, 2).rate(160).life(0.12, 0.2).speed(0, 0)
               .size(0.2, 0.3).color(0xffffff).gravity(0, 0).additive().glow(2.0));
    tools::smoothLoop(e);
    return e;
}

Effect winFrameFlash(const IdSource& id) {
    Effect e = begin(id, "Win Frame Flash", 1.5);
    // A winning symbol's frame: it fills with light, flashes and settles to a glow.
    const PointList frame = roundedRect(1.6, 2.6, 0.06, 2);
    add(e, Make(id, "Fill").path(frame).fillPath().burst(500).life(0.45, 0.6).speed(0, 0).size(0.3, 0.4)
               .color(0xff8a1a, 0.18).gravity(0, 0).fade({{0, 1}, {1, 0}}).additive());
    Make l(id, "Frame line");
    add(e, glowLine(l.path(frame), 0xffb04a, 0.06, 8.4));
    add(e, Make(id, "Sparkles").path(frame).band(0.15).rate(40).life(0.3, 0.5).speed(0, 0.1).size(0.08, 0.16)
               .turn(0, 45).color(0xfff1c2).gravity(0, 0).sizeOver({{0, 0}, {0.3, 1, true}, {1, 0}}).look("twinkle")
               .additive().glow(2.0));
    add(e, Make(id, "Burst").path(frame).burst(60).life(0.4, 0.7).speed(0.6, 1.4).sendOut().size(0.03, 0.06)
               .color(0xffd27a).gravity(0, -1).drag(1.5).additive().glow(2.0).streak(0.03));
    return e;
}

Effect sunOrb(const IdSource& id) {
    Effect e = begin(id, "Sun Orb", 2.0);
    add(e, Make(id, "Core").burst(1).life(2.0, 2.0).speed(0, 0).size(1.5, 1.5).color(0xffe35a).gravity(0, 0)
               .fade({{0, 1}, {1, 1}}).additive().glow(1.6));
    add(e, Make(id, "Corona").burst(1).life(2.0, 2.0).speed(0, 0).size(2.2, 2.2).color(0xffb01a, 0.5).gravity(0, 0)
               .additive());
    add(e, Make(id, "Flares").circle(0.7, true).rate(40).life(0.3, 0.5).speed(0.05, 0.2).toward(0, 1, 0, 180)
               .size(0.25, 0.45).turn(0, 360).color(0xfff2a0, 0.7).gravity(0, 0).spin(-180, 180).look("flame").sendOut().additive()
               .glow(1.6).turbulence(0.3, 0.3, 1.5));
    add(e, Make(id, "Filaments").circle(0.65, true).rate(30).life(0.1, 0.2).speed(0, 0).size(0.4, 0.6).turn(0, 360)
               .color(0xfff8d6).gravity(0, 0).look("zigzag").additive().glow(1.8).hold(15));
    return e;
}

Effect cometTrail(const IdSource& id) {
    Effect e = begin(id, "Comet Trail", 2.0);
    // Runs along a gentle arc, trailing glitter.
    PointList arc;
    for (int i = 0; i <= 24; ++i) {
        const double t = i / 24.0;
        arc.points.push_back(Point2{-2.0 + 4.0 * t, 0.25 * std::sin(t * 3.14159)});
    }
    add(e, Make(id, "Head").path(arc, false).travel(0.5, 0.004).rate(120).life(0.06, 0.08).speed(0, 0).size(0.45, 0.5)
               .color(0xffd04a).gravity(0, 0).additive().glow(2.0));
    add(e, Make(id, "Glitter tail").path(arc, false).travel(0.5, 0.01).rate(300).life(0.4, 0.8).speed(0, 0.15)
               .toward(0, 1, 0, 180).size(0.03, 0.07).color(0xffe08a).gravity(0, -0.2).fade({{0, 1}, {1, 0}}).additive()
               .glow(2.0));
    add(e, Make(id, "Glow tail").path(arc, false).travel(0.5, 0.03).rate(150).life(0.15, 0.25).speed(0, 0)
               .size(0.25, 0.35).color(0xff9a1a, 0.5).gravity(0, 0).sizeOver({{0, 1}, {1, 0.3}}).additive());
    return e;
}

// ---- Sabers and beams, after the saber styles Bala sent: a straight line
// of light with a core, a glow and its own character.

struct SaberStyle {
    unsigned core, glow;
    double glowAlpha;
    unsigned crackle;   // 0: none
    unsigned flame;     // 0: none
    unsigned smoke;     // 0: none
    double width;       // of the glow
    double wobble;      // turbulence on the glow
};

Effect saber(const IdSource& id, const char* name, const SaberStyle& s) {
    Effect e = begin(id, name, 1.0);
    Make core(id, "Core");
    add(e, glowLine(core.line(-2, 0, 2, 0), s.core, 0.035, 4.0));
    Make glow(id, "Glow");
    add(e, glowLine(glow.line(-2, 0, 2, 0), s.glow, s.width * 0.45, 4.0, s.glowAlpha * 0.45)
               .turbulence(s.wobble, 0.3, 2.0));
    add(e, Make(id, "Haze").line(-2, 0, 2, 0).rate(220).life(0.15, 0.25).speed(0, 0.05).toward(0, 1, 0, 180)
               .size(s.width * 1.6, s.width * 2.0).color(s.glow, s.glowAlpha * 0.25).gravity(0, 0)
               .fade({{0, 0}, {0.4, 1}, {1, 0}}).additive().turbulence(s.wobble, 0.3, 2.0));
    if (s.crackle) {
        add(e, Make(id, "Crackle").line(-2, 0, 2, 0).band(0.06).rate(120).life(0.05, 0.08).speed(0, 0).size(0.3, 0.5)
                   .color(s.crackle).gravity(0, 0).alongPath().look("zigzag").additive().glow(2.0).hold(15));
    }
    if (s.flame) {
        add(e, Make(id, "Flames").line(-2, 0, 2, 0).band(0.04).rate(220).life(0.2, 0.4).speed(0.2, 0.6).toward(0, 1, 0, 30)
                   .size(0.12, 0.24).turn(-10, 10).color(0xffffff).gravity(0, 0.5)
                   .tint({hexTint(0, 0xfff1b0), hexTint(0.4, s.flame), hexTint(1, 0x601008)})
                   .sizeOver({{0, 0.6}, {0.3, 1}, {1, 0.2}}).look("flame").additive().glow(1.3).turbulence(0.5, 0.3, 1.5));
    }
    if (s.smoke) {
        add(e, Make(id, "Smoke").line(-2, 0, 2, 0).band(0.05).rate(80).life(0.4, 0.8).speed(0.05, 0.25)
                   .toward(0, 1, 0, 180).size(0.25, 0.4).turn(0, 360).color(s.smoke, 0.35).gravity(0, 0.1)
                   .spin(-40, 40).sizeOver({{0, 0.5}, {1, 1.6}}).fade({{0, 0}, {0.3, 0.7}, {1, 0}}).look("smoke")
                   .additive().turbulence(0.4, 0.4, 0.8));
    }
    return e;
}

Effect saberBlue(const IdSource& id) { return saber(id, "Saber Blue", {0xe6f6ff, 0x2f8cff, 0.5, 0, 0, 0, 0.35, 0.0}); }
Effect saberArc(const IdSource& id) { return saber(id, "Saber Arc Reactor", {0xffffff, 0x2f7bff, 0.45, 0xbfe6ff, 0, 0, 0.3, 0.2}); }
Effect saberBurning(const IdSource& id) { return saber(id, "Saber Burning", {0xfff1c2, 0xff7a1a, 0.5, 0, 0xff7a1a, 0, 0.3, 0.2}); }
Effect saberFirestorm(const IdSource& id) { return saber(id, "Saber Firestorm", {0xffffff, 0xff5a12, 0.55, 0, 0xff6a12, 0x8a2a10, 0.45, 0.5}); }
Effect saberElectric(const IdSource& id) { return saber(id, "Saber Electric", {0xe6fbff, 0x1fb8ff, 0.4, 0xd6f6ff, 0, 0, 0.25, 0.3}); }
Effect saberGreen(const IdSource& id) { return saber(id, "Saber Green Energy", {0xf0ffd6, 0x6fdc1f, 0.45, 0xc8ff6a, 0, 0, 0.3, 0.3}); }
Effect saberNeon(const IdSource& id) { return saber(id, "Saber Neon", {0xffe0f4, 0xff2f9a, 0.55, 0, 0, 0, 0.3, 0.0}); }
Effect saberNebula(const IdSource& id) { return saber(id, "Saber Nebula", {0xf2e0ff, 0x9a4dff, 0.4, 0, 0, 0x8a3dff, 0.4, 0.4}); }
Effect saberSolar(const IdSource& id) { return saber(id, "Saber Solar", {0xffffff, 0xff9a1a, 0.6, 0xfff0b0, 0xffa21f, 0, 0.4, 0.4}); }
Effect saberStarkiller(const IdSource& id) { return saber(id, "Saber Starkiller", {0xffe0e0, 0xff1f2f, 0.55, 0xff8a8a, 0, 0, 0.3, 0.6}); }
Effect saberGhost(const IdSource& id) { return saber(id, "Saber Ghost", {0xd6e6ff, 0x3f5bff, 0.35, 0, 0, 0x3f6bff, 0.45, 0.6}); }
Effect saberYellowSmoke(const IdSource& id) { return saber(id, "Saber Yellow Smoke", {0xfffad6, 0xffd21a, 0.45, 0, 0, 0xffc21a, 0.35, 0.4}); }

// ------------------------------------------------- gameplay and UI, in 3D
// The effects a game screen is built from, made to be seen from any side:
// the warning on the ground, the build-up before a reveal, the hit frames,
// shafts of light, the shine across a badge and the shower of rewards.

Effect aoeTelegraph3d(const IdSource& id) {
    Effect e = begin3d(id, "AoE Telegraph 3D", 2.0);
    // A decal on the ground where an attack will land: it fills from the middle
    // and the whole circle pulses, with embers lifting off the rim.
    add(e, Make(id, "Danger glow").burst(1).life(2.0, 2.0).speed(0, 0).size(6.8, 6.8).color(0xff2a1a, 0.5)
               .gravity(0, 0).sizeOver({{0, 1.0}, {0.5, 1.05}, {1, 1.0}}).look("soft").additive().ground()
               .flatOnPlane().sound("shimmer", false, 0.0, 0.5));
    add(e, Make(id, "Fill").rate(1.0).life(2.0, 2.0).speed(0, 0).size(5.6, 5.6).color(0xff3a22, 0.6).gravity(0, 0)
               .sizeOver({{0, 0.05}, {1, 1.0, true}}).fade({{0, 0.9}, {0.85, 0.9}, {1, 0}}).look("soft")
               .additive().ground().flatOnPlane());
    add(e, Make(id, "Outer ring").burst(1).life(2.0, 2.0).speed(0, 0).size(6.0, 6.0).color(0xff4a2a).gravity(0, 0)
               .solid().look("ring").additive().glow(1.3).ground().flatOnPlane());
    add(e, Make(id, "Hazard ring").burst(1).life(2.0, 2.0).speed(0, 0).size(4.9, 4.9).color(0xffb02a).gravity(0, 0)
               .spin(-40, -40).solid().look("dashring").additive().glow(1.1).ground().flatOnPlane());
    add(e, Make(id, "Inner sigil").burst(1).life(2.0, 2.0).speed(0, 0).size(2.6, 2.6).color(0xff7a3a, 0.8)
               .gravity(0, 0).spin(90, 90).solid().look("sigil").additive().glow(1.0).ground().flatOnPlane());
    add(e, Make(id, "Edge pulse").rate(1.0).life(1.0, 1.0).speed(0, 0).size(6.0, 6.0).color(0xffd0a0).gravity(0, 0)
               .sizeOver({{0, 0.7}, {1, 1.0, true}}).fade({{0, 0}, {0.2, 0.8}, {1, 0}}).look("ring").additive()
               .ground().flatOnPlane());
    add(e, Make(id, "Rim flames").rate(18).circle(2.9, true).life(0.6, 1.0).speed(0.8, 1.6).toward(0, 1, 0, 8)
               .size(0.12, 0.2).color(0xff7a2a).gravity(0, 0.4).ground().sizeOver({{0, 0.6}, {0.3, 1}, {1, 0}})
               .fade({{0, 0}, {0.2, 1}, {1, 0}}).look("flame").additive().glow(1.5));
    add(e, Make(id, "Warning beams").rate(7).circle(2.9, true).life(0.7, 1.1).speed(2.5, 4).toward(0, 1, 0, 0)
               .size(0.1, 0.16).color(0xff5a3a, 0.8).gravity(0, 0).ground().fade({{0, 0}, {0.25, 1}, {1, 0}})
               .look("beam").additive().streak(0.3));
    add(e, Make(id, "Embers").rate(26).circle(2.6).life(1.0, 1.6).speed(0.6, 1.4).toward(0, 1, 0, 15)
               .size(0.04, 0.09).color(0xffb070).gravity(0, 0.3).ground().fade({{0, 0}, {0.2, 1}, {1, 0}})
               .additive().glow(2.0).turbulence(0.3, 0.8, 0.5));
    tools::smoothLoop(e);
    return e;
}

Effect anticipationTeaser3d(const IdSource& id) {
    Effect e = begin3d(id, "Anticipation Teaser 3D", 2.6);
    // A charge that builds for two seconds and breaks: a glow that swells, rings
    // that turn faster, ribbons then sparks circling up, and the flash that reveals.
    add(e, Make(id, "Charge glow").burst(1).life(2.3, 2.3).speed(0, 0).at3(0, 1.2, 0).size(3.4, 3.4).color(0xffb02a)
               .gravity(0, 0).sizeOver({{0, 0.2}, {0.9, 1.0}, {1, 1.2}})
               .fade({{0, 0}, {0.8, 0.8}, {1, 1.0}}).look("soft").additive().glow(1.3)
               .sound("shimmer", false, 0.0, 0.7));
    add(e, Make(id, "Floor ring").burst(1).life(2.3, 2.3).speed(0, 0).size(3.4, 3.4).color(0xffc24a).gravity(0, 0)
               .spin(90, 360).sizeOver({{0, 1.0}, {1, 0.7}}).fade({{0, 0}, {0.2, 0.9}, {1, 1}}).look("sigil")
               .additive().glow(1.1).ground().flatOnPlane());
    add(e, Make(id, "Floor glow").burst(1).life(2.3, 2.3).speed(0, 0).size(4.2, 4.2).color(0xff9f2a, 0.5).gravity(0, 0)
               .fade({{0, 0}, {0.8, 1}, {1, 1}}).look("soft").additive().ground().flatOnPlane());
    add(e, Make(id, "Slow ribbons").rate(70).circle(1.1, true).life(0.7, 1.0).speed(1.2, 1.8).toward(0, 1, 0, 3)
               .size(0.08, 0.14).color(0xfff0b0).gravity(0, 0).orbit(160).ground()
               .fade({{0, 0}, {0.2, 1}, {0.8, 1}, {1, 0}}).additive().glow(2.0).from(0.3, 2.0));
    add(e, Make(id, "Fast ribbons").rate(70).circle(0.8, true).life(0.45, 0.7).speed(2.5, 3.6).toward(0, 1, 0, 2)
               .size(0.08, 0.14).color(0xffffff).gravity(0, 0).orbit(420).ground()
               .fade({{0, 0}, {0.2, 1}, {0.8, 1}, {1, 0}}).additive().glow(2.2).from(1.3, 1.0));
    add(e, Make(id, "Crackle").rate(14).sphere(0.55).at3(0, 1.2, 0).life(0.05, 0.09).speed(0, 0).size(0.3, 0.55)
               .turn(0, 360).color(0xfff4c8, 0.8).gravity(0, 0).look("zigzag").additive().glow(2.0).hold(15)
               .from(1.8, 0.5));
    add(e, Make(id, "Rising sparks").rate(40).circle(0.9).life(0.5, 0.9).speed(1.5, 3).toward(0, 1, 0, 14)
               .size(0.04, 0.08).color(0xffd27a).gravity(0, 0).ground().additive().glow(2.0).streak(0.05)
               .from(0.8, 1.5));
    add(e, Make(id, "Reveal flash").burst(1).life(0.3, 0.3).speed(0, 0).at3(0, 1.2, 0).size(5.2, 5.2).color(0xffffff)
               .gravity(0, 0).sizeOver({{0, 0.4}, {1, 1.2}}).fade({{0, 1}, {1, 0}}).look("starflash").additive()
               .glow(1.6).from(2.3, 0.3));
    add(e, Make(id, "Reveal ring").burst(1).life(0.3, 0.3).speed(0, 0).size(4.5, 4.5).color(0xffe08a).gravity(0, 0)
               .sizeOver({{0, 0.1}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("shockwave").additive()
               .ground().flatOnPlane().from(2.3, 0.3));
    add(e, Make(id, "Reveal sparks").burst(50).sphere(0.15).at3(0, 1.2, 0).life(0.25, 0.28).speed(4, 9)
               .toward(0, 1, 0, 180).size(0.05, 0.1).color(0xffe08a).gravity(0, -4).drag(2).additive().glow(2.0)
               .streak(0.05).from(2.3, 0.3));
    return e;
}

Effect impactFrames3d(const IdSource& id) {
    Effect e = begin3d(id, "Impact Frames 3D", 0.6);
    // Hard, held frames: a white core, a coloured rim, lines and sparks, all on
    // the strike. Each picture is held for two or three frames, as in animation.
    add(e, Make(id, "Rim").burst(1).life(0.13, 0.13).speed(0, 0).at3(0, 1.4, 0).size(3.4, 3.4).color(0xff5a2a)
               .gravity(0, 0).look("starflash").additive().glow(1.2).hold(24).sound("hit", true));
    add(e, Make(id, "Core").burst(1).life(0.09, 0.09).speed(0, 0).at3(0, 1.4, 0).size(2.0, 2.0).color(0xfffbe0)
               .gravity(0, 0).look("starflash").additive().glow(1.5).hold(24));
    add(e, Make(id, "White flash").burst(1).life(0.06, 0.06).speed(0, 0).at3(0, 1.4, 0).size(2.8, 2.8)
               .color(0xffffff).gravity(0, 0).look("soft").additive().hold(24));
    add(e, Make(id, "Impact lines").burst(1).life(0.16, 0.16).speed(0, 0).at3(0, 1.4, 0).size(4.4, 4.4)
               .color(0xffe9a0).gravity(0, 0).fade({{0, 1}, {1, 0}}).look("speedlines").additive().hold(24));
    add(e, Make(id, "Shock ring").burst(1).life(0.2, 0.2).speed(0, 0).at3(0, 1.4, 0).size(3.2, 3.2).color(0xffd890)
               .gravity(0, 0).sizeOver({{0, 0.2}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("ring").additive()
               .glow(1.3).hold(24));
    add(e, Make(id, "Streaks").burst(26).sphere(0.1).at3(0, 1.4, 0).life(0.12, 0.26).speed(9, 16)
               .toward(0, 1, 0, 180).size(0.04, 0.07).color(0xffffff).gravity(0, 0).drag(3)
               .fade({{0, 1}, {1, 0}}).look("streak").additive().glow(1.8).streak(0.08));
    add(e, Make(id, "Sparks").burst(40, 0.02).sphere(0.1).at3(0, 1.4, 0).life(0.25, 0.6).speed(4, 10)
               .toward(0, 1, 0, 180).size(0.04, 0.08).color(0xffc060).gravity(0, -9).drag(2.5).additive()
               .glow(2.0).streak(0.04));
    add(e, Make(id, "Ground scuff").burst(1).life(0.3, 0.3).speed(0, 0).size(2.6, 2.6).color(0xffd890, 0.7)
               .gravity(0, 0).sizeOver({{0, 0.3}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("shockwave")
               .additive().ground().flatOnPlane());
    return e;
}

Effect godRays3d(const IdSource& id) {
    Effect e = begin3d(id, "God Rays 3D", 8.0);
    // Cones and spokes of soft additive light turning slowly behind a prize,
    // with dust floating in them and a halo on the floor.
    add(e, Make(id, "Haze").burst(1).life(8, 8).speed(0, 0).at3(0, 1.6, 0).size(5.0, 5.0).color(0xffd878, 0.5)
               .gravity(0, 0).sizeOver({{0, 1.0}, {0.5, 1.08}, {1, 1.0}}).solid().look("soft").additive());
    add(e, Make(id, "Wide rays").burst(1).life(8, 8).speed(0, 0).at3(0, 1.6, 0).size(6.2, 6.2).color(0xffe08a, 0.5)
               .spin(9, 9).gravity(0, 0).solid().look("rays").additive().glow(1.1));
    add(e, Make(id, "Counter rays").burst(1).life(8, 8).speed(0, 0).at3(0, 1.6, 0).size(5.0, 5.0).turn(22, 22)
               .color(0xfff4c8, 0.35).spin(-6, -6).gravity(0, 0).solid().look("sunburst").additive());
    add(e, Make(id, "Light cone").burst(1).life(8, 8).speed(0, 0).at3(0, 2.6, 0).size(4.4, 4.4).color(0xffe9a8, 0.5)
               .gravity(0, 0).sizeOver({{0, 1.0}, {0.5, 1.1}, {1, 1.0}}).solid().look("lightcone").additive());
    add(e, Make(id, "Core").burst(1).life(8, 8).speed(0, 0).at3(0, 1.6, 0).size(1.8, 1.8).color(0xffffff, 0.8)
               .gravity(0, 0).sizeOver({{0, 1.0}, {0.25, 1.12}, {0.5, 1.0}, {0.75, 1.12}, {1, 1.0}})
               .solid().look("soft").additive().glow(1.4));
    add(e, Make(id, "Floor halo").burst(1).life(8, 8).speed(0, 0).size(3.4, 3.4).color(0xffe39a, 0.7).gravity(0, 0)
               .spin(12, 12).solid().look("halo").additive().ground().flatOnPlane());
    add(e, Make(id, "Floor glow").burst(1).life(8, 8).speed(0, 0).size(4.6, 4.6).color(0xffc85a, 0.35).gravity(0, 0)
               .solid().look("soft").additive().ground().flatOnPlane());
    add(e, Make(id, "Shafts").rate(5).circle(1.2).at3(0, 5.0, 0).life(1.8, 2.6).speed(0.8, 1.2).toward(0, -1, 0, 4)
               .size(0.7, 1.2).color(0xfff1b8, 0.35).gravity(0, 0).ground()
               .fade({{0, 0}, {0.35, 1}, {0.7, 1}, {1, 0}}).look("beam").additive().streak(4.0));
    add(e, Make(id, "Dust in the light").rate(26).sphere(1.6).at3(0, 2.0, 0).life(1.6, 2.8).speed(0.05, 0.25)
               .toward(0, 1, 0, 180).size(0.03, 0.07).color(0xfff7d6).gravity(0, 0)
               .fade({{0, 0}, {0.3, 1}, {0.7, 1}, {1, 0}}).look("glint").additive().glow(1.8)
               .turbulence(0.15, 1.0, 0.3));
    tools::smoothLoop(e);
    return e;
}

Effect sheenPass3d(const IdSource& id) {
    Effect e = begin3d(id, "Sheen Pass 3D", 2.0);
    // A slanted bar of light slides across a badge standing about a metre and a
    // half up, with glints after it. It is not cut to the badge's outline; put it
    // in front of a model of the badge.
    add(e, Make(id, "Sheen bar").burst(1).sphere(0.05).at3(-1.3, 1.2, 0.05).life(0.7, 0.7).speed(3.7, 3.7)
               .toward(1, 0, 0, 0).size(2.2, 2.2).turn(70, 70).color(0xffffff, 0.9).gravity(0, 0)
               .fade({{0, 0}, {0.2, 1}, {0.8, 1}, {1, 0}}).look("beam").additive().glow(1.3)
               .sound("shimmer", false, 0.0, 0.6));
    add(e, Make(id, "Soft trail").burst(1).sphere(0.05).at3(-1.5, 1.2, 0.04).life(0.75, 0.75).speed(3.7, 3.7)
               .toward(1, 0, 0, 0).size(3.2, 3.2).turn(70, 70).color(0xfff0c0, 0.3).gravity(0, 0)
               .fade({{0, 0}, {0.25, 1}, {0.8, 1}, {1, 0}}).look("beam").additive());
    add(e, Make(id, "Floor sheen").burst(1).at3(-1.5, 0, 0).life(0.75, 0.75).speed(3.7, 3.7).toward(1, 0, 0, 0)
               .size(1.2, 1.2).color(0xfff0c0, 0.45).gravity(0, 0).fade({{0, 0}, {0.25, 1}, {0.8, 1}, {1, 0}})
               .look("soft").additive().ground().flatOnPlane());
    add(e, Make(id, "Glints").rate(30).sphere(0.7).at3(0, 1.2, 0.05).life(0.25, 0.45).speed(0, 0.2).size(0.14, 0.3)
               .turn(0, 45).color(0xffffff).gravity(0, 0).sizeOver({{0, 0}, {0.35, 1}, {1, 0}})
               .fade({{0, 0}, {0.3, 1}, {1, 0}}).look("twinkle").additive().glow(1.6).from(0.1, 0.7));
    add(e, Make(id, "Edge flare").burst(1).at3(1.1, 1.2, 0.05).life(0.35, 0.35).speed(0, 0).size(1.2, 1.2)
               .color(0xffffff).gravity(0, 0).sizeOver({{0, 0.2}, {0.4, 1}, {1, 0.3}}).fade({{0, 0}, {0.3, 1}, {1, 0}})
               .look("crossflare").additive().glow(1.5).from(0.6, 0.4));
    return e;
}

Effect coinFountain3d(const IdSource& id) {
    Effect e = begin3d(id, "Coin Fountain 3D", 3.0);
    // Coins and gems thrown up from a spot and falling back with gravity: a
    // reward shower for a big win. Coins tumble, gems turn, glints follow.
    add(e, Make(id, "Floor glow").burst(1).life(3.0, 3.0).speed(0, 0).size(3.2, 3.2).color(0xffb02a, 0.55)
               .gravity(0, 0).sizeOver({{0, 1.0}, {0.5, 1.1}, {1, 1.0}}).solid().look("soft").additive().ground()
               .flatOnPlane().sound("coin", true, 0.0, 0.9, 1.0));
    add(e, Make(id, "Ring pulse").rate(1.5).life(0.8, 0.8).speed(0, 0).size(2.8, 2.8).color(0xffe08a).gravity(0, 0)
               .sizeOver({{0, 0.2}, {1, 1.0, true}}).fade({{0, 0.9}, {1, 0}}).look("ring").additive().ground()
               .flatOnPlane());
    add(e, Make(id, "Fountain light").burst(1).life(3.0, 3.0).speed(0, 0).at3(0, 1.8, 0).size(1.8, 3.6)
               .color(0xffd75e, 0.4).gravity(0, 0).solid().look("lightcone").additive());
    add(e, Make(id, "Coins").rate(32).circle(0.12).life(1.3, 1.8).speed(5.2, 7.6).toward(0, 1, 0, 22)
               .size(0.2, 0.28).turn(0, 360).color(0xffc531).gravity(0, -9.8).spin(-420, 420).ground()
               .fade({{0, 1}, {0.88, 1}, {1, 0}}).look("coin"));
    add(e, Make(id, "Gems").rate(5).circle(0.12).life(1.3, 1.8).speed(5, 7).toward(0, 1, 0, 26)
               .size(0.2, 0.3).turn(0, 360).color(0x6fe0ff).gravity(0, -9.8).spin(-240, 240).ground()
               .fade({{0, 1}, {0.88, 1}, {1, 0}}).look("gem"));
    add(e, Make(id, "Coin trail").rate(30).circle(0.12).life(0.25, 0.4).speed(5.2, 7.6).toward(0, 1, 0, 22)
               .size(0.05, 0.1).color(0xffe08a, 0.8).gravity(0, -9.8).ground().fade({{0, 0.8}, {1, 0}})
               .look("sparkle").additive().glow(1.8));
    add(e, Make(id, "Glints").rate(24).sphere(1.2).at3(0, 1.6, 0).life(0.4, 0.8).speed(0.1, 0.6).size(0.12, 0.3)
               .color(0xfff7cf).gravity(0, 0).sizeOver({{0, 0.1}, {0.35, 1}, {1, 0.1}})
               .fade({{0, 0}, {0.3, 1}, {1, 0}}).look("twinkle").additive().glow(1.5));
    add(e, Make(id, "Landing sparks").rate(18).circle(0.9).life(0.2, 0.45).speed(0.8, 2.0).toward(0, 1, 0, 30)
               .size(0.03, 0.06).color(0xffe9a0).gravity(0, -5).drag(1).ground().additive().glow(2.0).streak(0.04));
    tools::smoothLoop(e);
    return e;
}

struct Entry {
    PresetInfo info;
    Effect (*build)(const IdSource&);
};

PresetInfo describe(const char* id, const char* name, const char* category, const char* description,
                    float viewX, float viewY, float viewHeight, double previewTime) {
    PresetInfo info;
    info.id = id;
    info.name = name;
    info.category = category;
    info.description = description;
    info.viewX = viewX;
    info.viewY = viewY;
    info.viewHeight = viewHeight;
    info.previewTime = previewTime;
    return info;
}

PresetInfo lookDown(PresetInfo info, float pitch) {
    info.viewPitch = pitch;
    return info;
}

// ---- Stage 1 of the expanded 3D library: eight new families (0.40). Each is built
// from the same layers and modules as every other preset, so everything in it
// can be changed after it is opened.

PointList closedCurve(int points, double (*x)(double), double (*y)(double)) {
    PointList out;
    for (int i = 0; i < points; ++i) {
        const double t = static_cast<double>(i) / points * 6.283185307179586;
        out.points.push_back(Point2{x(t), y(t)});
    }
    return out;
}
double circleX(double t) { return 1.5 * std::cos(t); }
double circleY(double t) { return 1.5 + 0.9 * std::sin(t); }
double eightX(double t) { return 1.9 * std::sin(t); }
double eightY(double t) { return 1.5 + 0.7 * std::sin(2 * t); }
double waveX(double t) { return -2.0 + 4.0 * t / 6.283185307179586; }
double waveY(double t) { return 1.5 + 0.55 * std::sin(2 * t); }

// ---- Sci-fi motion (0.41). Six families, three presets each. Every one is built
// in phases over its length: it builds up, goes active, runs and fades out, so one
// pass of the loop is the whole sequence. Each part is its own layer, so it can be
// turned, recoloured, hidden or timed on its own.

using Curve = std::vector<CurveKey>;
// Fades in by `in` (0 to 1 of the length), holds, and is gone by the end from `out`.
Curve appearCurve(double in, double out) { return {{0, 0, true}, {in, 1, true}, {out, 1, true}, {1, 0, true}}; }
// Grows from `from` to full size by `in`, holds, and shrinks to `end` at the close.
Curve growCurve(double in, double from, double end) { return {{0, from, true}, {in, 1, true}, {0.88, 1, true}, {1, end, true}}; }
// A glow that beats `beats` times between `lo` and 1 while it is held.
Curve beatCurve(double in, double out, int beats, double lo) {
    Curve c{{0, 0, true}, {in, 1, true}};
    const double span = out - in;
    for (int i = 0; i < beats; ++i) {
        c.push_back({in + span * (i + 0.5) / beats, lo, true});
        c.push_back({in + span * (i + 1.0) / beats, 1.0, true});
    }
    c.push_back({1, 0, true});
    return c;
}

// One flat, turning part of a hologram lying on the floor at a height.
Make flatPiece(const IdSource& id, const char* name, const char* shape, double length, double height, double size,
               unsigned colour, double alpha, double spin, double in, double out, double glow, double from = 0.3) {
    return Make(id, name).burst(1).life(length, length).at3(0, height, 0).speed(0, 0).size(size, size).color(colour, alpha)
        .gravity(0, 0).spin(spin, spin).sizeOver(growCurve(in, from, 0.5)).fade(appearCurve(in * 0.85, out)).look(shape)
        .additive().glow(glow).ground().flatOnPlane();
}

// The same, standing upright to face the viewer.
Make upright(const IdSource& id, const char* name, const char* shape, double length, double height, double size,
             unsigned colour, double alpha, double spin, double in, double out, double glow, double from = 0.3) {
    return Make(id, name).burst(1).life(length, length).at3(0, height, 0).speed(0, 0).size(size, size).color(colour, alpha)
        .gravity(0, 0).spin(spin, spin).sizeOver(growCurve(in, from, 0.4)).fade(appearCurve(in * 0.85, out)).look(shape)
        .additive().glow(glow);
}

Make motes(const IdSource& id, const char* name, unsigned colour, double rate, double radius, double height, double from,
           double length, double rise) {
    return Make(id, name).from(from, length).rate(rate).circle(radius, true).at3(0, height, 0).life(1.0, 1.8).speed(rise * 0.5, rise)
        .toward(0, 1, 0, 6).size(0.04, 0.09).color(colour).gravity(0, 0).ground().fade({{0, 0}, {0.2, 1}, {1, 0}})
        .look("glint").additive().glow(2.2);
}

// ---- Holographic Ring

Effect techRing3d(const IdSource& id) {
    constexpr double D = 6.0;
    Effect e = begin3d(id, "Tech Ring 3D", D);
    add(e, flatPiece(id, "Platform", "holodisc", D, 0.0, 4.8, 0x2fd0ff, 0.55, 6, 0.10, 0.92, 1.2, 0.5));
    add(e, flatPiece(id, "Outer ticks", "holoticks", D, 0.04, 4.3, 0x7fe6ff, 1.0, 18, 0.14, 0.9, 1.6));
    add(e, flatPiece(id, "Heavy segments", "holosegs", D, 0.42, 3.7, 0x4fd8ff, 1.0, -45, 0.2, 0.88, 1.8));
    add(e, flatPiece(id, "Nodes", "holonodes", D, 0.8, 3.0, 0xbff4ff, 1.0, 72, 0.26, 0.86, 1.7));
    add(e, flatPiece(id, "Inner segments", "holosegs", D, 1.2, 2.0, 0xffffff, 0.9, 135, 0.32, 0.84, 1.6));
    add(e, Make(id, "Scan lines").from(0.9, 4.4).rate(2.6).life(1.4, 1.4).at3(0, 0.05, 0).speed(1.1, 1.1).toward(0, 1, 0, 0)
               .size(3.2, 3.2).color(0x8fecff, 0.8).gravity(0, 0).fade({{0, 0}, {0.2, 1}, {1, 0}}).look("ring").additive()
               .glow(1.4).ground().flatOnPlane());
    add(e, upright(id, "Projection", "hexshield", D, 1.9, 1.5, 0x9fe8ff, 0.85, 55, 0.3, 0.84, 1.6));
    add(e, Make(id, "Core glow").burst(1).life(D, D).at3(0, 1.9, 0).speed(0, 0).size(2.2, 2.2).color(0x5fd8ff, 0.5).gravity(0, 0)
               .fade(beatCurve(0.3, 0.84, 4, 0.55)).look("soft").additive().glow(1.4));
    add(e, motes(id, "Data motes", 0xd8f8ff, 30, 1.9, 0.1, 0.9, 4.4, 1.4));
    add(e, Make(id, "Sparks").from(0.5, 1.0).burst(36, 0.0).circle(1.8, true).at3(0, 0.1, 0).life(0.5, 1.0).speed(1.2, 3.2)
               .toward(0, 1, 0, 60).size(0.03, 0.07).color(0xffffff).gravity(0, -2.0).ground().fade({{0, 1}, {1, 0}})
               .look("spark").additive().glow(2.4).streak(0.03));
    tools::smoothLoop(e);
    return e;
}

Effect energyRing3d(const IdSource& id) {
    constexpr double D = 6.0;
    Effect e = begin3d(id, "Energy Ring 3D", D);
    add(e, flatPiece(id, "Floor glow", "softring", D, 0.0, 5.0, 0x9a4dff, 0.5, 0, 0.12, 0.92, 0.66, 0.4));
    add(e, upright(id, "Oval halo", "ovalhalo", D, 1.9, 4.6, 0xa66bff, 1.0, 0, 0.14, 0.9, 1.04, 0.3));
    add(e, upright(id, "Oval halo inner", "ovalhalo", D, 1.9, 3.1, 0x6f8cff, 0.8, 0, 0.2, 0.88, 0.94, 0.3));
    add(e, flatPiece(id, "Equator ring", "doublering", D, 1.9, 5.6, 0xb98aff, 0.8, -22, 0.16, 0.9, 0.55));
    add(e, flatPiece(id, "Equator thin", "energyring", D, 1.9, 4.2, 0x7f9cff, 0.7, 35, 0.2, 0.88, 0.55));
        add(e, flatPiece(id, "Heavy arcs", "holosegs", D, 0.5, 3.6, 0xff4fd8, 1.0, 55, 0.22, 0.88, 1.10));
    add(e, flatPiece(id, "Inner arcs", "holosegs", D, 0.9, 2.5, 0xc06bff, 1.0, -110, 0.28, 0.86, 1.04));
    add(e, flatPiece(id, "Rim", "dashring", D, 1.3, 1.6, 0xd9c2ff, 0.9, 150, 0.34, 0.84, 0.94));
    add(e, Make(id, "Pulse rings").from(0.8, 4.6).rate(1.1).life(1.6, 1.6).at3(0, 0.2, 0).speed(0, 0).size(5.2, 5.2)
               .color(0xd49bff, 0.9).gravity(0, 0).sizeOver({{0, 0.15}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("energyring")
               .additive().glow(0.99).ground().flatOnPlane());
    add(e, Make(id, "Spiral energy").from(0.8, 4.4).rate(90).circle(1.7, true).at3(0, 0.05, 0).life(0.9, 1.3).speed(0.8, 1.6)
               .toward(0, 1, 0, 8).size(0.07, 0.13).color(0xff9aee).gravity(0, 0.2).orbit(-210).ground()
               .sizeOver({{0, 1}, {1, 0.2}}).fade({{0, 0}, {0.15, 1}, {1, 0}}).look("orb").additive().glow(1.21).trail(0.25, 0.08));
    add(e, Make(id, "Core").burst(1).life(D, D).at3(0, 1.9, 0).speed(0, 0).size(1.4, 1.4).color(0x9fc4ff, 0.9).gravity(0, 0)
               .fade(beatCurve(0.28, 0.84, 5, 0.5)).look("soft").additive().glow(1.21));
    add(e, Make(id, "Core point").burst(1).life(D, D).at3(0, 1.9, 0).speed(0, 0).size(0.45, 0.45).color(0xffffff, 1.0).gravity(0, 0)
               .fade(beatCurve(0.28, 0.84, 5, 0.6)).look("orb").additive().glow(1.32));
    tools::smoothLoop(e);
    return e;
}

Effect goldRing3d(const IdSource& id) {
    constexpr double D = 6.0;
    Effect e = begin3d(id, "Gold Ring 3D", D);
    add(e, flatPiece(id, "Floor disc", "holodisc", D, 0.0, 4.6, 0xffb53a, 0.5, -5, 0.1, 0.92, 1.1, 0.5));
    add(e, flatPiece(id, "Outer ring", "holoticks", D, 0.05, 4.2, 0xffd36a, 1.0, -16, 0.14, 0.9, 1.7));
    add(e, flatPiece(id, "Gold arcs", "holosegs", D, 0.4, 3.5, 0xffc23d, 1.0, 40, 0.2, 0.88, 1.9));
    add(e, flatPiece(id, "Sigils", "sigil", D, 0.75, 2.9, 0xffe49a, 0.9, 85, 0.26, 0.86, 1.6));
    add(e, flatPiece(id, "Nodes", "holonodes", D, 1.1, 2.1, 0xfff0c2, 1.0, -120, 0.32, 0.84, 1.7));
    add(e, Make(id, "Light column").burst(1).life(D, D).at3(0, 1.1, 0).speed(0, 0).size(3.2, 3.2).color(0xffd36a, 0.45).gravity(0, 0)
               .fade(beatCurve(0.3, 0.84, 3, 0.6)).look("glowbar").additive().glow(1.4));
    add(e, upright(id, "Crest", "glowdiamond", D, 1.6, 1.0, 0xfff0c2, 0.9, 0, 0.3, 0.84, 1.8));
    add(e, motes(id, "Embers", 0xffcf6a, 26, 1.8, 0.1, 0.9, 4.4, 1.6));
    add(e, Make(id, "Flash").from(0.3, 0.7).burst(1, 0.0).life(0.5, 0.5).at3(0, 0.1, 0).speed(0, 0).size(4.6, 4.6).color(0xfff0c2, 0.8)
               .gravity(0, 0).sizeOver({{0, 0.2}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("energyring").additive().glow(1.8)
               .ground().flatOnPlane());
    tools::smoothLoop(e);
    return e;
}

// ---- Scanning Beam

// The upright hologram column with its top and bottom emitter rings.
void beamBody(Effect& e, const IdSource& id, double D, unsigned colour, unsigned bright, double height, double widthSize) {
    add(e, flatPiece(id, "Base emitter", "holoticks", D, 0.0, 2.6, bright, 1.0, 20, 0.1, 0.92, 1.6));
    add(e, flatPiece(id, "Base glow", "scandisc", D, 0.02, 2.2, colour, 0.5, 0, 0.1, 0.92, 1.4));
    add(e, flatPiece(id, "Top emitter", "holoticks", D, height, 2.6, bright, 1.0, -20, 0.14, 0.9, 1.6));
    add(e, Make(id, "Light shaft").burst(1).life(D, D).at3(0, height * 0.5, 0).speed(0, 0).size(height * 1.02, height * 1.02).color(bright, 1.0)
               .gravity(0, 0).sizeOver({{0, 0.5}, {0.14, 1.0, true}, {0.88, 1.0, true}, {1, 0.7, true}})
               .fade(beatCurve(0.12, 0.88, 3, 0.7)).look("vstreaks").additive().glow(1.4));
    add(e, Make(id, "Beam").burst(1).life(D, D).at3(0, height * 0.5, 0).speed(0, 0).size(widthSize, widthSize).color(colour, 0.5)
               .gravity(0, 0).sizeOver({{0, 0.5}, {0.14, 1.0, true}, {0.88, 1.0, true}, {1, 0.7, true}})
               .fade(appearCurve(0.12, 0.88)).look("holocylinder").additive().glow(1.5));
}

Effect simpleBeam3d(const IdSource& id) {
    constexpr double D = 5.0;
    Effect e = begin3d(id, "Simple Beam 3D", D);
    beamBody(e, id, D, 0x3fb8ff, 0xaee8ff, 3.2, 4.0);
    add(e, Make(id, "Start flash").from(0, 0.5).burst(1).life(0.4, 0.4).at3(0, 0.1, 0).speed(0, 0).size(3.0, 3.0).color(0xffffff, 0.8)
               .gravity(0, 0).sizeOver({{0, 0.2}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("energyring").additive().glow(1.8)
               .ground().flatOnPlane());
    add(e, Make(id, "Scan plane").from(0.6, 3.6).rate(0.9).life(1.1, 1.1).at3(0, 0.1, 0).speed(2.8, 2.8).toward(0, 1, 0, 0)
               .size(2.5, 2.5).color(0xdff8ff, 1.0).gravity(0, 0).fade({{0, 0}, {0.12, 1}, {0.88, 1}, {1, 0}}).look("scandisc")
               .additive().glow(2.4).ground().flatOnPlane());
    add(e, Make(id, "Scan haze").from(0.6, 3.6).rate(0.9).life(1.1, 1.1).at3(0, 0.1, 0).speed(2.8, 2.8).toward(0, 1, 0, 0)
               .size(3.4, 3.4).color(0x7fd6ff, 0.5).gravity(0, 0).fade({{0, 0}, {0.12, 1}, {0.88, 1}, {1, 0}}).look("softring")
               .additive().glow(1.3).ground().flatOnPlane());
    add(e, Make(id, "Complete pulse").from(4.0, 0.9).burst(1, 0.0).life(0.8, 0.8).at3(0, 0.1, 0).speed(0, 0).size(4.6, 4.6)
               .color(0xaee8ff, 0.9).gravity(0, 0).sizeOver({{0, 0.2}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("energyring")
               .additive().glow(1.8).ground().flatOnPlane());
    add(e, motes(id, "Scan particles", 0xd8f4ff, 28, 0.6, 0.1, 0.7, 3.6, 1.8));
    tools::smoothLoop(e);
    return e;
}

Effect spiralBeam3d(const IdSource& id) {
    constexpr double D = 5.0;
    Effect e = begin3d(id, "Spiral Beam 3D", D);
    beamBody(e, id, D, 0xc24dff, 0xf0b8ff, 3.2, 3.4);
    add(e, Make(id, "Helix glow").burst(1).life(D, D).at3(0, 1.6, 0).speed(0, 0).size(3.6, 3.6).color(0xff7ae8, 0.9).gravity(0, 0)
               .sizeOver({{0, 0.4}, {0.15, 1.0, true}, {0.86, 1.0, true}, {1, 0.7, true}}).fade(appearCurve(0.14, 0.86))
               .look("helix").additive().glow(0.88));
    add(e, Make(id, "Spiral up").from(0.5, 3.8).rate(45).circle(0.62, true).at3(0, 0.05, 0).life(1.5, 1.5).speed(2.0, 2.2)
               .toward(0, 1, 0, 0).size(0.08, 0.13).color(0xff9af0).gravity(0, 0).orbit(300).ground()
               .fade({{0, 0}, {0.1, 1}, {0.85, 1}, {1, 0}}).look("orb").additive().glow(1.20).trail(0.3, 0.07));
    add(e, Make(id, "Spiral down").from(0.5, 3.8).rate(70).circle(0.62, true).at3(0, 3.2, 0).life(1.5, 1.5).speed(2.0, 2.2)
               .toward(0, -1, 0, 0).size(0.06, 0.1).color(0xffffff).gravity(0, 0).orbit(-300).ground()
               .fade({{0, 0}, {0.1, 1}, {0.85, 1}, {1, 0}}).look("glint").additive().glow(1.92));
    add(e, Make(id, "Scan plane").from(0.6, 3.6).rate(0.8).life(1.25, 1.25).at3(0, 3.2, 0).speed(2.5, 2.5).toward(0, -1, 0, 0)
               .size(2.2, 2.2).color(0xffe0fa, 1.0).gravity(0, 0).fade({{0, 0}, {0.12, 1}, {0.88, 1}, {1, 0}}).look("scandisc")
               .additive().glow(1.92).ground().flatOnPlane());
    tools::smoothLoop(e);
    return e;
}

Effect techBeam3d(const IdSource& id) {
    constexpr double D = 5.5;
    Effect e = begin3d(id, "Tech Beam 3D", D);
    add(e, flatPiece(id, "Grid floor", "holodisc", D, 0.0, 5.0, 0x3dff9a, 0.5, 0, 0.1, 0.92, 1.1, 0.5));
    beamBody(e, id, D, 0x2fffa0, 0xc8ffe4, 3.4, 4.2);
    add(e, flatPiece(id, "Ring segments", "holosegs", D, 0.05, 3.4, 0xffb13d, 1.0, -55, 0.16, 0.9, 1.8));
    add(e, Make(id, "Scan plane A").from(0.6, 4.0).rate(0.75).life(1.3, 1.3).at3(0, 0.1, 0).speed(2.6, 2.6).toward(0, 1, 0, 0)
               .size(2.5, 2.5).color(0xd8ffe8, 1.0).gravity(0, 0).fade({{0, 0}, {0.12, 1}, {0.88, 1}, {1, 0}}).look("scandisc")
               .additive().glow(2.4).ground().flatOnPlane());
    add(e, Make(id, "Scan plane B").from(1.3, 3.5).rate(0.75).life(1.3, 1.3).at3(0, 3.4, 0).speed(2.6, 2.6).toward(0, -1, 0, 0)
               .size(2.1, 2.1).color(0xffd08a, 0.9).gravity(0, 0).fade({{0, 0}, {0.12, 1}, {0.88, 1}, {1, 0}}).look("scandisc")
               .additive().glow(2.2).ground().flatOnPlane());
    add(e, Make(id, "Radial pulse").from(1.0, 4.0).rate(0.6).life(1.5, 1.5).at3(0, 0.15, 0).speed(0, 0).size(5.4, 5.4)
               .color(0x6fffc0, 0.9).gravity(0, 0).sizeOver({{0, 0.1}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("energyring")
               .additive().glow(1.8).ground().flatOnPlane());
    add(e, motes(id, "Data bits", 0xd8ffe8, 34, 0.6, 0.1, 0.7, 4.0, 1.8));
    tools::smoothLoop(e);
    return e;
}

// ---- Energy Wave

Effect spiralWave3d(const IdSource& id) {
    constexpr double D = 5.0;
    Effect e = begin3d(id, "Spiral Wave 3D", D);
    add(e, Make(id, "Helix").burst(1).life(D, D).at3(0, 1.7, 0).speed(0, 0).size(3.9, 3.9).color(0x3fc8ff, 0.95).gravity(0, 0)
               .sizeOver({{0, 0.15}, {0.18, 1.0, true}, {0.84, 1.0, true}, {1, 0.4, true}}).fade(appearCurve(0.16, 0.82))
               .look("helix").additive().glow(1.00));
    add(e, Make(id, "Counter helix").burst(1).life(D, D).at3(0, 1.7, 0).speed(0, 0).size(3.1, 3.1).color(0xff5fe8, 0.85).gravity(0, 0)
               .sizeOver({{0, 0.15}, {0.24, 1.0, true}, {0.84, 1.0, true}, {1, 0.4, true}}).fade(appearCurve(0.22, 0.82))
               .look("helix").additive().glow(0.90));
    add(e, Make(id, "Wave ribbons").from(0.5, 4.0).rate(120).circle(0.7, true).at3(0, 0.05, 0).life(1.4, 1.6).speed(2.2, 2.6)
               .toward(0, 1, 0, 0).size(0.07, 0.12).color(0x9fe8ff).gravity(0, 0).orbit(260).ground()
               .sizeOver({{0, 0.5}, {0.5, 1.2}, {1, 0.4}}).fade({{0, 0}, {0.1, 1}, {0.8, 1}, {1, 0}}).look("orb").additive()
               .glow(1.20).trail(0.4, 0.07));
    add(e, Make(id, "Base wave").from(0.3, 4.4).rate(1.2).life(1.7, 1.7).at3(0, 0.1, 0).speed(0, 0).size(5.0, 5.0).color(0x6fd8ff, 0.9)
               .gravity(0, 0).sizeOver({{0, 0.15}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("energyring").additive().glow(0.90)
               .ground().flatOnPlane());
    add(e, Make(id, "Core").burst(1).life(D, D).at3(0, 0.45, 0).speed(0, 0).size(1.3, 1.3).color(0xffffff, 0.9).gravity(0, 0)
               .fade(beatCurve(0.12, 0.84, 5, 0.55)).look("soft").additive().glow(1.00));
    add(e, motes(id, "Sparks", 0xffffff, 30, 1.0, 0.1, 0.7, 4.0, 1.6));
    tools::smoothLoop(e);
    return e;
}

Effect pulseWave3d(const IdSource& id) {
    constexpr double D = 6.0;
    Effect e = begin3d(id, "Pulse Wave 3D", D);
    add(e, Make(id, "Core flash").burst(1, 0.0).life(0.5, 0.5).at3(0, 0.9, 0).speed(0, 0).size(2.4, 2.4).color(0xffffff, 1.0).gravity(0, 0)
               .sizeOver({{0, 0.3}, {0.4, 1.2, true}, {1, 0.8}}).fade({{0, 1}, {1, 0}}).look("starflash").additive().glow(2.4));
    add(e, Make(id, "Core").burst(1).life(D, D).at3(0, 0.9, 0).speed(0, 0).size(1.0, 1.0).color(0x9fd8ff, 0.9).gravity(0, 0)
               .fade(beatCurve(0.06, 0.86, 4, 0.5)).look("soft").additive().glow(2.0));
    add(e, Make(id, "Floor waves").from(0.2, 4.0).rate(1.6).life(1.5, 1.5).at3(0, 0.05, 0).speed(0, 0).size(6.0, 6.0).color(0x4fa8ff, 1.0)
               .gravity(0, 0).sizeOver({{0, 0.08}, {1, 1.0, true}}).fade({{0, 1}, {0.7, 0.8}, {1, 0}}).look("energyring").additive()
               .glow(2.0).ground().flatOnPlane());
    add(e, Make(id, "Upright waves").from(0.2, 4.0).rate(1.6).life(1.5, 1.5).at3(0, 0.9, 0).speed(0, 0).size(5.0, 5.0).color(0xcfe8ff, 0.9)
               .gravity(0, 0).sizeOver({{0, 0.08}, {1, 1.0, true}}).fade({{0, 1}, {0.7, 0.8}, {1, 0}}).look("energyring").additive()
               .glow(1.8));
    add(e, Make(id, "Inner waves").from(0.5, 3.6).rate(1.6).life(1.2, 1.2).at3(0, 0.05, 0).speed(0, 0).size(3.4, 3.4).color(0xffffff, 0.8)
               .gravity(0, 0).sizeOver({{0, 0.08}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("ring").additive().glow(1.6)
               .ground().flatOnPlane());
    add(e, Make(id, "Sparks out").from(0.2, 4.0).rate(70).sphere(0.15).at3(0, 0.9, 0).life(0.5, 0.9).speed(2.0, 4.5).toward(0, 0, 0, 180)
               .size(0.04, 0.08).color(0xbfe0ff).gravity(0, -1.0).fade({{0, 1}, {1, 0}}).look("spark").additive().glow(2.4).streak(0.04));
    tools::smoothLoop(e);
    return e;
}

Effect sliceWave3d(const IdSource& id) {
    constexpr double D = 4.5;
    Effect e = begin3d(id, "Slice Wave 3D", D);
    add(e, Make(id, "Slices").from(0.1, 3.0).rate(2.4).at3(-3.4, 1.3, 0).life(1.1, 1.1).speed(5.4, 5.4).toward(1, 0, 0, 0).size(3.4, 3.4)
               .color(0xffa23a, 1.0).gravity(0, 0).sizeOver({{0, 0.5}, {0.4, 1.0, true}, {1, 1.2}}).fade({{0, 0}, {0.1, 1}, {0.75, 1}, {1, 0}})
               .look("slicearc").additive().glow(2.4).streak(0.0));
    add(e, Make(id, "White edge").from(0.1, 3.0).rate(2.4).at3(-3.4, 1.3, 0).life(1.1, 1.1).speed(5.4, 5.4).toward(1, 0, 0, 0).size(2.8, 2.8)
               .color(0xffffff, 0.9).gravity(0, 0).sizeOver({{0, 0.5}, {0.4, 1.0, true}, {1, 1.2}}).fade({{0, 0}, {0.1, 1}, {0.75, 1}, {1, 0}})
               .look("slicearc").additive().glow(2.0).streak(0.0));
    add(e, Make(id, "Big slice").from(0.4, 2.8).rate(1.0).at3(-3.6, 1.3, 0).life(1.2, 1.2).speed(4.6, 4.6).toward(1, 0, 0, 0).size(5.0, 5.0)
               .color(0xff6a1a, 0.7).gravity(0, 0).sizeOver({{0, 0.5}, {0.4, 1.0, true}, {1, 1.2}}).fade({{0, 0}, {0.1, 1}, {0.75, 1}, {1, 0}})
               .look("slicearc").additive().glow(1.8).streak(0.0));
    add(e, Make(id, "Trail sparks").from(0.1, 3.0).rate(70).rect(0.1, 1.6).at3(-2.6, 1.1, 0).life(0.4, 0.8).speed(3.5, 5.0).toward(1, 0, 0, 12)
               .size(0.04, 0.08).color(0xffd08a).gravity(0, -1.0).fade({{0, 1}, {1, 0}}).look("spark").additive().glow(2.4).streak(0.04));
    add(e, Make(id, "Ground scorch").from(0.2, 3.0).rate(3).rect(5.0, 0.2).at3(0, 0.02, 0).life(0.5, 0.8).speed(0, 0).size(0.9, 1.4)
               .color(0xff8a2a, 0.7).gravity(0, 0).fade({{0, 0}, {0.2, 1}, {1, 0}}).look("soft").additive().ground().flatOnPlane());
    tools::smoothLoop(e);
    return e;
}

// ---- Vortex Portal

// The standing portal: a dark swirling disc inside turning rings.
void portalBody(Effect& e, const IdSource& id, double D, unsigned arm1, unsigned arm2, unsigned rim, double size) {
    add(e, upright(id, "Back glow", "soft", D, 1.7, size * 1.25, arm1, 0.35, 0, 0.16, 0.84, 1.0, 0.2));
    add(e, upright(id, "Swirl", "swirlarms", D, 1.7, size, arm1, 1.0, -70, 0.22, 0.84, 1.2, 0.1));
    add(e, upright(id, "Inner swirl", "swirlarms", D, 1.7, size * 0.62, arm2, 0.95, 120, 0.28, 0.82, 1.2, 0.1));
    add(e, upright(id, "Rim", "portalring", D, 1.7, size * 1.02, rim, 0.8, 24, 0.18, 0.86, 0.9, 0.2));
    add(e, Make(id, "Core").burst(1).life(D, D).at3(0, 1.7, 0).speed(0, 0).size(size * 0.22, size * 0.22).color(0xffffff, 0.9).gravity(0, 0)
               .fade(beatCurve(0.26, 0.82, 4, 0.5)).look("soft").additive().glow(2.2));
}

Effect magicPortal3d(const IdSource& id) {
    constexpr double D = 6.0;
    Effect e = begin3d(id, "Magic Portal 3D", D);
    for (int i = 0; i < 7; ++i) {
        const double t = i / 6.0;  // 0 at the narrow foot, 1 at the wide mouth
        const double sz = 0.7 + 3.4 * t * t, h = 0.2 + 2.6 * t;
        const std::string nm = "Funnel " + std::to_string(i + 1);
        add(e, flatPiece(id, nm.c_str(), "swirlarms", D, h, sz, i % 2 ? 0x8f6bff : 0xb04dff, 0.9, (i % 2 ? -1 : 1) * (90 + 40 * i),
                         0.14 + 0.02 * i, 0.9, 1.8, 0.3));
    }
    add(e, Make(id, "Funnel glow").burst(1).life(D, D).at3(0, 1.5, 0).speed(0, 0).size(4.6, 4.6).color(0x7a4dff, 0.55).gravity(0, 0)
               .turn(180, 180).sizeOver(growCurve(0.2, 0.3, 0.4)).fade(appearCurve(0.18, 0.88)).look("lightcone").additive().glow(1.3));
    add(e, flatPiece(id, "Mouth ring", "doublering", D, 2.8, 4.3, 0xb98aff, 0.8, 20, 0.16, 0.9, 1.1));
    add(e, flatPiece(id, "Mouth glow", "softring", D, 2.8, 4.8, 0x8f6bff, 0.6, 0, 0.16, 0.9, 1.4));
    add(e, Make(id, "Foot glow").burst(1).life(D, D).at3(0, 0.2, 0).speed(0, 0).size(1.2, 1.2).color(0xe8c8ff, 0.9).gravity(0, 0)
               .fade(beatCurve(0.2, 0.84, 4, 0.5)).look("soft").additive().glow(2.4));
    add(e, upright(id, "Rune ring", "sigil", D, 1.7, 3.9, 0xd8b0ff, 0.0, -18, 0.2, 0.86, 1.6, 0.2));
    add(e, Make(id, "Gather sparks").from(0.0, 1.0).burst(30, 0.0).sphere(1.6, true).at3(0, 1.7, 0).life(0.6, 0.9).speed(2.2, 3.0)
               .toward(0, 1.7, 0, 0).size(0.05, 0.09).color(0xf0d0ff).gravity(0, 0).fade({{0, 0}, {0.3, 1}, {1, 0}}).look("glint")
               .additive().glow(2.4));
    add(e, Make(id, "Pulled in").from(1.0, 4.0).rate(46).circle(2.0, true).at3(0, 1.7, 0).life(0.8, 1.2).speed(1.2, 2.2)
               .toward(0, 0, 0, 0).size(0.05, 0.1).color(0xe8c0ff).gravity(0, 0).orbit(-140).fade({{0, 0}, {0.2, 1}, {1, 0}})
               .look("orb").additive().glow(2.2).trail(0.25, 0.06));
    add(e, Make(id, "Rocks").from(1.0, 4.0).rate(3).circle(2.0, true).at3(0, 1.7, 0).life(2.0, 2.6).speed(0.2, 0.4).toward(0, 0, 0, 0)
               .size(0.14, 0.26).turn(0, 360).color(0x6a4a8a).gravity(0, 0).orbit(40).spin(-60, 60).fade({{0, 0}, {0.2, 1}, {0.8, 1}, {1, 0}})
               .look("rock"));
    tools::smoothLoop(e);
    return e;
}

Effect techPortal3d(const IdSource& id) {
    constexpr double D = 6.0;
    Effect e = begin3d(id, "Tech Portal 3D", D);
    portalBody(e, id, D, 0x2fb8ff, 0x6fffe0, 0x9fe8ff, 3.4);
    add(e, upright(id, "Outer ticks", "holoticks", D, 1.7, 4.2, 0x7fe6ff, 1.0, 14, 0.18, 0.88, 1.7, 0.2));
    add(e, upright(id, "Heavy segments", "holosegs", D, 1.7, 3.9, 0x4fd8ff, 1.0, -38, 0.22, 0.86, 1.8, 0.2));
    add(e, upright(id, "Nodes", "holonodes", D, 1.7, 2.1, 0xd8f8ff, 0.9, 90, 0.3, 0.84, 1.6, 0.2));
    add(e, Make(id, "Data motes").from(0.9, 4.2).rate(40).circle(2.0, true).at3(0, 1.7, 0).life(0.8, 1.2).speed(1.0, 2.0).toward(0, 0, 0, 0)
               .size(0.04, 0.08).color(0xcfffff).gravity(0, 0).orbit(110).fade({{0, 0}, {0.2, 1}, {1, 0}}).look("square").additive().glow(2.2));
    add(e, Make(id, "Floor spill").burst(1).life(D, D).at3(0, 0.02, 0).speed(0, 0).size(3.6, 3.6).color(0x2fb8ff, 0.5).gravity(0, 0)
               .fade(appearCurve(0.2, 0.84)).look("softring").additive().glow(1.3).ground().flatOnPlane());
    tools::smoothLoop(e);
    return e;
}

Effect chaosPortal3d(const IdSource& id) {
    constexpr double D = 6.0;
    Effect e = begin3d(id, "Chaos Portal 3D", D);
    portalBody(e, id, D, 0xff4a2a, 0x66ff3d, 0xffb02a, 3.7);
    add(e, upright(id, "Cracked ring", "shardring", D, 1.7, 4.3, 0xff8a3a, 1.0, -30, 0.2, 0.86, 1.8, 0.2));
    add(e, Make(id, "Lightning").from(0.8, 4.4).rate(7).circle(1.9, true).at3(0, 1.7, 0).life(0.08, 0.16).speed(0, 0).size(1.6, 2.6)
               .turn(0, 360).color(0xffe0a0).gravity(0, 0).look("arcjag").additive().glow(2.6));
    add(e, Make(id, "Debris").from(0.8, 4.4).rate(9).circle(2.0, true).at3(0, 1.7, 0).life(1.8, 2.8).speed(0.2, 0.6).toward(0, 0, 0, 0)
               .size(0.16, 0.34).turn(0, 360).color(0x5a3030).gravity(0, 0).orbit(-70).spin(-180, 180)
               .fade({{0, 0}, {0.2, 1}, {0.8, 1}, {1, 0}}).look("rock"));
    add(e, Make(id, "Embers").from(0.8, 4.4).rate(60).circle(1.8, true).at3(0, 1.7, 0).life(0.7, 1.3).speed(1.0, 2.6).toward(0, 1, 0, 120)
               .size(0.04, 0.1).color(0xffb04a).gravity(0, -0.8).orbit(160).fade({{0, 1}, {1, 0}}).look("ember").additive().glow(2.4));
    tools::smoothLoop(e);
    return e;
}

// ---- Electric Arc

// A flickering arc between two ends: several bolts redrawn many times a second,
// each a little different, and a node of light at each end.
void arcBody(Effect& e, const IdSource& id, double D, const char* look, unsigned colour, unsigned core, double length,
             double thick, double rate, double jitter) {
    add(e, Make(id, "Arc glow").from(0.12, D - 0.4).rate(rate).rect(0.05, jitter).at3(0, 1.4, 0).life(0.06, 0.1).speed(0, 0)
               .size(length * 1.15, length * 1.22).turn(-2.0, 2.0).color(colour, 0.75).gravity(0, 0).look(look).additive().glow(1.00 * thick));
    add(e, Make(id, "Arc core").from(0.12, D - 0.4).rate(rate).rect(0.05, jitter * 0.6).at3(0, 1.4, 0).life(0.05, 0.09).speed(0, 0)
               .size(length * 1.13, length * 1.17).turn(-1.5, 1.5).color(core, 1.0).gravity(0, 0).look(look).additive().glow(1.30 * thick));
    add(e, Make(id, "Start node").burst(1).life(D, D).at3(-length * 0.47, 1.4, 0).speed(0, 0).size(0.9, 0.9).color(colour, 0.9).gravity(0, 0)
               .fade(appearCurve(0.04, 0.9)).look("soft").additive().glow(1.00));
    add(e, Make(id, "End node").burst(1).life(D, D).at3(length * 0.47, 1.4, 0).speed(0, 0).size(0.9, 0.9).color(colour, 0.9).gravity(0, 0)
               .fade(appearCurve(0.12, 0.9)).look("soft").additive().glow(1.00));
    add(e, Make(id, "Spark spray").from(0.2, D - 0.5).rate(60).rect(length * 0.9, 0.15).at3(0, 1.4, 0).life(0.2, 0.5).speed(0.5, 1.8)
               .toward(0, 1, 0, 180).size(0.025, 0.06).color(core).gravity(0, -3).fade({{0, 1}, {1, 0}}).look("spark").additive().glow(1.30).streak(0.03));
}

Effect thinArc3d(const IdSource& id) {
    constexpr double D = 3.0;
    Effect e = begin3d(id, "Thin Arc 3D", D);
    arcBody(e, id, D, "arcbolt", 0x4fd8ff, 0xffffff, 4.4, 0.8, 26, 0.35);
    add(e, Make(id, "Spawn flash").from(0, 0.4).burst(1).life(0.25, 0.25).at3(-2.0, 1.4, 0).speed(0, 0).size(1.8, 1.8).color(0xffffff, 0.9)
               .gravity(0, 0).fade({{0, 1}, {1, 0}}).look("starflash").additive().glow(2.4));
    add(e, Make(id, "Impact pulse").from(1.2, 1.6).burst(1, 0.0).burst(1, 0.9).life(0.35, 0.35).at3(2.0, 1.4, 0).speed(0, 0).size(1.8, 1.8)
               .color(0xaee8ff, 0.9).gravity(0, 0).sizeOver({{0, 0.3}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("starflash").additive().glow(2.4));
    tools::smoothLoop(e);
    return e;
}

Effect thickArc3d(const IdSource& id) {
    constexpr double D = 3.0;
    Effect e = begin3d(id, "Thick Arc 3D", D);
    arcBody(e, id, D, "arcjag", 0xb04dff, 0xf4e0ff, 4.6, 1.4, 22, 0.5);
    add(e, Make(id, "Ground flash").from(0.2, 2.6).rate(6).life(0.2, 0.3).at3(2.1, 0.03, 0).speed(0, 0).size(1.8, 2.6).color(0xd8a0ff, 0.9)
               .gravity(0, 0).fade({{0, 1}, {1, 0}}).look("energyring").additive().glow(1.20).ground().flatOnPlane());
    add(e, Make(id, "Haze").burst(1).life(D, D).at3(0, 1.4, 0).speed(0, 0).size(4.8, 4.8).color(0x8a3dff, 0.25).gravity(0, 0)
               .fade(appearCurve(0.06, 0.9)).look("soft").additive().glow(0.72));
    tools::smoothLoop(e);
    return e;
}

Effect splitArc3d(const IdSource& id) {
    constexpr double D = 3.0;
    Effect e = begin3d(id, "Split Arc 3D", D);
    arcBody(e, id, D, "arcfork", 0x3dff9a, 0xe0fff0, 4.4, 1.0, 24, 0.4);
    add(e, Make(id, "Second fork").from(0.2, D - 0.5).rate(18).rect(0.05, 0.3).at3(0, 1.4, 0).life(0.06, 0.1).speed(0, 0).size(4.1, 4.3)
               .turn(180.0, 182.0).color(0x7dffc0, 0.8).gravity(0, 0).look("arcfork").additive().glow(2.0));
    add(e, Make(id, "Branch sparks").from(0.2, D - 0.5).rate(40).circle(1.6).at3(0, 1.4, 0).life(0.15, 0.4).speed(1.5, 3.5).toward(0, 1, 0, 180)
               .size(0.03, 0.06).color(0xe0fff0).gravity(0, -3).fade({{0, 1}, {1, 0}}).look("spark").additive().glow(2.6).streak(0.03));
    tools::smoothLoop(e);
    return e;
}

// ---- Targeting Indicator

// A target on the floor: appears, closes in, locks on, tracks and lets go.
void targetBody(Effect& e, const IdSource& id, double D, unsigned colour, unsigned bright, double size) {
    add(e, Make(id, "Reticle").burst(1).life(D, D).at3(0, 0.03, 0).speed(0, 0).size(size, size).color(colour, 1.0).gravity(0, 0).spin(10, 10)
               .sizeOver({{0, 0.2}, {0.1, 1.5, true}, {0.22, 1.0, true}, {0.84, 1.0, true}, {1, 1.6, true}})
               .fade(appearCurve(0.08, 0.84)).look("reticle").additive().glow(1.9).ground().flatOnPlane());
    add(e, Make(id, "Centre point").burst(1).life(D, D).at3(0, 0.04, 0).speed(0, 0).size(0.5, 0.5).color(bright, 1.0).gravity(0, 0)
               .sizeOver({{0, 0.2}, {0.22, 1.0, true}, {0.3, 1.5, true}, {0.36, 1.0, true}, {1, 1.0}}).fade(beatCurve(0.12, 0.84, 6, 0.5))
               .look("soft").additive().glow(2.4).ground().flatOnPlane());
    add(e, Make(id, "Lock flash").from(1.2, 0.6).burst(1, 0.0).life(0.4, 0.4).at3(0, 0.05, 0).speed(0, 0).size(2.6, 2.6).color(bright, 0.9)
               .gravity(0, 0).sizeOver({{0, 0.4}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("energyring").additive().glow(2.0).ground().flatOnPlane());
}

Effect simpleTarget3d(const IdSource& id) {
    constexpr double D = 5.0;
    Effect e = begin3d(id, "Simple Target 3D", D);
    targetBody(e, id, D, 0xff4a3a, 0xffd0c0, 2.8);
    add(e, flatPiece(id, "Ground ring", "doublering", D, 0.02, 3.6, 0xff4a3a, 0.8, 0, 0.1, 0.86, 1.5, 0.3));
    tools::smoothLoop(e);
    return e;
}

Effect multiRingTarget3d(const IdSource& id) {
    constexpr double D = 5.0;
    Effect e = begin3d(id, "Multi Ring Target 3D", D);
    targetBody(e, id, D, 0xffb02a, 0xfff0c0, 2.4);
    add(e, flatPiece(id, "Lock ring", "lockring", D, 0.05, 3.6, 0xffc94a, 1.0, 40, 0.16, 0.86, 1.8, 1.35));
    add(e, flatPiece(id, "Outer ticks", "holoticks", D, 0.06, 4.0, 0xffd98a, 0.9, -24, 0.2, 0.86, 1.6, 1.4));
    add(e, flatPiece(id, "Dashed ring", "dashring", D, 0.07, 3.0, 0xfff0c0, 0.9, 80, 0.24, 0.84, 1.6, 1.4));
    add(e, flatPiece(id, "Nodes", "holonodes", D, 0.08, 2.0, 0xffe49a, 0.9, -120, 0.28, 0.84, 1.5, 1.3));
    tools::smoothLoop(e);
    return e;
}

Effect depthTarget3d(const IdSource& id) {
    constexpr double D = 5.0;
    Effect e = begin3d(id, "3D Target 3D", D);
    targetBody(e, id, D, 0x3dff7a, 0xd8ffe4, 2.6);
    add(e, flatPiece(id, "Lock ring", "lockring", D, 0.05, 3.4, 0x6fffa8, 1.0, -35, 0.16, 0.86, 1.8, 1.6));
    add(e, Make(id, "Scan column").burst(1).life(D, D).at3(0, 1.1, 0).speed(0, 0).size(2.8, 2.8).color(0x3dff7a, 0.7).gravity(0, 0)
               .sizeOver({{0, 0.4}, {0.2, 1.0, true}, {0.84, 1.0, true}, {1, 0.6}}).fade(appearCurve(0.2, 0.84)).look("holocylinder")
               .additive().glow(1.4));
    add(e, Make(id, "Marker").burst(1).life(D, D).at3(0, 2.7, 0).speed(0, 0).size(0.9, 0.9).color(0xd8ffe4, 1.0).gravity(0, 0)
               .sizeOver({{0, 0.2}, {0.2, 1.0, true}, {0.84, 1.0, true}, {1, 0.4}}).fade(appearCurve(0.2, 0.84)).look("glowdiamond")
               .additive().glow(2.2));
    add(e, Make(id, "Marker arrow").burst(1).life(D, D).at3(0, 2.0, 0).speed(0, 0).size(0.8, 0.8).color(0x6fffa8, 1.0).gravity(0, 0)
               .turn(180, 180).sizeOver({{0, 0.2}, {0.2, 1.0, true}, {0.84, 1.0, true}, {1, 0.4}}).fade(appearCurve(0.2, 0.84))
               .look("chevron").additive().glow(2.0));
    add(e, Make(id, "Depth markers").from(0.6, 4.0).rate(4).circle(1.5, true).at3(0, 0.1, 0).life(1.2, 1.4).speed(0.6, 1.0).toward(0, 1, 0, 0)
               .size(0.14, 0.2).color(0xa8ffc8).gravity(0, 0).ground().fade({{0, 0}, {0.2, 1}, {1, 0}}).look("chevron").additive().glow(2.0));
    tools::smoothLoop(e);
    return e;
}

// ---- Fish Swim

Effect fishAround(const IdSource& id, const char* name, PointList path, double rounds, std::int64_t heads, unsigned colour,
                  unsigned colour2, double size, const char* first, const char* second) {
    Effect e = begin3d(id, name, 4.0);
    add(e, Make(id, "Water light").burst(1).life(4, 4).at3(0, 1.5, -0.5).speed(0, 0).size(7.0, 5.0).color(0x2f9ccf, 0.28)
               .gravity(0, 0).solid().look("soft").additive());
    add(e, Make(id, first).path(path, true).travel(rounds, 0.002, heads).alongPath().rate(260).life(0.05, 0.06)
               .speed(0, 0).size(size, size).color(colour).gravity(0, 0).solid().look("fish").at3(0, 0, 0));
    add(e, Make(id, second).path(path, true).travel(rounds * 0.97, 0.002, heads).alongPath().rate(260).life(0.05, 0.06)
               .speed(0, 0).size(size * 0.8, size * 0.8).color(colour2).gravity(0, 0).solid().look("fish"));
    add(e, Make(id, "Bubbles").rate(8).circle(2.0).at3(0, 0.4, 0).life(1.6, 2.4).speed(0.4, 0.8).toward(0, 1, 0, 12)
               .size(0.08, 0.2).color(0xd6f4ff, 0.7).gravity(0, 0.2).fade({{0, 0}, {0.15, 1}, {0.85, 1}, {1, 0}})
               .look("bubble").turbulence(0.2, 0.8, 0.5));
    add(e, Make(id, "Swim wake").path(path, true).travel(rounds, 0.01, heads).rate(80).life(0.3, 0.5).speed(0, 0.1)
               .size(0.08, 0.16).color(0xd6f4ff, 0.5).gravity(0, 0.1).fade({{0, 0.7}, {1, 0}}).look("bubble"));
    tools::smoothLoop(e);
    return e;
}

Effect fishCircle3d(const IdSource& id) {
    return fishAround(id, "Fish Circle 3D", closedCurve(48, circleX, circleY), 0.25, 2, 0xff9a3c, 0xffd36a, 0.7, "Orange fish", "Gold fish");
}

Effect fishFigureEight3d(const IdSource& id) {
    return fishAround(id, "Fish Figure Eight 3D", closedCurve(64, eightX, eightY), 0.22, 1, 0x4fd0ff, 0x8fe8c8, 0.6, "Blue fish", "Green fish");
}

Effect fishSchool3d(const IdSource& id) {
    Effect e = begin3d(id, "Fish School 3D", 4.0);
    PointList path = closedCurve(64, waveX, waveY);
    // A school: many small fish sharing one wavy path, each a little off the others.
    add(e, Make(id, "Water light").burst(1).life(4, 4).at3(0, 1.5, -0.5).speed(0, 0).size(8.0, 5.0).color(0x2f9ccf, 0.28)
               .gravity(0, 0).solid().look("soft").additive());
    add(e, Make(id, "School").path(path, false).travel(0.25, 0.28, 1).alongPath().rate(14).life(0.9, 1.1).speed(0, 0.1)
               .toward(0, 1, 0, 40).size(0.32, 0.44).color(0xffc94a).gravity(0, 0).solid().look("fish"));
    add(e, Make(id, "Silver fish").path(path, false).travel(0.25, 0.22, 1).alongPath().rate(10).life(0.9, 1.1)
               .speed(0, 0.15).toward(0, 1, 0, 60).size(0.26, 0.36).color(0xcfe8ff).gravity(0, 0).solid().look("fish"));
    add(e, Make(id, "Bubbles").rate(10).circle(2.4).at3(0, 0.3, 0).life(1.6, 2.4).speed(0.4, 0.8).toward(0, 1, 0, 12)
               .size(0.08, 0.2).color(0xd6f4ff, 0.7).gravity(0, 0.2).fade({{0, 0}, {0.15, 1}, {0.85, 1}, {1, 0}})
               .look("bubble").turbulence(0.2, 0.8, 0.5));
    return e;
}

// ---- 3D Trails

Effect trailBase(const IdSource& id, const char* name, unsigned head, unsigned tail, double width, double life,
                 const char* look, double stretch) {
    Effect e = begin3d(id, name, 2.0);
    PointList arc;
    for (int i = 0; i <= 28; ++i) {
        const double t = i / 28.0;
        arc.points.push_back(Point2{-2.6 + 5.2 * t, 1.1 + 0.9 * std::sin(t * 3.14159)});
    }
    add(e, Make(id, "Head").path(arc, false).travel(0.5, 0.004).rate(140).life(0.05, 0.07).speed(0, 0).size(width * 2.2, width * 2.2)
               .color(head).gravity(0, 0).look("orb").additive().glow(2.0));
    add(e, Make(id, "Trail").path(arc, false).travel(0.5, 0.004).rate(260).life(life * 0.8, life).speed(0, 0)
               .size(width, width * 1.1).color(tail, 0.85).gravity(0, 0).sizeOver({{0, 1}, {1, 0.1}})
               .fade({{0, 0.9}, {1, 0}}).look(look).additive().glow(1.3).streak(stretch));
    add(e, Make(id, "Sparkle").path(arc, false).travel(0.5, 0.01).rate(70).life(0.3, 0.6).speed(0, 0.3).toward(0, 1, 0, 180)
               .size(0.04, 0.09).color(head).gravity(0, -0.4).fade({{0, 1}, {1, 0}}).look("glint").additive().glow(2.0));
    tools::smoothLoop(e);
    return e;
}

Effect energyTrail3d(const IdSource& id) {
    Effect e = trailBase(id, "Energy Trail 3D", 0xffffff, 0x4fd0ff, 0.22, 0.5, "soft", 0.0);
    e.layers[0].name = "Energy head";
    e.layers[1].name = "Energy ribbon";
    e.layers[2].name = "Energy sparkle";
    return e;
}
Effect swordTrail3d(const IdSource& id) {
    Effect e = trailBase(id, "Sword Slash Trail 3D", 0xffffff, 0xcfe8ff, 0.3, 0.3, "sliver", 0.04);
    e.layers[0].name = "Blade tip";
    e.layers[1].name = "Slash";
    e.layers[2].name = "Edge sparks";
    PointList arc;
    for (int i = 0; i <= 28; ++i) {
        const double t = i / 28.0;
        arc.points.push_back(Point2{-2.6 + 5.2 * t, 1.1 + 0.9 * std::sin(t * 3.14159)});
    }
    add(e, Make(id, "Afterglow").path(arc, false).travel(0.5, 0.004).rate(120).life(0.4, 0.5).speed(0, 0)
               .size(0.5, 0.6).color(0x9fc8ff, 0.4).gravity(0, 0).sizeOver({{0, 1}, {1, 0.1}})
               .fade({{0, 0.5}, {1, 0}}).look("soft").additive().glow(1.1));
    return e;
}
Effect fireTrail3d(const IdSource& id) {
    Effect e = trailBase(id, "Fire Trail 3D", 0xffe08a, 0xff7a22, 0.34, 0.6, "flame", 0.0);
    e.layers[0].name = "Fireball head";
    e.layers[1].name = "Flame ribbon";
    e.layers[2].name = "Embers";
    PointList arc;
    for (int i = 0; i <= 28; ++i) {
        const double t = i / 28.0;
        arc.points.push_back(Point2{-2.6 + 5.2 * t, 1.1 + 0.9 * std::sin(t * 3.14159)});
    }
    add(e, Make(id, "Smoke").path(arc, false).travel(0.5, 0.01).rate(50).life(0.8, 1.2).speed(0, 0.3).toward(0, 1, 0, 40)
               .size(0.3, 0.5).turn(0, 360).color(0x403838, 0.5).gravity(0, 0.5).sizeOver({{0, 0.6}, {1, 1.6}})
               .fade({{0, 0.5}, {1, 0}}).look("smoke"));
    return e;
}

// ---- 3D Clouds

Effect softClouds3d(const IdSource& id) {
    Effect e = begin3d(id, "Soft Clouds 3D", 6.0);
    add(e, Make(id, "Cloud puffs").rate(3).rect(6.0, 0.6).at3(0, 2.4, -1).life(5.0, 6.0).speed(0.1, 0.25)
               .toward(1, 0, 0, 10).size(1.4, 2.2).color(0xffffff, 0.95).gravity(0, 0).sizeOver({{0, 0.7}, {1, 1.2}})
               .fade({{0, 0}, {0.2, 1}, {0.8, 1}, {1, 0}}).solid().look("cloud"));
    add(e, Make(id, "Shadow puffs").rate(3).rect(6.0, 0.6).at3(0, 2.2, -1.1).life(5.0, 6.0).speed(0.1, 0.25)
               .toward(1, 0, 0, 10).size(1.6, 2.4).color(0xb8c8e8, 0.7).gravity(0, 0).sizeOver({{0, 0.7}, {1, 1.2}})
               .fade({{0, 0}, {0.2, 1}, {0.8, 1}, {1, 0}}).solid().look("puffball"));
    tools::smoothLoop(e);
    return e;
}

Effect lowFog3d(const IdSource& id) {
    Effect e = begin3d(id, "Low Fog 3D", 6.0);
    add(e, Make(id, "Fog banks").rate(6).circle(2.6).life(4.0, 5.5).speed(0.1, 0.3).toward(1, 0, 0, 40).size(2.2, 3.4)
               .turn(0, 360).color(0xdfe8f4, 0.5).gravity(0, 0).spin(-8, 8).sizeOver({{0, 0.6}, {1, 1.3}})
               .fade({{0, 0}, {0.25, 1}, {0.75, 1}, {1, 0}}).look("smoke").ground().turbulence(0.15, 1.5, 0.4));
    add(e, Make(id, "Wisps").rate(5).circle(2.4).life(2.5, 3.5).speed(0.15, 0.35).toward(1, 0.1, 0, 60).size(0.9, 1.6)
               .color(0xffffff, 0.35).gravity(0, 0.05).fade({{0, 0}, {0.3, 1}, {1, 0}}).look("wisp").ground()
               .turbulence(0.2, 1.2, 0.5));
    tools::smoothLoop(e);
    return e;
}

Effect cloudBurst3d(const IdSource& id) {
    Effect e = begin3d(id, "Cloud Burst 3D", 2.5);
    add(e, Make(id, "Puffs").burst(18).sphere(0.3).at3(0, 0.8, 0).life(1.2, 1.8).speed(1.6, 2.8).toward(0, 1, 0, 90)
               .size(0.9, 1.4).turn(0, 360).color(0xffffff, 0.9).gravity(0, 0.3).drag(2.0).spin(-30, 30)
               .sizeOver({{0, 0.4}, {0.4, 1.1, true}, {1, 1.5}}).fade({{0, 0}, {0.1, 1}, {1, 0}}).look("puffball"));
    add(e, Make(id, "Ring cloud").burst(1).life(1.4, 1.4).speed(0, 0).size(5.5, 5.5).color(0xf4f8ff, 0.7).gravity(0, 0)
               .sizeOver({{0, 0.1}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("smokering").ground().flatOnPlane());
    add(e, Make(id, "Dust").burst(20).circle(0.4).life(0.8, 1.4).speed(1.0, 2.4).toward(0, 1, 0, 100).size(0.4, 0.8)
               .turn(0, 360).color(0xd8c8a8, 0.6).gravity(0, 0.2).drag(2).sizeOver({{0, 0.5}, {1, 1.6}})
               .fade({{0, 0.8}, {1, 0}}).look("dustcloud").ground());
    return e;
}

// ---- Rays and Beams

Effect lightPillar3d(const IdSource& id) {
    Effect e = begin3d(id, "Light Pillar 3D", 3.0);
    add(e, Make(id, "Pillar").burst(1).life(3, 3).at3(0, 2.4, 0).speed(0, 0).size(1.2, 4.8).color(0xfff0b0, 0.7)
               .gravity(0, 0).sizeOver({{0, 1}, {0.5, 1.1}, {1, 1}}).solid().look("glowbar").additive().glow(1.4));
    add(e, Make(id, "Core beam").burst(1).life(3, 3).at3(0, 2.4, 0).speed(0, 0).size(0.45, 4.8).color(0xffffff)
               .gravity(0, 0).solid().look("glowbar").additive().glow(1.8));
    add(e, Make(id, "Floor ring").burst(1).life(3, 3).speed(0, 0).size(2.4, 2.4).color(0xffd878).gravity(0, 0)
               .spin(40, 40).solid().look("portalring").additive().glow(1.3).ground().flatOnPlane());
    add(e, Make(id, "Rising motes").rate(24).circle(0.6, true).life(1.2, 1.8).speed(1.6, 2.6).toward(0, 1, 0, 8)
               .size(0.05, 0.1).color(0xfff6d6).gravity(0, 0).fade({{0, 0}, {0.2, 1}, {1, 0}}).look("glint").additive()
               .glow(2.0).ground());
    tools::smoothLoop(e);
    return e;
}

Effect radialRays3d(const IdSource& id) {
    Effect e = begin3d(id, "Radial Rays 3D", 4.0);
    add(e, Make(id, "Rays").burst(1).life(4, 4).at3(0, 1.6, 0).speed(0, 0).size(5.6, 5.6).color(0xffe08a, 0.6)
               .spin(14, 14).gravity(0, 0).solid().look("rays").additive().glow(1.1));
    add(e, Make(id, "Counter rays").burst(1).life(4, 4).at3(0, 1.6, 0).speed(0, 0).size(4.4, 4.4).turn(15, 15)
               .color(0xffffff, 0.4).spin(-9, -9).gravity(0, 0).solid().look("sunburst").additive());
    add(e, Make(id, "Core").burst(1).life(4, 4).at3(0, 1.6, 0).speed(0, 0).size(1.5, 1.5).color(0xffffff, 0.9)
               .gravity(0, 0).sizeOver({{0, 1}, {0.5, 1.2}, {1, 1}}).solid().look("soft").additive().glow(1.6));
    tools::smoothLoop(e);
    return e;
}

Effect energyBeam3d(const IdSource& id) {
    Effect e = begin3d(id, "Energy Beam 3D", 2.0);
    add(e, Make(id, "Beam glow").burst(1).life(2, 2).at3(0, 1.4, 0).speed(0, 0).size(5.6, 1.0).color(0x3fa8ff, 0.6)
               .gravity(0, 0).solid().look("glowbar").additive().glow(1.2));
    add(e, Make(id, "Beam core").burst(1).life(2, 2).at3(0, 1.4, 0).speed(0, 0).size(5.4, 0.35).color(0xffffff)
               .gravity(0, 0).solid().look("glowbar").additive().glow(1.8));
    add(e, Make(id, "Pulses").rate(5).life(0.6, 0.6).at3(-2.6, 1.4, 0).speed(8, 8).toward(1, 0, 0, 0).size(0.5, 0.5)
               .color(0xcfeaff).gravity(0, 0).fade({{0, 0}, {0.1, 1}, {0.9, 1}, {1, 0}}).look("orb").additive().glow(2.0));
    add(e, Make(id, "Spark spray").rate(24).sphere(0.1).at3(2.8, 1.4, 0).life(0.2, 0.4).speed(2, 5).toward(-1, 0, 0, 90)
               .size(0.03, 0.06).color(0xbfe0ff).gravity(0, -4).additive().glow(2.0).streak(0.03));
    tools::smoothLoop(e);
    return e;
}

// ---- 3D Frames and Shapes

Effect frameBase(const IdSource& id, const char* name, const char* rim, const char* inner, unsigned colour, double size) {
    Effect e = begin3d(id, name, 4.0);
    add(e, Make(id, "Back glow").burst(1).life(4, 4).at3(0, 1.6, 0).speed(0, 0).size(size * 1.3, size * 1.3)
               .color(colour, 0.3).gravity(0, 0).solid().look("soft").additive().flatOnPlane());
    add(e, Make(id, "Frame").burst(1).life(4, 4).at3(0, 1.6, 0).speed(0, 0).size(size, size).color(colour)
               .gravity(0, 0).spin(30, 30).solid().look(rim).additive().glow(1.3).flatOnPlane());
    add(e, Make(id, "Inner frame").burst(1).life(4, 4).at3(0, 1.6, 0).speed(0, 0).size(size * 0.72, size * 0.72)
               .color(0xffffff, 0.8).gravity(0, 0).spin(-45, -45).solid().look(inner).additive().glow(1.2).flatOnPlane());
    add(e, Make(id, "Edge sparks").rate(18).circle(size * 0.5, true).at3(0, 1.6, 0).life(0.4, 0.7).speed(0.2, 0.6)
               .toward(0, 1, 0, 180).size(0.04, 0.08).color(colour).gravity(0, -0.8).additive().glow(2.0).streak(0.03));
    tools::smoothLoop(e);
    return e;
}

Effect hexFrame3d(const IdSource& id) {
    Effect e = frameBase(id, "Hex Frame 3D", "hexframe", "hexagon", 0x4fd0ff, 3.0);
    e.layers[0].name = "Hex glow";
    e.layers[1].name = "Hex frame";
    e.layers[2].name = "Inner hexagon";
    return e;
}
Effect diamondFrame3d(const IdSource& id) {
    Effect e = frameBase(id, "Diamond Frame 3D", "frame", "diamond", 0xff7ad8, 3.0);
    e.layers[0].name = "Diamond glow";
    e.layers[1].name = "Square frame";
    e.layers[2].name = "Diamond";
    return e;
}
Effect magicRing3d(const IdSource& id) {
    Effect e = frameBase(id, "Magic Ring 3D", "dashring", "pentacle", 0xb08aff, 3.2);
    e.layers[0].name = "Magic glow";
    e.layers[1].name = "Ring of dashes";
    e.layers[2].name = "Pentacle";
    return e;
}

// ---- Boosters

Effect speedBooster3d(const IdSource& id) {
    Effect e = begin3d(id, "Speed Booster 3D", 1.6);
    add(e, Make(id, "Launch flash").burst(1).life(0.2, 0.2).at3(0, 0.6, 0).speed(0, 0).size(3.0, 3.0).color(0xfff0b0)
               .gravity(0, 0).fade({{0, 1}, {1, 0}}).look("pow").additive().glow(1.6));
    add(e, Make(id, "Ground ring").burst(2, 0).burst(1, 0.15).life(0.6, 0.6).speed(0, 0).size(4.2, 4.2).color(0xffc94a)
               .gravity(0, 0).sizeOver({{0, 0.1}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("doublering").additive()
               .ground().flatOnPlane());
    add(e, Make(id, "Speed lines").burst(24).circle(0.5, true).at3(0, 0.6, 0).life(0.3, 0.5).speed(8, 14)
               .toward(-1, 0.1, 0, 25).size(0.05, 0.09).color(0xffffff).gravity(0, 0).fade({{0, 1}, {1, 0}})
               .look("streak").additive().glow(2.0).streak(0.07));
    add(e, Make(id, "Thrust flame").burst(40).sphere(0.15).at3(0, 0.6, 0).life(0.3, 0.6).speed(3, 6)
               .toward(-1, 0, 0, 14).size(0.3, 0.5).turn(0, 360).color(0xffffff).gravity(0, 0.4).spin(-90, 90)
               .tint({hexTint(0, 0xfff1b0), hexTint(0.5, 0xff8a2a), hexTint(1, 0x7a2a10)}).sizeOver({{0, 1}, {1, 0.3}})
               .fade({{0, 0.9}, {1, 0}}).look("flame").additive().glow(1.3));
    add(e, Make(id, "Dust").burst(14).circle(0.4).life(0.6, 1.0).speed(0.5, 1.4).toward(-1, 0.2, 0, 60).size(0.4, 0.7)
               .turn(0, 360).color(0xd8c8a8, 0.6).gravity(0, 0.2).drag(2).sizeOver({{0, 0.5}, {1, 1.5}})
               .fade({{0, 0.8}, {1, 0}}).look("dustcloud").ground());
    return e;
}

Effect launchBooster3d(const IdSource& id) {
    Effect e = begin3d(id, "Launch Booster 3D", 2.0);
    add(e, Make(id, "Charge ring").burst(1, 0).life(0.4, 0.4).speed(0, 0).size(3.0, 3.0).color(0x6fd6ff).gravity(0, 0)
               .sizeOver({{0, 1.2}, {1, 0.2}}).fade({{0, 0}, {0.3, 1}, {1, 0.4}}).look("ring").additive().glow(1.4)
               .ground().flatOnPlane());
    add(e, Make(id, "Launch burst").burst(1, 0.4).life(0.5, 0.5).speed(0, 0).size(5.0, 5.0).color(0xffffff).gravity(0, 0)
               .sizeOver({{0, 0.1}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("shockwave").additive().ground().flatOnPlane());
    add(e, Make(id, "Up flame").burst(60, 0.4).sphere(0.2).life(0.4, 0.8).speed(3, 7).toward(0, -1, 0, 20)
               .size(0.3, 0.55).turn(0, 360).color(0xffffff).gravity(0, 0.3).spin(-90, 90)
               .tint({hexTint(0, 0xfff1b0), hexTint(0.5, 0x4fc8ff), hexTint(1, 0x20308a)}).sizeOver({{0, 1}, {1, 0.3}})
               .fade({{0, 0.9}, {1, 0}}).look("flame").additive().glow(1.3));
    add(e, Make(id, "Rising rings").burst(1, 0.45).burst(1, 0.6).burst(1, 0.75).life(0.5, 0.5).speed(2.5, 2.5)
               .toward(0, 1, 0, 0).size(2.0, 2.0).color(0xbfe8ff).gravity(0, 0).sizeOver({{0, 0.6}, {1, 1.2}})
               .fade({{0, 1}, {1, 0}}).look("ring").additive().glow(1.3).ground().flatOnPlane());
    add(e, Make(id, "Vertical lines").burst(20, 0.4).circle(0.7, true).life(0.3, 0.5).speed(8, 13).toward(0, 1, 0, 6)
               .size(0.04, 0.08).color(0xffffff).gravity(0, 0).fade({{0, 1}, {1, 0}}).look("streak").additive().glow(2.0)
               .streak(0.08));
    return e;
}

Effect powerUpBurst3d(const IdSource& id) {
    Effect e = begin3d(id, "Power-up Burst 3D", 1.8);
    add(e, Make(id, "Energy gather").burst(30, 0).sphere(1.6, true).at3(0, 1.2, 0).life(0.5, 0.5).speed(2.6, 3.4)
               .toward(0, 0, 0, 0).size(0.06, 0.1).color(0xffe08a).gravity(0, 0).fade({{0, 0}, {0.3, 1}, {1, 0}})
               .look("glint").additive().glow(2.0));
    add(e, Make(id, "Burst glow").burst(1, 0.5).life(0.45, 0.45).at3(0, 1.2, 0).speed(0, 0).size(3.4, 3.4).color(0xfff6d6)
               .gravity(0, 0).sizeOver({{0, 0.3}, {1, 1.2}}).fade({{0, 1}, {1, 0}}).look("starflash").additive().glow(1.6));
    add(e, Make(id, "Rings").burst(1, 0.5).burst(1, 0.62).life(0.7, 0.7).at3(0, 1.2, 0).speed(0, 0).size(4.0, 4.0)
               .color(0xffd04a).gravity(0, 0).sizeOver({{0, 0.1}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}})
               .look("ring").additive().glow(1.4).ground().flatOnPlane());
    add(e, Make(id, "Stars").burst(24, 0.5).sphere(0.2).at3(0, 1.2, 0).life(0.6, 1.0).speed(2.5, 5.5).toward(0, 1, 0, 120)
               .size(0.15, 0.28).turn(0, 360).color(0xffe08a).gravity(0, -3).spin(-200, 200)
               .fade({{0, 1}, {0.7, 1}, {1, 0}}).look("star").additive().glow(1.6));
    return e;
}

// ---- 3D Water

Effect waterRipples3d(const IdSource& id) {
    Effect e = begin3d(id, "Water Ripples 3D", 3.0);
    add(e, Make(id, "Ripples").rate(1.4).life(2.2, 2.2).speed(0, 0).size(4.4, 4.4).color(0xa8e2ff, 0.8).gravity(0, 0)
               .sizeOver({{0, 0.05}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("ripple").additive().ground().flatOnPlane());
    add(e, Make(id, "Inner rings").rate(1.4).life(1.4, 1.4).speed(0, 0).size(2.6, 2.6).color(0xe0f6ff, 0.7).gravity(0, 0)
               .sizeOver({{0, 0.05}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("ring").additive().ground().flatOnPlane());
    add(e, Make(id, "Pool glow").burst(1).life(3, 3).speed(0, 0).size(4.0, 4.0).color(0x2f9ccf, 0.4).gravity(0, 0)
               .solid().look("soft").additive().ground().flatOnPlane());
    tools::smoothLoop(e);
    return e;
}

Effect waterJet3d(const IdSource& id) {
    Effect e = begin3d(id, "Water Jet 3D", 2.0);
    add(e, Make(id, "Jet drops").rate(90).circle(0.08).life(0.8, 1.1).speed(6, 8).toward(0, 1, 0, 6).size(0.08, 0.14)
               .color(0x7fd2ff).gravity(0, -12).ground().solid().look("droplet").streak(0.02));
    add(e, Make(id, "Column").burst(1).life(2, 2).at3(0, 1.0, 0).speed(0, 0).size(0.5, 2.2).color(0xbfeaff, 0.6)
               .gravity(0, 0).solid().look("glowbar").additive());
    add(e, Make(id, "Mist").rate(14).circle(0.5).at3(0, 2.2, 0).life(0.8, 1.4).speed(0.3, 0.8).toward(0, 1, 0, 80)
               .size(0.4, 0.8).turn(0, 360).color(0xffffff, 0.45).gravity(0, -0.5).sizeOver({{0, 0.5}, {1, 1.5}})
               .fade({{0, 0.7}, {1, 0}}).look("puff").ground());
    add(e, Make(id, "Base splash").rate(10).circle(0.6, true).life(0.3, 0.5).speed(1.5, 2.5).toward(0, 1, 0, 70)
               .size(0.06, 0.1).color(0xd6f2ff).gravity(0, -10).ground().solid().look("droplet"));
    add(e, Make(id, "Ripples").rate(1.5).life(1.3, 1.3).speed(0, 0).size(3.0, 3.0).color(0xa8e2ff, 0.8).gravity(0, 0)
               .sizeOver({{0, 0.1}, {1, 1.0, true}}).fade({{0, 1}, {1, 0}}).look("ripple").additive().ground().flatOnPlane());
    tools::smoothLoop(e);
    return e;
}

Effect waterVortex3d(const IdSource& id) {
    Effect e = begin3d(id, "Water Vortex 3D", 3.0);
    add(e, Make(id, "Whirlpool").burst(1).life(3, 3).speed(0, 0).size(4.0, 4.0).color(0x3fb6ff, 0.9).gravity(0, 0)
               .spin(-140, -140).solid().look("vortex").additive().glow(1.1).ground().flatOnPlane());
    add(e, Make(id, "Spiral spray").rate(50).circle(1.5, true).life(1.0, 1.4).speed(0.8, 1.6).toward(0, 1, 0, 10)
               .size(0.08, 0.14).color(0xbfeaff).gravity(0, -0.5).orbit(-200).ground()
               .fade({{0, 0}, {0.15, 1}, {1, 0}}).look("droplet").trail(0.25, 0.08));
    add(e, Make(id, "Foam ring").burst(1).life(3, 3).speed(0, 0).size(3.4, 3.4).color(0xffffff, 0.6).gravity(0, 0)
               .spin(60, 60).solid().look("dashring").ground().flatOnPlane());
    add(e, Make(id, "Bubbles").rate(12).circle(1.6).life(1.2, 1.8).speed(0.3, 0.8).toward(0, 1, 0, 20).size(0.08, 0.18)
               .color(0xd6f4ff, 0.7).gravity(0, 0.4).fade({{0, 0}, {0.15, 1}, {0.85, 1}, {1, 0}}).look("bubble").ground()
               .turbulence(0.2, 0.8, 0.5));
    tools::smoothLoop(e);
    return e;
}


const std::vector<Entry>& table() {
    static const std::vector<Entry> entries = {
        {describe("toon-puff", "Toon Puff", "Toon", "A hand-drawn style burst of puffy clouds.", 0, 0, 5.6f, 0.2), toonPuff},
        {describe("toon-hit", "Toon Hit", "Toon", "A spiky flash for a hit landing.", 0, 0, 5.2f, 0.08), toonHit},
        {describe("toon-explosion", "Toon Explosion", "Toon", "A flat-shaded explosion of fire and smoke puffs.", 0, 0.2f, 6.4f, 0.25), toonExplosion},
        {describe("slash", "Slash", "Toon", "A blade swipe.", 0.9f, 0.3f, 5.4f, 0.1), slash},
        {describe("toon-fire", "Toon Fire", "Toon", "A flat-shaded fire with a hot core.", 0, 1.3f, 5.2f, 2.0), toonFire},
        {describe("magic-orb", "Magic Orb", "Toon", "A floating orb with glints around it.", 0, 0, 5.4f, 2.0), magicOrb},
        {describe("toon-sparkles", "Toon Sparkles", "Toon", "Flat four-pointed sparkles, for anything shiny.", 0, 0, 5.2f, 2.0), toonSparkles},

        {describe("fire-blast", "Fire Blast", "Blasts", "A fiery explosion with smoke, sparks and a shockwave.", 0, 0.3f, 6.2f, 0.3), fireBlast},
        {describe("cartoon-pop", "Cartoon Pop", "Blasts", "A clean, flat pop of puffs and stars.", 0, 0, 5.6f, 0.22), cartoonPop},
        {describe("arcane-burst", "Arcane Burst", "Blasts", "A burst of magic energy with rings and rays.", 0, 0, 6.4f, 0.25), arcaneBurst},
        {describe("smoke-puff", "Smoke Puff", "Blasts", "A soft poof of dust, for landings and vanishing.", 0, 0.3f, 5, 0.35), smokePuff},

        {describe("campfire", "Campfire", "Fire", "A steady fire with embers and smoke.", 0, 1.7f, 5.5f, 2.0), campfire},
        {describe("spirit-flame", "Spirit Flame", "Fire", "A cold magical flame. Change its colour to re-hue it.", 0, 1.6f, 5, 2.0), spiritFlame},
        {describe("rising-embers", "Rising Embers", "Fire", "Embers drifting up across the whole scene.", 0, 2.2f, 6, 3.0), risingEmbers},

        {describe("bubbles", "Bubbles", "Water", "Bubbles drifting upward and popping.", 0, 1.8f, 5.5f, 3.2), bubbles},
        {describe("bubble-pop", "Bubble Pop", "Water", "One bubble bursting into droplets.", 0, 0, 4.4f, 0.18), bubblePop},
        {describe("water-splash", "Water Splash", "Water", "Drops thrown up and falling back.", 0, 1.3f, 5, 0.4), waterSplash},

        {describe("soft-glow", "Soft Glow", "Glow and magic", "A breathing glow with twinkles around it.", 0, 0, 5.5f, 2.0), softGlow},
        {describe("magic-sparkles", "Magic Sparkles", "Glow and magic", "A cloud of twinkling coloured glints.", 0, 0, 5.5f, 2.0), magicSparkles},
        {describe("healing-aura", "Healing Aura", "Glow and magic", "Rising green light for heals and buffs.", 0, 1.3f, 5.5f, 2.0), healingAura},
        {describe("level-up", "Level Up", "Glow and magic", "A golden column of light with stars.", 0, 1.6f, 6.2f, 0.55), levelUp},
        {describe("fairy-dust", "Fairy Dust", "Glow and magic", "Glitter falling from a point.", 0, -1, 4.6f, 2.0), fairyDust},

        {describe("frame-sparkle", "Frame Sparkle", "Frames", "A golden outline that twinkles, for buttons and cards.", 0, 0, 5, 2.0), frameSparkle},
        {describe("neon-frame", "Neon Frame", "Frames", "A glowing neon outline.", 0, 0, 5, 2.0), neonFrame},
        {describe("burning-frame", "Burning Frame", "Frames", "An outline on fire.", 0, 0.3f, 5.4f, 2.0), burningFrame},

        {describe("muzzle-flash", "Muzzle Flash", "Shooting", "A gun firing to the right.", 1.2f, 0, 4.5f, 0.04), muzzleFlash},
        {describe("laser-bolts", "Laser Bolts", "Shooting", "Energy bolts flying to the right.", 3.4f, 0, 4.5f, 1.0), laserBolts},
        {describe("hit-impact", "Hit Impact", "Shooting", "The flash and lines of a hit landing.", 0, 0, 5.5f, 0.07), hitImpact},
        {describe("shooting-stars", "Shooting Stars", "Shooting", "Streaks of light crossing a night sky.", 0, 0, 5.5f, 2.2), shootingStars},

        {describe("coin-burst", "Coin Burst", "Rewards", "Coins thrown up from a point.", 0, 1.5f, 6.4f, 0.5), coinBurst},
        {describe("confetti", "Confetti", "Rewards", "A cannon of coloured paper.", 0, 2.2f, 6.5f, 0.8), confetti},
        {describe("star-burst", "Star Burst", "Rewards", "Stars flying out for a win or a reward.", 0, 0, 6, 0.3), starBurst},
        {describe("hearts", "Hearts", "Rewards", "Hearts floating upward.", 0, 1.6f, 5.5f, 2.2), hearts},
        {describe("gem-shatter", "Gem Shatter", "Rewards", "A gem breaking into shards.", 0, -0.3f, 6, 0.25), gemShatter},
        {describe("trophy-win", "Trophy Win", "Rewards", "A trophy bouncing in on golden rays, with paper and stars.", 0, 0.3f, 7.4f, 0.5), trophyWin},

        {describe("rain", "Rain", "Weather", "Rain across the whole scene.", 0, 0, 6, 1.5), rain},
        {describe("snow", "Snow", "Weather", "Snow drifting down.", 0, 0, 6, 4.0), snow},
        {describe("fireflies", "Fireflies", "Weather", "Small lights wandering and blinking.", 0, 0, 5, 3.6), fireflies},
        {describe("falling-leaves", "Falling Leaves", "Weather", "Autumn leaves turning over as they drift down.", 0, 0, 6, 3.6), fallingLeaves},

        {describe("candy-pop", "Candy Pop", "Match-3", "A candy clearing: glossy pieces, a pop ring and twinkles.", 0, 0, 5.4f, 0.12), candyPop},
        {describe("jelly-splat", "Jelly Splat", "Match-3", "Jelly breaking: a wet splat and flying drops.", 0, 0, 5.4f, 0.12), jellySplat},
        {describe("line-blast-across", "Line Blast Across", "Match-3", "A striped piece firing beams along its row.", 0, 0, 7, 0.14), lineBlastAcross},
        {describe("line-blast-down", "Line Blast Down", "Match-3", "A striped piece firing beams along its column.", 0, 0, 7, 0.14), lineBlastDown},
        {describe("candy-bomb", "Candy Bomb", "Match-3", "A wrapped bomb going off twice, throwing candy.", 0, 0, 7.5f, 0.42), candyBomb},
        {describe("rainbow-burst", "Rainbow Burst", "Match-3", "A rainbow orb firing coloured beams in every direction.", 0, 0, 7, 0.22), rainbowBurst},
        {describe("sweet-celebration", "Sweet Celebration", "Match-3", "Candy thrown up across the screen for a level win.", 0, 3.4f, 8, 0.9), sweetCelebration},
        {describe("collect-sparkle", "Collect Sparkle", "Match-3", "A piece arriving at its goal.", 0, 0, 4.4f, 0.12), collectSparkle},
        {describe("hint-glow", "Hint Glow", "Match-3", "A soft pulse with twinkles, to point at a move.", 0, 0, 4.4f, 0.6), hintGlow},

        {describe("starburst-flare", "Starburst Flare", "Light and energy", "A white-hot star of thin rays in a blue halo.", 0, 0, 5.6f, 0.1), starburstFlare},
        {describe("reward-rays", "Reward Rays", "Light and energy", "Slowly turning golden rays to put behind a reward.", 0, 0, 7, 1.5), rewardRays},
        {describe("meteor-shower", "Meteor Shower", "Light and energy", "Comets with white-hot heads and coloured tails.", -2.2f, -3.2f, 8, 2.0), meteorShower},
        {describe("stylized-comets", "Stylized Comets", "Light and energy", "Glowing comets with long flowing ribbon trails.", -2.5f, 0.5f, 9, 1.3), stylizedComets},
        {describe("firework-sparks", "Firework Sparks", "Light and energy", "Sparks with trails, falling as they fade.", 0, -0.5f, 8, 0.6), fireworkSparks},
        {describe("lightning-strike", "Lightning Strike", "Light and energy", "A flickering bolt of electricity.", 0, 0, 7, 0.08), lightningStrike},
        {describe("electric-orb", "Electric Orb", "Light and energy", "A ball of crackling arcs.", 0, 0, 5, 1.0), electricOrb},

        {describe("magic-circle", "Magic Circle", "Ground and portals", "A turning circle of runes on the floor, with light rising from it.", 0, 0.9f, 6.6f, 2.0), magicCircle},
        {describe("vortex-portal", "Vortex Portal", "Ground and portals", "A whirlpool of light lying on the ground, with beams and sparks.", 0, 0.9f, 6.8f, 2.0), vortexPortal},
        {describe("ground-slam", "Ground Slam", "Ground and portals", "A shockwave racing across the floor, with dust and chips.", 0, 0.8f, 7.0f, 0.18), groundSlam},
        {describe("rune-ward", "Rune Ward", "Ground and portals", "A turning seal on the floor with runes rising inside a shield.", 0, 1.0f, 6.8f, 2.0), runeWard},

        {describe("rising-smoke", "Rising Smoke", "Smoke and wind", "A column of smoke winding upward and thinning away.", 0, 0.4f, 7.0f, 4.0), risingSmoke},
        {describe("ground-mist", "Ground Mist", "Smoke and wind", "Low mist drifting across the scene.", 0, -0.8f, 6.5f, 5.0), groundMist},
        {describe("spirit-wisps", "Spirit Wisps", "Smoke and wind", "Ribbons of light winding upward on moving air.", 0, 0.2f, 7.0f, 4.0), spiritWisps},
        {describe("burn-away", "Burn Away", "Smoke and wind", "A sheet burning through with white-hot edges, embers and smoke.", 0, 0.4f, 7.0f, 1.3), burnAway},
        {describe("vanish", "Vanish", "Smoke and wind", "A glowing shape breaking up and blowing away as dust.", 0, 0.2f, 6.0f, 0.75), vanish},

        {describe("bouncy-pop", "Bouncy Pop", "Cartoon motion", "A candy blob that lands flat, wobbles and settles: squash and stretch.", 0, 0, 5.4f, 0.2), bouncyPop},
        {describe("wind-up-blast", "Wind-up Blast", "Cartoon motion", "Energy drawn in, a held breath, then the blast: anticipation.", 0, 0, 7.4f, 0.68), windUpBlast},
        {describe("spiral-burst", "Spiral Burst", "Cartoon motion", "Sparks that curl out in two spirals: arcs.", 0, 0, 6.6f, 0.45), spiralBurst},
        {describe("springy-poof", "Springy Poof", "Cartoon motion", "Puffs that shoot past, spring back and hang: overshoot and settle.", 0, 0, 6.0f, 0.3), springyPoof},

        {describe("buff-up", "Buff Up", "Status", "Green arrows shooting up: stronger, faster, healed.", 0, 1.3f, 5.6f, 0.6), buffUp},
        {describe("sleep", "Sleep", "Status", "Zs floating up from a sleeper.", 0.6f, 1.1f, 4.6f, 2.0), sleepZs},
        {describe("poisoned", "Poisoned", "Status", "A skull in sickly fumes, with bubbles and drips.", 0, 0.7f, 4.8f, 1.6), poisoned},
        {describe("stunned", "Stunned", "Status", "Stars circling a dizzy spiral.", 0, 0, 4.4f, 1.0), stunned},
        {describe("alert", "Alert", "Status", "An exclamation mark that lands with a bang.", 0, 0, 5.6f, 0.2), alert},

        {describe("aura-3d", "Aura 3D", "3D", "A ring on the ground and motes rising round a character, seen from any side.", 0, 1.2f, 5.5f, 1.2), aura3d},
        {describe("level-up-3d", "Level-up Pillar 3D", "3D", "A shockwave on the ground, a column of light and stars circling up.", 0, 1.4f, 6.0f, 0.5), levelUp3d},
        {describe("portal-3d", "Portal 3D", "3D", "A whirlpool lying on the ground, with beams and motes going round.", 0, 0.6f, 5.5f, 1.2), portal3d},
        {describe("ground-slam-3d", "Ground Slam 3D", "3D", "A shockwave and a crack on the ground, dust and rocks thrown up.", 0, 0.6f, 6.0f, 0.2), groundSlam3d},
        {describe("campfire-3d", "Campfire 3D", "3D", "Flames, embers and smoke that read from every side.", 0, 0.9f, 4.5f, 1.5), fire3d},
        {describe("hit-sparks-3d", "Hit Sparks 3D", "3D", "A flash and sparks flying out every way, falling as they go.", 0, 1.4f, 4.5f, 0.1), hitSparks3d},
        {describe("smoke-column-3d", "Smoke Column 3D", "3D", "Grey puffs rising and spreading, drawn far ones first.", 0, 1.6f, 6.0f, 2.5), smokeColumn3d},
        {describe("guardian-orbs-3d", "Guardian Orbs 3D", "3D", "Three orbs going round a character with trails and sparkles.", 0, 1.4f, 5.0f, 0.8), guardianOrbs3d},
        {describe("standing-portal-3d", "Standing Portal 3D", "3D", "An upright gateway: a whirl in a turning rim, motes drawn round its edge.", 0, 1.8f, 5.5f, 1.0), standingPortal3d},
        {describe("holy-light-3d", "Holy Light 3D", "3D", "Shafts of light falling on a spot, glinting dust and drifting feathers.", 0, 2.4f, 6.0f, 2.0), lightRays3d},
        {describe("shield-bubble-3d", "Shield Bubble 3D", "3D", "A hexagon bubble round a character, rippling, with sparks running over it.", 0, 1.4f, 5.0f, 0.5), shieldBubble3d},
        {describe("teleport-3d", "Teleport 3D", "3D", "A ring closes on the floor, light rushes up, a flash and falling sparkles.", 0, 1.5f, 5.0f, 0.75), teleport3d},
        {describe("loot-beam-3d", "Loot Beam 3D", "3D", "A dropped treasure: a tall beam, a ring on the floor and a turning gem.", 0, 1.8f, 5.0f, 1.0), lootBeam3d},
        {describe("tornado-3d", "Tornado 3D", "3D", "Dust, leaves and stones whirled up a widening funnel.", 0, 2.2f, 6.5f, 2.0), tornado3d},
        {describe("poison-cloud-3d", "Poison Cloud 3D", "3D", "A low green cloud with bubbles and rising skulls.", 0, 1.0f, 5.0f, 2.0), poisonCloud3d},
        {describe("rain-3d", "Rain 3D", "3D", "Rain falling over an area, splashing and rippling where it lands.", 0, 1.6f, 6.0f, 1.0), rain3d},
        {describe("water-splash-3d", "Water Splash 3D", "3D", "A crown of drops, spray and widening ripples.", 0, 1.0f, 4.5f, 0.25), waterSplash3d},
        {describe("anticipation-teaser-3d", "Anticipation Teaser 3D", "3D", "A charge that builds for two seconds: ribbons and sparks circle up, then a flash reveals.", 0, 1.3f, 5.5f, 1.9), anticipationTeaser3d},
        {describe("god-rays-3d", "God Rays 3D", "3D", "Slow cones and spokes of light turning behind a prize, with dust and a floor halo.", 0, 1.6f, 8.0f, 3.0), godRays3d},
        {describe("sheen-pass-3d", "Sheen Pass 3D", "3D", "A slanted bar of light sliding across a badge, with glints after it.", 0, 1.2f, 4.0f, 0.4), sheenPass3d},
        {describe("coin-fountain-3d", "Coin Fountain 3D", "3D", "Coins and gems thrown up and falling back, for a big win.", 0, 1.8f, 6.0f, 1.2), coinFountain3d},
        {describe("laser-beam-3d", "Laser Beam 3D", "3D Combat", "Charges, fires a long beam with crackling sparks, and burns where it hits.", 0, 1.5f, 6.5f, 0.9), laserBeam3d},
        {describe("gunfire-3d", "Gunfire 3D", "3D Combat", "Four shots a second: flash, tracer, flying shell, smoke and sparks on the target.", 0, 1.4f, 5.5f, 0.06), gunfire3d},
        {describe("fireball-3d", "Fireball 3D", "3D Combat", "A fireball flies trailing flame and smoke and bursts where it lands.", 0, 1.5f, 5.0f, 1.0), fireball3d},
        {describe("explosion-3d", "Explosion 3D", "3D Combat", "Flash, rolling fireball, smoke, flying rocks and sparks, a shockwave and a scorch.", 0, 1.4f, 6.0f, 0.3), explosion3d},
        {describe("lightning-strike-3d", "Lightning Strike 3D", "3D Combat", "A bolt from the sky flickers three times and crackles on the ground.", 0, 2.2f, 6.5f, 0.1), lightning3d},
        {describe("meteor-strike-3d", "Meteor Strike 3D", "3D Combat", "A burning rock falls from the sky and blasts a crater.", -1.0f, 2.5f, 7.5f, 1.15), meteor3d},
        {describe("frost-nova-3d", "Frost Nova 3D", "3D Combat", "A freezing wave: ice spikes rise round the caster, chips fly, mist and snow.", 0, 1.0f, 5.0f, 0.4), frostNova3d},
        {describe("sword-slash-3d", "Sword Slash 3D", "3D Combat", "Two quick sword strokes with an afterglow and sparks.", 0, 1.5f, 4.5f, 0.05), swordSlash3d},
        {describe("aoe-telegraph-3d", "AoE Telegraph 3D", "3D Combat", "A ring and a filling disc on the ground that warn where an attack will land.", 0, 0.8f, 6.5f, 1.0), aoeTelegraph3d},
        {describe("impact-frames-3d", "Impact Frames 3D", "3D Combat", "A few held, hard frames of white and colour with lines and sparks on the strike.", 0, 1.4f, 4.5f, 0.05), impactFrames3d},

        {describe("gold-outline", "Gold Outline", "Symbol Outlines", "A golden line round the symbol, sparks running round it and glints.", 0, 0, 3.4f, 0.6), goldOutline},
        {describe("rare-outline", "Rare Outline", "Symbol Outlines", "A blue outline crackling with electricity.", 0, 0, 3.4f, 0.6), rareOutline},
        {describe("epic-outline", "Epic Outline", "Symbol Outlines", "A violet outline giving off smoke and motes.", 0, 0, 3.4f, 1.0), epicOutline},
        {describe("legendary-outline", "Legendary Outline", "Symbol Outlines", "A white-hot outline in flames, two comets racing round it.", 0, 0.2f, 3.8f, 0.6), legendaryOutline},
        {describe("outline-trace", "Outline Trace", "Symbol Outlines", "The outline is drawn round once by a spark, then glows and fades.", 0, 0, 3.4f, 0.7), outlineDrawOn},
        {describe("symbol-glitter", "Symbol Glitter", "Symbol Outlines", "Glitter and shimmer all over the symbol, inside its shape.", 0, 0, 3.4f, 1.0), symbolGlitter},
        {describe("fire-spark-frame", "Fire Spark Frame", "Energy Frames", "A tall frame of fire with sparkles and embers.", 0, 0, 4.0f, 1.0), fireSparkFrame},
        {describe("corner-fire-frame", "Corner Fire Frame", "Energy Frames", "Fireballs at the corners, fire along the edges, sparks running round.", 0, 0, 4.2f, 1.0), cornerFireFrame},
        {describe("gold-electric-frame", "Gold Electric Frame", "Energy Frames", "A square frame crackling with golden electricity.", 0, 0, 3.2f, 1.0), goldElectricFrame},
        {describe("neon-electric-frame", "Neon Electric Frame", "Energy Frames", "A square frame crackling with cyan electricity.", 0, 0, 3.2f, 1.0), cyanElectricFrame},
        {describe("blue-smoke-frame", "Blue Smoke Frame", "Energy Frames", "A square frame wrapped in drifting blue smoke.", 0, 0, 3.2f, 1.5), blueSmokeFrame},
        {describe("violet-smoke-frame", "Violet Smoke Frame", "Energy Frames", "A square frame wrapped in drifting violet smoke.", 0, 0, 3.2f, 1.5), violetSmokeFrame},
        {describe("frame-trace", "Frame Trace", "Energy Frames", "A frame drawn on from a corner by a spark, then a flash.", 0, 0, 3.2f, 0.6), frameDrawOn},
        {describe("cyan-lightning-column", "Cyan Lightning Column", "Energy Frames", "A reel column lit from below, lightning rising through it.", 0, 0, 4.4f, 1.0), cyanLightningColumn},
        {describe("gold-lightning-column", "Gold Lightning Column", "Energy Frames", "A reel column of fire-gold light and lightning.", 0, 0, 4.4f, 1.0), goldLightningColumn},
        {describe("light-pillars", "Light Pillars", "Energy Frames", "Two upright beams of light with swirls, as at a winning reel's sides.", 0, 0, 4.4f, 1.0), lightPillars},
        {describe("portal-gate", "Portal Gate", "Energy Frames", "A tall rounded gate of blue electricity with light running round it.", 0, 0, 3.8f, 1.0), portalGate},
        {describe("win-frame-flash", "Win Frame Flash", "Energy Frames", "A winning symbol's frame fills with light, flashes and settles.", 0, 0, 3.4f, 0.15), winFrameFlash},
        {describe("sun-orb", "Sun Orb", "Energy Frames", "A blazing orb with flares and crackling filaments.", 0, 0, 3.4f, 1.0), sunOrb},
        {describe("comet-trail", "Comet Trail", "Energy Frames", "A comet running along an arc, trailing glitter.", 0, 0, 3.0f, 0.9), cometTrail},
        {describe("saber-blue", "Saber Blue", "Sabers and Beams", "A clean blue blade of light.", 0, 0, 3.0f, 0.5), saberBlue},
        {describe("saber-arc-reactor", "Saber Arc Reactor", "Sabers and Beams", "A white blade in blue glow, crackling.", 0, 0, 3.0f, 0.5), saberArc},
        {describe("saber-burning", "Saber Burning", "Sabers and Beams", "An orange blade licked by flames.", 0, 0, 3.0f, 0.5), saberBurning},
        {describe("saber-firestorm", "Saber Firestorm", "Sabers and Beams", "A blade in a storm of fire and smoke.", 0, 0, 3.0f, 0.5), saberFirestorm},
        {describe("saber-electric", "Saber Electric", "Sabers and Beams", "A cyan blade crackling with electricity.", 0, 0, 3.0f, 0.5), saberElectric},
        {describe("saber-green-energy", "Saber Green Energy", "Sabers and Beams", "A green blade of crackling energy.", 0, 0, 3.0f, 0.5), saberGreen},
        {describe("saber-neon", "Saber Neon", "Sabers and Beams", "A pink neon blade.", 0, 0, 3.0f, 0.5), saberNeon},
        {describe("saber-nebula", "Saber Nebula", "Sabers and Beams", "A violet blade in drifting purple smoke.", 0, 0, 3.0f, 0.5), saberNebula},
        {describe("saber-solar", "Saber Solar", "Sabers and Beams", "A white-hot blade with solar flames.", 0, 0, 3.0f, 0.5), saberSolar},
        {describe("saber-starkiller", "Saber Starkiller", "Sabers and Beams", "An unstable red blade.", 0, 0, 3.0f, 0.5), saberStarkiller},
        {describe("saber-ghost", "Saber Ghost", "Sabers and Beams", "A faint blue blade in ghostly haze.", 0, 0, 3.0f, 0.5), saberGhost},
        {describe("saber-yellow-smoke", "Saber Yellow Smoke", "Sabers and Beams", "A yellow blade in golden smoke.", 0, 0, 3.0f, 0.5), saberYellowSmoke},

        {describe("gold-flash", "Gold Flash", "Flashes", "A burst of white spikes in a golden glow.", 0, 0, 5.6f, 0.08), goldFlash},
        {describe("ice-flash", "Ice Flash", "Flashes", "A burst of white spikes in a blue glow.", 0, 0, 5.6f, 0.08), iceFlash},
        {describe("rose-flash", "Rose Flash", "Flashes", "A pink burst with a ring breaking apart.", 0, 0, 5.6f, 0.12), roseFlash},
        {describe("violet-flash", "Violet Flash", "Flashes", "A burst of white spikes in a violet glow.", 0, 0, 5.6f, 0.08), violetFlash},
        {describe("ember-flash", "Ember Flash", "Flashes", "A hot orange burst with a breaking ring.", 0, 0, 5.6f, 0.12), emberFlash},


        {lookDown(describe("tech-ring-3d", "Tech Ring 3D", "Holographic Ring", "Layered cyan rings, ticks and nodes turning at different speeds over a scanning platform.", 0, 1.1f, 6.0f, 3.2), 34), techRing3d},
        {lookDown(describe("energy-ring-3d", "Energy Ring 3D", "Holographic Ring", "Violet and magenta arcs turning round a pulsing core, with waves spreading out.", 0, 0.9f, 6.2f, 3.2), 34), energyRing3d},
        {lookDown(describe("gold-ring-3d", "Gold Ring 3D", "Holographic Ring", "A golden ring of sigils and ticks round a column of light, with rising embers.", 0, 1.2f, 6.0f, 3.2), 34), goldRing3d},
        {lookDown(describe("simple-beam-3d", "Simple Beam 3D", "Scanning Beam", "A hologram cylinder with emitter rings and a bright plane scanning up through it.", 0, 1.7f, 5.4f, 1.5), 24), simpleBeam3d},
        {lookDown(describe("spiral-beam-3d", "Spiral Beam 3D", "Scanning Beam", "A violet beam wound with a spiral of light that runs up and down it.", 0, 1.7f, 5.4f, 1.8), 24), spiralBeam3d},
        {lookDown(describe("tech-beam-3d", "Tech Beam 3D", "Scanning Beam", "A green scanner over a grid floor with two planes, amber rings and data bits.", 0, 1.7f, 5.8f, 1.8), 24), techBeam3d},
        {lookDown(describe("spiral-wave-3d", "Spiral Wave 3D", "Energy Wave", "Two counter-turning helixes of blue and pink, spiralling ribbons and a pulsing core.", 0, 1.6f, 5.6f, 2.0), 24), spiralWave3d},
        {lookDown(describe("pulse-wave-3d", "Pulse Wave 3D", "Energy Wave", "A bright core throwing out rings of energy along the floor and upright.", 0, 0.9f, 6.2f, 1.0), 24), pulseWave3d},
        {lookDown(describe("slice-wave-3d", "Slice Wave 3D", "Energy Wave", "Crescent slices of fire-orange energy cutting across with a white edge and sparks.", 0, 1.2f, 8.5f, 1.2), 24), sliceWave3d},
        {lookDown(describe("magic-portal-3d", "Magic Portal 3D", "Vortex Portal", "A violet portal of swirling arms and runes, pulling sparks in with rocks drifting round it.", 0, 1.7f, 5.8f, 2.8), 18), magicPortal3d},
        {lookDown(describe("tech-portal-3d", "Tech Portal 3D", "Vortex Portal", "A cyan portal ringed by ticks, segments and nodes, with data motes.", 0, 1.7f, 5.8f, 2.8), 18), techPortal3d},
        {lookDown(describe("chaos-portal-3d", "Chaos Portal 3D", "Vortex Portal", "A red and green portal that crackles with lightning, debris and embers.", 0, 1.7f, 5.8f, 2.8), 18), chaosPortal3d},
        {lookDown(describe("thin-arc-3d", "Thin Arc 3D", "Electric Arc", "A fine cyan arc that flickers between two nodes, with sparks.", 0, 1.4f, 6.0f, 0.4), 14), thinArc3d},
        {lookDown(describe("thick-arc-3d", "Thick Arc 3D", "Electric Arc", "A heavy violet arc with a haze round it and a flash on the ground at the end.", 0, 1.4f, 6.0f, 0.4), 14), thickArc3d},
        {lookDown(describe("split-arc-3d", "Split Arc 3D", "Electric Arc", "A green arc that forks into branches, with a second fork the other way.", 0, 1.4f, 6.0f, 0.4), 14), splitArc3d},
        {lookDown(describe("simple-target-3d", "Simple Target 3D", "Targeting Indicator", "A red reticle closes in on the floor, locks on and pulses.", 0, 0.3f, 5.0f, 1.5), 38), simpleTarget3d},
        {lookDown(describe("multi-ring-target-3d", "Multi Ring Target 3D", "Targeting Indicator", "Gold rings, ticks and nodes closing in one after another to lock on.", 0, 0.3f, 5.5f, 1.5), 38), multiRingTarget3d},
        {lookDown(describe("depth-target-3d", "3D Target 3D", "Targeting Indicator", "A green ground target with a scan column, a floating marker and depth chevrons.", 0, 1.1f, 5.5f, 1.5), 38), depthTarget3d},

        {describe("fish-circle-3d", "Fish Circle 3D", "Fish Swim", "Two fish swimming round in a circle with bubbles and a wake.", 0, 1.5f, 5.0f, 1.0), fishCircle3d},
        {describe("fish-figure-eight-3d", "Fish Figure Eight 3D", "Fish Swim", "A pair of fish swimming a figure eight.", 0, 1.5f, 5.0f, 1.0), fishFigureEight3d},
        {describe("fish-school-3d", "Fish School 3D", "Fish Swim", "A school of gold and silver fish following a wavy path.", 0, 1.5f, 5.5f, 1.5), fishSchool3d},

        {describe("energy-trail-3d", "Energy Trail 3D", "3D Trails", "A bright head leaving a long blue energy trail and sparkles.", 0, 1.4f, 6.0f, 0.9), energyTrail3d},
        {describe("sword-slash-trail-3d", "Sword Slash Trail 3D", "3D Trails", "A thin white slash trail with a sharp tail.", 0, 1.4f, 6.0f, 0.9), swordTrail3d},
        {describe("fire-trail-3d", "Fire Trail 3D", "3D Trails", "A hot head trailing flames.", 0, 1.4f, 6.0f, 0.9), fireTrail3d},

        {describe("soft-clouds-3d", "Soft Clouds 3D", "3D Clouds", "Soft cartoon clouds drifting across, white with a blue shadow.", 0, 2.4f, 8.0f, 3.0), softClouds3d},
        {describe("low-fog-3d", "Low Fog 3D", "3D Clouds", "Banks of fog and wisps drifting low across the ground.", 0, 0.8f, 7.0f, 3.0), lowFog3d},
        {describe("cloud-burst-3d", "Cloud Burst 3D", "3D Clouds", "Cloud puffs thrown out, a ring of cloud and dust, as at a landing.", 0, 0.9f, 5.5f, 0.8), cloudBurst3d},

        {describe("light-pillar-3d", "Light Pillar 3D", "Rays and Light Beams", "A tall pillar of light from the ground, with a ring and rising motes.", 0, 2.4f, 6.5f, 1.5), lightPillar3d},
        {describe("radial-rays-3d", "Radial Rays 3D", "Rays and Light Beams", "Two sets of rays turning against each other round a bright core.", 0, 1.6f, 7.0f, 2.0), radialRays3d},
        {describe("energy-beam-3d", "Energy Beam 3D", "Rays and Light Beams", "A horizontal beam with a white core, pulses running along it and sparks at the end.", 0, 1.4f, 7.0f, 1.0), energyBeam3d},

        {describe("hex-frame-3d", "Hex Frame 3D", "3D Frames", "A turning hexagon frame with a counter-turning one inside it.", 0, 1.6f, 4.5f, 1.0), hexFrame3d},
        {describe("diamond-frame-3d", "Diamond Frame 3D", "3D Frames", "A pink frame turning round a diamond.", 0, 1.6f, 4.5f, 1.0), diamondFrame3d},
        {describe("magic-ring-3d", "Magic Ring 3D", "3D Frames", "A violet ring of dashes turning round a pentacle.", 0, 1.6f, 4.8f, 1.0), magicRing3d},

        {describe("speed-booster-3d", "Speed Booster 3D", "Boosters", "A double ring on the ground, speed lines and a thrust flame, then dust.", 0, 0.8f, 5.5f, 0.3), speedBooster3d},
        {describe("launch-booster-3d", "Launch Booster 3D", "Boosters", "A charge, a blast on the ground and a blue flame under a straight-up launch.", 0, 1.0f, 5.5f, 0.6), launchBooster3d},
        {describe("power-up-burst-3d", "Power-up Burst 3D", "Boosters", "Sparks drawn in, a burst with rings, then stars thrown out.", 0, 1.2f, 5.0f, 0.7), powerUpBurst3d},

        {describe("water-ripples-3d", "Water Ripples 3D", "3D Water", "Rings spreading across a pool, with a soft glow.", 0, 0.3f, 6.0f, 1.0), waterRipples3d},
        {describe("water-jet-3d", "Water Jet 3D", "3D Water", "A jet thrown up, falling as drops, with mist, a splash at the base and ripples.", 0, 1.5f, 5.5f, 1.0), waterJet3d},
        {describe("water-vortex-3d", "Water Vortex 3D", "3D Water", "A whirlpool with spray winding up, a foam ring and bubbles.", 0, 0.8f, 5.5f, 1.0), waterVortex3d},

    };
    return entries;
}

}  // namespace

const std::vector<PresetInfo>& presets() {
    static const std::vector<PresetInfo> list = [] {
        std::vector<PresetInfo> out;
        for (const Entry& entry : table()) {
            out.push_back(entry.info);
        }
        return out;
    }();
    return list;
}

std::vector<std::string> presetCategories() {
    std::vector<std::string> out;
    for (const PresetInfo& info : presets()) {
        bool seen = false;
        for (const std::string& have : out) {
            seen = seen || have == info.category;
        }
        if (!seen) {
            out.push_back(info.category);
        }
    }
    return out;
}

const PresetInfo* findPreset(std::string_view id) {
    for (const PresetInfo& info : presets()) {
        if (info.id == id) {
            return &info;
        }
    }
    return nullptr;
}

Result<Effect> makePreset(std::string_view id, const IdSource& newId) {
    for (const Entry& entry : table()) {
        if (entry.info.id == id) {
            return entry.build(newId);
        }
    }
    return makeError("That preset is not in this version of VFX Forge.",
                     "unknown preset '" + std::string(id) + "'");
}

}  // namespace vfx::editor
