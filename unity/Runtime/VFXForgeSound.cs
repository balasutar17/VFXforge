// VFX Forge: plays a layer's sound in step with its effect.
//
// VFX Forge works out, when it exports, exactly when in each pass of the
// effect the sound plays (when its layer starts, or at each burst, plus its
// delay). This component watches the effect's top Particle System and plays
// the sound at those moments, every pass, with the volume, pitch, pan,
// fades, trimming, looping and randomness set in VFX Forge. It runs in the
// game, not only in the editor.
using UnityEngine;

namespace VFXForge
{
    [AddComponentMenu("VFX Forge/Sound")]
    public class VFXForgeSound : MonoBehaviour
    {
        [Tooltip("The effect's top Particle System: its time says where in the effect we are.")]
        public ParticleSystem timer;
        public AudioClip clip;
        [Tooltip("Seconds into each pass of the effect at which the sound plays.")]
        public float[] times = new float[0];
        [Tooltip("For each time: seconds of the sound skipped, for a sound that starts before the effect does.")]
        public float[] skips = new float[0];
        [Range(0f, 4f)] public float volume = 1f;
        [Tooltip("Semitones up or down.")] public float pitch = 0f;
        [Range(-1f, 1f)] public float pan = 0f;
        [Tooltip("Each time it plays, the pitch moves by up to this many semitones.")] public float randomPitch = 0f;
        [Range(0f, 1f)] public float randomVolume = 0f;
        public float fadeIn = 0f;
        public float fadeOut = 0f;
        [Tooltip("Seconds skipped at the start of the sound.")] public float trimStart = 0f;
        [Tooltip("Seconds of the sound played; 0 plays it to the end.")] public float length = 0f;
        public bool loop = false;
        [Tooltip("For a looping sound: the moment in the pass at which it stops.")] public float loopEnd = 0f;
        [Tooltip("How many plays of this sound can overlap.")] public int maxVoices = 8;

        class Voice
        {
            public AudioSource source;
            public float started;   // Time.time
            public float duration;  // seconds of real time
            public float gain;
        }

        Voice[] voices;
        float last = -1f;

        void Awake()
        {
            int count = Mathf.Clamp(maxVoices, 1, 32);
            voices = new Voice[count];
            for (int i = 0; i < count; i++)
            {
                var source = gameObject.AddComponent<AudioSource>();
                source.playOnAwake = false;
                source.spatialBlend = 0f;
                voices[i] = new Voice { source = source };
            }
        }

        void Update()
        {
            if (timer == null || clip == null || voices == null)
                return;
            if (!timer.isPlaying)
            {
                if (!timer.isPaused)
                    last = -1f;
                FadeVoices();
                return;
            }
            float now = timer.time;
            float duration = Mathf.Max(0.01f, timer.main.duration);
            if (last < 0f)
                Fire(-1f, now);
            else if (now < last)
            {
                // The effect went round again.
                Fire(last, duration + 1f);
                Fire(-1f, now);
            }
            else
                Fire(last, now);
            last = now;
            FadeVoices();
        }

        // Plays every sound time in (from, to].
        void Fire(float from, float to)
        {
            for (int i = 0; i < times.Length; i++)
            {
                float t = times[i];
                if (t > from && t <= to)
                    Play(i, t);
            }
        }

        void Play(int index, float at)
        {
            Voice voice = null;
            foreach (var v in voices)
            {
                if (!v.source.isPlaying)
                {
                    voice = v;
                    break;
                }
            }
            if (voice == null)
                return;  // too many at once
            float skip = index < skips.Length ? skips[index] : 0f;
            float semitones = pitch + randomPitch * (Random.value * 2f - 1f);
            float rate = Mathf.Pow(2f, semitones / 12f);
            float start = Mathf.Clamp(trimStart + skip, 0f, Mathf.Max(0f, clip.length - 0.001f));
            float available = clip.length - start;
            float used = length > 0f ? Mathf.Min(length - skip, available) : available;
            voice.gain = volume * (1f - randomVolume * Random.value);
            voice.duration = loop ? Mathf.Max(0f, loopEnd - at) : Mathf.Max(0f, used) / rate;
            voice.started = Time.time;
            var s = voice.source;
            s.clip = clip;
            s.loop = loop;
            s.pitch = rate;
            s.panStereo = pan;
            s.volume = fadeIn > 0f ? 0f : voice.gain;
            s.time = start;
            if (voice.duration > 0f)
                s.Play();
        }

        void FadeVoices()
        {
            foreach (var v in voices)
            {
                if (!v.source.isPlaying)
                    continue;
                float age = Time.time - v.started;
                if (age >= v.duration)
                {
                    v.source.Stop();
                    continue;
                }
                float g = v.gain;
                if (fadeIn > 0f)
                    g *= Mathf.Clamp01(age / fadeIn);
                if (fadeOut > 0f)
                    g *= Mathf.Clamp01((v.duration - age) / fadeOut);
                v.source.volume = g;
            }
        }
    }
}
