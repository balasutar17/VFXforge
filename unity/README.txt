VFX Forge for Unity
===================

This folder was put here by VFX Forge's "Export to Unity". It lets Unity
open effects made in VFX Forge.

  Effects/    one .vfxforge file per exported effect. Unity turns each one
              into a prefab: drag it into a scene, or into another prefab.
  Editor/     the importer that builds the prefabs (editor only, not part
              of a game build).
  Shaders/    the particle shader the effects are drawn with.
  Textures/   the picture that holds VFX Forge's built-in particle shapes.

Each effect is ordinary Unity Particle Systems, one per layer, under one
parent. Play the parent to play the whole effect.

Exporting the same effect again updates the prefab, and every scene that
uses it. Changes made to the prefab in Unity are replaced at that point: to
keep your own changes, right-click the prefab instance in a scene and choose
"Prefab > Unpack" first.

Works with the Built-in render pipeline and with URP (including the 2D
Renderer). Unity 2021.3 or newer.
