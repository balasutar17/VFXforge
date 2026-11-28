// VFX Forge test: how should a flat (2D) "every way" sub emitter be set up so
// that Unity's alignment of sub-emitter shapes to the parent's velocity does
// not turn its ring edge-on? Builds three set-ups against three parents and
// photographs each into VFXForgeCaptures/subexp_<setup>_<parent>.png.
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;

namespace VFXForge.Tests
{
    public static class SubEmitterExperiment
    {
        [MenuItem("VFX Forge/Test/Sub Emitter Experiment")]
        public static void Run()
        {
            string folder = Path.Combine(Path.GetDirectoryName(Application.dataPath), "VFXForgeCaptures");
            Directory.CreateDirectory(folder);
            var log = new System.Text.StringBuilder();
            if (!EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo())
                return;
            var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
            var material = new Material(Shader.Find("Universal Render Pipeline/Particles/Unlit") ?? Shader.Find("Particles/Standard Unlit"));
            int size = 256;
            var rt = new RenderTexture(size, size, 24);
            var cam = new GameObject("cam").AddComponent<Camera>();
            cam.orthographic = true; cam.orthographicSize = 3.2f;
            cam.transform.position = new Vector3(0, 0, -50);
            cam.clearFlags = CameraClearFlags.SolidColor; cam.backgroundColor = Color.black;
            cam.targetTexture = rt;

            string[] setups = { "circle", "circleYZ", "sphereFlat" };
            string[] parents = { "up", "side", "still" };
            foreach (var setup in setups)
            foreach (var parentKind in parents)
            {
                var root = new GameObject("exp");
                var parent = root.AddComponent<ParticleSystem>();
                parent.Stop(true, ParticleSystemStopBehavior.StopEmittingAndClear);
                var m = parent.main;
                m.duration = 2f; m.loop = false; m.startLifetime = 0.6f; m.startSize = 0.2f;
                m.startSpeed = parentKind == "still" ? 0f : 5f; m.gravityModifier = 0f;
                m.simulationSpace = ParticleSystemSimulationSpace.Local;
                var em = parent.emission; em.rateOverTime = 0;
                em.SetBursts(new[] { new ParticleSystem.Burst(0f, 1) });
                var sh = parent.shape; sh.enabled = true; sh.shapeType = ParticleSystemShapeType.Cone;
                sh.angle = 0f; sh.radius = 0.0001f;
                // Cone points along local +Z; turn it to point up or sideways in the picture.
                sh.rotation = parentKind == "side" ? new Vector3(0, 90, 0) : new Vector3(-90, 0, 0);
                sh.position = parentKind == "side" ? new Vector3(-2, 0, 0) : new Vector3(0, -2, 0);
                parent.GetComponent<ParticleSystemRenderer>().sharedMaterial = material;

                var childObject = new GameObject("child");
                childObject.transform.SetParent(root.transform, false);
                var child = childObject.AddComponent<ParticleSystem>();
                child.Stop(true, ParticleSystemStopBehavior.StopEmittingAndClear);
                var cm = child.main; cm.playOnAwake = false; cm.startLifetime = 1.5f; cm.startSpeed = 2f;
                cm.startSize = 0.12f; cm.gravityModifier = 0f; cm.loop = false;
                var cem = child.emission; cem.rateOverTime = 0;
                cem.SetBursts(new[] { new ParticleSystem.Burst(0f, 36) });
                var csh = child.shape; csh.enabled = true; csh.radius = 0.0001f;
                if (setup == "sphereFlat")
                {
                    csh.shapeType = ParticleSystemShapeType.Sphere;
                    var limit = child.limitVelocityOverLifetime;
                    limit.enabled = true; limit.separateAxes = true;
                    limit.limitX = 1000f; limit.limitY = 1000f; limit.limitZ = 0f; limit.dampen = 1f;
                }
                else
                {
                    csh.shapeType = ParticleSystemShapeType.Circle; csh.arc = 360f;
                    csh.rotation = setup == "circleYZ" ? new Vector3(0, 90, 0) : Vector3.zero;
                }
                child.GetComponent<ParticleSystemRenderer>().sharedMaterial = material;
                var subs = parent.subEmitters; subs.enabled = true;
                subs.AddSubEmitter(child, ParticleSystemSubEmitterType.Death, ParticleSystemSubEmitterProperties.InheritNothing);

                // Step forward as playing does: the parent dies at 0.6 s, then 0.5 s more.
                parent.Simulate(0f, true, true, true);
                for (int i = 0; i < 66; i++)
                    parent.Simulate(1f / 60f, true, false, true);

                var parts = new ParticleSystem.Particle[child.particleCount];
                int n = child.GetParticles(parts);
                Vector3 mean = Vector3.zero, spread = Vector3.zero;
                for (int i = 0; i < n; i++) mean += parts[i].position;
                if (n > 0) mean /= n;
                for (int i = 0; i < n; i++) { var d = parts[i].position - mean; spread += Vector3.Scale(d, d); }
                if (n > 0) spread = new Vector3(Mathf.Sqrt(spread.x / n), Mathf.Sqrt(spread.y / n), Mathf.Sqrt(spread.z / n));
                log.AppendLine(setup + " / " + parentKind + ": " + n + " sparks, centre " + mean.ToString("F2") + ", spread x/y/z " + spread.ToString("F2"));

                var request = new RenderPipeline.StandardRequest { destination = rt };
                if (GraphicsSettings.currentRenderPipeline != null && RenderPipeline.SupportsRenderRequest(cam, request))
                    RenderPipeline.SubmitRenderRequest(cam, request);
                else
                    cam.Render();
                var picture = new Texture2D(size, size, TextureFormat.RGBA32, false);
                RenderTexture.active = rt; picture.ReadPixels(new Rect(0, 0, size, size), 0, 0); picture.Apply();
                RenderTexture.active = null;
                File.WriteAllBytes(Path.Combine(folder, "subexp_" + setup + "_" + parentKind + ".png"), picture.EncodeToPNG());
                Object.DestroyImmediate(picture);
                Object.DestroyImmediate(root);
            }
            File.WriteAllText(Path.Combine(folder, "subexp-log.txt"), log.ToString());
            Debug.Log("VFX Forge sub emitter experiment:\n" + log);
        }
    }
}
