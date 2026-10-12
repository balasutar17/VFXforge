# Shapes you can change (0.14, 0.15)

Until now a star had five points and a ring was as thick as it was drawn.
From 0.14, shapes have **settings of their own**: how many points, sides,
rays or teeth; how thick; how pointed; how round. They are sliders under
the shapes in the **Shape** control, and they show only for a shape that
has them. 0.14 gave them to twenty-two shapes; 0.15 to twenty-two more,
**forty-four** in all.

## Which shapes, and what

| Shape | Count | Thickness | Pointiness | Roundness |
| --- | --- | --- | --- | --- |
| **Star** | Points, 3 to 16 | | yes | sharper or rounder |
| **Four-point, Six-point, Eight-point star, Plump star** | Points, 3 to 16 | | yes | sharper or rounder |
| **Star outline** | Points, 3 to 16 | yes | yes | sharper or rounder |
| **Triangle, Pentagon, Hexagon, Octagon** | Sides, 3 to 12 | | | rounder |
| **Hex frame** | Sides, 3 to 12 | yes | | rounder |
| **Square** | | | | sharper or rounder |
| **Frame** | | yes | | sharper or rounder |
| **Plus, Cross** | | yes (of the arms) | | sharper or rounder |
| **Ring, Double ring, Arc** | | yes | | |
| **Dashed ring** | Dashes, 3 to 24 | yes | | |
| **Sunburst** | Rays, 5 to 24 | | | |
| **Flower** | Petals, 3 to 12 | | | |
| **Gear** | Teeth, 5 to 16 | | | |
| **Diamond** | | | yes (narrower) | rounder |
| **Wedge** | | | yes (narrower) | |
| **Clover** | Leaves, 3 to 8 | | | |
| **Snowflake** | Arms, 3 to 12 | yes | | |
| **Pinwheel** | Blades, 2 to 8 | | | |
| **Crown** | Points, 3 to 9 | | | |
| **Pow** | Spikes, 6 to 20 | | yes | |
| **Bang** | Spikes, 6 to 24 | yes | yes | |
| **Pentacle** | Points, 5 to 12 | | | |
| **Speed lines** | Lines, 6 to 31 | yes | | |
| **Impact lines** | Lines, 6 to 24 | yes | | |
| **Claw marks** | Marks, 1 to 5 | yes | | |
| **Ripple** | Rings, 1 to 6 | yes | | |
| **Zigzag** | Bends, 1 to 9 | yes | | |
| **Spiral** | Turns, 2 to 6 | yes | | |
| **Portal ring** | Pieces, 2 to 8 | yes | | |
| **Smoke ring** | Puffs, 3 to 16 | yes | | |
| **Soft ring, Halo, Donut, Crosshair, Glow bar** | | yes | | |

Star, Ring, Square and Diamond are four of the original 35 shapes; the
rest are from the shape library. Claw marks that are close together are
drawn thinner so they stay apart, and a Pentacle with an even number of
points is two figures woven together (six makes a hexagram).

## What each setting does

- **Points / Sides / Dashes / Rays / Petals / Teeth**: a whole number. The
  slider starts on the shape's own count.
- **Thickness**, from -1 to 1: left is thinner, right is thicker, **0 is
  the shape as it was drawn**. The outside of a ring or an outline stays
  where it is and the line grows inward, so the particle does not change
  size. A Ring at 1 is a disc; a Frame at 1 is a filled square.
- **Pointiness**, from -1 to 1: the notches between a star's points move
  in (right: longer, thinner points) or out (left: blunter, until the star
  is nearly a plain figure with straight sides). The points themselves
  stay where they are. 0 is the shape as drawn.
- **Roundness**: right is rounder corners, 0 is the shape as drawn, and
  where the shape was drawn with rounded corners (Star, Square, Frame,
  Plus, Cross) left is sharper.
  - On a **figure with straight sides** the corners are taken off and the
    sides stay where they are: at 1 it is the circle that fits inside. A
    Triangle at 1 is therefore a smaller circle than a Hexagon at 1.
  - On a **star** it works the other way about: the points stay where they
    are and the star fills out, toward a flower.

Changing the count keeps a shape's character: a Four-point star given
seven points is still a thin, sharp star, and a Plump star is still plump.
An odd-sided figure points up; a figure whose sides come in fours (a
square, an octagon, twelve sides) sits flat.

**As drawn** (next to "Change this shape") puts everything back.

**Picking another shape gives that shape as it was drawn.** The settings
of the last one are put back to none in the same undo step, so one undo
returns the old shape with its settings.

