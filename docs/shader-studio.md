# Shader Studio (the Shader tab)

Press **Shader** in the top bar. Pick a shader family and preset on the left, change its controls on
the right, watch it animate in the middle, key it on the timeline, and **Export to Unity…**.

## How it is built

* `editor/shaders/ShaderCore.hlsli` is the shader maths, written once in the small part of HLSL that is
  also valid C++. The preview compiles it as C++ (a vector-type shim in `ShaderMath.h`) and draws it on
  a small mesh; the Unity export pastes the *same text* into the generated `.shader`. The preview and
  Unity therefore run the same code, but see "What is not verified".
* `editor/shaders/ShaderParams.def` lists every parameter once. It becomes the editor's tables, the
  C++ index constants, and the shader's `#define`s and properties.
* `ShaderLib` holds families, presets, keyframe animation (linear / smooth / ease in / ease out;
  loop / ping-pong / once), saving (`.vfxshader` JSON, repaired on load), validation, quality profiles.
* `ShaderPreview` renders any settings at any time deterministically (scrubbing is exact).
* `ShaderExport` writes the Unity files; `unity/Runtime/VFXForgeShaderAnimator.cs` plays the timeline and
  takes gameplay calls (`Hit`, `Burst`, `Reveal`) with no editor open.
* All animation is shared: the "Animation" buttons (Pulse, Flow, Rotate, Scroll, Dissolve, Expand,
  Flicker, Hit reaction, Keyframes, Noise) are recipes over the common parameters and keyframes; no
  preset has its own animation code.

## Families and presets (39)

11 Emissive / Glow (7), 12 Hologram / Energy (6), 14 Force Field / Shield (6), 15 Particle / VFX
material (7), 16 UI / Sprite (7), 18 Ice / Snow (6).

## What is exported

`Assets/VFXForge/Shaders/<Name>/` gets `<Name>.shader` (+ `.meta` with a fixed GUID), `<Name>.mat`,
`<Name>.vfxshader` (to reopen it), `<Name>.anim.json`, `README.txt` (setup and what is not supported);
`Assets/VFXForge/Runtime/` gets the animator scripts, `Assets/VFXForge/Editor/` the
*Assets ▸ VFX Forge ▸ Create Shader Demo Object* menu.

## What is not verified, or not done

* **Unity itself.** Nothing here can run Unity. The generated HLSL is parsed by `glslangValidator`
  (`tools/scripts/check_shaders.py`, run in CI on Linux) and the C# is compiled against Unity's libraries
  in CI, but no shader or material has been imported into a Unity editor by VFX Forge.
* One file with a URP pass and a Built-in pass (UI shaders: Built-in only). The URP pass is checked only
  as HLSL here, not compiled by Unity. No HDRP, no Shader Graph version.
* The particle family is a material only: it does not spawn, move or time particles. Soft particles and
  flipbook sheets are not implemented.
* Ice has no true refraction and no crack propagation (the cracks are a procedural pattern; "melt" is the
  Reveal control).
* UI shaders read the sprite's alpha at 0-1 UV: use Full Rect sprites outside an atlas, or Border mode.
* No textures, masks or gradient-ramp assets are exported: colours come from three colour controls.
* The preview is a CPU render of the shared source on a few meshes. It shows the look, not the GPU cost.
  No cost estimate is shown because none is measured.
* Bloom is the preview's own; in Unity add Bloom with HDR.
* The exported demo is an editor menu that builds an object; no `.unity` scene file is written.
