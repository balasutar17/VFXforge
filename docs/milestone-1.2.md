# Milestone 1.2: the simulation

The third of Phase 1's seven milestones. It adds the compile step, the CPU
particle simulation, the playback clock and two command-line commands. There
is still no window and no GPU code; drawing starts in milestone 1.3.

## What works

| Area | What it does |
| --- | --- |
| Compile step | Turns each enabled layer into an immutable `EmitterProgram`: plain numbers, defaults filled in, units converted, limits worked out. The simulation never reads the document. |
| Incremental compile | `LiveProgram` listens to the document's change records. An edit recompiles only the layer it touched; untouched layers keep the very same program object. |
| Simulation | Fixed steps of 1/60 s. Emission (steady rate, bursts), six shapes, direction and spread, gravity, drag, spin, lifetime. One array per particle field; no memory is allocated while stepping. |
| Frame output | `extract` fills a `RenderFrame`: one batch per drawn layer and one plain instance per particle. Size, colour and opacity over lifetime are worked out here, only when a frame is wanted. |
| Scrubbing | `seek` goes to any step, forwards or backwards. |
| Live editing | `setProgram` swaps in an edited program at the current moment. Unchanged layers keep their particles; changed layers catch up. |
| Snapshots | The whole state can be copied and restored, ready for a checkpoint cache. |
| Playback clock | Play, pause, stop, loop, scrub, frame step and time scale. It owns no timer and no thread, so it is exact to test. |
| `vfxcli simulate` | Runs an effect with no window and prints particle counts and fingerprints. |
| `vfxcli benchmark` | Times the simulation on a file, or on a stand-in effect of a chosen size. |

## The one promise

**What you see at a moment depends only on the effect and the moment.** Not
on how playback got there, what was edited before, how fast frames arrived,
or which machine it is running on.

Three things make that true.

1. **Fixed step.** The simulation only ever advances in whole steps, and each
   step's time comes from its number, never from adding steps up.
2. **Hashed randomness.** Every random number is a hash of the effect's seed,
   the layer's ID, which pass of the effect it is, the particle's number and
   which property it is for. There is no shared generator whose state could
   depend on timing. Layers cannot affect each other.
3. **Portable maths.** Plain arithmetic and square roots give the same bits
   everywhere. The system's sin, cos, pow, exp and cbrt do not, so the
   simulation never calls them. `DetMath` provides replacements built only
   from portable operations. The build also forbids fused multiply-add, which
   would make Arm and Intel round differently.

This is stronger than the architecture document first asked for. It aimed
for "visually identical, tested with a tolerance" across platforms. The
simulation is now built to be bit-identical, and a test pins exact
fingerprints so that any platform that differs fails loudly.

## How the simulation behaves

These are the rules an artist will feel, settled in this milestone.

- **Looping keeps particles alive across the wrap.** Time keeps counting, pass
  after pass; the emitters see time wrap, the particles do not. A looping fire
  has no gap at the seam.
- **Each pass emits on its own.** What a pass emits depends only on which pass
  it is. That is what lets playback jump a year ahead instantly: only the last
  lifetime's worth of passes needs simulating, and the result is exactly what
  the long way round gives.
- **Each pass gets fresh random numbers,** so a looping burst varies naturally
  from pass to pass. An option for identical passes (for seamless sprite-sheet
  loops) belongs with export in Phase 5.
- **A one-shot effect stops emitting at its end.** Particles finish their
  lives; the clock stops at the end of the timeline.
- **Scrubbing shows the first pass,** so the same spot on the timeline always
  shows the same picture.
- **A layer emits only inside its own start and duration,** and never past the
  effect's end.
- **Curves on birth values** (lifetime, speed, size, rotation, and the rate)
  read across the layer's duration. **Curves on Over Lifetime and Spin** read
  across the particle's life. A fixed number or random range in Over Lifetime
  is a per-particle multiplier chosen at birth.
- **Particles born within one step are spread through it,** so a high rate
  does not pulse in bands.
- **Draw order is birth order.** Survivors close up in order, so overlapping
  transparent particles never flicker.
- **2D is 3D with depth locked to zero.** A sphere becomes a disc, a box a
  rectangle, a cone a fan.
- **Cone** emits from a disc across the direction, straight ahead at the
  centre and tilted by the cone angle at the rim. Spread is then applied on top.
- **Each missing module has a meaning.** No Emission: nothing is born. No
  Shape: a point. No Initial State: that module's defaults. No Motion: no
  forces. No Over Lifetime: nothing changes with age. No Sprite: simulated but
  not drawn.
