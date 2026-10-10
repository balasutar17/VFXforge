# Match-3 and light effects, gloss, eleven new shapes (0.5)

Made from the artist's reference boards: Candy Crush and other casual
candy art, "lighting VFX for games" boards, an anime lightning bolt, and
stylized trails.

## Gloss

Every shape now has a third value besides coverage and toon tone: **gloss**,
which turns the colour white at the particle's own brightness. It is what
puts the pure-white shine on candy and the white-hot middle in a flare. The
picture renderer, the window's shader and the Unity shader all apply it the
same way; in the Unity shape atlas it is the green channel (tone is red,
coverage alpha). The old shapes look as before, except that the orb and the
heart now have a white shine spot and a streak's head burns white.

## New shapes

candy (glossy gumdrop), shard (two-faceted fragment), drop (flying drop,
head first), splat (jelly splat), shockwave (thick ring with a bright inner
edge), twinkle (sharp four-pointed sparkle), flare (starburst of thin rays),
rays (sunburst to spin behind rewards), swirl (peppermint/lollipop), bean
(jelly bean), bolt (lightning).

## New presets

**Match-3:** Candy Pop, Jelly Splat, Line Blast Across, Line Blast Down,
Candy Bomb (goes off twice), Rainbow Burst, Sweet Celebration, Collect
Sparkle, Hint Glow. A tile is taken as about 1.2 units across.

**Light and energy:** Starburst Flare, Reward Rays, Meteor Shower,
Lightning Strike, Electric Orb.

## NOT IMPLEMENTED yet

- Ribbon trails behind moving particles (the "Stylized Trails" reference),
  and emitters that move along a path.
- Magic circles and other ground decals.
- Particles flying to a target (a collected piece flying to the goal
  counter).
- Score numbers and text.
