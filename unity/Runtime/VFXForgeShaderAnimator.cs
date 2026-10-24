// VFX Forge: plays a shader's timeline in Unity and lets game code trigger it.
//
// A shader exported from the Shader tab comes with a .anim.json file: the keyframes made on
// the timeline. This component reads it and, every frame, sets the material's properties to
// what the keyframes say, the same way the VFX Forge preview does. It needs no editor and no
// VFX Forge: it runs in Play mode and in a built game.
//
// Game code can call:
//   Hit(worldPosition, worldRadius)   a shield impact at a point (Force Field shaders)
//   Burst(peak, seconds)              a quick surge of brightness (any shader)
//   Reveal(show, seconds)             fades the shader in or out with its Reveal control
//   Play(), Pause(), Stop(), SetTime(seconds)
//
// Place it on the object that has the material (a Renderer). For a UI Image, set `material`
// to the material the Image uses.
using System;
using UnityEngine;

namespace VFXForge
{
    [Serializable]
    public class VFXShaderKey
    {
        public float time;
        public float value;
        public int easing; // 0 linear, 1 smooth, 2 ease in, 3 ease out
    }

    [Serializable]
    public class VFXShaderTrack
    {
        public string property; // the material property without its underscore, "HG_intensity"
        public float min;
        public float max;
        public VFXShaderKey[] keys;
    }

    [Serializable]
    public class VFXShaderAnimation
    {
        public float duration = 4f;
        public int loop; // 0 loop, 1 ping-pong, 2 once
        public float speed = 1f;
        public string revealProperty = "CM_reveal";
        public string hitRadiusProperty = "";
        public VFXShaderTrack[] tracks = new VFXShaderTrack[0];
    }

    [AddComponentMenu("VFX Forge/Shader Animator")]
    [DisallowMultipleComponent]
    public class VFXForgeShaderAnimator : MonoBehaviour
    {
        [Tooltip("The .anim.json file exported with the shader.")]
        public TextAsset animationFile;
        [Tooltip("Leave empty to use the Renderer on this object (its own copy of the material). For UI, set the material the Image uses.")]
        public Material material;
        public bool playOnEnable = true;
        public float playbackSpeed = 1f;
        [Tooltip("Measures the object's size so object-space patterns fit it. Turn off if you set Bounds Extent yourself.")]
        public bool autoBounds = true;

        VFXShaderAnimation data = new VFXShaderAnimation();
        Material mat;
        Renderer rend;
        bool playing;
        float clock;       // seconds since the start, never wrapped
        float hitAge = -1f;
        float burstAge = -1f, burstSeconds = 0.5f, burstPeak = 1f;
        float revealNow = 1f, revealFrom = 1f, revealTo = 1f, revealAge = 1f, revealSeconds = 0.5f;
        bool revealActive;
        Vector3 center;
        float extent = 0.5f;

        static readonly int ScriptTime = Shader.PropertyToID("_ScriptTime");
        static readonly int UseScriptTime = Shader.PropertyToID("_UseScriptTime");
        static readonly int HitPos = Shader.PropertyToID("_HitPos");
        static readonly int HitAge = Shader.PropertyToID("_HitAge");
        static readonly int Burst_ = Shader.PropertyToID("_Burst");
        static readonly int BoundsCenter = Shader.PropertyToID("_BoundsCenter");
        static readonly int BoundsExtent = Shader.PropertyToID("_BoundsExtent");
        static readonly int Lifetime = Shader.PropertyToID("_Lifetime");

        /// <summary>The playhead on the timeline, after looping.</summary>
        public float TimelineTime { get { return Wrap(clock * data.speed); } }
        public bool IsPlaying { get { return playing; } }

        void Awake() { Prepare(); }

        void OnEnable()
        {
            if (mat == null) Prepare();
            playing = playOnEnable;
            Apply();
        }

        void OnDestroy()
        {
            if (mat != null && material == null) Destroy(mat);
        }

        void Prepare()
        {
            if (animationFile != null)
            {
                try { data = JsonUtility.FromJson<VFXShaderAnimation>(animationFile.text); }
                catch (Exception e) { Debug.LogWarning("VFX Forge: the shader animation could not be read: " + e.Message, this); }
                if (data == null) data = new VFXShaderAnimation();
                if (data.tracks == null) data.tracks = new VFXShaderTrack[0];
            }
            rend = GetComponent<Renderer>();
            if (material != null) mat = material;
            else if (rend != null) mat = rend.material;
            if (mat == null)
            {
                Debug.LogWarning("VFX Forge: Shader Animator has no material to animate. Put it on a Renderer or set Material.", this);
                enabled = false;
                return;
            }
            if (autoBounds && rend != null)
            {
                Bounds b = rend.localBounds;
                center = b.center;
                extent = Mathf.Max(0.0001f, Mathf.Max(b.extents.x, Mathf.Max(b.extents.y, b.extents.z)));
            }
            else if (mat.HasProperty(BoundsExtent))
            {
                extent = Mathf.Max(0.0001f, mat.GetFloat(BoundsExtent));
                Vector4 c = mat.GetVector(BoundsCenter);
                center = new Vector3(c.x, c.y, c.z);
            }
            mat.SetVector(BoundsCenter, center);
            mat.SetFloat(BoundsExtent, extent);
            mat.SetFloat(Lifetime, Mathf.Max(0.1f, data.duration));
            mat.SetFloat(UseScriptTime, 1f);
        }

