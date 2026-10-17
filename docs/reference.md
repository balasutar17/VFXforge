# Reference to VFX (0.9, lines, sheets and flipbooks in 0.23)

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

### 1b. A sheet of several effects (0.23)

A reel or presentation sheet often holds many effects playing at once (the
slot frame sheet that prompted this holds about thirty). When a reference
holds two or more separate effects, the workspace says how many and draws a
numbered box round each. **Clicking a number studies that effect on its
own**: the original file is read again, cut to that box *before* it is made
smaller, so a 1920-pixel video sheet gives each effect its full sharpness
rather than a few dozen pixels. **Back to the whole sheet** returns. Which
effect was picked is saved with the project.

How the boxes are found (`findEffects`, `editor/src/ReferenceSheet.cpp`):
whatever is clearly lit at any of sixteen moments across the clip is
gathered on one grid, so an effect that only shows for a few frames is
found too; parts that touch are one effect; specks near an effect join it
(a spark flying off a frame), specks far from everything are left out; the
boxes are padded a little and put in reading order.

Its limits: it cannot know what belongs together beyond touching. Two
blades of one saber standing apart are two effects; a frame whose sparks
fly far is one effect and some loose specks. Drag a box by hand for those.

### 2. Reading settings

- **Light on a background that is not black** is read as the light that
  was added, not the paler colour it made (blue light over grey shows as
  pale blue), and rebuilt layers are dimmed by the room the background
  leaves. (Until 0.9 such a reference came back washed out.)
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
| Something lying on the ground | See "Lying on the ground" below |
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

#### Lines of light (0.23)

A frame round a symbol, the edge of a portal, a bolt down the side of a
light column. Read as a whole, such a picture misleads: a hollow frame has
the size and spread of a solid shape, two bolts side by side look like one
comet. Each bright part is now also looked at on its own
(`editor/src/ReferenceLines.cpp`):

| Found | How |
| --- | --- |
| That it is a line | A part that stands out from what is round it (the picture less a smoothing pass that removes thin things, so bolts over a bright fill are found too) and is long and thin all along: at least eight times as long as its core is wide. A part that narrows from one end to the other is a head with a tail, not a line |
| Its middle | Thinned to one point wide (Zhang and Suen); the longest way along it, end to end, side branches left off. An outline: its outer edge with the hole filled, moved in by half the core's width, corners kept |
| Goes round or not | A hole as large as a good share of the part. A stroke whose ends meet is closed too (a frame with a gap) |
| Broken lines | Pieces whose ends meet and run on the same way are joined; ends that nearly touch join whichever way (a frame breaks at its corners) |
| Core and halo | The core's width from how deep its middle lies inside it; the halo read ring by ring outward until it fades, or stops fading (where the light behind begins) |
| Crackling | Strands: a bundle of thin crooked lines is several times wider than any one strand; side branches; a bend in an open stroke. Sparkles stuck to a clean line do not count |
| Sparkles | Small bright spots off the line, near it: how many, how big, how far off |
| Light between | What is left once the lines, halos and sparkles are taken out, when it lies between or inside them: its box, colour, and brightness from one end to the other |
| Rays, not lines | Several straight strokes pointing out from the middle every which way are rays or streaks thrown out, and are left to that reading. A round ring stays a ring |

When the lines and their glow make up most of the effect, the effect is
read as lines: the solid-shape, comet, glow, ring and ray readings stand
aside. For a clip, each line is also followed through every frame:

| Found | How |
| --- | --- |
| Brightness over time | Along the line, frame by frame |
| Flicker | How much each part of it changes from frame to frame |
| Drawn on | The lit share climbs from little to nearly all over a few frames; from where, which way (or both ways from a point), and how fast, from the longest lit stretch growing |
| Light running round | The pattern along it lined up from frame to frame; kept only when it runs the same way nearly every frame |

#### Lying on the ground

A magic circle, a portal or a shockwave on the floor shows in a picture as
a level oval, usually with shafts of light on it. It is read as what it is
(`editor/src/ReferenceGround.cpp`):

