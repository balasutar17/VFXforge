# The first build of the app

This is the first version of VFX Forge that opens a window. It covers the
skeleton (milestone 1.0) and the first slice of the renderer and editing UI
(milestones 1.3 and 1.4), enough to open the app and shape an effect.

## What it does

- **Viewport.** Shows the effect live. Drag to move (2D) or look around (3D),
  Shift-drag to move in 3D, scroll to zoom, double-click to reset.
- **Simple controls.** Size, Speed, Amount, Lifetime, Color, Glow, Spread and
  Direction for the selected layer. Number controls can be one value or a
  random range per particle. Each slider drag is one undo step.
- **Layers.** Add, remove, rename, switch on and off.
- **Effect settings.** Name, length, frames per second, variation number
  (the seed), and "Try another variation".
- **Playback.** Play, pause, restart, loop, step a frame, scrub, and
  quarter, half, normal and double speed.
- **Files.** New 2D, New 3D, Open, Save, Save As, with a warning before
  unsaved changes are lost. Files from a newer version open read-only.
- **Undo and redo** for every change, with no limit that matters (1000 steps).

## What is NOT IMPLEMENTED yet

- Advanced and Expert views (the module inspector, curves, gradients, bursts).
  A control whose value is a curve is shown but cannot be edited.
- Textures. Every particle is the built-in soft dot.
- The full renderer: this build draws through Qt Quick's scene graph in the
  window's own colour space. Linear-light blending, tone mapping, a ground
  grid, depth sorting of 3D alpha particles and the standalone QRhi renderer
  belong to milestone 1.3.
- The timeline panel (layer bars, keys), checkpoints for instant scrubbing.
- Export of any kind.
- Autosave, crash recovery, recent files, opening a file by double-clicking it.
- A menu bar. Everything is on buttons and keyboard shortcuts for now.
- A signed and notarised Mac app.

## How it is put together

`app/` is the only code that uses Qt. It holds no rules about effects:

- `AppController` translates between the window and `vfx::editor::Session`.
- `ViewportItem` asks the editor layer for the frame's triangles and hands
  them to the graphics card with one small shader.
- `app/qml/` is the window's layout.

Everything else (what a slider drag does, what undo restores, where the
camera puts a particle, how colours are prepared for blending) lives in
`editor/` and is tested there without a window.

## How it is checked

- The editor layer has its own tests (`editor/tests`), run on every platform.
- The app has a self-test: `"VFX Forge" --self-test picture.png` starts the
  real app, runs it for four seconds, saves a picture of the window, and
  fails if the window reported any error, time did not advance, or the
  viewport shows no particles. The automatic builds run it on Linux, on the
  packaged Mac app and on the packaged Windows app.

## Status

See the "ci-results" branch for the outcome of the latest automatic build, and
[library-and-shapes.md](library-and-shapes.md) for what version 0.2 added.
