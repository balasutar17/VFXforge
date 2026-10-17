# Reference-to-VFX Reconstruction: where it stands (0.23)

This page sets the brief "Reference-to-VFX Reconstruction Studio" (October
2026) against what VFX Forge actually does, section by section. **Works**
means it is built, used by the app, and covered by tests. **Partly** says
what part. **Not built** means nothing does it yet, whatever the interface
suggests. The detail of each working part is in [reference.md](reference.md).

Most of the brief's pipeline already existed before 0.23 (import, analysis,
rebuild, side-by-side comparison, measured fitting, refinements, versions,
editable output, export). 0.23 adds what testing it against a real slot
reference sheet showed was missing: reading lines of light, splitting a sheet
of many effects, and a flipbook rebuild. Measured on twelve effects cut from
that sheet (the measured likeness after fitting, a guide rather than a
verdict):

| Effect | Before | After |
| --- | --- | --- |
| Orange spark frame | 63% | 90% |
| Neon electric frame | 58% | 83% |
| Plain orange box | 56% | 79% |
| Purple smoke frame | 74% | 83% |
| Cyan lightning column | 69% | 78% |
| Yellow lightning column | 68% | 75% |
| Corner fire frame | 66% | 73% |
| Saber | 57% | 66% |
| Gold lightning square | 71% | 74% |
| Blue smoke frame | 68% | 69% |
| Sun orb, comet (not lines) | 87%, 49% | unchanged |

A flipbook of the same column measures 96%: it is the reference itself.

## 3. Reference analysis

