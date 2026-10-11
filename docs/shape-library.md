# The shape library (0.13)

Until now a layer could draw one of 35 shapes, or a picture you painted
yourself. There was a soft dot but no triangle, a star but no coin, a flame
but no leaf. From 0.13 there are **143 shapes**, sorted into nine groups,
and a picker that lets you find one by name.

| Group | Shapes |
| --- | --- |
| **Basic** (21) | Soft dot, Disc, Ring, Square, Diamond, Triangle, Pentagon, Hexagon, Octagon, Oval, Capsule, Half disc, Wedge, Plus, Cross, Frame, Hex frame, Arc, Dashed ring, Double ring, Soft ring |
| **Stars and sparkles** (21) | Sparkle, Star, Burst, Glint, Twinkle, Flare, Rays, Star flash, Four-point star, Six-point star, Eight-point star, Plump star, Star outline, Pinwheel, Sunburst, Pow, Bang, Speed lines, Lens flare, Bokeh, Glow diamond |
| **Smoke and clouds** (8) | Smoke, Puff, Cloud, Puffball, Dust cloud, Wisp, Smoke ring, Steam |
| **Fire and light** (14) | Streak, Flame, Blaze, Bolt, Sliver, Beam, Fireball, Ember, Flame tongue, Spark, Zigzag, Light cone, Halo, Glow bar |
| **Water and nature** (15) | Bubble, Drop, Splat, Droplet, Splash, Ripple, Wave, Leaf, Petal, Flower, Clover, Snowflake, Feather, Pebble, Rain streak |
| **Candy and rewards** (21) | Heart, Orb, Candy, Swirl, Bean, Coin, Gem, Emerald, Crystal, Crown, Trophy, Key, Gift, Ribbon, Confetti, Balloon, Donut, Bonbon, Medal, Music note, Cherry |
| **Magic** (19) | Shockwave, Shard ring, Vortex, Sigil, Pentacle, Triad, Spiral, Hex shield, Eye, Moon, Wing, Skull, Octagram, Portal ring, Crosshair, Yin and yang, three Runes |
| **Hits and debris** (12) | Crescent, Shard, Slash, Claw marks, Crack, Impact lines, Rock, Splinter, Glass shard, Gear, Bone, Brick |
| **Symbols** (12) | Arrow, Chevron, Tick, Exclamation, Question, Lightning, Shield, Sword, Paw, Sleep, Infinity, Lock |

## Choosing a shape

The **Shape** control (Simple view, and Advanced > Sprite) now has:

- **A search box.** Type part of a name ("star", "ring", "rune") and every
  shape with that in its name is shown, from every group. Esc clears it.
- **The groups**, as a row of buttons. One group shows at a time; when you
  select a layer it opens on the group that layer's shape is in.
- **The shapes of that group**, as before: click one. Hold the pointer over
  a shape for its name.

Everything else about a layer works on a library shape exactly as on the
old ones: Color tints it, Glow and Blend light it, Size, Squash and Stretch,
Rotation, Spin, Align to movement, Lie Flat, Dissolve, Trails, Hold Frames.

## How they are drawn

- **Each takes the layer's colour.** A shape is not a coloured picture: it
  says where the particle is covered, where it is lit or in shadow (toon
  shading, a lighter and a darker step of your colour) and where it shines
  (toward white). A gold Color gives a gold coin with a pale rim and a
  shine; a green one gives an emerald coin.
- **The plain figures are plain.** Triangle, Pentagon, Hexagon, Octagon,
  Oval, Capsule, Half disc, Wedge, Plus, Cross, the frames and rings are one
  flat colour with no shading at all, so they take a colour exactly and
  work as building blocks.
- **Things that fly point along +x**, like the Streak: Arrow, Chevron,
  Fireball, Rain streak. Set Align to movement and they point where they
  are going. The Buff Up preset turns its chevrons a quarter
  turn (Rotation 90) to point them up instead.
- **Glowing line work** (the runes, Zigzag, Spiral) has a soft halo round
  a crisp line and is made to be used with Blend: Glow.

## How it works

The first 35 shapes are formulas written twice: once for the picture
renderer and once, in shader language, for the graphics card, and tests
keep the two in step. Writing a hundred more that way would have meant a
hundred more chances for the two to disagree, in a language the build
machines are the only place to check.

So a library shape is written **once**, as a formula in
`editor/src/ShapeLibrary.cpp`. The picture renderer (and so `vfxshot`,
exported frames and sprite sheets) works the formula out directly, at any
size, perfectly sharp. The app's live view cannot run that code on the
graphics card, so the first time a shape is used it is drawn into a
512 by 512 picture, and the graphics card draws the particle from that
picture, colouring it with the same sums as the old shapes. A shape costs
nothing until it is used. The library's cards and the picker's icons use
much smaller pictures.

A shape's place in the list is its number. The first 35 keep the numbers
they have always had, and new shapes are only ever added at the end, so a
file never means a different shape tomorrow.

