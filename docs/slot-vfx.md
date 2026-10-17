# Slot-game VFX studio: milestone 1, effects that follow a symbol (0.22)

Bala's brief "Premium Slot Game VFX Animation Studio" asks for a lot more
than one release can hold. This is the first milestone: the shape-adaptive
part (section 4 of the brief) for 2D symbols, the presets built on it, and
the acceptance tests of section 16 up to exporting and reading back a PNG
sequence.

The ArtStation portfolio in the brief could not be studied: its images and
videos do not load outside a browser. Bala's video and his two reference
sheets (the energy frames and the saber styles) were studied instead.

## What works

**The target.** Under *Target* in the left panel: *Import a symbol…* takes
a PNG (or JPG, WebP, BMP: turned into a PNG) as the picture the effect is
made for. It is drawn under the effect, saved with it (in the project's
images folder, like other pictures), and placed with Width, X and Y.

**The Path shape.** A new emitter shape (Shape > Path): particles come from
a line or outline. Its settings:

| Setting | What it does |
| --- | --- |
| Path | the points (traced, drawn by a preset, or moved with the handles) |
| Fit to Target | traced again from the target whenever the target changes |
| Closed | an outline, or an open line |
| Emit From | Edge: on the line. Volume: inside the outline (shape fill) |
| Padding | the outline moved out (or in), corners mitred |
| Thickness | a band either side of the line |
| Travel, Travel Length, Travel Heads | energy running round the outline: rounds a second, how much of it at once, how many heads |
| Travel Mode | Run: round and round. Draw: drawn on once from the start |
| Align to Path | each particle turned to lie along the line (crackles, streaks) |
| Send | the layer's Direction, straight out from the outline, or along it |

Particles are spaced evenly along a path (each a golden-ratio step from
the last), so a line of short-lived dots has no gaps. Everything is part of
the deterministic simulation: the same seed draws the same frames.

**Tracing.** The silhouette is the outer edge of the largest solid part of
the picture (alpha at least half), smoothed and given 200 evenly spaced
points. A second symbol of another shape refits every layer set to Fit to
Target in one undo step; the layers' own settings are untouched, so a
preset is reusable on any symbol.

**Selecting and fitting.** *Fit selected layers to it* turns any layers
into outline layers. Each layer can still be moved, turned and sized alone
with the handles (a Path's points move with it).

**Preview.** Top of the viewport: *Both / Effect / Target* (when there is a
target) and the background: Dark, Black, White, Checker.

**Presets** (32 new):
- *Symbol Outlines* (follow the target): Gold, Rare (electric blue), Epic
  (violet smoke), Legendary (fire, comets), Outline Trace (drawn on by a
  spark), Symbol Glitter (fills the shape).
- *Energy Frames*, after Bala's frame sheet: Fire Spark Frame, Corner Fire
  Frame, Gold and Neon Electric Frames, Blue and Violet Smoke Frames, Frame
  Trace, Cyan and Gold Lightning Columns, Light Pillars, Portal Gate, Win
  Frame Flash, Sun Orb, Comet Trail.
- *Sabers and Beams*, after the saber sheet: Blue, Arc Reactor, Burning,
  Firestorm, Electric, Green Energy, Neon, Nebula, Solar, Starkiller, Ghost,
  Yellow Smoke.

**Export.** PNG frames with see-through backgrounds, the sprite sheet and
Unity, as before; the target is not drawn into them. In Unity a Path is
approximated by a box round it (its points are written along with it for a
script); the running energy is not reproduced there.

## The acceptance tests of the brief

| Brief | Where it is checked | Status |
| --- | --- | --- |
| Import a transparent PNG symbol | `test_target.cpp`, app self-test | done |
| Apply a golden outline | same | done |
| Change thickness and colour | `test_target.cpp` | done |
| Outline travelling round the symbol | `test_target.cpp`, `test_simulation.cpp` | done |
| Replace the symbol with another shape; the effect adapts | `test_target.cpp`, app self-test | done |
| Independent particles and a glow layer | `test_target.cpp` | done |
| Move the glow without moving the particles | `test_target.cpp` | done |
| Animate all properties with keyframes | | **not yet**: curves over a particle's life and over the layer's time exist for some settings, not keyframes on every setting |
| Preview at the frame rate | the viewport, as before | done |
| Export a transparent PNG sequence | `test_target.cpp` | done |
| Re-import it and check playback | `test_target.cpp` reads every frame back and checks size and see-through | done in the library; the app's Reference panel reads GIFs and video, **not** a folder of PNGs yet |
| Save as a reusable preset | | **not yet**: effects save as files; your own Library entries are not built |
| Close, reopen, still editable | `test_target.cpp` | done |
| A 3D model, surface conform | | **not yet**: a model is shown and particles hide behind it, but particles do not come off its surface. The outline here is a 2D silhouette and is never presented as a 3D surface |
| Unsupported features say so | tracing an empty picture, fitting with no target | done for these |

## Next milestones

1. Keyframes on any setting, over the layer's time, with easing and a
   timeline track per layer.
2. Your own presets: save, variations, tags, favourites, search.
3. Surface conform on 3D models (sampling triangles of the model).
4. PNG-sequence import as a target or reference; sprite-sheet metadata.
5. More slot families: win tiers, coin showers, multipliers, reel stops.

## Tests

`tests/test_simulation.cpp` (the Path shape: even by length, padding and
corners, thickness, fill inside an L shape, outward from either winding,
running and drawing on, aligning, on the ground in 3D, too short a path,
determinism); `tests/test_detmath.cpp` (a deterministic atan2);
`editor/tests/test_target.cpp` (tracing a disc and a crown, the largest
part, nothing to trace; the whole workflow above; rounded rectangles);
`editor/tests/test_presets.cpp` (every new preset valid, loops cleanly).
The app's self-test sets a coin then a crown as the target under
Legendary Outline and checks the layers follow both.
