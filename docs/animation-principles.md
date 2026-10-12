# The animation principles (0.12)

Hand-drawn effects feel alive because of a handful of rules animators have
used for a century: things squash and stretch, they wind up before they go,
they overshoot and settle, they travel in arcs, they ease in and out of a
move, and the drawings are held, not smooth. Until now VFX Forge could do
the first few of these only by bending a size or opacity curve. From 0.12
four of them are settings of their own, on every layer, with ready-made
curves, one-click changes for a whole effect, and presets that show them.

| Principle | Setting | Where |
| --- | --- | --- |
| Squash and stretch | **Squash and Stretch** | Simple view; Advanced > Over Lifetime |
| Timing: drawings held "on twos" | **Hold Frames** | Simple view; Advanced > Sprite |
| Slow in and slow out, anticipation, overshoot and follow-through | **Speed over Life** | Advanced > Motion |
| Arcs | **Orbit** | Advanced > Motion |

The rest of the twelve are already there in other forms, or are the
artist's job and not a setting:

- **Exaggeration**, **appeal**, **staging**: your choices of size, colour
  and what goes where. *More stylized* (Reference workspace) pushes shapes
  to pop in past full size.
- **Secondary action**: a second, finer layer. *Add finer particles* makes
  one.
- **Overlapping action**: layers that start at different moments (Starts At
  in Advanced > Timing), and particles with a random range of lifetimes.
- **Straight ahead and pose to pose**, **solid drawing**: about drawing by
  hand; your own sprite sheets ([pictures.md](pictures.md)) are where that
  comes in.

## Squash and Stretch

`overLife.stretch`. Above 0 the particle is drawn longer and thinner; below
0, shorter and wider. **Its area never changes**: 0.5 is half as long again
and two thirds as wide; -0.5 is the same shape lying down.

- The length runs along the particle's **own upright** (so a turned
  particle stretches along its turned upright), or along **the way it is
  moving** when Align is set to movement. A flying drop that points along
  its flight stretches along its flight.
- A **number** stretches every particle the same. A **random range** gives
  each particle its own amount, kept for life. A **curve** runs from birth
  to death: stretched as it launches, squashed as it lands.
- It multiplies with Size and Size over Lifetime; it changes shape, never
  how big the particle is.
- It works on the built-in shapes, on your own pictures and sprite sheets,
  on particles lying flat, and on particles lying on the plane of a 3D
  effect.

Five ready-made curves for it: **Stretch, then settle** · **Squash, then
spring** · **Wobble** · **Stretch away** · **Breathe**.

## Hold Frames

`sprite.hold`, in drawings per second. Hand-drawn animation is not smooth:
at 24 frames a second, a new drawing every second frame ("on twos") is 12
drawings a second, and every third ("on threes") is 8. Set Hold Frames to
12 and the layer is shown exactly that way: twelve drawings a second, each
held still until the next. 0 is smooth.

- It is per layer, so puffs can be on twos while a glow behind them moves
  smoothly.
- **Nothing is lost between drawings.** The effect is simulated at full
  rate underneath; a held layer just shows the moment its current drawing
  was taken. Its particles are in the same places they would be without it.
- Everything about the layer is held together: position, size, colour,
  sprite-sheet picture, trail, dissolve.
- **Nothing is late.** A burst at the start of the effect is in the first
  drawing, a sixtieth of a second in. Drawings are counted from the start
  of each pass of a loop, so every pass is drawn at the same moments and a
  looping export joins up.
- 60 or above is every step of the simulation, which is smooth.
- Animation frames exported from a held effect show the held drawings.
  Export at 24 frames a second to get true twos.

## Speed over Life

`motion.speedOverLife`, a multiplier on how fast a particle travels its own
path: 1 is its own speed, 0 stands still, **below 0 goes backwards**.

- It multiplies the distance the particle covers, **not its speed**, so
  gravity and drag act exactly as before and nothing else needs retuning.
  At a steady 0.5 every particle follows the same path in the same time
  and gets half as far along it.
- Moving air (Turbulence) carries a particle whatever this says: a
  particle at 0 still drifts.
- A **number** or a **random range** is each particle's own pace for life.
  A **curve** runs from birth to death, and that is where the principles
  are:

Seven ready-made curves: **Burst and coast** (off at once, coasting to a
stop: slow in) · **Gather speed** (slow out) · **Slow in and out** · **Back
off, then go** (anticipation) · **Overshoot and settle** (follow-through) ·
**Out and back** (thrown out and drawn back to where it began) · **Hit
pause** (stops dead for a beat, then carries on).

## Orbit

`motion.orbit`, in degrees per second. Every particle is turned about the
layer's centre (its Position) as it travels, so a straight path becomes an
arc and a burst becomes a spiral. Above 0 turns counter-clockwise.

- It turns where the particle is **and** which way it is heading, so
  without gravity the whole burst simply rotates as it flies: distances
  from the centre are exactly what they were.
- Two layers with opposite Orbit make the crossed spirals of the Spiral
  Burst preset.
- In a 3D effect it turns about the axis that points at the viewer; depth
  is left alone. A layer whose Shape is set to **Lie on the ground**
  (Plane: ground) turns about the upright instead, height left alone, so
  orbs circle a character and a ring of runes turns on the floor (see
  [3d-effects.md](3d-effects.md)).

## Curves made for one setting

The curve editor lists the shapes made for the setting being edited first,
then the fifteen general ones. The ones made for a setting (the twelve
above) are in that setting's own numbers and are applied as they are; the
general ones are still scaled to stand as high as the curve does now.

## One click for the whole effect

**Motion…** on the top bar. Each button changes every layer that is not
locked, as one undo step, and the bar at the bottom says what it did.

