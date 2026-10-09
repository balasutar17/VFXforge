# Exporting (version 0.3)

The Export button (Ctrl/Cmd+E) offers three ways out.

## Into a Unity project

Choose the Unity project's folder once; VFX Forge remembers it. It writes:

```text
Assets/VFXForge/Editor/VFXForgeImporter.cs   builds prefabs from .vfxforge files
Assets/VFXForge/Editor/VFXForgeJson.cs       reads them
Assets/VFXForge/Shaders/VFXForgeParticle.shader
Assets/VFXForge/Textures/VFXForgeShapes.png  every built-in shape, in a grid
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
| Rectangle or circle with a direction | Box turned to face the direction; spread becomes Randomize Direction |
| Rectangle outline | Box Edge with no depth |
| Streak (align to movement, stretch) | Stretched Billboard: Speed Scale = stretch |
| Shape, blend, glow | The VFX Forge particle shader: shape picture, shape number, Glow, Additive |
| Layer order | Sorting Order |

Where a shape is approximated, the description says `"exact": false`.

### Known differences

- Unity's randomness is not VFX Forge's, so particles are not in the same
  places; the look and timing are the same.
- A circle or rectangle that emits in a direction becomes a box facing that
  way; the spread becomes "randomize direction", which is close but not
  identical.
- In a project set to Linear colour space, glowing (additive) layers look
  somewhat brighter than in VFX Forge, which blends as the screen shows.
- Built-in and URP pipelines (including the 2D Renderer) are supported.
  HDRP is not.

## As a .unitypackage

The same files in one package, for Assets > Import Package > Custom Package,
or to send to someone else.

## As animation frames

Numbered PNG files of one pass of the effect, at the effect's frame rate,
showing exactly what the viewport shows, plus one sprite sheet. The
background is see-through (glow counts toward coverage by its brightness)
or black (for additive blending in an engine). A looping effect is recorded
on its second pass, so the last frame leads straight into the first.

In Unity, select all the frames in the Project window and drag them into a
scene: Unity creates the animation clip and the Animator for them.
