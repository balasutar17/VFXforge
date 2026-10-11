VFX Forge for Unity
===================

This folder was put here by VFX Forge's "Export to Unity". It lets Unity
open effects made in VFX Forge.

  Effects/    one .vfxforge file per exported effect. Unity turns each one
              into a prefab: drag it into a scene, or into another prefab.
  Editor/     the importer that builds the prefabs (editor only, not part
              of a game build).
  Shaders/    the particle shader the effects are drawn with.
  Textures/   the picture that holds VFX Forge's first thirty-five
              particle shapes.
  Shapes/     one picture for each shape from the shape library that an
              exported effect uses (a leaf, a trophy, a rune...), and for
              each shape whose own settings were changed in VFX Forge (a
              star with seven points is star_7_0_0_0.png). Like the
              picture in Textures/, these hold a shape, not colours: leave
              their import settings as they are.
  Sounds/     the sounds the effects play: your own, and VFX Forge's
              library sounds (made by VFX Forge, free to use in your game).
  Runtime/    VFXForgeSound, which plays each layer's sound in step with
              the effect, in the game as well as in the editor, and
              VFXForgeHold, which shows a layer as held drawings ("on
              twos") when Hold Frames is set in VFX Forge. Held drawings
              show in Play mode and in a built game; the Scene view's own
              particle preview stays smooth.
  Images/     your own pictures and sprite sheets, used by layers that draw
              them. A sprite sheet plays through Unity's Texture Sheet
              Animation module, set up for you.

Each effect is ordinary Unity Particle Systems, one per layer, under one
parent. Play the parent to play the whole effect.

Exporting the same effect again updates the prefab, and every scene that
uses it. Changes made to the prefab in Unity are replaced at that point: to
keep your own changes, right-click the prefab instance in a scene and choose
"Prefab > Unpack" first.

Works with the Built-in render pipeline and with URP (including the 2D
Renderer). Unity 2021.3 or newer.
