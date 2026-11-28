// VFX Forge test: how strongly does Unity's Limit Velocity drag slow
// particles, and do random lifetimes give the expected survivors? Writes
// VFXForgeCaptures/drag-log.txt.
using System.IO;
using UnityEditor;
using UnityEngine;

namespace VFXForge.Tests
{
    public static class DragExperiment
    {
        [MenuItem("VFX Forge/Test/Drag Experiment")]
        public static void Run()
        {
            string folder = Path.Combine(Path.GetDirectoryName(Application.dataPath), "VFXForgeCaptures");
            Directory.CreateDirectory(folder);
            var log = new System.Text.StringBuilder();
            log.AppendLine("fixedDeltaTime " + Time.fixedDeltaTime + ", maximumDeltaTime " + Time.maximumDeltaTime);
            foreach (float drag in new[] { 0f, 1f, 2.6f, 4.2f })
            foreach (bool stepped in new[] { false, true })
            {
                var go = new GameObject("drag");
                var ps = go.AddComponent<ParticleSystem>();
                ps.Stop(true, ParticleSystemStopBehavior.StopEmittingAndClear);
                var m = ps.main;
                m.duration = 3f; m.loop = false; m.startLifetime = 5f; m.startSpeed = 10f; m.gravityModifier = 0f;
                m.maxParticles = 1000;
                var em = ps.emission; em.rateOverTime = 0; em.SetBursts(new[] { new ParticleSystem.Burst(0f, 100) });
                var shape = ps.shape; shape.shapeType = ParticleSystemShapeType.Sphere; shape.radius = 0.0001f;
                if (drag > 0)
                {
                    var limit = ps.limitVelocityOverLifetime;
                    limit.enabled = true; limit.limit = 100000f; limit.dampen = 0f; limit.drag = drag;
                    limit.multiplyDragByParticleSize = false; limit.multiplyDragByParticleVelocity = false;
                }
                string line = "drag " + drag + (stepped ? " stepped" : " jump") + ":";
                float reached = 0f;
                if (stepped) ps.Simulate(0f, true, true, true);
                foreach (float t in new[] { 0.3f, 1.0f })
                {
                    if (stepped)
                        while (reached + 1f / 120f < t) { ps.Simulate(1f / 60f, true, false, true); reached += 1f / 60f; }
                    else
                        ps.Simulate(t, true, true, true);
                    var parts = new ParticleSystem.Particle[ps.particleCount];
                    int n = ps.GetParticles(parts);
                    float speed = 0f, dist = 0f;
                    for (int i = 0; i < n; i++) { speed += parts[i].totalVelocity.magnitude; dist += parts[i].position.magnitude; }
                    line += "  t=" + t + ": " + n + " alive, speed " + (speed / Mathf.Max(n, 1)).ToString("F2") +
                            ", distance " + (dist / Mathf.Max(n, 1)).ToString("F2") + ", time " + ps.time.ToString("F3");
                }
                log.AppendLine(line);
                Object.DestroyImmediate(go);
            }
            // Candidates for drag in proportion to speed: Limit Velocity with no
            // limit and a dampen, and drag multiplied by velocity. Each run with
            // 60 and 30 steps a second, to see whether the frame rate changes it.
            foreach (string mode in new[] { "dampen0.05", "dampen0.1", "dampen0.2", "byVelocity1" })
            foreach (float frame in new[] { 1f / 60f, 1f / 30f })
            {
                var go = new GameObject("damp");
                var ps = go.AddComponent<ParticleSystem>();
                ps.Stop(true, ParticleSystemStopBehavior.StopEmittingAndClear);
                var m = ps.main;
                m.duration = 3f; m.loop = false; m.startLifetime = 5f; m.startSpeed = 10f; m.gravityModifier = 0f;
                var em = ps.emission; em.rateOverTime = 0; em.SetBursts(new[] { new ParticleSystem.Burst(0f, 50) });
                var shape = ps.shape; shape.shapeType = ParticleSystemShapeType.Sphere; shape.radius = 0.0001f;
                var limit = ps.limitVelocityOverLifetime;
                limit.enabled = true;
                if (mode.StartsWith("dampen"))
                {
                    limit.limit = 0f; limit.dampen = float.Parse(mode.Substring(6), System.Globalization.CultureInfo.InvariantCulture);
                }
                else
                {
                    limit.limit = 100000f; limit.dampen = 0f; limit.drag = 1f; limit.multiplyDragByParticleVelocity = true;
                }
                string line = mode + " frame 1/" + Mathf.RoundToInt(1f / frame) + ":";
                float reached = 0f;
                ps.Simulate(0f, true, true, true);
                foreach (float t in new[] { 0.1f, 0.3f, 1.0f })
                {
                    while (reached + frame * 0.5f < t) { ps.Simulate(frame, true, false, false); reached += frame; }
                    var parts = new ParticleSystem.Particle[ps.particleCount];
                    int n = ps.GetParticles(parts);
                    float speed = 0f;
                    for (int i = 0; i < n; i++) speed += parts[i].totalVelocity.magnitude;
                    line += "  t=" + t + ": speed " + (speed / Mathf.Max(n, 1)).ToString("F3");
                }
                log.AppendLine(line);
                Object.DestroyImmediate(go);
            }
            // Survivors with a random lifetime between 1.2 and 1.7 s, at 1.5 s: expect 40%.
            {
                var go = new GameObject("life");
                var ps = go.AddComponent<ParticleSystem>();
                ps.Stop(true, ParticleSystemStopBehavior.StopEmittingAndClear);
                var m = ps.main; m.duration = 3f; m.loop = false; m.maxParticles = 2000;
                m.startLifetime = new ParticleSystem.MinMaxCurve(1.2f, 1.7f); m.startSpeed = 0f;
                var em = ps.emission; em.rateOverTime = 0; em.SetBursts(new[] { new ParticleSystem.Burst(0f, 1000) });
                ps.Simulate(1.5f, true, true, true);
                log.AppendLine("lifetimes 1.2-1.7 s, 1000 born: " + ps.particleCount + " alive at 1.5 s (expect about 400)");
                Object.DestroyImmediate(go);
            }
            File.WriteAllText(Path.Combine(folder, "drag-log.txt"), log.ToString());
            Debug.Log("VFX Forge drag experiment:\n" + log);
        }
    }
}