| Button | What it does |
| --- | --- |
| **On twos** | Hold Frames 12 on every layer. Again for on threes (8). |
| **Smooth** | Hold Frames back to 0. |
| **Squash and stretch** | A single big shape lands flat, springs and settles. Round flying pieces (discs and soft dots) are pointed along their flight and stretch while they are fast. Puffs and other shapes with an upright of their own are left alone. |
| **Anticipation** | Flying pieces back off before they go. A single big shape draws in before it grows. |
| **Overshoot** | Flying pieces go past where they will rest and settle back. |
| **Curl** | Flying pieces curve round the centre, one layer one way and the next the other. Again for half as much more. |

The same six are among the refinements in the Reference workspace, which
now has twenty-six. *Faster* and *Slower* keep a layer's number of turns
and leave Hold Frames alone (it is a way of drawing, not a speed).

## Presets

A new category, **Cartoon motion**:

- **Bouncy Pop**: a candy blob arrives stretched, lands flat, wobbles and
  settles. Squash and stretch, on twos.
- **Wind-up Blast**: motes thrown out and drawn back in, the core
  flattening, then the blast. Anticipation.
- **Spiral Burst**: sparks curling out in two spirals with ribbon trails.
  Arcs.
- **Springy Poof**: puffs that shoot past, spring back and hang before
  they tear away. Overshoot and settle.

And the hand-drawn presets are now shown on twos: Toon Puff, Toon Hit, Toon
Explosion, Slash, Toon Fire and Cartoon Pop. Set Hold Frames to 0 on a
layer, or press Motion… > Smooth, to have them as they were. That made 70
presets (78 from 0.13).

## Trails, streaks and the rest

- A **trail** follows the path the particle really took: eased, turned,
  carried by air. It is worked out, as before, by undoing the simulation's
  own steps.
- A **streak** (Align: movement) points the way its particle is really
  going, backwards included.
- Like everything in the simulation, all four are **exactly repeatable**:
  the same effect gives the same picture at the same moment on every
  machine, played or scrubbed to. A layer that uses none of them simulates
  and fingerprints exactly as it did in 0.11.

## In files

Four new properties, each written with its default when unused:
`"speedOverLife": 1.0` and `"orbit": 0.0` in the motion module,
`"stretch": 0.0` in the overLife module, `"hold": 0.0` in the sprite
module. An effect saved by 0.11 opens unchanged and plays exactly as it
did. An effect that uses the new settings opens in 0.11 and earlier with
them ignored (and kept, if it is saved there).

## Unity

| VFX Forge | Unity |
| --- | --- |
| Speed over Life | Velocity over Lifetime: **Speed Modifier**, as the same number, range or curve |
| Orbit | Velocity over Lifetime: **Orbital Z** (radians a second), with the layer's Position as its Offset; **Orbital Y** for a 3D layer on the ground |
| Squash and Stretch | Size over Lifetime with **Separate Axes**: width and height each on a curve that also carries the size |
| Hold Frames | The **VFXForgeHold** script on the layer, which keeps its Particle System paused and moves it on one drawing at a time |

What is not the same:

- **Orbit** in Unity mixes a turn with gravity and drag in Unity's own way,
  so paths are alike, not identical. The export marks it `"exact": false`.
- **A random range of Squash and Stretch** cannot be told to Unity (it
  would pick width and height separately), so every particle gets the
  middle amount. Marked `"exact": false`.
- **A streak** in Unity has one length for its whole life, so a streak
  with a Squash and Stretch curve gets the average. Marked `"exact": false`.
- **Held frames show in Play mode and in a built game.** In the Scene
  view's own particle preview Unity moves the system itself and the layer
  looks smooth.
- **None of this has been run inside Unity.** The build machines compile
  the importer and both scripts against two Unity versions; they do not
  run an effect. In particular, which way Unity's Orbital Z turns, and
  whether its Speed Modifier also slows the Noise module's drift, have not
  been checked by anyone. If a spiral turns the wrong way in Unity, flip
  the sign of Orbital Z on that layer and tell me.

## Limits

- **Particles cannot be sent toward or away from a point.** Direction is
  one heading with a spread, so "gather inward" is done here by throwing
  motes out and drawing them back (the Out and back curve). A radial
  direction, and a pull toward the centre, are not built.
- **One Orbit per layer, the same all life long**, about one fixed centre
  and one axis (towards the viewer, or the upright for a layer on the
  ground). No curve on it, and no orbit about a tilted axis in 3D.
- **Hold Frames is not smeared.** Real animation on twos often adds smear
  drawings on fast moves; here a drawing is simply held.
- **Reference to VFX does not measure any of these.** A clip animated on
  twos is not recognised as such; press On twos after rebuilding.
- **Squash and Stretch does not follow speed by itself.** To stretch
  something while it is fast, give it a curve that is high early on (the
  Squash and stretch button does this for round flying pieces).
- Seen in pictures drawn by the picture renderer and in the app's own
  screenshots on the build machines. **No person has used them in the app
  yet.**

## Tests

`tests/test_principles.cpp`: what each setting does to a particle; Speed
over Life under gravity, drag and moving air; Orbit keeping distances;
which drawing a held layer shows at every step, in loops, after edits and
from a snapshot; scrubbing equal to playing with all four at once, loop or
not, at five hold rates; fingerprints unchanged for layers that use none.

`editor/tests/test_principles.cpp`: the drawn shape and its area; trails
that retrace an eased and turned path to within half a pixel; streak
headings; the ready-made curves; the Advanced view; the six one-click
changes and what they leave alone; the presets; what Unity is told.
