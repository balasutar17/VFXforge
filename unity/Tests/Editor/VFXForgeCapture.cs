// VFX Forge: photographs exported effects inside Unity, for comparing with
// VFX Forge's own pictures of the same moments.
//
// Put VFXForgeCaptures/plan.json next to Assets (vfxexport plan writes it,
// with VFX Forge's pictures), then VFX Forge > Test > Capture Comparison
// Frames. Each effect is placed in a scene of its own and seen by a camera
// framed as in VFX Forge; the particle systems are run to each moment and
// photographed into VFXForgeCaptures/<name>_<time>_unity.png.
using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
using VFXForge.EditorTools;

namespace VFXForge.Tests
{
    public static class VFXForgeCapture
    {
        [MenuItem("VFX Forge/Test/Capture Comparison Frames")]
        public static void Capture()
        {
            string root = Path.GetDirectoryName(Application.dataPath);
            string folder = Path.Combine(root, "VFXForgeCaptures");
            string planPath = Path.Combine(folder, "plan.json");
            if (!File.Exists(planPath))
            {
                Debug.LogError("VFX Forge capture: no " + planPath);
                return;
            }
            var plan = VFXForgeJson.Parse(File.ReadAllText(planPath)) as Dictionary<string, object>;
            int size = (int)Num(plan, "size", 400);
            var log = new System.Text.StringBuilder();
            log.AppendLine("shader asset: " + (AssetDatabase.LoadAssetAtPath<Shader>("Assets/VFXForge/Shaders/VFXForgeParticle.shader") != null) +
                           ", Shader.Find: " + (Shader.Find("VFX Forge/Particle") != null) +
                           ", atlas: " + (AssetDatabase.LoadAssetAtPath<Texture2D>("Assets/VFXForge/Textures/VFXForgeShapes.png") != null));
            log.AppendLine("colour space: " + QualitySettings.activeColorSpace + ", pipeline: " +
                           (GraphicsSettings.currentRenderPipeline ? GraphicsSettings.currentRenderPipeline.name : "built-in") +
                           ", unity: " + Application.unityVersion);

            // With --reimport in the plan, every effect is built again first.
            if (plan.ContainsKey("reimport") && (bool)plan["reimport"])
            {
                foreach (var item in (List<object>)plan["effects"])
                    AssetDatabase.ImportAsset("Assets/VFXForge/Effects/" + (string)((Dictionary<string, object>)item)["name"] + ".vfxforge",
                                              ImportAssetOptions.ForceUpdate);
            }
            var previous = EditorSceneManager.GetActiveScene().path;
            if (!EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo())
                return;
            var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);

            var rt = new RenderTexture(size, size, 24, RenderTextureFormat.ARGB32, RenderTextureReadWrite.sRGB);
            rt.antiAliasing = 1;
            var camObject = new GameObject("Capture Camera");
            var cam = camObject.AddComponent<Camera>();
            cam.clearFlags = CameraClearFlags.SolidColor;
            cam.backgroundColor = new Color(0f, 0f, 0f, 1f);
            cam.targetTexture = rt;
            cam.nearClipPlane = 0.05f;
            cam.farClipPlane = 1000f;
            cam.allowHDR = false;
            cam.allowMSAA = false;

