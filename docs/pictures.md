# Your own pictures and sprite sheets (0.4)

Any layer can draw a picture the artist painted instead of one of the
built-in shapes. A picture divided into a grid is a sprite sheet: each
particle steps through its cells as an animation.

## Using it

In the Simple controls, the Shape section has **Use my own picture…**. Any
picture Qt can read works (PNG, WebP, TIFF, JPEG, BMP, GIF); a PNG with a
see-through background is what effects are usually painted as.

- **Across / Down**: the sheet's grid. A file named like `fire_4x2.png` or
  `smoke 8x8.png` sets it on its own.
- **Use**: how many cells to play, left to right and top to bottom. 0 plays
  them all. For a sheet whose last row is not full.
- **Play the pictures**: *Once per life* plays the sheet from each particle's
  birth to its death; *Loop* plays it over and over at the frame rate (with
  an option to start each particle on a different cell); *One at random*
  gives each particle one cell, held for its life.
- **Color** tints the picture. The first time a layer gets a picture its
  colour is set to white, so the picture shows as painted. Glow, opacity
  over life and additive blending all apply as they do to shapes.
- **Use shape** goes back to the built-in shapes.

**Try a sample sheet** loads a hand-drawn toon flame (8 frames, 4 by 2) that
ships with the app, made by `tools/make_sample_sheet.py`.

## Where pictures are kept

A picture is copied, as a PNG, into an `images` folder next to the `.vfx`
file, named after the original plus a short fingerprint of its contents
(`images/fire-1a2b3c4d.png`). The effect refers to it by that relative path,
so a project folder can be moved or shared whole. Until an effect is first
saved, its pictures wait in a private scratch folder, and Save As copies
them next to the new file. A picture that has gone missing is reported in
the status bar; the layer keeps drawing (as a soft dot in the window) until
the picture is chosen again.

## How it is drawn

The simulation decides which cell each particle shows (a whole number, part
of every frame and of the pinned fingerprints); a random cell comes from the
particle's own random stream, so it is the same on every machine and at
every scrub position. The mesh builder turns the cell into texture
coordinates half a pixel inside the cell, so smoothing never shows the
neighbouring cell. The window draws pictures with a texture (premultiplied,
with mipmaps); the picture renderer used for exported frames does the same
sums on the CPU with its own mipmaps, so a big painted sheet drawn small
stays smooth in both.

## In Unity

The picture is written to `Assets/VFXForge/Images/` and set as the layer's
material texture (sRGB, uncompressed, mipmapped, clamped). A sprite sheet is
played by Unity's own **Texture Sheet Animation** module in Lifetime mode:

| VFX Forge | Unity |
|---|---|
| Once per life | Frame over Time 0 → (frames ÷ cells), 1 cycle |
| Loop | the same, with Cycles = the whole number of plays closest to lifetime × frame rate ÷ frames; Random Start gives a random Start Frame |
| One at random | Frame over Time 0, random Start Frame |

Known differences:

- **Loop** in Unity plays a whole number of times per particle life, so its
  speed matches exactly only when every particle lives as long as the
  average one.
- With **Loop** and Random Start on a sheet whose last cells are unused,
  Unity can wrap into the unused cells.
- Not yet checked inside Unity by a person: the cell order (Unity counts
  rows from the top, as VFX Forge does, per its documentation) and the
  colours in a Linear-colour-space project.
