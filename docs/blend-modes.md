# Blend modes

Each layer has a **Blend Mode** (in its Sprite settings): how its particles are
laid over what is behind them, with Photoshop's list in Photoshop's groups.
**Normal** covers what is behind; **Linear Dodge (Add)** adds light, so
overlaps glow (it was called *Glow* before); the others do their sums with the
colour behind each pixel.

Like Photoshop's layer opacity, a particle's own alpha mixes the result in:
a faint particle in Multiply darkens a little, not all the way.

## What draws what

| | Normal, Linear Dodge (Add), Screen, Multiply, Exclusion | the other 22 |
|---|---|---|
| Live preview | exact (the graphics card's own blending) | drawn as Normal for now |
| Exported frames, sheets, pictures | exact | exact |
| Unity | Normal and Linear Dodge (Add) exact; Screen as Add; Multiply and Exclusion as Normal | the lightening modes (Lighten, Color Dodge, Lighter Color) as Add; the rest as Normal |

The menu marks the modes the live preview cannot draw yet with ≈, and a line
under the menu says what the preview and Unity do with the chosen mode. After
an export to Unity, the message lists the layers whose mode Unity can only
approximate.

The live preview can do five modes because the graphics card blends from the
particle and the pixel under it alone (`applyBlend` in `app/src/SpriteNode.cpp`).
The rest need the pixel behind to do sums with it. Frames are drawn by the
picture renderer instead, where every mode is a function of the particle's
colour and the pixel (`editor/src/Blend.cpp`), so they are always exact.

## How a mode is applied

Colours are screen colours (as in Photoshop), a particle is blended in the
order its layers are listed, and each pixel of it is laid over the picture
made so far. The five card modes use exactly the card's formulas
(`p` is the particle's colour times its alpha `a`, `d` the pixel):

| Mode | Result |
|---|---|
| Normal, Linear Dodge (Add) | `p + d (1 - a)` (Add has a = 0) |
| Screen | `p + d (1 - p)` |
| Multiply | `p d + d (1 - a)` |
| Exclusion | `p (1 - d) + d (1 - p)` |

The others use Photoshop's formulas on the particle's straight colour `s`,
and mix the answer in by alpha: `d (1 - a) + a B(d, s)`. Dissolve shows the
whole particle at the pixels where a per-pixel random number is below its
alpha, and leaves the rest. The colour modes (Hue, Saturation, Color,
Luminosity) use the W3C compositing formulas, which are Photoshop's.

## In the file

`blend` on the Sprite module, one of `alpha` (Normal), `dissolve`, `darken`,
`multiply`, `colorburn`, `linearburn`, `darkercolor`, `lighten`, `screen`,
`colordodge`, `additive` (Linear Dodge (Add)), `lightercolor`, `overlay`,
`softlight`, `hardlight`, `vividlight`, `linearlight`, `pinlight`, `hardmix`,
`difference`, `exclusion`, `subtract`, `divide`, `hue`, `saturation`, `color`,
`luminosity`. Older files only use `alpha` and `additive`, and open as before.

## Still to do

The live preview for the 22 modes that need the pixel behind (it will draw a
layer's particles separately and combine them with what is below in a shader),
and Unity: matching more than two modes needs a grab of the scene behind the
particles, which its gamma-buffer renderer feature does not have yet.
