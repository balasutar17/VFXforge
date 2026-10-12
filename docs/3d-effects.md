# 3D effects (0.17)

0.16 let an effect be made on a 3D model. To find out what was missing, I
made six 3D effects myself, as an artist would, from an empty effect:
an aura round a character, a level-up, a portal, a ground slam, a campfire
and hit sparks. They were drawn by `vfxshot` from a camera turned round the
practice dummy. What went wrong in them is what 0.17 fixes.

## What was wrong

| Tried | What happened |
| --- | --- |
| A ring of runes on the floor under the character | Nothing could lie on the ground. A circle emitter and a flat sprite both stood upright, facing the front, so from the side the "floor" ring was a line. |
| Orbs circling a character | Orbit turned about the axis that points at the viewer, so the orbs went round in front of the face, like a clock, not round the body. |
| Smoke and dust in front of each other | Particles were drawn in the order they were born, so a far puff could be drawn over a near one. |
| An aura half behind the body | A whole particle was hidden or shown by its middle, so a big ring round the feet was all hidden or all shown. |
| Starting a 3D effect | Every preset was 2D. There was nothing to start from. |

## What 0.17 adds

- **Lie on the ground** (Shape > Plane: upright / ground). In a 3D effect,
  a layer on the ground:
  - places its circle and rectangle on the floor (across X and Z) instead
    of standing up (across X and Y);
  - lays its flat sprites (Facing: lie flat) on the floor, so a shockwave,
    a crack or a magic circle is seen in perspective from wherever the
    camera is;
  - turns about the upright with Orbit, its height left alone, so orbs go
    round the body and a rune ring turns on the floor.
  It means nothing in a 2D effect, and Upright, the default, is exactly
  what was there before: no saved effect changes.
- **Sorted by distance.** In 3D, see-through (alpha) particles are drawn
  furthest first, so a near puff is always over a far one. Glowing
  (additive) particles add up whichever order they come in, so they are
  not sorted, which keeps them cheap.
- **Hidden pixel by pixel.** Each pixel of a particle is hidden behind the
  model, or faded just behind its surface, by its own depth: in `vfxshot`'s
  pictures and on the graphics card in the app. A ring round the feet goes
  behind the legs and comes round in front.
- **A 3D preset category**, made from the tries: Aura 3D, Level Up 3D,
  Portal 3D, Ground Slam 3D, Campfire 3D, Hit Sparks 3D, Smoke Column 3D,
  Guardian Orbs 3D.

## In Unity

- A circle on the ground sending particles every way becomes Unity's
  circle laid down (turned 90 degrees about X); one sending them one way
  becomes a box with no height, its flat side along the floor.
- Flat sprites on the ground lean back 90 degrees (`"tilt": 90`).
- Orbit on a ground layer is **Orbital Y** instead of Orbital Z.
- See-through layers in a 3D effect sort by **Distance**.
- The importer (version 6) reads all of these. **Not yet checked in Unity
  by a person.**

## Not done yet

- No other tilt than upright or flat on the floor: a wall decal, or a
  ring tilted to follow a sword swing, cannot be made.
- Particles do not come off the model's surface, and do not touch it.
- Sorting is by each particle's middle: two large crossing puffs can
  still show the wrong one in front where they cross.
- Lit smoke (light from one side) and soft particles (fading where they
  meet the ground) are not built; 3D smoke is flat-shaded.

## Tests

`tests/test_principles.cpp`: a circle and a rectangle on the ground spread
over the floor at height 0; Orbit on a ground layer turns about the
upright through the layer's Position and leaves height alone; the plane
changes nothing in 2D. `editor/tests/test_sprite_mesh.cpp`: see-through
particles drawn far to near, glowing ones as they come; a flat ground
sprite squashed when seen from low and square from above.
`editor/tests/test_unity.cpp`: tilt 90, distance sort, the laid-down box
and Orbital Y. `editor/tests/test_model.cpp`: hiding pixel by pixel.
`editor/tests/test_presets.cpp`: every 3D preset a valid 3D effect.