- **Each layer holds at most 200,000 particles.** Past that, new particles are
  not created and the layer reports how many were dropped.

## Known limits of this milestone

- The simulation runs on whichever thread calls it. The worker thread and the
  hand-off to the renderer arrive with the renderer in 1.3.
- A layer has no position or rotation of its own yet; every emitter sits at
  the origin.
- Curves are straight lines between keys. Tangents arrive with the curve
  editor in Phase 2.
- Drag divides the speed each step rather than using an exponential. It is
  stable at any strength and needs no maths library, at the cost of matching
  the textbook formula only approximately.
- A particle's age is a 32-bit number added to every step. Over lifetimes of
  many minutes it can run up to about 1% slow. This is the same on every
  machine and so does not affect repeatability.
- When a layer is full, the instant-jump shortcut for looping effects is not
  guaranteed to match the long way round exactly, because which particles were
  dropped depends on history.
- Reverse and ping-pong playback are not here; they need the checkpoint cache.

## How it was tested

163 test cases in all, 77 of them new. The ones that matter most:

| Test | What it proves |
| --- | --- |
| Exact motion | A particle under gravity, and one under drag, match the step rule to the last bit over many steps. |
| Jump equals play | Jumping to a step gives the same fingerprint as playing to it, for one-shot and looping effects, in 2D and 3D, including random scrubbing. |
| Entering a loop late | Jumping to a late pass, and a year into playback, matches the long way round. |
| Live editing | After each edit of a slider drag, the simulation equals a fresh one of the edited effect, and the layer that was not edited kept its particles untouched. |
| Layer independence | Removing, disabling, reordering or renaming one layer changes nothing in another. |
| Shapes | Every shape stays within its bounds, fills evenly, and puts edge particles exactly on the edge. |
| Random quality | Numbers are evenly spread, and unrelated between properties, particles, passes, layers and seeds. |
| Any valid effect | Random valid values for every property, extremes included, never produce a number that is not finite. |
| Pinned fingerprints | Exact results recorded once; every platform must reproduce them. |
| Portable maths only | The simulation sources are scanned for calls that differ between platforms. |

## Speed

Measured with `vfxcli benchmark --particles N --3d`, optimised build, one
thread. "Together" is one simulation step plus building the frame data.

| Particles | Intel server, GCC 13 | MacBook Pro (Apple Silicon), in a Linux virtual machine, GCC 11 |
| --- | --- | --- |
| 5,000 | 0.09 ms (0.6% of a 60 fps frame) | 0.06 ms (0.4%) |
| 20,000 | 0.44 ms (2.6%) | 0.25 ms (1.5%) |
| 50,000 | 1.29 ms (7.8%) | 0.66 ms (3.9%) |
| 150,000 | 3.62 ms (21.7%) | 2.34 ms (14.0%) |

The architecture's target was 20,000 particles at 60 fps. The simulation's
share of that is under 3% of a frame on both machines. The Mac figures are
from inside a virtual machine, not a native macOS build. This says nothing
yet about drawing them, which is milestone 1.3.

One thing found by measuring: closing up the arrays after deaths was first
written to walk all fifteen arrays at once, and at 50,000 particles a step
took 3.1 ms. Walking one array at a time brought it to 0.58 ms with identical
results.

## What has and has not been verified

| Check | Status |
| --- | --- |
| Linux on Intel, GCC 13, debug and release | Built with no warnings; all tests pass |
| Address and undefined-behaviour sanitizers, with leak detection | Clean |
| Strict warnings including float and double conversions | Clean in the core |
| Heavy run: 200,000 fuzz iterations per fuzz test, 400 random edit sequences | Passed |
| Linux on Arm (the Mac's own processor, in a virtual machine), GCC 11 | Built with no warnings; all 163 tests pass |
| Intel versus Arm | **Bit-identical.** Every pinned fingerprint matches, `vfxcli simulate` prints the same fingerprints line for line, and the benchmark ends in the same state on both, with different compilers (GCC 13 and GCC 11) |
| macOS (Apple Clang) | **Not yet built.** |
| Windows (MSVC) | **Not yet built.** |

## Next: milestone 1.3

The renderer: sprite drawing through Qt's rendering interface, the offscreen
target, the viewport, camera and backgrounds. This is the first milestone that
needs Qt installed and a real GPU, so it has to be built on the Mac.
