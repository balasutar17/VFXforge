# Milestone 1.1: the document

Phase 1 has seven milestones. This is the second (after the repository
skeleton, which it also includes). It delivers the data model, the `.vfx`
format, commands with undo and redo, and a command-line tool. It has no
simulation, no rendering and no window; those are milestones 1.2 to 1.6.

## What works

| Area | What it does |
| --- | --- |
| Data model | An effect holds assets and ordered layers. A layer holds ordered modules and its Simple-mode control bindings. Six module types: Emission, Shape, Initial State, Motion, Over Lifetime, Sprite. 24 properties in all. |
| Values | A property is true/false, a whole number, a number, text, an option, three numbers, a colour, an asset reference, a burst list, a colour gradient, or a *scalar*: a fixed number, a random range, or a curve over the particle's life. |
| Metadata | Each property is declared once: key, artist label, type, default, hard limits, slider range, unit, level (Simple or Advanced), help text. Validation, the file loader, the command layer and `vfxcli schema` all read this one table. |
| IDs | Every effect, layer, module, control and asset has a random 64-bit ID that never changes and is never reused. |
| Property paths | Every editable value has one address, such as `layer/l-…/module/m-…/gravity`. |
| Commands | Set property, add/remove/reorder layer, add/remove/reorder module, add/remove asset, and a composite that groups any of these. Nothing else can change a document. |
| Undo and redo | Unlimited steps up to a cap (1000 by default). Transactions turn a slider drag into one step. The save point tracks unsaved changes correctly through undo and redo. |
| Change records | Every command reports what it changed, on apply and on undo. |
| `.vfx` format | Canonical JSON. Saving an unchanged effect gives a byte-identical file. |
| Loading | Structural damage is refused with a readable message. Bad values are repaired and reported. Unknown fields and unknown module types are kept and written back. Files from a newer format version open read-only. |
| Saving | Atomic: a temporary file is written and flushed beside the target, then swapped in. A crash part-way leaves the old file intact. |
| `vfxcli` | `new`, `validate`, `format`, `info`, `list`, `get`, `set`, `schema`. |

## Decisions made along the way

These are details the architecture document left open, settled here.

1. **Every property is written to the file, defaults included.** If a default
   changes in a later version, effects saved earlier keep the look they had.
2. **`id` and `type` are reserved property names.** A module stores those two
   beside its properties, so the Shape module's choice of shape is stored as
   `shape`. A test enforces the rule for every module.
3. **When to raise the format version.** Adding a new property or module type
   does not need it: old versions keep what they do not understand. The version
   is raised only when an old version would misread the file, for example when
   curves gain tangents. Old versions then open such files read-only instead of
   silently dropping the new data.
4. **Repair policy.** A value of the wrong type is replaced with the default. A
   number out of range is clamped. Curve points out of order are sorted. Each
   repair is reported, and the loaded effect is flagged so the editor can tell
   the artist before the file is saved over.
5. **A layer has at most one module of each type.** The command refuses a
   second one. A file that already has two is loaded as it is, with a note.
6. **An asset in use cannot be removed.** Clear the references first.
7. **Each effect has a frame rate** (default 30), for a timeline that shows
   frames as well as seconds and for later image-sequence export.
8. **Looping is an effect setting, not an emitter setting.** Per-layer repeat
   will be decided with the simulation in milestone 1.2.

## Known limits of this milestone

- Simple-mode controls are stored and round-trip, but there is no command to
  add, remove or rebind one yet. That arrives with the inspector (1.4). If a
  module a control points at is removed, the binding stays and simply has no
  effect until the module returns.
- Undo history is in memory only. It is not saved with the file.
- Asset files are not opened or hashed yet. Only their relative path is
  checked for portability. Hashing and missing-file handling are in 1.6.
- The maths library named in the architecture (glm) is not included yet.
  Nothing in this milestone needs it; it arrives with the simulation.
- Autosave and crash recovery are in milestone 1.6. Atomic save is done.

## How it was tested

86 test cases. The ones that matter most:

| Test | What it proves |
| --- | --- |
| Command round trip | For every command type and every property: apply then undo gives a byte-identical file; redo gives the same result as the first apply. |
| Refused edits | Every invalid edit returns a readable error and leaves the file byte-identical. |
| Random edit sequences | Long random runs of edits, drags, cancelled drags, undo and redo. After every step the document saves, reloads cleanly and saves back identical; every undo and redo lands exactly on the earlier state. |
| Format round trip | Save, load, save is byte-identical, including unusual numbers and non-English text. |
| Forward compatibility | A file with fields and a module type from a future version loads, and saves back byte-identical. |
| Loader fuzzing | Hundreds of thousands of damaged files, both random byte damage and well-formed files with hostile values. None may crash; any that load must then save and reload cleanly. |
| Atomic save | Overwrites cleanly, leaves no temporary files, keeps permissions, and a failed save leaves the old file untouched. |

Scale up the two heavy tests with environment variables:

```sh
VFX_FUZZ_ITERATIONS=300000 VFX_RANDOM_SCALE=20 ./build/release/tests/vfx_tests
```

## What has and has not been verified

| Check | Status |
| --- | --- |
| Linux, GCC 13, debug and release | Built with no warnings; all tests pass |
| Extra-strict warnings (`-Wconversion`, `-Wsign-conversion`, `-Wold-style-cast`) | Clean in project code |
| Address and undefined-behaviour sanitizers, with leak detection | Clean |
| Heavy run: 300,000 fuzz iterations per fuzz test, 800 random sequences, one 20,000-step sequence | Passed |
| Linux on Arm (the Mac's own processor, inside a virtual machine), GCC 11 | Built with no warnings; all tests pass with exactly the same assertion counts as the Intel build |
| macOS (Apple Clang) | **Not yet built.** The macOS-only code is the `F_FULLFSYNC` flush in `FileIO.cpp`. |
| Windows (MSVC) | **Not yet built.** The Windows-only code is the save path in `FileIO.cpp` and the wide-character entry point in `vfxcli`. |
| CI workflow (`.github/workflows/ci.yml`) | **Written but never run.** It needs the repository to be on GitHub. |

On a Mac, double-click `Build VFX Forge.command` to build and run the tests;
the result is saved to `build-log.txt`.

The first thing to do with this milestone is build and run the tests on a Mac
and a Windows PC. Until that is green, 1.1 is not finished by the project's
own rule that every milestone compiles and runs on both platforms.

## Next: milestone 1.2

The compile step (document to immutable program), the fixed-step CPU
simulation with seed-derived randomness, the playback clock with seek, and
`vfxcli simulate` and `vfxcli benchmark`. Still no window and no GPU.