        public void Play() { playing = true; }
        public void Pause() { playing = false; }
        public void Stop() { playing = false; clock = 0f; hitAge = -1f; burstAge = -1f; Apply(); }

        /// <summary>Jumps the timeline to a time in seconds (for scrubbing).</summary>
        public void SetTime(float seconds)
        {
            clock = data.speed > 0.0001f ? Mathf.Max(0f, seconds) / data.speed : 0f;
            Apply();
        }

        /// <summary>A shield impact at a point in the world. radius is in world units; 0 or less keeps the shader's own.</summary>
        public void Hit(Vector3 worldPosition, float worldRadius = 0f)
        {
            if (mat == null) return;
            Vector3 local = transform.InverseTransformPoint(worldPosition);
            Vector3 n = (local - center) / extent;
            if (n.sqrMagnitude < 1e-8f) n = Vector3.up;
            n = n.normalized;
            mat.SetVector(HitPos, new Vector4(n.x, n.y, n.z, 0f));
            hitAge = 0f;
            if (worldRadius > 0f && !string.IsNullOrEmpty(data.hitRadiusProperty))
            {
                float scale = Mathf.Max(0.0001f, Mathf.Max(transform.lossyScale.x, Mathf.Max(transform.lossyScale.y, transform.lossyScale.z)));
                mat.SetFloat("_" + data.hitRadiusProperty, Mathf.Clamp(worldRadius / (extent * scale), 0.05f, 2f));
            }
            mat.SetFloat(HitAge, 0f);
        }

        /// <summary>A quick surge of brightness: up to 1 + peak times the colour, then back over seconds.</summary>
        public void Burst(float peak = 1f, float seconds = 0.5f)
        {
            burstPeak = peak;
            burstSeconds = Mathf.Max(0.05f, seconds);
            burstAge = 0f;
        }

        /// <summary>Fades the shader in (true) or out (false) with its Reveal control.</summary>
        public void Reveal(bool show, float seconds = 0.6f)
        {
            revealFrom = revealNow;
            revealTo = show ? 1f : 0f;
            revealSeconds = Mathf.Max(0.01f, seconds);
            revealAge = 0f;
            revealActive = true;
        }

        void Update()
        {
            if (mat == null) return;
            float dt = Time.deltaTime;
            if (playing) clock += dt * playbackSpeed;
            if (hitAge >= 0f)
            {
                hitAge += dt;
                if (hitAge > 12f) hitAge = -1f;
                mat.SetFloat(HitAge, hitAge);
            }
            if (burstAge >= 0f)
            {
                burstAge += dt;
                float k = burstAge / burstSeconds;
                if (k >= 1f) { burstAge = -1f; mat.SetFloat(Burst_, 0f); }
                else mat.SetFloat(Burst_, burstPeak * (1f - k) * (1f - k));
            }
            Apply();
            if (revealActive)
            {
                revealAge += dt;
                float k = Mathf.Clamp01(revealAge / revealSeconds);
                revealNow = Mathf.Lerp(revealFrom, revealTo, k * k * (3f - 2f * k));
                if (!string.IsNullOrEmpty(data.revealProperty)) mat.SetFloat("_" + data.revealProperty, revealNow);
                if (k >= 1f) revealActive = false;
            }
        }

        float Wrap(float t)
        {
            float d = Mathf.Max(data.duration, 0.01f);
            t = Mathf.Max(t, 0f);
            switch (data.loop)
            {
                case 0: return t - d * Mathf.Floor(t / d);
                case 1:
                    float m = t - 2f * d * Mathf.Floor(t / (2f * d));
                    return m <= d ? m : 2f * d - m;
                default: return Mathf.Min(t, d);
            }
        }

        static float Ease(int e, float t)
        {
            t = Mathf.Clamp01(t);
            switch (e)
            {
                case 1: return t * t * (3f - 2f * t);
                case 2: return t * t;
                case 3: return 1f - (1f - t) * (1f - t);
                default: return t;
            }
        }

        void Apply()
        {
            if (mat == null) return;
            float shown = clock * data.speed;
            mat.SetFloat(ScriptTime, shown);
            float time = Wrap(shown);
            foreach (VFXShaderTrack track in data.tracks)
            {
                if (track == null || track.keys == null || track.keys.Length == 0) continue;
                if (revealActive && track.property == data.revealProperty) continue;
                VFXShaderKey[] keys = track.keys;
                float v = keys[0].value;
                if (time >= keys[keys.Length - 1].time) v = keys[keys.Length - 1].value;
                else if (time > keys[0].time)
                {
                    for (int i = 0; i + 1 < keys.Length; i++)
                    {
                        if (time >= keys[i].time && time < keys[i + 1].time)
                        {
                            float span = Mathf.Max(keys[i + 1].time - keys[i].time, 1e-6f);
                            v = Mathf.Lerp(keys[i].value, keys[i + 1].value, Ease(keys[i].easing, (time - keys[i].time) / span));
                            break;
                        }
                    }
                }
                mat.SetFloat("_" + track.property, Mathf.Clamp(v, track.min, track.max));
            }
        }
    }
}
