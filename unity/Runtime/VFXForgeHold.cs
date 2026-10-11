// VFX Forge: shows a layer as held drawings, the way hand-drawn animation
// is shown ("on twos" is twelve drawings a second, "on threes" eight).
//
// A Particle System normally moves on every frame. This component takes
// that over for one layer: it keeps the layer's system paused and moves it
// on itself, a whole drawing's worth of time at once, each time a new
// drawing is due. Between drawings nothing in the layer changes at all.
// The effect's top Particle System is the clock, so every layer of the
// effect, held or not, stays in step.
//
// It runs in Play mode and in a built game. In the Scene view's own
// particle preview (not playing), Unity moves the system itself and the
// layer looks smooth.
using UnityEngine;

namespace VFXForge
{
    [AddComponentMenu("VFX Forge/Hold Frames")]
    [RequireComponent(typeof(ParticleSystem))]
    public class VFXForgeHold : MonoBehaviour
    {
        [Tooltip("The effect's top Particle System: its time says where in the effect we are.")]
        public ParticleSystem timer;
        [Tooltip("How many drawings a second this layer shows. 12 is \"on twos\", 8 \"on threes\".")]
        public float drawingsPerSecond = 12f;

        ParticleSystem system;
        float last = -1f;     // the clock when we last looked
        float elapsed = 0f;   // seconds the effect has been running, all passes together
        float shown = 0f;     // seconds this layer has been moved on by

        void OnEnable()
        {
            system = GetComponent<ParticleSystem>();
            last = -1f;
        }

        void OnDisable()
        {
            // Hand the layer back to Unity as it was.
            if (system != null && timer != null && timer.isPlaying && system.isPaused)
                system.Play(false);
        }

        void Update()
        {
            if (system == null || timer == null || drawingsPerSecond <= 0f)
                return;
            if (!timer.isPlaying)
            {
                // Stopped clears the layer for the next run; paused holds it.
                if (!timer.isPaused)
                    last = -1f;
                if (!system.isPaused && system.isPlaying)
                    system.Pause(false);
                return;
            }

            float now = timer.time;
            float duration = Mathf.Max(0.01f, timer.main.duration);
            if (last < 0f)
            {
                // A fresh run: start this layer from nothing.
                system.Simulate(0f, false, true, true);
                elapsed = now;
                shown = 0f;
            }
            else if (now < last)
                elapsed += duration - last + now;  // the effect went round again
            else
                elapsed += now - last;
            last = now;

            // Unity would move the layer on by itself; it is ours to move.
            if (!system.isPaused)
                system.Pause(false);

            // The start of the drawing we are in, and a sixtieth past it so
            // that a burst at that very moment is already in the picture.
            float drawing = Mathf.Floor(elapsed * drawingsPerSecond + 0.0001f) / drawingsPerSecond + 1f / 60f;
            if (drawing > shown + 0.0005f)
            {
                system.Simulate(drawing - shown, false, false, true);
                shown = drawing;
            }
        }
    }
}