| Found | How |
| --- | --- |
| That it is one | The bright mass, with thin things smoothed away, is a level oval clearly wider than tall; it is filled (or a ring unbroken most of the way round), the same on the left as on the right, and holds a good share of all the light |
| How far it leans back | Looking outward from the middle in 48 directions, the place where the light falls away fastest is found. Ovals are put through threes of those places and the one most places agree with is kept, so that directions which run up a shaft of light do not decide. The lean is the angle that squashes a circle into that oval |
| What is on it | Fine line (a circle of lines), light that differs from one side to the other going round (a whirl), an empty middle (a ring), or none of these (plain light) |
| A white middle | A bright patch near the centre much whiter than the base |
| Shafts of light | Thin upright columns above the base: how many, how tall, how wide, where |
| How fast it turns (a clip) | Round the near half of the base as if seen from above, the pattern of light and dark is lined up from one frame to the next, after taking off what never changes. Checked against clips of known speed |

What a flat picture cannot say is marked as assumed: that a still's base
turns at all, that sparks rise, how the shafts come and go.

Its limits: only a **round** base seen **level** (not rolled to one side)
is read this way; the lean is good to about six degrees, less for a soft
edge; with nothing rising from it, an oval has to be clearly flat (leaning
back 57 degrees or more) and crisp flat shapes are left alone, so that a
drawn bean or sweet is not taken for the floor. Every other preset in the
library, photographed at three moments, is checked not to be read as one.

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
| Something lying on the ground | A base that lies flat (Lie Flat set to the lean found) and turns: the built-in whirl, circle of lines, ring, or a soft disc with a rim; a soft glow under it; a white core; shafts as upright streaks of the "beam" shape; motes drifting up. It lasts a whole number of turns |

**Lines** (0.23) are rebuilt on path emitters that follow each line's own
middle, as read:

| Found | Becomes |
| --- | --- |
| The line | Soft dots a quarter of a dot apart along its path, sized to its core |
| Its halo | Larger, fainter dots along the same path |
| Crackling | The built-in bolt (thin branching strands) laid along the path, overlapping so they braid, redrawn 15 times a second when it flickers |
| Sparkles | Twinkles on and just off the path (not on a crackling line, whose bits are its crackle) |
| Light running round | A bright patch travelling round it at the speed seen |
| Drawn on | The path starts where the drawing starts and is drawn the same way, at the same speed; both ways at once becomes two halves |
| Light between | Soft streaks rising from its bright end, fading as the brightness does; or an even wash |
| Brightness over time | Each line's layers follow it, through how many dots are lit |

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
  For something lying on the ground the cut-out is the base itself, stood
  back upright (each row of the oval stretched back to the circle it is a
  view of), with shafts of light bridged out of it, so that its layer can
  lay it flat again and turn it.
- **Flipbook** (0.23, off unless turned on, clips only): one layer plays the
  clip's own frames, cut from their background, as a sprite sheet in step
  with the effect (up to 64 pictures, each up to 192 pixels). It is the
  reference itself, so it matches as closely as anything can; it is edited
  as a whole (size, place, timing, colour), not part by part. The layers
  built from the analysis are kept, switched off, to switch on in its place
  or alongside. Like cut-outs, those are the reference's own pixels, and the
  app says so. Fitting is skipped: there is nothing to adjust.
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
- **Twenty-six refinements**, each a plain edit to plain properties and one
  undo step: bigger, smaller, wider, tighter, more sparks, fewer sparks,
  more glow, less glow, faster, slower, sharper, softer, more stylized, add
  finer particles, lighter for mobile, more swirl, calmer, dissolve away,
  on twos, smooth, squash and stretch, anticipation, overshoot, curl,
  reference colours again, reference length again. (More swirl, calmer and
  dissolve away came with 0.10; see
  [turbulence-and-dissolve.md](turbulence-and-dissolve.md). The six from on
  twos to curl came with 0.12 and are also under Motion… in the main
  window; see [animation-principles.md](animation-principles.md).)
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
  random. Whether they fall, rise or swirl is not measured. Smoke is given
  a little moving air and a ragged dissolve because that is how smoke goes,
  and the report marks both as assumed; for sparks, "More swirl" adds
  turbulence when the original has it, and how much is your call.
