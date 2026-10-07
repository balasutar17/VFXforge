# VFX Forge

An offline-first desktop tool for creating real-time visual effects for games
and animation, on Windows and macOS.

The app is built automatically for Mac and Windows every time this repository
changes (see **Getting the app** below). Nobody using it needs a compiler.

What exists so far:

- the **core library**: the data model, the `.vfx` file format, commands with
  undo and redo, the compile step, and the fixed-step particle simulation that
  gives the same result on every machine
- the **editor layer**: what an editing session is (open, edit, undo, play,
  scrub, save), with no window attached, so every rule is tested without one
- the **desktop app**: a window with a live viewport, an effect library of
  ready-made presets, layers, the Simple controls, a backdrop picture to work
  against, playback, open and save. What it does and does not do yet is in
  [docs/app-first-build.md](docs/app-first-build.md) and
  [docs/library-and-shapes.md](docs/library-and-shapes.md)
- **`vfxshot`**, which draws any effect or preset to a PNG with no window and
  no graphics card
- **`vfxcli`**, a command-line tool that drives the core with no UI
- a **test suite**, including random edit sequences, a fuzzed file loader and
  pinned simulation fingerprints

The full plan is in the Phase 1 architecture document. What each milestone
covers, and what has and has not been verified, is in
[docs/milestone-1.1.md](docs/milestone-1.1.md),
[docs/milestone-1.2.md](docs/milestone-1.2.md) and
[docs/app-first-build.md](docs/app-first-build.md).

## Getting the app

Open the repository's **Releases** page and download the latest build:
`VFX-Forge-mac.dmg` for a Mac (open it and drag VFX Forge to Applications) or
`VFX-Forge-windows.zip` for Windows (unzip it and run `VFX Forge.exe`).

The Mac app is not yet signed with an Apple developer certificate, so the
first time it is opened macOS asks for confirmation.

## Build and test

You need CMake 3.24 or newer and a C++20 compiler: Xcode's command-line tools
on macOS, Visual Studio 2022 on Windows, or GCC 13 / Clang 16 on Linux.
The two third-party libraries the core uses are in `third_party/`.

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Use `release` in place of `dev` for an optimised build. On macOS and Linux,
`sanitize` adds memory and undefined-behaviour checks.

That builds everything except the app. The app also needs Qt 6.5 or newer
(with the Shader Tools module); add `-DVFX_BUILD_APP=ON` and point
`CMAKE_PREFIX_PATH` at the Qt installation. `.github/workflows/ci.yml` is the
exact recipe the automatic builds use.

## Try it

```sh
# the tool is at build/dev/tools/vfxcli/vfxcli (on Windows, under Debug\)
vfxcli new spark.vfx --name "Spark"
vfxcli info spark.vfx
vfxcli list spark.vfx                 # every property path and its value
vfxcli set spark.vfx effect/duration 3.5
vfxcli validate samples/coin_burst.vfx
vfxcli schema                         # every module and property, as JSON
vfxcli simulate samples/coin_burst.vfx --every 0.25   # run it, no window
vfxcli benchmark --particles 20000    # how fast is this machine?
```

Open `samples/coin_burst.vfx` in any text editor to see the format.

## Layout

```text
core/          the library: no UI toolkit, no graphics API
  include/vfx/   public headers
  src/           implementation
editor/        an editing session and the particle mesh: still no UI toolkit
app/           the desktop app (Qt 6): window, viewport, packaging
tools/vfxcli/  command-line tool, links core only
tools/vfxshot/ pictures of effects and presets, links the editor layer
tests/         core test suite and fixture files
samples/       example .vfx files
third_party/   vendored libraries and their licences
docs/          milestone notes
```

## The rules

There are two.

**The simulation must give the same result everywhere.** It never calls the
system's sin, cos, pow or exp, which differ between platforms; it uses the
functions in `DetMath.h`. Every random number is a hash of what it is for,
never a running generator. A test scans the sources for forbidden calls, and
another pins exact fingerprints that every platform must reproduce.

`core` and `editor` must never depend on the UI or the GPU. The same code will later power
the editor, exporters and engine runtimes, and that only works if it stands
alone. The build has only the C++ standard library and one JSON header on
`core`'s include path, so a stray dependency fails to compile.
