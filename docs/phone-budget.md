# Phone budget

The badge at the top of the window ("Phone: Light", "OK", "Heavy" or "Too
heavy") says how heavy the effect is to draw on a low-end phone. It follows
the effect a moment after each change. Click it for the details.

## What is measured

The effect is run through once (a looping effect twice, so its second pass is
measured with the first pass's particles still alive) and looked at 30 times a
second. Three things decide most of what a particle effect costs on a phone:

| Measure | What it is | Light | OK | Heavy | Too heavy |
|---|---|---|---|---|---|
| Particles at once | the most alive at any moment | up to 150 | 400 | 1000 | more |
| Screen painted | overdraw at the busiest moment, in whole screens | up to 1.5 | 4 | 8 | more |
| Separate draws | one per layer, one more per trail | up to 6 | 12 | 24 | more |

The worst of the three is the grade. More than four real lights (Unity only)
makes an effect at least Heavy.

**Screen painted** counts the whole square of every particle, however
see-through its edges are, because a phone's GPU paints all of it: a few big
soft glows can cost more than hundreds of small sparks. A streak counts its
length and a trail its ribbon. The screen is a portrait phone 10 units tall
(a Unity orthographic camera of size 5), the usual set-up for a casual game.

These are rules of thumb for a low-end phone with the whole effect on screen.
A high-end phone, or an effect shown small, can afford much more.

## Make mobile-safe

The button in the panel makes a lighter copy of the effect, aiming for OK:

- fewer particles in the layers that have the most;
- big glows (a layer of four particles or fewer) a little smaller instead of fewer;
- shorter trails where they are heavy;
- at most two real lights a layer.

No layer loses more than half its particles or a fifth of its size, so the
effect still looks like itself; when that is not enough, the message says so.
Locked layers are left alone. It is one step to undo.

Separate draws are not merged automatically: which layers can share a
material is an artist's call. The message suggests it when draws are the
problem.

From the command line, `vfxexport budget <effect.vfx | preset-id>...` prints
the same report, and what Make mobile-safe would do.
