# The Advanced view (0.11)

Until now a layer showed a handful of Simple controls and everything else
could only be reached by opening the file. The Advanced view shows **every
setting of the selected layer** and gives the two kinds of value that had
no editor at all, curves and colour gradients, editors of their own.

The switch is at the top of the Controls panel: **Simple | Advanced**. The
Simple view is unchanged, except that a control whose value is a curve now
shows the curve editor instead of a note saying it could not be edited.

## What is listed

The settings are in groups; click a group's heading to fold it away.

| Group | What is in it |
| --- | --- |
| Timing | When the layer starts and how long it emits |
| Emission | Amount per second, and the list of bursts |
| Shape | Where particles appear: point, circle, rectangle, sphere, box or cone; its size and position |
| Initial State | Lifetime, speed, direction, spread, size, rotation, colour |
| Motion | Gravity, drag, spin, turbulence and its swirl size and change |
| Over Lifetime | Size, colour and opacity from birth to death; dissolve |
| Sprite | Blend, glow, facing, shape, align, stretch, trail, lie flat |
| Sound | The layer's sound, as in the Simple view |

Not listed separately, because a richer control already looks after them:
a layer's picture and its sprite-sheet grid (in the Shape control), and
the settings of its sound (in the Sound control).

Each kind of value has its own editor:

- **A number**: a slider and a box. Values past the slider's ends can be typed.
- **A number that can vary** (lifetime, speed, size, spin, amount...): a
  choice of **Fixed**, **Random** (a range each particle picks from) or
  **Curve**. Changing the choice keeps the look as near as it can: a curve
  made from a number starts as a level line at that number.
- **Three numbers** (gravity, position, a rectangle's size): across, up
  and toward you. A flat effect shows the first two.
- **A choice**: buttons.
- **Bursts**: a list of "at this moment, let go this many", with Add and
  remove.
- **A curve** and **a colour gradient**: below.

Every change is one undo step, and a drag is one step however far it goes.

## The curve editor

A graph of the value. For Over Lifetime settings and Spin it runs from a
particle's birth to its death; for Amount, and for Lifetime, Speed, Size
and Rotation, it runs from the layer's start to its end (particles born
later get later values).

- **Drag a point** to move it. **Double-click** the graph to add one, or
  use **Add a point**. Select a point to type its time and value exactly,
  or to remove it. Up to 32 points.
- A point is a **corner** (square) or **smooth** (round). The curve runs
  straight between two corners. Through a smooth point it bends: it eases
  to a stop where the point is an end, a peak or a dip, and passes through
  without a kink where it is on the way up or down.
- A bent curve **never swings past its own points**. A fade between 0 and
  1 stays between 0 and 1 however its points are smoothed.
- The graph fits itself round the curve when you let go. To go higher than
  the top of the graph, type the value.
- **What is drawn is what plays.** The line comes from the same function
  the simulation and the exports use (`flattenCurve`), which turns each
  bent stretch into twelve short straight pieces.

### Ready-made shapes

Fifteen, each set to stand as high as the curve does now, so applying one
to a size that peaked at 1.6 gives a shape that peaks at 1.6:

- **Level**: Steady, Fade out, Fade in, In-hold-out.
- **Ease** (the same journeys, with weight): Slow start, Slow stop, Slow
  start and stop, Ease away, Quick drop, Linger then go.
- **Life**: Pop and settle (overshoot), Wind up then go (anticipation),
  Swell, Pulse, Flicker.

These are the first of the animation principles; the next version builds
on them.

## The colour-gradient editor

A strip of colour with stops along it; under it, a thin strip showing
opacity (white is solid).

- **Click** a stop to choose it, **drag** it to move it, **double-click**
  the strip to add one where you clicked (it takes the colour already
  there). Up to 32 stops.
- The chosen stop has hue, colour strength and lightness sliders, an
  **Opacity**, and a **Brightness** from 1 upward. Above 1 the stop is
  brighter than the screen's white: that is what makes the start of a
  flame white-hot when the layer glows. The strip shows such a stop as
  bright as a screen can, so 2 and 3 look alike there; the preview shows
  the difference.
- The gradient multiplies the layer's own Colour, as before, so changing
  that one colour still re-hues the whole effect.

## In files

A smooth point is written with a third entry: `[0.18, 1.0, "smooth"]`. A
corner is written as it always was, `[0.18, 1.0]`, so an effect with no
smooth points is byte for byte what 0.10 would have saved, and plays
exactly as it did. An effect that does use smooth points will not open
correctly in 0.10 or earlier: that curve is put back to its default there,
with a note.

## Unity

A bent curve is sent to Unity as the same short straight pieces the
preview plays, so it matches to within the thickness of the line.

## Limits

- **One curve per value.** The big tools can also pick, per particle,
  between two curves; not built.
- **No handles to pull.** A point is a corner or smooth, and how the curve
  bends follows from that. You cannot drag a tangent to make a bend
  sharper on one side. Add a point instead.
- **The graph has no zoom or pan**; it fits the curve.
- **Swirl Change** and the rest are plain sliders with no curve: only the
  values listed above as "can vary" take curves, because only those are
  read over time by the simulation.
- The layer's name, on/off and lock stay in the layer list on the left.
- The **Expert view** (a node graph) is not built.
- Checked by tests of the tools behind it, and by the app opening the
  view, applying a shape, turning a number into a curve and reading a
  gradient on the build machines, with pictures. **No person has dragged a
  point yet.**

## Tests

`tests/test_compile.cpp` (how smooth points bend; a layer plays the bent
curve; corners untouched), `tests/test_serialize.cpp` (the saved form),
`editor/tests/test_curve_tools.cpp` (every shape, fitting a shape to a
value, tidying curves, gradients and bursts, the full list of settings and
changing each kind), `editor/tests/test_unity.cpp` (bent curves in Unity).