## Presets

Eight new presets are built on the library, five of them in a new
category, **Status**, for the marks that float over a character's head:

- **Buff Up** (Status): green chevrons shooting up and coasting, with plus
  signs.
- **Sleep** (Status): Zs drifting up, wobbling, on threes.
- **Poisoned** (Status): a skull popping out of sickly fumes, with bubbles
  and drips.
- **Stunned** (Status): five stars going round a dizzy spiral. Each pops
  out of the middle and is carried once round by Orbit, so they stay a
  fifth of a turn apart however long it plays.
- **Alert** (Status): an exclamation mark that lands flat, springs up and
  settles, in a comic-book bang.
- **Trophy Win** (Rewards): a trophy bouncing in on golden rays, with
  ribbons, paper and stars.
- **Falling Leaves** (Weather): autumn leaves turning over as they fall
  (Squash and Stretch does the turning).
- **Rune Ward** (Ground and portals): a seal turning on the floor, runes
  rising inside a hexagon shield.

Six older presets now use the shapes they always wanted: Coin Burst throws
coins, Confetti throws paper and ribbons, Gem Shatter breaks into glass
shards, Snow has snowflakes near the camera, Healing Aura has crosses
rising in it and Level Up has chevrons. That makes 78 presets.

## In files

Nothing new: the sprite module's `"shape"` holds the shape's name, as it
always has (`"shape": "leaf"`). An effect saved by 0.12 opens unchanged. An
effect that uses a library shape opens in 0.12 and earlier with that
layer's shape reported as unknown and put back to the soft dot; the app
says so when it opens the file.

## Unity

The 35 formula shapes travel as before, in one picture
(`Textures/VFXForgeShapes.png`). A library shape travels as a picture of
its own, and **only the ones the effect uses are sent**:

```text
Assets/VFXForge/Shapes/leaf.png
Assets/VFXForge/Shapes/trophy.png
```

Each is 512 by 512 and laid out like one tile of the big picture (toon
tone in red and blue, shine in green, coverage in alpha), so the VFX Forge
particle shader draws it with no change: the importer simply tells the
layer's material that its picture has one column and one row. The importer
also sets these pictures' import settings (not colour, no compression);
leave them as they are. Exporting another effect that uses the same shape
writes the same file again.

- **Not run inside Unity.** The build machines compile the importer
  against two Unity versions; they do not open a project or look at a
  prefab. The picture, the description and the importer's logic are tested
  here, but nobody has yet seen a library shape drawn by Unity.

## Limits

- **A library shape softens when it is very large.** In the app's live
  view it is a 512-pixel picture: a particle drawn wider than about 500
  pixels on screen has slightly soft edges, where the 35 formula shapes
  stay sharp at any size. Exported frames and sprite sheets are not
  affected (the picture renderer uses the formula), and neither is
  `vfxshot`. In Unity the same 512-pixel picture is used.
- **Reference to VFX only knows the first 35.** A reference clip of
  falling leaves is rebuilt from the nearest of the old shapes; choose Leaf
  afterwards.
- **The shapes cannot be changed yet.** A star has five points and a gear
  has eight teeth. Shapes you can edit (how many points, how thick, how
  round) are the next step, and the library was built for it: a shape
  whose formula takes settings only needs its picture drawn again.
- **No pictures of things in colours of their own.** Every shape is one
  colour with shading. A red cherry with a green stalk, or a gold trophy
  on a brown base, needs two layers or a picture you painted.
- **Some are busy at small sizes.** Snowflake, Pentacle, Hex shield and
  the line work want to be at least thirty pixels across to read.
- **The first use of a shape takes a moment** while its picture is drawn:
  a few hundredths of a second for most, measured on the machine this was
  written on, and over a tenth for the most intricate (the Infinity sign).
  After that it is kept.
- Seen in pictures drawn by the picture renderer, and in the app's own
  screenshots on the build machines. **No person has used the picker in
  the app yet.**

## Tests

`editor/tests/test_shape_library.cpp`: the library's list is the Shape
setting's list, in order, with a readable name and a group for every
shape; every library shape stays inside its square, has a solid part and
gives sane numbers everywhere; a few dozen spot checks that shapes are
what they claim (a triangle points up, a doughnut has a hole, an arrow
points along +x); the plain figures have no shading and the treasures do;
a shape's picture is the formula pixel for pixel, is drawn once and kept;
the mesh sends each library shape to its own picture, in layer order; the
picture renderer draws them the right way up; a layer keeps a library
shape through save, load and undo; Unity is sent exactly the pictures an
effect uses, laid out as its shader expects; the new presets use the
shapes they say.

The app's self-test opens Trophy Win, checks what the panel offers for
Shape, and takes pictures of the preset and of the picker on each build
machine.

`vfxshot library out.png` draws every shape and lists each one's number,
name and group; `--category "Magic"` draws one group.
