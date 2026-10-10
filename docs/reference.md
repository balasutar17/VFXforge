# Reference to VFX (0.8.1)

Bring a picture, a GIF or a short video of an effect. VFX Forge studies it,
builds an **editable effect** from what it finds, and shows the two side by
side. The result is ordinary layers of ordinary modules in the ordinary
`.vfx` file. It is never a flattened picture of the reference.

Everything is worked out on the computer with plain image arithmetic. No
network, no cloud service, no trained model; none is downloaded.

This page says what works, how, and where it stops.

## What works

### 1. Importing

- **Pictures:** PNG, JPG, WebP, BMP, and anything else the app's image
  readers handle. **Clips:** GIF and animated WebP, and MP4, WebM and MOV
  video through the computer's own media support.
- Drop a file on the workspace or choose one. Unsupported and unreadable
  files are refused with a reason. The name, size, frame count, length and
  frame rate are shown.
- A picture is kept at up to 1536 pixels a side. A clip is read for at most
  12 seconds and 240 frames, at up to 320 pixels a side. For a video the
  sampling rate (10, 15, 24 or 30 frames a second) is yours to choose; a
  GIF's uneven frame times are laid onto an even beat.
- **The original file is kept with the project, untouched**: copied byte for
  byte into `reference/` next to the `.vfx` file and listed in it (asset
  kind `reference`), with the settings it was read with. Saving a copy takes
  it along; opening the project brings it back. Files over 96 MB are not
  copied, and that is said.

### 2. Reading settings

- **Background:** worked out (see-through, dark or light, from the picture's
  edge), or told, or a colour picked by clicking the picture. The effect is
  separated from it by "colour to alpha": the least amount of some colour
  that, over that background, gives the pixel seen. A difference too small
  to see (true black beside a nearly black background, true white on
  off-white paper) is not counted. (Until 0.8.1 it was, and a picture
  saved from the web could be read as one large black shape.)
- **Crop** by dragging a box on the picture. **Trim** a clip's start and end,
  and set its **playback speed**. **Cut-off:** how faint still counts.
  **Detail:** the analysis grid, 128 / 192 / 256 points on the longest side.
- Any change starts the reading again, in the background, with progress and
  a Stop button.

### 3. The analysis

For a picture (`editor/src/ReferenceStill.cpp`):

| Found | How |
| --- | --- |
| Centre, extent, long direction, left/right symmetry | Centre of mass, the radius holding 95% of the light, the spread's main axis |
| Colours | Up to five by k-means, with their share |
| Crisp or soft | The share of in-between values |
| The round part | The lower quarter of the brightness round each circle (so rays and sparks do not lift it) |
| Glow (one or two) | The round part fitted with one or two soft blobs. Where the picture is clipped to white, the true strength of the light is worked out from the colour channel that clipped least |
| White core | Only when the middle is whiter than the glow's own colour, however strong, could make it |
| Main shape | The largest solid, sharp-edged patch, compared by outline and by light/dark areas with each built-in shape at 48 turns |
| Rays | What is left along each direction once the round part is taken away; the built-in flash shape and turn that fits |
| Rings | A peak in brightness away from the centre that goes most of the way round; plain, soft or jagged |
| Sparks, streaks, loose pieces | What a smoothing pass removes, sorted by size and thinness; count, sizes, distances, colours (up to three), softness, which way they sit or point |
| A head with a tail | One joined, long shape with its brightest part at one end: length, direction, width and colour along it |
| Several separate effects | Counted, with advice to crop |

For a clip, additionally (`editor/src/ReferenceTime.cpp`), from measuring
every frame as a whole:

| Found | How |
| --- | --- |
| Start, fullest moment, end | When the amount of effect crosses 6% of its most |
| The moment that stands for it | By which half of all its light has been shown |
| One burst, or keeps going | Whether it is still there, steadily, when the clip ends; and when a steady effect has finished starting up |
| Loop | The smallest step at which frames come round to the same picture; or the same burst twice |
| Bursts | Sudden growth |
| Spreading | The extent's growth and how quickly it dies off (a drag figure) |
| Travel | A line or a curve through the centre's path |
| Turning | The shift that best lines up the brightness round the centre from frame to frame |
| Sparks' speed and lifetime | From the group's average distance and count over time |
| An opening flash | When the brightest moment comes well before the representative one and has a form of its own, it is described separately |