Everything else about a layer works on a changed shape as before: Color,
Glow, Squash and Stretch, Dissolve, Trails, Lie Flat, Hold Frames.

## How it works

A changed shape is one of endlessly many, so it cannot be written into
the graphics card's shader the way the original 35 are. It is drawn the
way a library shape is ([shape-library.md](shape-library.md)): from its
formula into a picture, and from the picture by the graphics card. That
holds for Star, Ring, Square and Diamond too: unchanged, they are drawn by the
graphics card's own formula exactly as before; changed, they go by
picture. Each of the four was written a second time for this, starting
from the same measurements, and a test holds the second drawing to the
first: one step of a slider away from "as drawn" is nearly the same shape.

Amounts are kept in 63 steps each way, so a setting has one exact value
everywhere it is drawn (the app, exported frames, Unity) and a slider
cannot ask for more different pictures than that. The app keeps the
pictures of the last dozen changed shapes it drew and lets older ones go.

## In files

Four new properties in the sprite module, each written with its default
when unused:

```json
"shapeCount": 0,
"shapeThickness": 0.0,
"shapeSharpness": 0.0,
"shapeRound": 0.0
```

`shapeCount` 0 means the shape's own count. A setting the layer's shape
does not have is kept in the file and ignored. An effect saved by 0.13
opens unchanged and plays exactly as it did (the simulation's pinned
fingerprints are untouched: these settings change only what is drawn). An
effect that uses them opens in 0.13 and earlier with them ignored.

In the Advanced view they are also listed under Sprite, as **Shape
Points**, **Shape Thickness**, **Shape Pointiness** and **Shape
Roundness**.

## Unity

A changed shape travels as a picture of its own, with its settings in its
name, and two layers with the same change share one:

```text
Assets/VFXForge/Shapes/star_7_0_32_0.png     Star: 7 points, pointiness 32 steps of 63
Assets/VFXForge/Shapes/triangle_0_0_0_63.png Triangle, fully rounded
```

Nothing changed in the importer or the shader: it is the route the shape
library already uses.

- **Not run inside Unity**, like the shape library before it. The build
  machines compile the importer; nobody has looked at a changed shape in a
  Unity scene.
- Changing a setting and exporting again writes a new picture and leaves
  the old one in `Assets/VFXForge/Shapes/`. Unused ones can be deleted by
  hand; nothing breaks if they stay.

## Limits

- **Not every shape.** Forty-four of the 143 have settings. The rest have
  nothing obvious to count or thicken (a leaf, a key, a skull, a cloud),
  or were left as drawn for now: Burst, Sparkle, Twinkle and the other
  glows of the first 35, Octagram, Triad, Eye, Gem, and the debris.
- **A changed shape is a 512-pixel picture** in the app's live view and in
  Unity, like a library shape: very large on screen, its edges soften
  slightly. A Star drawn unchanged is still sharp at any size; move a
  slider and it is not quite. Exported frames and sprite sheets are not
  affected.
- **Dragging a slider may not be perfectly smooth.** Every stop is a new
  picture, which takes a few hundredths of a second to draw, so the view
  can stutter while you drag. How much has not been measured in the app.
- **The settings cannot change over a particle's life.** A star cannot
  grow points as it ages; no curve can be put on them.
- **The picker's small icons show shapes as drawn**, not with the layer's
  settings. The main view shows the change.
- **Roundness means two different things** (corners cut off a figure;
  a star filling out). Each is the natural one for its shape, but they are
  not the same operation.
- **Reference to VFX does not use them.** A rebuilt effect always gets
  shapes as drawn.
- Seen in pictures drawn by the picture renderer and in the app's own
  screenshots on the build machines. **No person has moved these sliders
  in the app yet.**

## Tests

`editor/tests/test_shape_edits.cpp`: which shapes can be changed and how
far; a change held to what its shape can use (own count, range, whole
steps); unchanged is the shape as drawn, point for point, and one slider
step is a small change; every changed shape stays inside its square at
both ends of every setting, in every combination; the count is the number
of points, sides, dashes, rays, petals and teeth actually drawn; thicker
covers more and a ring becomes a disc; more pointed covers less and the
points stay put; a square's corners go sharp and round while its sides
stay put; a changed shape's picture ID says which shape and which change;
the mesh and the picture renderer use it; only the last few changed
pictures are kept; the settings compile in whole steps, reach the frame,
and save and load; Unity gets one picture per distinct change.

The app's self-test gives a preset's stars eight points, picks another
shape (which must come as drawn), undoes that (which must bring back the
eight points), and takes a picture of the sliders on each build machine.

`vfxshot edits out.png` draws every changeable shape as drawn and at each
end of each of its settings.
