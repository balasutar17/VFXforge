# Handles: move, turn and size by dragging (0.19)

Bala asked for a gizmo: handles in the viewport to move, turn and size an
effect by hand, instead of clicking buttons. Select a layer and its handles
appear at the middle of it, or of its whole group when it is in one (an
effect added from the library). **Handles** (top of the viewport, beside
Reset view) shows or hides them.

| Handle | Drag it to |
| --- | --- |
| Square in the middle | move freely: across the picture in 2D, along the ground in 3D |
| Red arrow | move left and right only |
| Green arrow | move up and down only |
| Blue arrow (3D) | move nearer and further only |
| Orange ring | turn. In 3D the ring lies on the ground and turns the effect about the upright, as a character turns |
| White square on the ring | size: drag out to make it larger, in to make it smaller |

- **Shift** while dragging: moves in steps of 0.1, turns in steps of 15°,
  sizes in steps of 10%.
- **Escape** while dragging puts everything back.
- A small label says what each handle does when the mouse is over it, and
  how far you have moved, turned or sized while you drag.
- A whole drag is **one undo step** ("Move Guardian Orbs 3D", "Turn ...").
- Dragging anywhere else still turns and moves the camera as before.
- The Group panel also has **↺ 15°** and **↻ 15°** buttons.

## What a handle changes

Ordinary settings, so the result exports to Unity like any other effect:

- **Move**: each layer's Position.
- **Turn**: each Position about the pivot, the way particles are sent
  (Direction), and the sprites' Rotation where they lie in the turning
  plane (every sprite in 2D; in 3D those lying on the ground). Gravity
  still points down.
- **Size**: particle Size, Speed, the emitter's Radius and Size, Gravity
  and swirls, with the Positions spread out from the pivot by as much.

## Not done yet

- A rectangle emitter stays square to the picture when turned (its sides
  do not turn), and a cone or circle that is not sent every way keeps its
  own facing in Unity's terms only through Direction.
- In 3D only turning about the upright: no tipping over.
- Handles for the camera-facing Lie Flat of 2D layers.
- Tested by unit tests and the build machines' self-test (which drags a
  quarter turn); not yet by a person.

## Tests

`editor/tests/test_transform.cpp`: a 2D layer turned a quarter about a
pivot (Position, Direction, Rotation), moved and doubled about it; a 3D
layer turned about the upright, with only ground-lying sprites turning;
the targets of a handle drag (the whole group, or a layer alone); a drag
of twenty updates undone as one step, a cancelled drag leaving nothing to
undo, the simulation keeping up.
