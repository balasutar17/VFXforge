// VFX Forge test: which Unity Shape set-up reproduces a flat rectangle that
// sprays within an angle (VFX Forge: Rectangle + Direction with a Spread)?
// Target here: 0.5 x 0.1 rectangle, straight up, spread 26 degrees each side.
// Writes VFXForgeCaptures/spray-log.txt.
using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEngine;

namespace VFXForge.Tests
{
    public static class SprayExperiment
    {
        [MenuItem("VFX Forge/Test/Spray Experiment")]
        public static void Run()
        {
            string folder = Path.Combine(Path.GetDirectoryName(Application.dataPath), "VFXForgeCaptures");
            Directory.CreateDirectory(folder);
            var log = new System.Text.StringBuilder();
            log.AppendLine("target: x within +-0.25, y within +-0.05, angle from up uniform within +-26 (mean |angle| 13), no depth");
            var setups = new List<string> { "boxRandomDir", "circleArc_randPos", "circleArc_scaled", "coneSpread", "edgeArc" };
            foreach (float a in new[] { 0.02f, 0.05f, 0.08f, 0.1f, 0.15f, 0.2f, 0.3f, 0.4f, 0.5f, 0.7f, 1f })
                setups.Add("box" + a.ToString(System.Globalization.CultureInfo.InvariantCulture));
            foreach (var setup in setups)
            {
                var go = new GameObject("spray");
                var ps = go.AddComponent<ParticleSystem>();
                ps.Stop(true, ParticleSystemStopBehavior.StopEmittingAndClear);
                var m = ps.main;
                m.duration = 1f; m.loop = false; m.startLifetime = 5f; m.startSpeed = 1f; m.gravityModifier = 0f;
                m.maxParticles = 5000;
                var em = ps.emission; em.rateOverTime = 0; em.SetBursts(new[] { new ParticleSystem.Burst(0f, 4000) });
                var s = ps.shape; s.enabled = true;
                switch (setup)
                {
                    case "boxRandomDir": // what the export does now
                        s.shapeType = ParticleSystemShapeType.Box; s.scale = new Vector3(0.5f, 0f, 0.1f);
                        s.rotation = new Vector3(-90, 0, 0); s.randomDirectionAmount = 26f / 180f;
                        break;
                    case "circleArc_randPos": // exact arc, position jitter
                        s.shapeType = ParticleSystemShapeType.Circle; s.radius = 0.0001f; s.arc = 52f;
                        s.rotation = new Vector3(0, 0, 90 - 26); s.randomPositionAmount = 0.25f;
                        break;
                    case "circleArc_scaled": // exact arc, jitter shaped by the scale
                        s.shapeType = ParticleSystemShapeType.Circle; s.radius = 0.0001f; s.arc = 52f;
                        s.rotation = new Vector3(0, 0, 90 - 26); s.randomPositionAmount = 1f;
                        s.scale = new Vector3(0.25f, 0.05f, 0f);
                        break;
                    case "coneSpread": // a cone's base as a line: two opposite sides only
                        s.shapeType = ParticleSystemShapeType.Cone; s.radius = 0.25f; s.angle = 26f;
                        s.arc = 360f; s.arcMode = ParticleSystemShapeMultiModeValue.BurstSpread; s.arcSpread = 0.5f;
                        s.rotation = new Vector3(-90, 0, 0); s.radiusThickness = 1f;
                        break;
                    case "edgeArc": // Single Sided Edge with the spread as random direction
                        s.shapeType = ParticleSystemShapeType.SingleSidedEdge; s.radius = 0.25f;
                        s.randomDirectionAmount = 26f / 180f;
                        break;
                    default: // "box<amount>": how far Random Direction fans a box out
                        s.shapeType = ParticleSystemShapeType.Box; s.scale = new Vector3(0.5f, 0f, 0.1f);
                        s.rotation = new Vector3(-90, 0, 0);
                        s.randomDirectionAmount = float.Parse(setup.Substring(3), System.Globalization.CultureInfo.InvariantCulture);
                        break;
                }
                ps.Simulate(0.02f, true, true, false);
                var parts = new ParticleSystem.Particle[ps.particleCount];
                int n = ps.GetParticles(parts);
                float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f, maxZ = 0f, absAngle = 0f, maxAngle = 0f, dirZ = 0f;
                var bins = new int[20];
                for (int i = 0; i < n; i++)
                {
                    var p = parts[i].position; var v = parts[i].velocity.normalized;
                    minX = Mathf.Min(minX, p.x); maxX = Mathf.Max(maxX, p.x); minY = Mathf.Min(minY, p.y); maxY = Mathf.Max(maxY, p.y);
                    maxZ = Mathf.Max(maxZ, Mathf.Abs(p.z)); dirZ += Mathf.Abs(v.z);
                    float a = Mathf.Abs(Mathf.Atan2(v.x, v.y) * Mathf.Rad2Deg);
                    absAngle += a; maxAngle = Mathf.Max(maxAngle, a);
                    bins[Mathf.Clamp((int)(a / 9f), 0, 19)]++;
                }
                var spread = new List<string>();
                foreach (int b in bins) spread.Add((100f * b / Mathf.Max(n, 1)).ToString("F0"));
                log.AppendLine(setup + ": x " + minX.ToString("F2") + ".." + maxX.ToString("F2") + ", y " + minY.ToString("F2") + ".." +
                               maxY.ToString("F2") + ", |z| up to " + maxZ.ToString("F2") + ", mean |angle| " + (absAngle / n).ToString("F1") +
                               ", max " + maxAngle.ToString("F1") + ", mean |dir z| " + (dirZ / n).ToString("F2") +
                               ", % per 9 degrees: " + string.Join(" ", spread));
                Object.DestroyImmediate(go);
            }
            // The same measure for a flat outline (the importer's Path shape, a mesh):
            // one layer per Random Direction amount, in an effect made for this.
            var calibration = AssetDatabase.LoadAssetAtPath<GameObject>("Assets/VFXForge/Effects/Spray Calibration.vfxforge");
            if (calibration != null)
            {
                var instance = (GameObject)Object.Instantiate(calibration);
                foreach (var ps in instance.GetComponentsInChildren<ParticleSystem>())
                {
                    if (ps.transform == instance.transform)
                        continue;
                    ps.Simulate(0.02f, false, true, false);
                    var parts = new ParticleSystem.Particle[ps.particleCount];
                    int n = ps.GetParticles(parts);
                    float absAngle = 0f, maxAngle = 0f, dirZ = 0f;
                    var bins = new int[20];
                    for (int i = 0; i < n; i++)
                    {
                        var v = ps.transform.TransformDirection(parts[i].velocity).normalized;
                        dirZ += Mathf.Abs(v.z);
                        float a = Mathf.Abs(Mathf.Atan2(v.x, v.y) * Mathf.Rad2Deg);
                        absAngle += a; maxAngle = Mathf.Max(maxAngle, a);
                        bins[Mathf.Clamp((int)(a / 9f), 0, 19)]++;
                    }
                    var spread = new List<string>();
                    foreach (int b in bins) spread.Add((100f * b / Mathf.Max(n, 1)).ToString("F0"));
                    log.AppendLine("path " + ps.name + ": " + n + " born, mean |angle| " + (absAngle / Mathf.Max(n, 1)).ToString("F1") +
                                   ", max " + maxAngle.ToString("F1") + ", mean |dir z| " + (dirZ / Mathf.Max(n, 1)).ToString("F2") +
                                   ", % per 9 degrees: " + string.Join(" ", spread));
                }
                Object.DestroyImmediate(instance);
            }
            File.WriteAllText(Path.Combine(folder, "spray-log.txt"), log.ToString());
            Debug.Log("VFX Forge spray experiment:\n" + log);
        }
    }
}
