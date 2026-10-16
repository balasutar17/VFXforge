# Handles: move, turn and size what is selected (0.19, 0.20)

The handles in the viewport (the gizmo) change **the selection and nothing
else**: one layer, a group, or layers picked one by one. Every other layer
stays exactly as it was. 0.20 follows the specification Bala wrote,
"Gizmo Handling: Individual, Group, and Multi-Layer Transformations".

## Selecting

| You do | Selected | The handles change |
| --- | --- | --- |
| Click a layer in **Layers** | that layer | only that layer, even inside a group |
| Click a group's heading | the group | every layer in it, groups inside it included |
| **Cmd-click** (Ctrl-click on Windows) a layer or heading | adds it, or takes it away | only what is selected |
| **Shift-click** a layer | every layer between the last clicked and this one | those layers |
| Click a small **dot** in the viewport | that layer (each dot is a layer's emitter; orange ones are selected) | that layer |
| Click empty space in the viewport | nothing | nothing: the handles go away |

- A layer selected twice over (on its own and in a selected group) is
  changed **once**.
- **Locked** layers are never changed. Lock a layer with Lock, or a whole
  group with the padlock on its heading. A selection of only locked layers
  shows a grey handle and says it is locked.
- Hidden layers (tick box off) are changed like any other when selected,
  so they stay in place with the rest when shown again.
- Changing the selection moves the handles at once.

## Groups

- Groups hold layers and other groups. An effect added from the Library is
  a group. **Group N layers** (Cmd+G) makes the selected layers a group,
  inside the deepest group they all share, and puts them together in the
  list. **Ungroup** (Cmd+Shift+G) takes a group away one level: its layers
  and groups move up into its parent.
- The triangle on a heading folds the group; its tick box hides or shows
  all of it; × removes all of it; the padlock locks all of it.
- In the file a layer's group is a path, `"Fire Explosion/Sparks"`.

## The handles

Choose with the buttons at the top left of the viewport, or the keys
**W**, **E**, **R**:

| Mode | Handle | Drag it to |
| --- | --- | --- |
| Move (W) | circle in the middle | move freely: across the picture in 2D, along the ground in 3D |
| | red / green / blue arrow | move along X (left-right), Y (up-down), Z (near-far) only |
| Turn (E) | ring (2D) | turn in the picture |
| | red / green / blue ring (3D) | turn about X (tip forward), Y (the upright, as a character turns), Z (tip sideways) |
| Size (R) | square in the middle | everything larger or smaller: particles and how far they go |
| | red / green / blue box | stretch along that axis only: the emitters spread apart that way and the emitter's own width, height or depth changes; the particles keep their size |

- **Pivot** (what Turn and Size happen about): **Middle** of everything
  selected, the **Active** layer (the one clicked last, with the orange
  border), or **Each** layer about its own position.
- **Option-drag** the middle to move the pivot only; nothing else moves.
  **Reset pivot** puts it back. It also goes back when the selection changes.
- **World / Local**: the world's axes, or the active layer's own: up is
  the way it sends its particles. Size always works along the world's axes.
- **Shift** while dragging snaps (0.1 units, 15°, 10%). **Escape** puts
  everything back. One drag is one Undo step, and Redo brings it back
  exactly.

## What a handle changes

Ordinary settings, so the result exports to Unity like any other effect:
Position (moved, turned and spread about the pivot), Direction (turned),
Rotation of sprites lying across the turning axis (every sprite in 2D; in
3D those lying on the ground for the upright, those standing flat for Z),
Size, Speed, the emitter's Radius and Size, Gravity and swirls (Size).
Gravity always points down.

## Where this differs from a 3D program, and why

- **A layer has no rotation or scale of its own** to show. It is an
  emitter with a Position and a Direction, so "Local" axes come from its
  Direction, and turning a layer changes where it is and which way it sends
  particles rather than an angle stored on it.
- **An emitter lying on the ground stays on the ground** when tipped about
  X or Z: its particles' direction tips, the circle it emits from does not
  (only upright or ground exist; see [3d-effects.md](3d-effects.md)).
- **A rectangle emitter stays square to the axes** when turned.
- **Stretching** spreads emitters and changes emitter extents; particles
  keep their shape (a particle has one size, not a width and a height).
- **Selection is not highlighted in the picture** itself; the dots and the
  handles show it. Particles of selected layers are not tinted.
- Seen working by the unit tests and the build machines' self-test; not
  yet by a person.

## Tests

`editor/tests/test_selection.cpp`, one test for each acceptance test of
the specification: one layer moved, turned and sized changes only it (and
undoes exactly); a group, groups inside it included, nothing outside, its
layers keeping their places relative to each other; two layers from
different groups; a group and its own layer moved once; locked layers left
alone, in groups and by the group buttons; undo and redo after a dragged
tip about X with sizing in 3D; the three pivots; tipping about X and Z;
stretching along one axis; turning about any axis (local); grouping into
the shared group, side by side, and ungrouping one level; half of a
thousand layers moved quickly. `editor/tests/test_transform.cpp`: the
arithmetic. The app's self-test selects one layer of a group (one layer
reached), Cmd-clicks a second (two), moves them and checks that exactly
two layers changed.
