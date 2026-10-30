# Exporting (version 0.3)

The Export button (Ctrl/Cmd+E) offers three ways out.

## Into a Unity project

Choose the Unity project's folder once; VFX Forge remembers it. It writes:

```text
Assets/VFXForge/Editor/VFXForgeImporter.cs   builds prefabs from .vfxforge files
Assets/VFXForge/Editor/VFXForgeJson.cs       reads them
Assets/VFXForge/Shaders/VFXForgeParticle.shader
Assets/VFXForge/Textures/VFXForgeShapes.png  the first 35 shapes, in a grid
Assets/VFXForge/Shapes/<shape>.png           each shape from the shape library the effect uses
Assets/VFXForge/Effects/<name>.vfxforge      the effect
Assets/VFXForge/README.txt
```

Unity turns each `.vfxforge` file into a prefab: a parent object whose
silent Particle System times the whole effect, and one child Particle System
per layer. Exporting the same effect again replaces the file, Unity rebuilds
the prefab, and every scene using it updates.

The extension is `.vfxforge`, not `.vfx`, because Unity's Visual Effect
Graph already uses `.vfx`.

### How settings carry over

All of the judgement is in `editor/src/UnityExport.cpp`, which is tested;
the Unity script only reads the numbers and sets fields.

| VFX Forge | Unity Particle System |
| --- | --- |
| Effect length and loop | Duration and Looping, on every system |
| Layer start and length | Emission rate held at zero outside the layer's time; bursts moved by the start |
| Amount (rate), bursts | Emission: Rate over Time, Bursts |
| Lifetime, speed, size | Start Lifetime, Start Speed, Start Size (constant, random between two, or curve) |
| Rotation | Start Rotation, in radians and clockwise, as Unity counts |
| Colour | Start Color, as shown on screen |
| A fixed or random size/opacity "over life" | Folded into Start Size / Start Color, where it is a per-particle multiplier |
| Gravity | Force over Lifetime, world space |
| Drag | Limit Velocity over Lifetime: Drag |
| Spin | Rotation over Lifetime |
| Size over life (curve) | Size over Lifetime |
| Colour and opacity over life | Color over Lifetime. Unity gradients cannot be brighter than white, so a white-hot gradient is divided down and the difference moves into the material's Glow |
| Point, any spread (2D) | Circle shape of no size whose arc is the spread: exact |
| Point (3D) | Cone up to 90 degrees, Sphere for every direction |
| Rectangle with a direction | Box turned to face the direction; spread becomes Randomize Direction |
| Rectangle or circle flat across its direction (a ring on the ground sending dust up) | A mesh of that outline or area whose normals point the layer's way (a flat Unity box has no depth to send particles along) |
| Circle with a direction | A mesh of that circle or ring, normals the layer's way |
| Path | A mesh along the path, cut into equal steps; particles spaced along it as VFX Forge spaces them; turned particles drawn as streaks along it |
| Streak (align to movement, stretch) | Stretched Billboard: Speed Scale = stretch |
| Speed over Life | Velocity over Lifetime: Speed Modifier |
| Orbit | Velocity over Lifetime: Orbital Z (Y on the ground), about the layer's Position. Particles sent out from the centre move out by Radial velocity instead of their own speed, which is VFX Forge's turning spoke; others are close, not exact |
| 3D positions and directions | Z mirrored: VFX Forge's front looks from +Z, Unity's from -Z, so the effect faces the same way in a Unity scene |
| Squash and Stretch | Size over Lifetime with Separate Axes |
| Hold Frames | The VFXForgeHold script, which moves the layer on one drawing at a time |
| Shape, blend, glow | The VFX Forge particle shader: shape picture, shape number, Glow, Additive |
| Layer order | Sorting Order |
| Force, Force Ramp, Velocity Drift | Force over Lifetime (plus gravity), Velocity over Lifetime: Linear |
| Speed Limit, Inherit Velocity | Limit Velocity over Lifetime: Speed and Dampen; Inherit Velocity (also used for a sub emitter's Inherit Motion) |
| Rotation / Size / Colour by Speed | Rotation, Size and Color by Speed |
| Collide (floor) | Collision: a plane at the floor height, with Bounce, Dampen, Lifetime Loss, Min Kill Speed |
| Kill Zone | Triggers: a box collider, Inside or Outside set to Kill |
| Emit When (parent born / dies / bounces) | Sub Emitters: Birth, Death or Collision, with Emit Probability and Inherit |
| Trail Share, Tail Width, Fades, Detail, Picture, In World | Trails: Ratio, Width over Trail, Color over Trail, Minimum Vertex Distance, Texture Mode, World Space |
| Light Share, Intensity, Range, Follows Size/Fade, Limit | Lights: Ratio, Intensity, Range, Size Affects Range, Alpha Affects Intensity, Max Lights (a point light template is made under the layer) |
| Max Particles, Prewarm, Simulation Speed/Space, Scaling Mode, Culling Mode, Ring Buffer, Stop Action | The same settings in the Particle System main module. Only Max Particles shows in the preview; the others are Unity-only |


Where a shape is approximated, the description says `"exact": false`.

### Known differences

- A layer's Opacity read as a curve across its time (a line rebuilt from a
  reference that brightens and dims) goes to Unity as a Start Color
  gradient over the effect's time, at most eight steps.
