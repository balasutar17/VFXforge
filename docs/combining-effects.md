# Combining effects (0.18)

Bala asked how to put several 3D effects into the one he was working on.
0.17 could already do it, with a small "Add to my effect" button that only
showed when the mouse was over a card, and the added layers arrived loose:
nothing kept them together, so moving or timing an added effect meant
changing every layer of it by hand.

## Adding

In the **Library**, every card now has a **+ Add** button. Clicking it puts
that effect into the one you have open; clicking the card itself still
opens it as a new effect. One Undo takes the whole addition out again.

- The added layers are a **group**, named after the effect ("Guardian Orbs
  3D"; "Guardian Orbs 3D 2" for a second one).
- A looping effect added to a looping effect that lasts a whole number of
  times as long is **played that many times over**: Guardian Orbs (2 s) in
  Portal 3D (4 s) goes round twice, instead of stopping half way.
- When the added effect lasts longer than yours, yours is made as long, so
  none of the new one is cut off. (Your own layers then play once in the
  longer loop: lengthen or repeat them yourself.)

## Groups

Under **Layers**, a group has a heading: click it to fold the group shut,
use its tick box to hide or show all of it, and × to remove all of it.
Select any layer of a group and the **Group** controls appear under the
layer buttons. Each click is one undo step:

| Control | What it does |
| --- | --- |
| Move ← → ↑ ↓ (and Nearer, Further in 3D) | Moves every emitter of the group a quarter of a unit |
| Starts − 0.1 s / + 0.1 s | Starts the whole group earlier or later, for "slam, *then* the portal opens, *then* the orbs". The effect grows to fit; nothing starts before the effect does. |
| Size Smaller / Larger | The whole group 10% smaller or larger, particles and how far they go, about the middle of its emitters |
| Ungroup | The layers stay, no longer as a group |

All of these are ordinary edits to ordinary settings (Position, Start,
Size, Speed...), so the result exports to Unity like any other effect. A
group is saved in the `.vfx` file (`"group"` on each layer, written only
when set, so older files are unchanged); Unity gets the layers as before.

## Not done yet

- No **Turn** for a group in 3D.
- Groups cannot be nested, and a layer you make yourself cannot be put
  into a group (Copy of a grouped layer stays in its group).
- When a group is longer than your effect, your own layers are not
  repeated to fill the longer loop.
- Adding a 2D effect into a 3D one: it stands upright, facing the front.
- Seen working only by tests and the build machines' screenshots so far.

## Tests

`editor/tests/test_presets.cpp`: an added effect is a group, numbered when
added twice, repeated to fill a longer loop; moved (and undone), delayed
with the effect growing to fit and never before the start, sized about its
middle, hidden, removed and brought back, ungrouped; unknown groups
refused; groups saved and read back. The app's self-test adds Guardian
Orbs 3D to Portal 3D, moves, delays and sizes it, and photographs the 3D
cards and the groups.
