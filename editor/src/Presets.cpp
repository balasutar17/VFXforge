#include "vfx/editor/Presets.h"

#include <cmath>
#include <initializer_list>
#include <utility>

#include "vfx/Metadata.h"
#include "vfx/Value.h"
#include "vfx/editor/Session.h"
#include "vfx/editor/SoundLibrary.h"

#include "LayerMake.h"

namespace vfx::editor {

namespace {

using namespace make;

// A gradient that means "white-hot, then your colour, then dark and gone".
// Because it multiplies the layer's colour, changing that one colour re-hues
// the whole fire.
#define VFX_HOT_TO_DARK \
    {{0.0, 2.6, 2.2, 1.9, 1.0}, {0.25, 1.0, 1.0, 1.0, 1.0}, {0.7, 0.55, 0.3, 0.3, 0.8}, {1.0, 0.15, 0.08, 0.08, 0.0}}

// ------------------------------------------------------------------ toon
// Hard-edged shapes with a lighter inner tone, in the manner of hand-drawn
// 2D effects.

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
    return e;
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
    return e;
}

Effect toonExplosion(const IdSource& id) {
    Effect e = begin(id, "Toon Explosion", 1.8);
    add(e, Make(id, "Smoke").burst(8, 0.06).circle(0.3).life(0.9, 1.3).speed(1.6, 3.6).aim(90, 180)
               .size(1.2, 1.9).turn(-30, 30).color(0x574b66).drag(2.8).gravity(0, 0.8)
               .sizeOver({{0, 0.4}, {0.3, 1.0}, {0.7, 0.9}, {1, 0}}).solid().look("puff"));
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
    return e;
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
    return e;
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
    return e;
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
    return e;
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
               .fade({{0, 0.95}, {0.4, 0.75}, {1, 0}}).look("smoke"));
    add(e, Make(id, "Dust").burst(12).life(0.4, 0.8).speed(3, 6).aim(90, 180).size(0.1, 0.18)
               .color(0xd8d0c4).drag(3).gravity(0, -1).sizeOver({{0, 1}, {1, 0}}).solid().look("disc"));
    return e;
}

// ----------------------------------------------------------------- fire

Effect campfire(const IdSource& id) {
    Effect e = begin(id, "Campfire", 4.0);
    add(e, Make(id, "Smoke").rate(6).circle(0.25).life(2.2, 3.0).speed(0.9, 1.5).aim(90, 10)
               .size(0.9, 1.4).turn(0, 360).spin(-25, 25).color(0x38322f, 0.6).gravity(0, 0.5)
               .sizeOver({{0, 0.5}, {1, 1.7}}).fade({{0, 0}, {0.3, 0}, {0.55, 0.5}, {1, 0}}).look("smoke").sound("fire-loop", false, 0.0, 0.8).soundLoop());
    add(e, Make(id, "Flames").rate(42).circle(0.38).life(0.6, 1.05).speed(1.3, 2.5).aim(90, 9)
               .size(0.75, 1.25).color(0xff6412).gravity(0, 1.6).drag(0.4)
               .sizeOver({{0, 0.55}, {0.2, 1.0}, {1, 0.12}}).solid().tint(VFX_HOT_TO_DARK)
               .look("flame").additive().glow(0.7));
    add(e, Make(id, "Glow").rate(5).life(0.9, 1.2).speed(0, 0).size(3.2, 3.8).color(0xff7a1f)
               .gravity(0, 0).fade({{0, 0}, {0.5, 0.2}, {1, 0}}).additive());
    add(e, Make(id, "Embers").rate(11).circle(0.3).life(1.2, 2.2).speed(1.6, 3.2).aim(90, 22)
               .size(0.05, 0.1).color(0xffb44a).gravity(0, 0.5).drag(0.3)
               .fade({{0, 1}, {0.6, 1}, {1, 0}}).additive().glow(2.2));
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
               .look("sparkle").additive().glow(1.4));
    return e;
}

Effect risingEmbers(const IdSource& id) {
    Effect e = begin(id, "Rising Embers", 5.0);
    add(e, Make(id, "Embers").rate(26).rect(8, 0.6).life(2.2, 3.6).speed(0.9, 2.2).aim(90, 16)
               .size(0.05, 0.12).color(0xff9d3c).gravity(0.25, 0.35).drag(0.2)
               .fade({{0, 0}, {0.15, 1}, {0.7, 1}, {1, 0}}).additive().glow(2.4));
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
               .fade({{0, 1}, {0.6, 1}, {1, 0}}).additive().glow(2.0));
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
               .fade({{0, 1}, {0.6, 1}, {1, 0}}).additive().glow(2.2));
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
               .size(0.34, 0.44).color(0xffc531).gravity(0, -15)
               .fade({{0, 1}, {0.85, 1}, {1, 0}}).look("disc").sound("coin", true, 0.0, 0.9, 1.0));
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
                   .size(0.22, 0.36).turn(0, 360).spin(-420, 420).color(colors[i]).gravity(0, -6)
                   .drag(1.7).fade({{0, 1}, {0.85, 1}, {1, 0}}).look(i % 2 == 0 ? "square" : "diamond"));
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
               .sizeOver({{0, 1}, {0.7, 1}, {1, 0}}).solid().look("diamond"));
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
               .fade({{0, 0}, {0.15, 1}, {0.85, 1}, {1, 0}}));
    add(e, Make(id, "Near flakes").rate(22).rect(12, 8).life(2.6, 3.8).speed(0.7, 1.3).aim(262, 16)
               .size(0.14, 0.26).color(0xffffff, 0.9).gravity(0, 0)
               .fade({{0, 0}, {0.15, 1}, {0.85, 1}, {1, 0}}));
    return e;
}

Effect fireflies(const IdSource& id) {
    Effect e = begin(id, "Fireflies", 6.0);
    add(e, Make(id, "Fireflies").rate(5).rect(9, 5).life(2.6, 4.0).speed(0.15, 0.45).aim(90, 180)
               .size(0.3, 0.46).color(0xa8f03c).gravity(0, 0)
               .fade({{0, 0}, {0.25, 1}, {0.5, 0.2}, {0.75, 1}, {1, 0}}).additive().glow(2.1));
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

        {describe("rain", "Rain", "Weather", "Rain across the whole scene.", 0, 0, 6, 1.5), rain},
        {describe("snow", "Snow", "Weather", "Snow drifting down.", 0, 0, 6, 4.0), snow},
        {describe("fireflies", "Fireflies", "Weather", "Small lights wandering and blinking.", 0, 0, 5, 3.6), fireflies},

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

        {describe("gold-flash", "Gold Flash", "Flashes", "A burst of white spikes in a golden glow.", 0, 0, 5.6f, 0.08), goldFlash},
        {describe("ice-flash", "Ice Flash", "Flashes", "A burst of white spikes in a blue glow.", 0, 0, 5.6f, 0.08), iceFlash},
        {describe("rose-flash", "Rose Flash", "Flashes", "A pink burst with a ring breaking apart.", 0, 0, 5.6f, 0.12), roseFlash},
        {describe("violet-flash", "Violet Flash", "Flashes", "A burst of white spikes in a violet glow.", 0, 0, 5.6f, 0.08), violetFlash},
        {describe("ember-flash", "Ember Flash", "Flashes", "A hot orange burst with a breaking ring.", 0, 0, 5.6f, 0.12), emberFlash},
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
