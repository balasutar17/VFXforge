# The effect library, particle shapes and the backdrop (version 0.2)

## What was added

**An effect library.** 36 presets in nine groups: Toon, Blasts, Fire, Water,
Glow and magic, Frames, Shooting, Rewards and Weather. The library opens as a
gallery in which every card is the effect itself, playing. Clicking a card
opens the preset as a new effect; "Add to my effect" puts its layers into the
effect already open, as one undo step.

A preset is an ordinary effect built from ordinary modules
(`editor/src/Presets.cpp`). Nothing in the simulation or the renderer knows
what a preset is, so everything in one can be changed after opening it.

**Particle shapes.** Seventeen built-in shapes, chosen per layer with the new
Shape control: soft, disc, ring, bubble, sparkle, star, smoke, square,
diamond, heart, streak, flame, and five hard-edged toon shapes with a lighter
inner tone and a shadow: puff, burst, crescent, orb and glint. Each shape is
a formula, not an image, so it is sharp at any size.

The formulas exist twice and must be kept the same: `editor/src/Shapes.cpp`
(tests, pictures) and `app/shaders/sprite.frag` (the graphics card).

**Streaks.** A layer can point its particles the way they are moving
(`align`) and draw them as a streak whose length is so many seconds of travel
(`stretch`). Sparks, rain, bolts and slashes use this.

**A backdrop.** A PNG or JPG can be shown behind the effect to work against:
a character, a button, a scene. Option-drag moves it and Option-scroll
resizes it. It is a reference only: it is not part of the effect, is not
saved in the `.vfx` file, and is remembered per effect on this computer.

**A picture renderer.** `editor/src/Picture.cpp` draws exactly the triangles
the app sends to the graphics card, into memory, with the same shapes and
blending. `vfxshot` uses it to write PNG files. This is how the presets were
tuned and how they are tested, and it is the basis for image-sequence export.

## Changes to the file format

The Sprite module has three new properties: `shape`, `align` and `stretch`.
Files without them load with the old look (a soft dot, upright, no streak).
The format version is still 1.

New layers have two more Simple controls, Shape and Blend. Layers in older
files keep the controls they were saved with.

## Changes to fingerprints

A frame now also carries each particle's velocity and each layer's shape, so
the frame fingerprints in `tests/test_determinism.cpp` were recorded again.
The simulation itself did not change and the state fingerprints are the ones
recorded for milestone 1.2.

## What is NOT IMPLEMENTED yet

- **Your own pictures as particles**, including sprite sheets of hand-drawn
  frames. The built-in shapes are the only particle pictures.
- **3D models** as a backdrop or as part of an effect.
- Curves, gradients and bursts cannot be edited in the window. Presets use
  them, and they play, but changing them needs the Advanced view.
- Saving your own effects into the library.
- A layer cannot share another layer's randomness, so two layers cannot be
  made to move together except when nothing about them is random.
- Motion that swirls, wobbles, orbits or is pulled toward a point.
- Trails and ribbons.
- All presets are 2D. They can be added to a 3D effect, where they lie on
  the flat plane.
- Export of any kind.