Moments are marked on the timeline: appears, main burst, fullest, widest,
second burst, main part, fading, gone.

**The report** lists every finding with how sure it is (0 to 100%) and
whether it was **seen** in the reference or **assumed** because the
reference cannot show it. A still picture's timing, the speed of its
sparks, gravity, what is in front of what, and whether light is added or
painted are always marked assumed. Things the reference cannot settle are
listed. How each part was worked out, and what a smarter method might add,
is listed too.

### 4. Rebuilding

`reconstruct()` (`editor/src/Reconstruct.cpp`) chooses a representation for
each thing found:

| Found | Becomes |
| --- | --- |
| Glow | One or two large soft sprites, sized and levelled from the fit |
| Main shape | One sprite of the closest built-in shape, turned to fit; or a cluster of puffs when only a blobby shape roughly fits; or (if allowed) a cut-out |
| Rays | One sprite of the closest built-in flash; or thin slivers thrown outward when there are many and nothing fits; or (if allowed) a cut-out |
| Rings | One ring, shockwave or jagged-ring sprite each, opening outward |
| Sparks, streaks, pieces | Bursts (or steady emission) of particles: count, size, colour and spread as seen; a layer per clearly different colour |
| Head and tail | A soft head, and particles streaming back from it that shrink and change colour |
| Grey, soft areas on a light background | A few smoke puffs |
| An opening flash | Its own short-lived layer |

From a clip, one-shot layers get **curves** made from the measurements:
size over life from the extent, opacity from the brightness, colour over
life from the colour, each simplified to at most six or eight keys; spin
from the turning; speed, direction and pull from the travel. Particles get
their drag from the spreading and burst times from the bursts.

The opacity of a burst is the lower of two readings: how bright the
brightest parts are, and how much effect there is for how far it has
spread (a filled shape's amount goes with its area, a ring's with its
length). The second catches a glow or ring that thins out while a few
sparks stay bright.

In an effect that keeps going, a sprite that is there the whole time is
made to hand over to its next self without a frame of neither or of both:
the effect's length is rounded to a whole number of simulation steps
(sixtieths of a second) and the sprite is born half a step in.

- **Four modes:** Balanced (default), Shape and colour, Motion, Layered.
  They differ in how many layers are kept, how sparks are split, and for a
  still, how long the look is held.
