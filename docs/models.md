# Making effects on a 3D model (0.16)

An effect is made for something: a sword swing, a character levelling up,
a chest opening. From 0.16 you can bring that something into VFX Forge
and make the effect on it.

**Model** in the left panel (under Backdrop):

- **Import a model…** reads a `.obj`, `.glb` or `.gltf` file: the formats
  Blender, Maya, 3ds Max, Cinema 4D and every game engine export.
- **Dummy** shows a practice dummy, made for trying effects on.

The model stands in the effect's world, drawn under the particles in a toon
style: three tones of its own colours, lit from over your left shoulder, with
a light rim at its edge. Then:

| Setting | What it does |
| --- | --- |
| **Height** | How tall it stands, in world units. A model comes in 3 units tall, whatever units its file used. |
| **Turn** | Degrees round its own upright. |
| **X, Y** (and **Z** in a 3D effect) | Where it stands: Y is where its feet are, X and Z the middle of its footprint. |
| **Hide particles behind it** | Particles that pass behind the model are hidden. |
| **Change…**, **Remove** | Another model, or none. |

In a **3D effect** drag the camera round as usual and the model turns with
the world. **Hide particles behind it** starts on, so an aura's back half
goes behind the character and comes round again in front.

In a **2D effect** the model is seen from the front and the particles are
a flat sheet through its middle, so hiding what is behind it would hide
half the effect. There it starts off: the effect is drawn over the model,
as a 2D game draws it over a sprite.

## What it is for, and what it is not

The model is for **seeing the effect on**. It is not part of the effect:

- It is **not saved in the effect file** and **not exported** to Unity or
  in frames and sprite sheets. In the game the character is already there.
- Like the backdrop picture, it is **remembered for each effect on this
  computer**: open the effect again and it comes back where you left it
  (if its file is still where it was).

`vfxshot` can draw an effect on a model too, for a quick picture:
`vfxshot preset healing-aura out.png --model dummy` or `--model hero.glb
--place 0,0,0,30,3` (x, y, z, turn, height).

## What is read from a file

- **The shape**: every mesh in the file, placed as the file places it
  (glTF nodes, with their moves, turns and sizes).
- **Plain colours**: an OBJ's material colours (from the `.mtl` file it
  names, beside it) or colours written after each corner; a glTF's
  material base colours and vertex colours.
- **Normals**, when the file has them; when not, smooth ones are worked out.

## Limits

- **No textures.** Colours painted in a picture are not read: a textured
  model is drawn in its plain material colour, or clay grey. That is
  usually enough to place an effect; it is not a material preview.
- **No animation, no rig.** The model stands in its rest pose. An effect on
  a running character cannot be checked against the run.
- **Not FBX.** Export OBJ or glTF from your 3D program. Compressed glTF
  (Draco, meshopt) is not read; export without compression.
- **Hidden by the particle's middle.** A particle is hidden, or faded, by
  whether its centre is behind the model. A large puff half behind an
  edge is all hidden or all shown (with a short fade as it passes behind
  the surface), not cut along the edge.
- **Particles do not touch the model.** They pass through it, do not
  bounce off it, and cannot yet be made to come off its surface. Particles
  from the model's surface is the next step.
- **Drawn by the computer's processor, not the graphics card.** The model
  is drawn again only when it, its place or the camera changes. A model of
  a few thousand triangles is drawn in a few hundredths of a second; one of
  hundreds of thousands takes longer, and turning the camera round it will
  not be smooth. While the camera moves it is drawn at lower quality and
  sharpened when you let go.
- **One model at a time.**
- Seen in pictures drawn by `vfxshot` and in the app's own screenshots on
  the build machines. **No person has imported a model of their own yet.**

## Tests

`editor/tests/test_model.cpp`: OBJ read with its material colours, vertex
colours, given and worked-out normals, and refused when broken; glTF read
from .glb and from text with its data inside, node moves applied, linear
colours turned to screen colours, compressed files refused with a message;
files read by extension with their .mtl beside them; the model drawn
standing where it is put, as tall as asked, turned, lit from the left,
with smoothed edges; how far away each pixel is; particles behind it
hidden, in front of it and beside it kept, just behind it faded, from
either side of a 3D camera; an effect drawn over a model.

The app's self-test shows the practice dummy under Healing Aura and takes
a picture of it on each build machine.
