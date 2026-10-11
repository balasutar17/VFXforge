# Turbulence and dissolve (0.10)

Two things every professional effects tool has, and VFX Forge did not:
moving air, and a way for things to break up as they go instead of only
fading. Unity calls the first its Noise module; the second is the
"erosion" or "dissolve" that toon smoke and burning edges are made with.

## Turbulence

Air that carries particles about. Smoke winds, embers wander, snow drifts,
fireflies meander, ribbons trail in curls.

| Control | Property | What it does |
| --- | --- | --- |
| **Turbulence** | `motion.turbulence` | How hard the air pushes, in units per second. 0 is still air. |
| **Swirl Size** | `motion.turbulenceSize` | How wide one swirl is, in units. Small swirls jitter; large ones drift and curl. |
| **Swirl Change** (Advanced view, from 0.11) | `motion.turbulenceChange` | How quickly the swirls are replaced by new ones, per second. 0 keeps them fixed, so every particle follows the same winding roads. Default 0.6. |

How it works, for anyone who needs to know:

- The air is a **field of swirls that never squeezes or spreads** (what
  graphics programmers call curl noise). Particles carried by it wind and
  mix but do not bunch up into clumps or leave holes, which is what plain
  random pushing does.
- It **carries** particles; it does not push them. A particle's own speed is
  left alone, so Speed, Gravity and Drag mean what they always meant, and
  the turbulence is added on top.
- A value of 1 moves a particle a little under one unit a second on
  average, about two at the strong places, and now and then three.
- Each layer has its own air, from the layer's own identity, so two layers
  with the same settings do not move in step.
- Like everything in the simulation it is **exactly repeatable**: the same
  effect gives the same picture at the same moment on every machine,
  whether it was played or scrubbed to. A layer with no turbulence
  simulates and fingerprints exactly as it did before.
- **Trails** follow the true winding path (the ribbon is worked out by
  undoing the simulation's steps, the air included), and **streaks** point
  the way the particle is really going.
- Works in flat (2D) and 3D effects. In 3D the swirls turn in every plane.

## Dissolve

A particle eaten away in ragged patches toward the end of its life.

| Control | Property | What it does |
| --- | --- | --- |
| **Dissolve** | `overLife.dissolve` | The share of its life, at the end, over which it is eaten away: 0 never, 0.5 over the second half, 1 over all of it. |
| **Dissolve Softness** | `overLife.dissolveSoft` | 0 torn like paper, 1 thinning away like smoke. Default 0.15. |
| **Dissolve Edge Glow** | `overLife.dissolveEdge` | How brightly the eaten edge glows: 0 for smoke and mist, 1 white-hot like burning paper. |

- Every particle has **its own pattern**, so a puff of twelve do not all
  tear in the same places.
- The pattern has big patches with smaller ones inside, the outside tends
  to go a little before the middle, and **about as much goes in each part
  of the dissolve as in any other**, so a particle never sits whole and
  then vanishes at once.
- It works on the built-in shapes and on **your own pictures and sprite
  sheets**: give a layer a picture and Dissolve, and the picture burns away.
- It multiplies with the ordinary Opacity fade. For a dissolve you can see,
  do not let Opacity reach nothing long before the end; the presets leave
  it at a quarter or a third and let the dissolve finish the job.
- A particle's trail fades with it. The trail itself is not torn.
- Drawn three times with the same sums: by the graphics card in the app
  (`app/shaders/sprite.frag`, `image.frag`), by the picture renderer
  (`editor/include/vfx/editor/Shapes.h`), and by the Unity shader.

## Controls in older effects

From 0.10 the Simple panel shows a control for every setting marked Simple,
whether or not the layer's own list names it. So an effect saved by an
earlier version shows Lie Flat, Turbulence, Swirl Size and the three
Dissolve controls too. Nothing is written into the effect for this: the
file is not changed by looking, only by changing a value.

## Presets

A new category, **Smoke and wind**:

- **Rising Smoke**: a column winding upward and thinning away.
- **Ground Mist**: low mist drifting across the scene.
- **Spirit Wisps**: ribbons of light winding upward on moving air.
- **Burn Away**: a sheet burning through with white-hot edges, embers and
  smoke. Change its Shape, or give it a picture, to burn anything.
- **Vanish**: a glowing shape breaking up and blowing away as dust.

And these existing presets now use one or both: Toon Explosion (the smoke
tears away), Smoke Puff, Campfire (smoke and embers), Spirit Flame, Rising
Embers, Fairy Dust, Burning Frame, Confetti (it flutters), Snow, Fireflies,
Magic Circle and Vortex Portal (the motes), Ground Slam (the dust). That
makes 66 presets.

## Reference to VFX

- Smoke rebuilt from a reference is given a little moving air and a soft
  dissolve. The report says these are assumed, not measured.
- Three new refinements: **More swirl** (small particles and smoke are
  carried by moving air; again for more), **Calmer**, and **Dissolve away**
  (smoke and big shapes are eaten away in patches as they go).
- Bigger, Smaller, Wider and Tighter scale the swirls with everything else;
  Faster and Slower change how fast the air moves and changes.
- **Turbulence is not measured from a reference.** That needs single
  particles followed through a clip, which is not built.

## Unity

- **Turbulence** becomes Unity's own Noise module: strength from Turbulence,
  frequency from Swirl Size, scroll speed from how fast the swirls change.
  Unity's swirls are Unity's: particles wander as much and as widely as in
  VFX Forge, **not along the same paths**. The strength is set to twice the
  Turbulence value, on the reckoning that Unity's figure is the most the
  noise can push and ours is the usual push; that reckoning has not been
  measured inside Unity.
- **Dissolve** is drawn by the VFX Forge shader in Unity with the same
  pattern, using each particle's age and its own random number (sent by
  the importer as custom vertex streams). A trail does not fade with its
  dissolving particle in Unity as it does here.
- **Neither has been checked inside Unity by a person.** The build
  machines compile the importer against two Unity versions; they do not
  compile the shader or run the effect.

## Limits

- One field of air per layer, the same strength everywhere and all life
  long: no wind zones, no turbulence that grows with age, no vortex you can
  place. Those are separate tools in the big programs and are not built.
- The dissolve pattern is one built-in pattern at one scale. You cannot
  give it your own noise picture, and the glowing edge is always white-hot
  rather than a colour of your choosing.
- Seen in pictures drawn by the picture renderer and in the app's own
  screenshots on the build machines. **No person has used them in the app
  yet.**

## Tests

`tests/test_detmath.cpp` (the air: pinned values, average push, no
bunching, smoothness), `tests/test_simulation.cpp` (carried not pushed, 3D,
scrubbing equals playing, fingerprints), `tests/test_compile.cpp` (limits),
`editor/tests/test_sprite_mesh.cpp` (trails on winding paths; what a
dissolving particle carries), `editor/tests/test_shapes.cpp` (the pattern:
steady, never coming back, different for each particle, soft and torn, the
glowing edge; the picture renderer), `editor/tests/test_unity.cpp`,
`editor/tests/test_session.cpp` (controls in older effects), and the
refinement case in `editor/tests/test_reference.cpp`.