| Asked for | Status |
| --- | --- |
| Import a video, image sequence, sprite sheet or picture | **Works:** picture, GIF, animated WebP, MP4/WebM/MOV (the computer's own decoder). **Partly:** an image sequence (a folder of PNGs) is read by `vfxref`, not yet by the app; a sprite sheet of one effect's frames is not cut into frames on import |
| Frame rate, resolution, duration, aspect | **Works** |
| Every frame, key moments, anticipation/impact/peak/ending | **Works:** every frame is measured; appears, burst, fullest, widest, second burst, fading, gone are marked |
| Trajectories, scale, rotation, deformation, colour and brightness over time | **Works** for the effect as a whole and (0.23) for each line; **partly** for other parts (below) |
| Adaptive sampling for fast flashes | **Partly:** the sampling rate is chosen (10–30 a second), not adapted; an opening flash is found and described on its own |
| Decomposition into editable layers | **Works:** glow, core, solid shape, rays, rings, sparks, streaks, pieces, smoke, head and tail, something lying on the ground, an opening flash, and (0.23) lines with their halo, crackle and sparkles, and the light between lines |
| Each layer's own timing | **Partly:** lines (0.23) and an opening flash have their own; the other parts of a burst share one growth and one fade |
| Estimates marked for the artist | **Works:** every finding says how sure it is and whether it was seen or assumed |
| Several effects on one reference | **Works (0.23):** found, numbered, each studied on its own at full sharpness |

## 4. Reconstruction

| Asked for | Status |
| --- | --- |
| Procedural shapes, particles, vector shapes and outlines | **Works:** built-in and library shapes, particle layers, and (0.23) path emitters along each line |
| Frame-based reconstruction, flipbook | **Works (0.23):** the clip's own frames as a sprite-sheet layer, built layers kept beside it, switched off. Cut-outs (pieces lifted from one frame) since 0.9 |
| Hybrid of procedural and frames | **Partly:** both are in one effect, switched as wholes; a flipbook is not yet split into parts that can be swapped one by one |
| Animated gradients, masks, noise | **Partly:** colour over life, dissolve, turbulence. No masks |
| Distortion, heat haze | **Not built** |
| 3D meshes, volumetrics | **Not built** (a flat base leaned back, and 3D placement on a ground plane, only) |

## 5. Motion and timing

| Asked for | Status |
| --- | --- |
| Delay, acceleration, impact, expansion curves, bursts, drag, travel, rotation | **Works:** measured and turned into curves and settings |
| Single particles' paths, turbulence, gravity | **Not measured:** sparks match in number, size, colour and spread, not one by one; swirl and fall are assumed and said to be |
| Keyframes and curves the artist can edit | **Works:** the curves made are edited in the Advanced view's curve editor. Keyframes on any setting over the effect's time: **not built** |
| Time remapping, playback speed, loop, scrubbing | **Works:** trim, speed, loop found and kept seamless, one timeline for reference and effect |
| Reference timing kept by default | **Works** |

## 6. Colour and shape matching

| Asked for | Status |
| --- | --- |
| Palette, per-part colours, gradients over time | **Works** |
| Silhouette comparison, edge and glow measures, particle size and density | **Works** |
| Sample a colour from any reference frame onto a layer | **Not built** (only the background colour can be picked) |
| Irregular shapes kept, not replaced by circles | **Partly:** outlines and strokes are followed exactly (0.23); hand-drawn solid shapes become the closest built-in shape unless cut-outs or a flipbook are allowed |

## 7. Comparison studio

| Asked for | Status |
| --- | --- |
| Reference and result, synchronised playback, frame stepping | **Works** |
| Overlay with opacity, difference view | **Works** |
| Split screen (a wipe) | **Not built** (side by side instead) |
| Silhouette side by side, colour difference view | **Not built** as views; both are measured |
| Timeline markers where they differ | **Not built:** the reference's moments and the effect's layer starts are marked, not mismatches |
| Measured likeness, used as guidance only | **Works:** seven parts, labelled as a guide, never as proof of a match |

## 8. Iterative refinement

**Works:** build, compare, fit (each change kept only if it measures closer),
26 targeted refinements, the largest differences put into words, every
version kept. **Partly:** fitting adjusts size, spread and brightness; it does
not yet work through the brief's order (outline, then timing, then colour...)
as separate passes.

## 9. Editable output

**Works:** layers with emitters, curves, gradients, glow and blending;
visibility, locking, grouping, duplicating, reordering; transforms of one
layer, a group or a selection with the gizmo; presets; undo and redo; the
project saved and reopened with its reference, the settings it was read with
and (0.23) the part of a sheet that was picked. **Not built:** meshes and
materials.

## 10. Rendering and export

**Works:** see-through PNG sequences and sprite sheets of one pass, a Unity
package with its particle systems. **Partly:** a see-through or black
background, not a choice of premultiplied or straight alpha; resolution and
frame range follow the effect. **Not built:** Unreal.

## 11. Audio

**Partly:** sounds can be imported and placed at layer starts and bursts,
with volume, pitch and delay. The reference video's own soundtrack is not
read or placed.

## 12. Performance and offline

**Works:** everything runs on the computer, offline; analysis and fitting run
on a worker thread with progress and a Stop button; the preview is drawn by
the graphics card; macOS (Apple Silicon) and Windows builds.

## 13. Workflow levels

**Partly:** Simple and Advanced views. The Expert view is labelled as not
built in the app.

## 15. Acceptance criteria

| Criterion | Status |
| --- | --- |
| Imports and analyses supported videos | Met |
| A playable reconstruction, not a static approximation | Met |
| Major shapes, colours, timing and motion preserved as closely as possible | Met for glows, bursts, rings, rays, ground effects and (0.23) lines; partly for hand-drawn solid shapes and smoke (cut-outs or a flipbook come closer) |
| Detected components remain editable | Met |
| Original and result compared at synchronised timestamps | Met |
| The artist can identify and correct visible differences | Met (differences in words, refinements, editing), without mismatch markers on the timeline |
| One layer's change leaves the others alone | Met |
| Saved, reopened and exported | Met |
| Transparent exports validated for edges and frame consistency | Partly: frames are one size and see-through; no premultiplied/straight choice |
| Offline | Met |
| Errors, unsupported features and mismatches reported honestly | Met: this page, the report's seen/assumed marks and its "not built" notes |

## Next, in order

1. Keyframes on any setting over the effect's time, with a timeline track
   per layer (asked for by both briefs).
2. Each part of a burst on its own timing (a core that stays while a ring
   thins).
3. Mismatch markers on the comparison timeline, a wipe view, and picking a
   colour from the reference onto a layer.
4. Image sequences and sprite sheets of one effect imported in the app.
5. Sound from the reference video, placed at its own moments.
