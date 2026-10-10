# Sound (0.7): phase 1 of the VFX + sound studio

## What works

- **A sound on any layer.** The Sound section (under the Simple controls)
  gives the selected layer a Sound module: a sound played when the layer
  starts, or at every burst of its particles. Delay (negative plays early),
  volume, pitch (semitones), pan, random pitch, random volume, fade in and
  out, trim start, length, loop until the layer ends, mute. Every change is
  an ordinary undoable edit and is saved in the `.vfx` file.
- **Exact sync.** When a sound plays is worked out from the compiled effect:
  the same layer start, length and burst times the particle simulation uses
  (`soundEvents` in `editor/include/vfx/editor/Audio.h`), never estimated.
  Randomness is a hash of the effect's seed, the layer, the pass and the
  play, so a preview, an export and a replay sound the same.
- **Preview.** The app keeps about 0.12 s of mixed sound queued ahead of the
  playback clock. A seek, restart, loop, pause or speed change, or drift of
  more than 0.06 s, throws the queue away and refills it from the clock, so
  sound stays within a few hundredths of a second of the picture. At other
  speeds sound plays faster or slower, higher or lower, like a tape. A
  "Sound" switch in the playback bar silences everything.
- **Import.** WAV (8/16/24/32-bit, float, extensible) and AIFF/AIFC are read
  by VFX Forge itself; MP3, OGG, FLAC, M4A and others go through the
  system's decoder. Each sound is kept as a WAV in `sounds/` next to the
  effect (`sounds/<name>-<fingerprint>.wav`); the original file is never
  touched. Missing files are reported.
- **The sound library.** 18 sounds made by code from waves, noise, filters
  and envelopes (`editor/src/SoundLibrary.cpp`): pop, bubble, coin, collect
  chime, sparkle, level up, jelly splat, magic shimmer, whoosh, boom,
  explosion, hit, zap, laser, electric hum (loop), fire crackle (loop), rain
  (loop), thunder. They are original, belong to this project, and may be
  used in games freely. A library sound is stored as `sounds/lib-<id>.wav`
  and made again whenever it is missing. 30 of the presets now come with
  sound.
- **Export.** Frames export also writes `<name> sound.wav`: the same pass,
  mixed down. Export to Unity writes the sounds to `Assets/VFXForge/Sounds`
  and gives each layer with a sound a `VFXForgeSound` component
  (`Runtime/VFXForgeSound.cs`) that plays it at the exported moments of
  each pass, in the game as well as the editor, with the same settings.

## NOT IMPLEMENTED yet (phase 2 and 3)

- A multi-track audio timeline with draggable clips, splitting, markers,
  solo, and waveforms on the timeline itself.
- Events other than layer start and bursts (particle death, collision).
- Volume automation, 3D/spatial sound, buses, voice limits in the app.
- Hearing sound while scrubbing.
- Video export; exporters for Unreal, Godot and Blender.
- Not yet checked by ear on a real machine: the library sounds were judged
  by their waveforms and spectra only.