            foreach (var item in (List<object>)plan["effects"])
            {
                var e = (Dictionary<string, object>)item;
                string name = (string)e["name"];
                string assetPath = "Assets/VFXForge/Effects/" + name + ".vfxforge";
                var prefab = AssetDatabase.LoadAssetAtPath<GameObject>(assetPath);
                if (prefab == null)
                {
                    log.AppendLine(name + ": NOT FOUND at " + assetPath);
                    continue;
                }
                bool flat = e.ContainsKey("flat") && (bool)e["flat"];
                float x = (float)Num(e, "x", 0), y = (float)Num(e, "y", 0), height = (float)Num(e, "height", 6);
                if (flat)
                {
                    cam.orthographic = true;
                    cam.orthographicSize = height * 0.5f;
                    cam.transform.position = new Vector3(x, y, -50f);
                    cam.transform.rotation = Quaternion.identity;
                }
                else
                {
                    // As VFX Forge's orbiting camera sees it.
                    float yaw = (float)Num(e, "yaw", 30) * Mathf.Deg2Rad, pitch = (float)Num(e, "pitch", 15) * Mathf.Deg2Rad;
                    float distance = (float)Num(e, "distance", 10);
                    var target = new Vector3(x, y, 0f);
                    // VFX Forge looks from +Z; the export mirrors Z, so Unity's front is -Z.
                    var back = new Vector3(Mathf.Sin(yaw) * Mathf.Cos(pitch), Mathf.Sin(pitch), -Mathf.Cos(yaw) * Mathf.Cos(pitch));
                    cam.orthographic = false;
                    cam.fieldOfView = (float)Num(e, "fov", 40);
                    cam.transform.position = target + back * distance;
                    cam.transform.LookAt(target, Vector3.up);
                }
                var instance = (GameObject)PrefabUtility.InstantiatePrefab(prefab, scene);
                foreach (var r in instance.GetComponentsInChildren<ParticleSystemRenderer>())
                {
                    var m = r.sharedMaterial;
                    if (m == null)
                    {
                        log.AppendLine("  " + r.name + ": NO MATERIAL (renderer " + (r.enabled ? "on" : "off") + ")");
                        continue;
                    }
                    var tex = m.HasProperty("_MainTex") ? m.GetTexture("_MainTex") : null;
                    log.AppendLine("  " + r.name + ": shader " + (m.shader ? m.shader.name + (m.shader.isSupported ? "" : " (NOT SUPPORTED)") : "none") +
                                   ", picture " + (tex ? tex.name + " " + tex.width + "x" + tex.height : "NONE") +
                                   ", shape " + (m.HasProperty("_Shape") ? m.GetFloat("_Shape").ToString() : "?") +
                                   ", additive " + (m.HasProperty("_Additive") ? m.GetFloat("_Additive").ToString() : "?") +
                                   ", mode " + r.renderMode);
                }
                instance.transform.position = Vector3.zero;
                var system = instance.GetComponent<ParticleSystem>();
                foreach (var t in (List<object>)e["times"])
                {
                    float at = (float)System.Convert.ToDouble(t);
                    system.Simulate(at, true, true, true);
                    Render(cam, rt);
                    var picture = new Texture2D(size, size, TextureFormat.RGBA32, false, false);
                    var was = RenderTexture.active;
                    RenderTexture.active = rt;
                    picture.ReadPixels(new Rect(0, 0, size, size), 0, 0);
                    picture.Apply();
                    RenderTexture.active = was;
                    string file = Path.Combine(folder, name + "_" + at.ToString("0.00", System.Globalization.CultureInfo.InvariantCulture) + "_unity.png");
                    File.WriteAllBytes(file, picture.EncodeToPNG());
                    Object.DestroyImmediate(picture);
                    int alive = 0;
                    foreach (var ps in instance.GetComponentsInChildren<ParticleSystem>())
                        alive += ps.particleCount;
                    log.AppendLine(name + " @" + at + "s: " + alive + " particles");
                }
                Object.DestroyImmediate(instance);
            }
            cam.targetTexture = null;
            Object.DestroyImmediate(camObject);
            rt.Release();
            File.WriteAllText(Path.Combine(folder, "unity-log.txt"), log.ToString());
            Debug.Log("VFX Forge capture done:\n" + log);
            if (!string.IsNullOrEmpty(previous))
                EditorSceneManager.OpenScene(previous);
        }

        static void Render(Camera cam, RenderTexture rt)
        {
            var request = new RenderPipeline.StandardRequest { destination = rt };
            if (GraphicsSettings.currentRenderPipeline != null && RenderPipeline.SupportsRenderRequest(cam, request))
                RenderPipeline.SubmitRenderRequest(cam, request);
            else
                cam.Render();
        }

        static double Num(Dictionary<string, object> o, string key, double fallback)
        {
            return o != null && o.TryGetValue(key, out var v) && v != null ? System.Convert.ToDouble(v) : fallback;
        }
    }
}
