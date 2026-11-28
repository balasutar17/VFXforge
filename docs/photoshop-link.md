# Photoshop link (0.52)

Paint in Photoshop and see it on the particles in VFX Forge, without
exporting and importing by hand. Mac and Windows; not on the iPad.

## Edit in Photoshop (no plugin needed)

In the Shape section of a layer:

- **Paint in Photoshop** (a layer still drawing a built-in shape): VFX Forge
  writes the shape as a white, lightly shaded Photoshop file, puts it on the
  layer and opens it in Photoshop as a starting point.
- **Edit in Photoshop** (a layer with a picture): opens the picture in
  Photoshop as a layered .psd. A sprite sheet keeps its grid in the file name.

Paint, press Cmd+S (Ctrl+S), and the layer updates in VFX Forge each time. The
files live in **Documents › VFX Forge Link**.

## Following any picture file

A picture chosen with **Use my own picture…** is followed too: when the file
is saved again, by Photoshop or any other app, the layer updates. The panel
shows "Follows fire.psd: saving it updates this layer", with **Stop
following**. Following lasts while VFX Forge is open. Each update is one
step you can undo.

## Photoshop files

**Use my own picture…** takes .psd and .psb files, RGB or greyscale, 8 or 16
bits a channel. VFX Forge reads the flattened copy Photoshop keeps in the
file, so layer styles (glows!), adjustment layers and blend modes look as
they do in Photoshop. If Maximize PSD and PSB File Compatibility is turned off
(Photoshop › Settings › File Handling), there is no flattened copy: VFX Forge
then puts the visible layers together itself as Normal layers and says that
styles and adjustments are missing.

## The VFX Forge Link panel

Install `VFX-Forge-Link.ccx` from the latest release (double-click it), then
open it from Plugins › VFX Forge Link. The first send asks once for the folder:
choose **Documents › VFX Forge Link** (VFX Forge makes it when it starts).

- **Send the whole picture**: as Photoshop shows it, saved as `<document>.png`.
- **Send the selected layer**: that layer or group alone, on the document's
  canvas, as `<layer>.png`.
- **Send frames as a sprite sheet**: every layer in the selected group (or
  every top-level layer) is a frame, the bottom one first, laid out without
  empty cells when the count allows, as `<group>_4x2.png`. VFX Forge sets the
  grid from the name.

In VFX Forge, a new picture goes onto the selected layer. Sending the same
name again updates every layer using it, whichever layer is selected.

## How it was checked

The .psd reader is tested against files made by psd-tools, an independent
library (raw, RLE and ZIP layers, opacity, hidden layers), and psd-tools reads
the files VFX Forge writes. The app's self-test, run on Mac, Windows and Linux
for every build, loads a .psd, saves a new version over it the way Photoshop
does, and checks the layer follows; it also drops a sheet into the link folder
and checks it lands on the layer with its grid.
