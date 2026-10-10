# Trails, layer positions, flashes (0.6)

## Ribbon trails

The sprite module has **Trail** (seconds) and **Trail Width** (a share of the
particle's size). Each particle draws a ribbon along the path it took: it
swells out of the particle, narrows to nothing at the end, fades out, and
burns white along its centre near the particle.

The path is not recorded. It is worked out when drawing by undoing the
simulation's own steps from where the particle is now (each step adds
gravity, divides by drag, then moves), so for every motion the simulation
has (gravity, drag) it is exactly the path taken, back to the particle's
birth at most. It costs no memory and needs no change to the simulation
state, so scrubbing and the pinned fingerprints are unaffected (a frame's
fingerprint includes trail settings only when a layer has a trail).

In Unity, a trail is the Particle System's own **Trails** module: lifetime
as a share of the particle's life (trail seconds over the average
lifetime), width over trail from the Trail Width down to 0, alpha from 1 to
0 along it, the particle's colour, and a trail material using the VFX Forge
shader's ribbon mode.

## Layer position

The shape module has **Position**: where a layer's particles appear relative
to the effect's centre. Layers that belong together (a comet's glow, head
and wisps) share a position and so travel together. In Unity it is the Shape
module's position.

## New shapes

starflash (white spikes and core in a coloured halo), sliver (a thin needle
of light), shardring (a ring breaking into pieces).

## New presets

**Flashes:** Gold, Ice, Rose, Violet and Ember Flash.
**Light and energy:** Stylized Comets, Firework Sparks; Meteor Shower now
uses trails.

## NOT IMPLEMENTED yet

- Trails for particles moved by anything other than gravity and drag (there
  is nothing else yet; when there is, its trail will need recording).
- Emitters that move along a path; magic circles and ground decals.