- **Three takes:** Closest (default), Lighter (half the particles, at most
  four layers), Enhanced (adds glints and an afterglow, each marked "added,
  not in the reference"). Enhanced is only ever built when chosen.
- **Device:** Mobile, Desktop or VR sets the particle limit (about 120, 600,
  250 at once); counts are scaled to fit.
- **One burst or keeps going** can be set by hand for a still.
- **Cut-outs** (off unless turned on): rays and hand-drawn shapes no
  built-in shape matches are lifted out of the reference as a picture for
  the layer to draw. For rays the cut-out is what the fitted glow leaves
  undrawn, colour channel by colour channel, so glow and rays together add
  back up to the reference. Closer, but those are the reference's own
  pixels, so the app says to use it only with art that is yours to use.
  Fitting leaves cut-out layers, and the glows under them, alone.
- Each layer records its **role** (glow, core, body, rays, ring, sparks,
  streaks, bits, smoke, trail), how it is built, and whether its look and
  its motion were seen or assumed.
- Building is **one undo step**. **Locked layers stay** as they were.

### 5. Comparing

- The effect is drawn by the reference renderer, framed as the reference is
  framed. **Side by side**, **overlay** with an opacity slider, and a
  **difference** view (black the same, warm where the effect is brighter,
  cool where dimmer). Wheel to zoom, drag to move: all pictures together.
- **One timeline for both.** Scrub, play, or step frame by frame; the
  reference's frame and the effect's moment move together. Moments in the
  reference are marked above the bar, the effect's layer starts and bursts
  below it. A single burst is shown once through, and then nothing, as the
  clip shows it (the effect itself is set to repeat for watching). An
  effect that keeps going, or a burst the clip shows more than once, comes
  round again in step with the clip.
- **Measured likeness**, in seven parts: outline, colour, brightness,
  number of pieces, fine detail, motion and timing (the last two only for a
  clip). The largest differences are put into words ("the outer edge is
  dimmer than in the reference: the glow stops short"). Seven sliders set
  how much each part counts.
- It is labelled for what it is: a guide for finding differences, **not a
  measure of how alike the two look to a person**.

### 6. Fitting and refining

- **Fitting** (`fitToReference`): after building, the size, spread and
  brightness of each unlocked layer are nudged, the effect is drawn again,
  and a change is kept only if the pictures measure closer. What was
  changed is listed. "Match again" and "Improve the outline" run it again.
- **Seventeen refinements**, each a plain edit to plain properties and one
  undo step: bigger, smaller, wider, tighter, more sparks, fewer sparks,
  more glow, less glow, faster, slower, sharper, softer, more stylized, add
  finer particles, lighter for mobile, reference colours again, reference
  length again.
- **Versions:** every build and refinement is kept in a list while the
  window is open; clicking one brings it back (also one undo step).

### 7. Layers

Rename, show or hide, lock, move up and down, copy, and open in the main
window to edit with the Simple controls. Lock, move and copy are in the
main window's layer list too.

### 8. Cost

Shown after building: how long the analysis and the fitting took, how many
times the effect was drawn, the number of layers, particles at its busiest
and the device's limit. Work runs on a background thread and can be stopped.

## What it does not do

- **It does not follow single particles.** Sparks match the reference in
  number, size, colour and spread, not one by one, and their positions are
  random. Whether they fall, rise or swirl is not measured.
- **No manual correction of tracking**, and **no curve editor**: the curves
  are made and saved, but the Advanced and Expert editors that would change
  them are not built. Timing can be changed with Faster, Slower and the
  layer's Lifetime control.
- **No AI models.** Busy backgrounds are not cut out: the background must be
  close to one plain colour, or the picture must have its own transparency.
  The report says so when it is not.
- **One effect per reference.** A sheet of several must be cropped to one.
  The crop is a box: when effects overlap or sit too close for a box to
  separate them, a neighbour's edge comes along and the report says so.
- **Only two moments of a burst are described**: the opening flash and the
  main part. Something else that shows briefly in between is missed.
- **One growth and one fade for the whole burst.** The glow, shape, rays
  and rings of a burst share the size and opacity curves measured from the
  whole picture (a ring's fade allows for its being an outline). A part
  that fades on its own schedule, such as a bright middle that stays while
  a ring thins, or smoke that follows fire, is not followed separately.
- **Not built:** heat haze and distortion, turbulence, beams and cones as
  meshes, depth, emitters that move along a path (a travelling effect's
  particle layers start from where it began).
- **Built-in shapes are approximations** of hand-drawn art. Cut-outs are
  closer, with the caution above.
- **Video** depends on what the computer can decode. If a video cannot be
  read, the app says so and suggests an MP4 (H.264) or a GIF.
- **Repeatable, not identical everywhere:** the same reference and settings
  give the same effect on the same computer. The analysis uses ordinary
  floating-point maths, so another computer may differ in the last decimal
  places. (The simulation itself stays bit-identical across computers.)

## From a terminal

`vfxref` runs the same steps without the app:

```
vfxref analyze flash.png --crop 0,0,0.5,0.5
vfxref build flash.png out.vfx --mode layered --cutouts --picture compare.png
vfxref build frames/ out.vfx --fps 30 --strip moments.png
```

`--picture` writes reference | effect | overlay | difference.

## Tests

`editor/tests/test_reference.cpp` (37 cases) paints its own references, so
what should be found is known: import checks, each background kind, glow,
ring, sparks, rays, shape matching, head and tail, cropping, start/peak/end,
trim and speed, loop, turning, travel, curve making, the clocks lining up,
a burst shown once through and a repeated one coming round again, a ring
that thins while the middle stays bright, nothing blinking between passes,
comparison of an effect with its own picture, fitting (and locked layers,
and stopping), cut-outs, every refinement, one-undo-step rebuilds, keeping
the reference through save and reopen, and progress and cancelling.

The app's self-test reads a picture, a GIF, an MP4 and a WebM, builds from
them, and photographs the workspace on every build.
