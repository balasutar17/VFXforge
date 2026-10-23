# Effects made on a model's shape (Mesh VFX)

Import a model (or press **Block** for the built-in hard-surface sample), choose a quality, press
**Generate on model**. VFX Forge reads the model, then adds a group "Mesh VFX" whose layers follow
the real geometry:

| Layer | What it follows |
|---|---|
| Outline glow / Edge outline / Edge core | hard edges (dihedral angle over 35° and open edges) |
| Silhouette outline | the outline seen from the camera at generation time |
| Energy coat, Surface ripples, Scan glow, Scan bands, Surface motes | the surface (area-even samples), with height scans and ripples from a point |
| Ground disc, Base ring, Outer ring, Ring light, Energy beams, Ground motes | the model's footprint (smallest enclosing rotated rectangle) |

**Fit to model** replaces only the geometry the layers carry; colours, sizes and timing you edited stay.
Each layer is an ordinary layer: switch it off, restyle it, change its blend.

## How it is stored
An effect never holds the model file. Each mesh layer keeps a simplified copy (up to 7,900 triangles
or edge segments) in its Shape module (`meshData`), already placed in the world.

## Unity
The exporter writes the same triangles/edges; the importer builds a Mesh and uses Unity's mesh shape
(Triangle or Edge). Limits: Unity cannot confine emission to a moving band, so scans export as the
whole surface; a silhouette is fixed to the camera it was made from. These are listed in the export notes.

## Not done yet
Glass/fresnel hologram material, per-layer keyframe timeline, the 15 animation presets, material
presets, variations, per-submesh selection, skinned meshes, progress/cancel for long analysis.
