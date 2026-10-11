# Effects on the ground (0.9)

A magic circle, a portal, a shockwave across the floor: a round, flat thing
lying on the ground and seen from an angle, usually with light rising out of
it. Game engines build these from three parts, and VFX Forge now has the
same three:

| Part | In a game engine | In VFX Forge |
| --- | --- | --- |
| The base | A flat quad on the floor with a picture on it, turning | A layer with **Lie Flat** set, turning with Spin |
| Shafts of light | Upright stretched billboards | Particles of the **beam** shape, Align set to movement, with Stretch |
| Motes | Small particles drifting up | Ordinary particles |

## Lie Flat

Every new layer and every preset layer has a **Lie Flat** control, from 0 to
85 degrees. (From 0.10 a layer in an effect saved by an earlier version
shows the control too.) At 0 the
particle faces you, as before. Higher, it leans back as if it lay on the
ground: 60 degrees makes a circle an oval half as tall as it is wide.

- The particle turns **in its own plane first** and is leaned back second,
  so a spinning ring stays a level oval instead of tumbling.
- It is one flat shape leaned back, **not true depth**: the far side is not
  drawn smaller than the near side. Flat (2D) effects only; a 3D effect has
  a camera to do this.
- It is ignored when Align is set to movement: a streak has no plane to lie in.
- Old files open unchanged (0 degrees), and a layer that does not lie flat
  draws, simulates and fingerprints exactly as it did.

Property: `sprite.tilt`, degrees.

## Three new shapes

- **vortex**: a whirl of soft arms round an open eye.
- **sigil**: a magic circle: rings, tick marks and a six-pointed star.
- **beam**: a shaft of light with a white-hot thread down its middle.

That makes 35 built-in shapes (the shape library, from 0.13, adds 108
more that are written once; see [shape-library.md](shape-library.md)).
Each of the 35 is drawn three ways that must agree: by
the graphics card in the app (`app/shaders/sprite.frag`), by the picture
renderer (`editor/src/Shapes.cpp`), and in the atlas sent to Unity (now
eight columns by five rows).

## Presets

A new library category, **Ground and portals**:

- **Magic Circle**: two circles of runes turning opposite ways, a pulse
  spreading across the floor, shafts of light and motes.
- **Vortex Portal**: two whirls turning at different speeds, a white core,
  shafts and motes.
- **Ground Slam**: a flash, a shockwave and a second ring racing outward,
  dust and chips.

In a looping effect a sprite that is there the whole time must hand over to
its next self without a blink, and must come round to where it started. So
these presets are a whole number of turns long (a circle of lines looks the
same every sixth of a turn; a whirl only after a whole one).

## Unity

A layer that lies flat is exported with its lean. The importer fixes the
particle in the effect's own space, spins it about its face and then leans
it back (Unity applies those in that order). **Not yet checked in a Unity
project by a person**; the two Unity versions on the build machines compile
the importer, and that is all that is verified.

## Reference to VFX

A reference of something lying on the ground is now read as that. See
[reference.md](reference.md), "Lying on the ground".

## Tests

`tests/test_compile.cpp` (the lean, and its limits), `editor/tests/
test_sprite_mesh.cpp` (the oval's proportions; turning before leaning),
`editor/tests/test_unity.cpp` (the lean reaches Unity), `editor/tests/
test_presets.cpp` (every preset, the new ones included), and the ground
cases in `editor/tests/test_reference.cpp`.