- Unity's randomness is not VFX Forge's, so particles are not in the same
  places; the look and timing are the same.
- A circle or rectangle that emits in a direction becomes a box facing that
  way; the spread becomes "randomize direction", which is close but not
  identical.
- In a project set to Linear colour space, Unity adds glow up in linear
  light, which looks dimmer and more orange than VFX Forge. With URP 17
  (Unity 6) the VFX Forge Colours renderer feature, added to the project's
  URP renderers when the files are first imported (or by VFX Forge > Use
  Exact VFX Forge Colours), draws VFX Forge particles into a buffer of
  screen values and lays it over the scene, so they blend exactly as in
  VFX Forge. Over a dark background the result is VFX Forge's; over a lit
  scene the background shows through by the particles' cover. Without it
  (Built-in pipeline in Linear, or older URP), glow looks as Unity blends it.
- Unity's Particle System, played back in big steps (Simulate in the
  editor), carries fast, sharply slowing particles a little further than
  VFX Forge does; in play at normal frame rates the difference is small.
- Built-in and URP pipelines (including the 2D Renderer) are supported.
  HDRP is not.

## As a .unitypackage

The same files in one package, for Assets > Import Package > Custom Package,
or to send to someone else.

## For Unreal Engine

Export > Unreal Engine effect writes a folder, "VFXForge for Unreal", with
the effect as a flipbook: every frame of one pass, drawn by the same
renderer as the viewport (as the viewport's camera sees it now), packed into
one picture of at most 4096 pixels a side, plus a description and the
import script. Exporting more effects to the same folder adds them beside
the others.

In Unreal (5.x, with the Python Editor Script Plugin and Paper2D on, as
they are in a new project): Tools > Execute Python Script, and choose
`VFXForge_Import.py` in that folder. For each effect it makes, under
Content/VFXForge/<name>:

| Asset | What it is |
| --- | --- |
| BP_<name> | An actor with the flipbook: drag it into a level. It plays when it appears, once or looping as the effect does |
| FB_<name> | The Paper2D flipbook, at the effect's frame rate |
| S_<name>_<n> | One sprite per frame, its pivot at VFX Forge's centre, 1 VFX Forge unit = 100 Unreal units |
| MI_<name> | Its material, with Glow |
| T_<name>_Flipbook | The frames, uncompressed and without mipmaps |
| M_VFXForge_Flipbook (shared) | Unlit, two-sided, alpha composite; undoes eye adaptation so the frames show at their own strength |

Running the script again updates the assets in place.

What it is not: the effect is not rebuilt from Niagara emitters, so its
layers are not separate in Unreal; edit it in VFX Forge and export again. It
is flat, in Paper2D's X-Z plane. Unreal's tonemapper still shapes its
brightest colours, as it does everything in the game.

A flat effect is framed to what it covers over its whole pass, so the
frames spend their pixels on the effect; a frame is as wide as the effect
is, and its longer side is the size chosen (made smaller to fit).

## As animation frames

Numbered PNG files of one pass of the effect, at the effect's frame rate,
showing exactly what the viewport shows, plus one sprite sheet. The
background is see-through (glow counts toward coverage by its brightness)
or black (for additive blending in an engine). A looping effect is recorded
on its second pass, so the last frame leads straight into the first.

In Unity, select all the frames in the Project window and drag them into a
scene: Unity creates the animation clip and the Animator for them.