- **No manual correction of tracking**, and **no curve editor**: the curves
  are made and saved, but the Advanced and Expert editors that would change
  them are not built. Timing can be changed with Faster, Slower and the
  layer's Lifetime control.
- **No AI models.** Busy backgrounds are not cut out: the background must be
  close to one plain colour, or the picture must have its own transparency.
  The report says so when it is not.
- **One effect at a time.** A sheet of several is split into its effects
  (above), and each is studied on its own. The split goes by what touches;
  effects that overlap, or sit closer than a few pixels, come as one.
- **Only two moments of a burst are described**: the opening flash and the
  main part. Something else that shows briefly in between is missed.
- **One growth and one fade for the whole burst.** The glow, shape, rays
  and rings of a burst share the size and opacity curves measured from the
  whole picture (a ring's fade allows for its being an outline). A part
  that fades on its own schedule, such as a bright middle that stays while
  a ring thins, or smoke that follows fire, is not followed separately.
- **Something lying on the ground is one flat shape leaned back**, not true
  depth, and its pattern is a built-in one of the same kind unless cut-outs
  are on. A base that is not round, or is rolled to one side, is not read
  as lying down at all.
- **Lines** (0.23): lines that cross or touch are one line, followed along
  its longest way; a crossing's other arms are left to the halo and
  crackle. Crackle is the built-in bolt, not the reference's own strands.
  The light between lines is one box with brightness along its length, not
  a painted gradient. Brightness over time is followed through how many
  dots are lit, so a line that dims deeply also thins to beads. Only the
  first line's colours are restored by "reference colours again".
- **Not built:** heat haze and distortion, beams and cones as meshes, true depth, emitters that move along a path (a travelling effect's
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
vfxref split sheet/ --fps 30 --sheet boxes.png
vfxref build sheet/ out.vfx --fps 30 --effect 7 --flipbook
```

`--picture` writes reference | effect | overlay | difference.

## Tests

`editor/tests/test_reference.cpp` (43 cases) paints its own references, so
what should be found is known: import checks, each background kind, glow,
ring, sparks, rays, shape matching, head and tail, cropping, start/peak/end,
trim and speed, loop, turning, travel, curve making, the clocks lining up,
a burst shown once through and a repeated one coming round again, a ring
that thins while the middle stays bright, nothing blinking between passes,
something on the ground read as a level oval with its lean and its shafts,
what is not on the ground left alone, a turning base followed through a
clip and rebuilt lying flat, the base lifted and laid flat by cut-outs,
light on a grey background read as light,
comparison of an effect with its own picture, fitting (and locked layers,
and stopping), cut-outs, every refinement, one-undo-step rebuilds, keeping
the reference through save and reopen, and progress and cancelling.

`editor/tests/test_reference_lines.cpp` (0.23) paints frames, bolts and
sheets: a hollow frame read as one outline where it is and rebuilt on a path
that matches once fitted; two crackling bolts with light between read as
bolts and a fill brightest at the bottom, not a comet; a frame drawn on from
its top-left corner rebuilt drawn on from there; light running round a frame
at half a turn a second measured and rebuilt running; a steady frame that
neither runs, draws on nor flickers; a sheet of three split in reading order
with a stray spark joined to its frame and noise left out, each part read as
what it is; one effect alone not taken for a sheet; and a flipbook in step
with its clip, the built layers switched off.

The app's self-test reads a picture, a GIF, an MP4 and a WebM, builds from
them, reads a clip of a portal as lying on the ground and turning and
rebuilds it with a layer that lies flat, finds the three effects on a sample
sheet, picks the lightning column and reads it as two bolts and a fill,
makes a flipbook of it, and photographs the workspace on every build.
