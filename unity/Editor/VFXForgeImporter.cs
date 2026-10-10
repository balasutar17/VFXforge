// VFX Forge: turns a .vfxforge file into a prefab of Unity Particle Systems.
//
// Export an effect from VFX Forge into this project and Unity imports it
// here: a prefab appears next to the file, with one child Particle System
// per layer. Export again and the prefab, and every scene that uses it,
// update. Everything in the prefab is ordinary Unity Particle System
// settings and can be changed in the Inspector, but those changes are
// replaced the next time the effect is exported. To keep your own changes,
// unpack a copy of the prefab first.
//
// VFX Forge has already translated every setting into Unity terms; this
// script only reads the numbers and sets the fields.
using System;
using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEditor.AssetImporters;
using UnityEngine;

namespace VFXForge.EditorTools
{
    [ScriptedImporter(3, "vfxforge")]
    public class VFXForgeImporter : ScriptedImporter
    {
        public const string ShaderPath = "Assets/VFXForge/Shaders/VFXForgeParticle.shader";
        public const string AtlasPath = "Assets/VFXForge/Textures/VFXForgeShapes.png";
        public const string ImagesFolder = "Assets/VFXForge/Images/";

        // How streak shapes line up with a stretched billboard's texture.
        // 0: the shape's head is along the texture's U axis.
        const float StretchedMotionAxis = 0f;

        public override void OnImportAsset(AssetImportContext ctx)
        {
            Dictionary<string, object> root;
            try
            {
                root = VFXForgeJson.Parse(File.ReadAllText(ctx.assetPath)) as Dictionary<string, object>;
            }
            catch (Exception e)
            {
                ctx.LogImportError("This VFX Forge effect could not be read: " + e.Message);
                return;
            }
            if (root == null || Str(root, "format") != "vfxforge.unity")
            {
                ctx.LogImportError("This file is not a VFX Forge export.");
                return;
            }
            if (Num(root, "formatVersion", 1) > 1)
            {
                ctx.LogImportWarning("This effect was exported by a newer VFX Forge. Update the VFX Forge folder in this project by exporting from that version.");
            }

            // Build after the shader and the atlas, and again if either changes.
            ctx.DependsOnArtifact(ShaderPath);
            ctx.DependsOnArtifact(AtlasPath);
            Shader shader = AssetDatabase.LoadAssetAtPath<Shader>(ShaderPath);
            if (shader == null)
                shader = Shader.Find("VFX Forge/Particle");
            Texture2D atlas = AssetDatabase.LoadAssetAtPath<Texture2D>(AtlasPath);
            if (shader == null || atlas == null)
                ctx.LogImportWarning("The VFX Forge shader or shape picture is missing from Assets/VFXForge. Export from VFX Forge again to restore them.");

            var atlasInfo = Obj(root, "atlas");
            float columns = (float)Num(atlasInfo, "columns", 8);
            float rows = (float)Num(atlasInfo, "rows", 4);
            float duration = Mathf.Max(0.05f, (float)Num(root, "duration", 2));
            bool loop = Bool(root, "loop", true);

            string name = Path.GetFileNameWithoutExtension(ctx.assetPath);
            var top = new GameObject(name);

            // The top object holds a silent system that only times the whole
            // effect, so playing it plays every layer together.
            var topSystem = top.AddComponent<ParticleSystem>();
            topSystem.Stop(true, ParticleSystemStopBehavior.StopEmittingAndClear);
            var topMain = topSystem.main;
            topMain.duration = duration;
            topMain.loop = loop;
            topMain.playOnAwake = true;
            topMain.startLifetime = 0.01f;
            topMain.startSize = 0f;
            topMain.maxParticles = 1;
            var topEmission = topSystem.emission;
            topEmission.enabled = false;
            var topShape = topSystem.shape;
            topShape.enabled = false;
            top.GetComponent<ParticleSystemRenderer>().enabled = false;

            var layers = List(root, "layers");
            for (int i = 0; i < layers.Count; i++)
            {
                var layer = layers[i] as Dictionary<string, object>;
                if (layer == null)
                    continue;
                var child = new GameObject(Str(layer, "name", "Layer " + (i + 1)));
                child.transform.SetParent(top.transform, false);
                var system = child.AddComponent<ParticleSystem>();
                system.Stop(true, ParticleSystemStopBehavior.StopEmittingAndClear);
                // A layer that draws the artist's picture waits for it too.
                Texture2D picture = null;
                string picturePath = Str(Obj(layer, "render"), "picture", "");
                if (picturePath.Length > 0)
                {
                    ctx.DependsOnArtifact(picturePath);
                    picture = AssetDatabase.LoadAssetAtPath<Texture2D>(picturePath);
                    if (picture == null)
                        ctx.LogImportWarning("The picture " + picturePath + " is missing, so the layer \"" + child.name + "\" draws its shape. Export from VFX Forge again to restore it.");
                }
                Material trailMaterial;
                Material material = BuildLayer(system, layer, duration, loop, shader, atlas, columns, rows, picture,
                                               out trailMaterial);
                if (material != null)
                    ctx.AddObjectToAsset("material " + i, material);
                if (trailMaterial != null)
                    ctx.AddObjectToAsset("trail material " + i, trailMaterial);

                // The layer's sound, played in step by VFXForgeSound.
                var soundInfo = Obj(layer, "sound");
                if (soundInfo != null)
                {
                    string clipPath = Str(soundInfo, "clip", "");
                    ctx.DependsOnArtifact(clipPath);
                    var clip = AssetDatabase.LoadAssetAtPath<AudioClip>(clipPath);
                    if (clip == null)
                    {
                        ctx.LogImportWarning("The sound " + clipPath + " is missing, so the layer \"" + child.name + "\" is silent. Export from VFX Forge again to restore it.");
                    }
                    else
                    {
                        var player = child.AddComponent<VFXForge.VFXForgeSound>();
                        player.timer = topSystem;
                        player.clip = clip;
                        player.times = Floats(soundInfo, "times");
                        player.skips = Floats(soundInfo, "skips");
                        player.volume = (float)Num(soundInfo, "volume", 1);
                        player.pitch = (float)Num(soundInfo, "pitch", 0);
                        player.pan = (float)Num(soundInfo, "pan", 0);
                        player.randomPitch = (float)Num(soundInfo, "randomPitch", 0);
                        player.randomVolume = (float)Num(soundInfo, "randomVolume", 0);
                        player.fadeIn = (float)Num(soundInfo, "fadeIn", 0);
                        player.fadeOut = (float)Num(soundInfo, "fadeOut", 0);
                        player.trimStart = (float)Num(soundInfo, "trimStart", 0);
                        player.length = (float)Num(soundInfo, "length", 0);
                        player.loop = Bool(soundInfo, "loop", false);
                        player.loopEnd = (float)Num(soundInfo, "loopEnd", 0);
                    }
                }
                child.SetActive(Bool(layer, "enabled", true));
            }

            ctx.AddObjectToAsset("effect", top);
            ctx.SetMainObject(top);
        }

        static Material BuildLayer(ParticleSystem system, Dictionary<string, object> layer, float duration,
                                   bool loop, Shader shader, Texture2D atlas, float columns, float rows,
                                   Texture2D picture, out Material trailMaterial)
        {
            trailMaterial = null;
            // ---- main
            var main = system.main;
            main.duration = duration;
            main.loop = loop;
            main.playOnAwake = true;
            main.startDelay = 0f;
            main.startLifetime = Curve(layer, "startLifetime", 1);
            main.startSpeed = Curve(layer, "startSpeed", 0);
            main.startSize = Curve(layer, "startSize", 1);
            main.startRotation = Curve(layer, "startRotation", 0);
            main.startColor = StartColor(layer);
            main.gravityModifier = 0f;
            main.simulationSpace = ParticleSystemSimulationSpace.Local;
            main.scalingMode = ParticleSystemScalingMode.Hierarchy;
            main.maxParticles = (int)Num(layer, "maxParticles", 1000);
            system.useAutoRandomSeed = false;
            system.randomSeed = (uint)Num(layer, "seed", 1);

            // ---- emission
            var emission = system.emission;
            emission.enabled = true;
            emission.rateOverTime = Curve(layer, "rate", 0);
            var bursts = new List<ParticleSystem.Burst>();
            foreach (var item in List(layer, "bursts"))
            {
                var pair = item as List<object>;
                if (pair == null || pair.Count < 2)
                    continue;
                float time = Mathf.Clamp((float)(double)pair[0], 0f, duration);
                short count = (short)Mathf.Clamp((float)(double)pair[1], 0f, 32767f);
                bursts.Add(new ParticleSystem.Burst(time, count));
            }
            emission.SetBursts(bursts.ToArray());

            // ---- where particles appear
            var shapeInfo = Obj(layer, "shape");
            var shape = system.shape;
            shape.enabled = true;
            string type = Str(shapeInfo, "type", "circle");
            if (type == "sphere")
                shape.shapeType = ParticleSystemShapeType.Sphere;
            else if (type == "cone")
                shape.shapeType = ParticleSystemShapeType.Cone;
            else if (type == "box")
                shape.shapeType = Bool(shapeInfo, "edge", false) ? ParticleSystemShapeType.BoxEdge : ParticleSystemShapeType.Box;
            else
                shape.shapeType = ParticleSystemShapeType.Circle;
            shape.radius = (float)Num(shapeInfo, "radius", 0.0001);
            shape.radiusThickness = (float)Num(shapeInfo, "radiusThickness", 1);
            shape.arc = (float)Num(shapeInfo, "arc", 360);
            shape.arcMode = ParticleSystemShapeMultiModeValue.Random;
            shape.angle = (float)Num(shapeInfo, "angle", 0);
            shape.scale = Vec(shapeInfo, "scale", Vector3.one);
            shape.rotation = Vec(shapeInfo, "rotation", Vector3.zero);
            shape.position = Vec(shapeInfo, "position", Vector3.zero);
            shape.randomDirectionAmount = (float)Num(shapeInfo, "randomDirection", 0);
            shape.sphericalDirectionAmount = (float)Num(shapeInfo, "sphericalDirection", 0);

            // ---- movement
            if (layer.ContainsKey("force") && layer["force"] != null)
            {
                Vector3 force = Vec(layer, "force", Vector3.zero);
                var forces = system.forceOverLifetime;
                forces.enabled = true;
                forces.space = ParticleSystemSimulationSpace.World;
                forces.x = force.x;
                forces.y = force.y;
                forces.z = force.z;
            }
            if (layer.ContainsKey("drag") && layer["drag"] != null)
            {
                var limit = system.limitVelocityOverLifetime;
                limit.enabled = true;
                limit.limit = 100000f;
                limit.dampen = 0f;
                limit.drag = (float)Num(layer, "drag", 0);
                limit.multiplyDragByParticleSize = false;
                limit.multiplyDragByParticleVelocity = false;
            }
            if (layer.ContainsKey("spin") && layer["spin"] != null)
            {
                var rotation = system.rotationOverLifetime;
                rotation.enabled = true;
                rotation.z = Curve(layer, "spin", 0);
            }

            // ---- over each particle's life
            if (layer.ContainsKey("sizeOverLife") && layer["sizeOverLife"] != null)
            {
                var size = system.sizeOverLifetime;
                size.enabled = true;
                size.size = Curve(layer, "sizeOverLife", 1);
            }
            var gradientInfo = Obj(layer, "colorOverLife");
            if (gradientInfo != null)
            {
                var colorKeys = new List<GradientColorKey>();
                foreach (var item in List(gradientInfo, "colors"))
                {
                    var k = item as List<object>;
                    if (k != null && k.Count >= 4)
                        colorKeys.Add(new GradientColorKey(new Color((float)(double)k[1], (float)(double)k[2], (float)(double)k[3]), (float)(double)k[0]));
                }
                var alphaKeys = new List<GradientAlphaKey>();
                foreach (var item in List(gradientInfo, "alphas"))
                {
                    var k = item as List<object>;
                    if (k != null && k.Count >= 2)
                        alphaKeys.Add(new GradientAlphaKey((float)(double)k[1], (float)(double)k[0]));
                }
                var gradient = new Gradient();
                gradient.SetKeys(colorKeys.ToArray(), alphaKeys.ToArray());
                var colour = system.colorOverLifetime;
                colour.enabled = true;
                colour.color = new ParticleSystem.MinMaxGradient(gradient);
            }

            // ---- drawing
            var renderInfo = Obj(layer, "render");
            var renderer = system.GetComponent<ParticleSystemRenderer>();
            string mode = Str(renderInfo, "mode", "billboard");
            if (mode == "stretched")
            {
                renderer.renderMode = ParticleSystemRenderMode.Stretch;
                renderer.velocityScale = (float)Num(renderInfo, "velocityScale", 0);
                renderer.lengthScale = (float)Num(renderInfo, "lengthScale", 1);
                renderer.cameraVelocityScale = 0f;
            }
            else
            {
                renderer.renderMode = ParticleSystemRenderMode.Billboard;
                renderer.alignment = mode == "plane" ? ParticleSystemRenderSpace.Local : ParticleSystemRenderSpace.View;
            }
            renderer.sortMode = ParticleSystemSortMode.YoungestInFront;
            renderer.sortingOrder = (int)Num(renderInfo, "order", 0);
            renderer.minParticleSize = 0f;
            renderer.maxParticleSize = 10f;
            renderer.enabled = Bool(layer, "drawn", true);

            // ---- a sprite sheet, played by Unity's own Texture Sheet Animation
            var sheetInfo = Obj(renderInfo, "sheet");
            var sheet = system.textureSheetAnimation;
            sheet.enabled = picture != null && sheetInfo != null;
            if (sheet.enabled)
            {
                sheet.mode = ParticleSystemAnimationMode.Grid;
                sheet.numTilesX = Mathf.Max(1, (int)Num(sheetInfo, "columns", 1));
                sheet.numTilesY = Mathf.Max(1, (int)Num(sheetInfo, "rows", 1));
                sheet.animation = ParticleSystemAnimationType.WholeSheet;
                sheet.timeMode = ParticleSystemAnimationTimeMode.Lifetime;
                sheet.frameOverTime = Curve(sheetInfo, "frameOverTime", 0);
                sheet.startFrame = Curve(sheetInfo, "startFrame", 0);
                sheet.cycleCount = Mathf.Max(1, (int)Num(sheetInfo, "cycles", 1));
            }

            // ---- a ribbon behind each particle, with Unity's own Trails module
            var trailInfo = Obj(renderInfo, "trail");
            var trails = system.trails;
            trails.enabled = trailInfo != null && shader != null;
            if (trails.enabled)
            {
                trails.mode = ParticleSystemTrailMode.PerParticle;
                trails.ratio = 1f;
                trails.lifetime = (float)Num(trailInfo, "ratio", 0.3);
                trails.minVertexDistance = 0.05f;
                trails.worldSpace = false;
                trails.dieWithParticles = true;
                trails.sizeAffectsWidth = true;
                trails.inheritParticleColor = true;
                trails.textureMode = ParticleSystemTrailTextureMode.Stretch;
                trails.widthOverTrail = new ParticleSystem.MinMaxCurve((float)Num(trailInfo, "width", 0.6),
                                                                       AnimationCurve.Linear(0f, 1f, 1f, 0f));
                var fade = new Gradient();
                fade.SetKeys(new[] { new GradientColorKey(Color.white, 0f), new GradientColorKey(Color.white, 1f) },
                             new[] { new GradientAlphaKey(1f, 0f), new GradientAlphaKey(0f, 1f) });
                trails.colorOverTrail = new ParticleSystem.MinMaxGradient(fade);
                trailMaterial = new Material(shader);
                trailMaterial.name = system.gameObject.name + " trail";
                trailMaterial.SetFloat("_Ribbon", 1f);
                trailMaterial.SetFloat("_Glow", (float)Num(renderInfo, "glow", 1));
                trailMaterial.SetFloat("_Additive", Bool(renderInfo, "additive", false) ? 1f : 0f);
                renderer.trailMaterial = trailMaterial;
            }

            if (shader == null)
                return null;
            var material = new Material(shader);
            material.name = system.gameObject.name;
            if (picture != null)
                material.SetTexture("_MainTex", picture);
            else if (atlas != null)
                material.SetTexture("_MainTex", atlas);
            material.SetFloat("_Picture", picture != null ? 1f : 0f);
            material.SetFloat("_Shape", (float)Num(renderInfo, "shape", 0));
            material.SetFloat("_Columns", columns);
            material.SetFloat("_Rows", rows);
            material.SetFloat("_Glow", (float)Num(renderInfo, "glow", 1));
            material.SetFloat("_Additive", Bool(renderInfo, "additive", false) ? 1f : 0f);
            material.SetFloat("_MotionAxis", mode == "stretched" ? StretchedMotionAxis : 0f);
            renderer.sharedMaterial = material;
            return material;
        }

        // ---------------------------------------------------------- reading

        // A number, a random range [min, max], or a curve
        // {"multiplier": m, "keys": [[time, value, inSlope, outSlope], ...]}.
        static ParticleSystem.MinMaxCurve Curve(Dictionary<string, object> from, string key, double fallback)
        {
            object value;
            if (from == null || !from.TryGetValue(key, out value) || value == null)
                return new ParticleSystem.MinMaxCurve((float)fallback);
            if (value is double)
                return new ParticleSystem.MinMaxCurve((float)(double)value);
            var pair = value as List<object>;
            if (pair != null && pair.Count >= 2)
                return new ParticleSystem.MinMaxCurve((float)(double)pair[0], (float)(double)pair[1]);
            var curve = value as Dictionary<string, object>;
            if (curve != null)
            {
                var keys = new List<Keyframe>();
                foreach (var item in List(curve, "keys"))
                {
                    var k = item as List<object>;
                    if (k != null && k.Count >= 4)
                        keys.Add(new Keyframe((float)(double)k[0], (float)(double)k[1], (float)(double)k[2], (float)(double)k[3]));
                }
                return new ParticleSystem.MinMaxCurve((float)Num(curve, "multiplier", 1), new AnimationCurve(keys.ToArray()));
            }
            return new ParticleSystem.MinMaxCurve((float)fallback);
        }

        static ParticleSystem.MinMaxGradient StartColor(Dictionary<string, object> layer)
        {
            object value;
            if (!layer.TryGetValue("startColor", out value))
                return new ParticleSystem.MinMaxGradient(Color.white);
            var list = value as List<object>;
            if (list == null || list.Count == 0)
                return new ParticleSystem.MinMaxGradient(Color.white);
            if (list[0] is List<object> && list.Count >= 2)
                return new ParticleSystem.MinMaxGradient(ToColor(list[0] as List<object>), ToColor(list[1] as List<object>));
            return new ParticleSystem.MinMaxGradient(ToColor(list));
        }

        static Color ToColor(List<object> c)
        {
            if (c == null || c.Count < 4)
                return Color.white;
            return new Color((float)(double)c[0], (float)(double)c[1], (float)(double)c[2], (float)(double)c[3]);
        }

        static float[] Floats(Dictionary<string, object> from, string key)
        {
            var list = List(from, key);
            var result = new float[list.Count];
            for (int k = 0; k < list.Count; k++)
                result[k] = list[k] is double ? (float)(double)list[k] : 0f;
            return result;
        }

        static Vector3 Vec(Dictionary<string, object> from, string key, Vector3 fallback)
        {
            object value;
            if (from == null || !from.TryGetValue(key, out value))
                return fallback;
            var v = value as List<object>;
            if (v == null || v.Count < 3)
                return fallback;
            return new Vector3((float)(double)v[0], (float)(double)v[1], (float)(double)v[2]);
        }

        static Dictionary<string, object> Obj(Dictionary<string, object> from, string key)
        {
            object value;
            if (from == null || !from.TryGetValue(key, out value))
                return null;
            return value as Dictionary<string, object>;
        }

        static List<object> List(Dictionary<string, object> from, string key)
        {
            object value;
            if (from == null || !from.TryGetValue(key, out value))
                return new List<object>();
            return value as List<object> ?? new List<object>();
        }

        static string Str(Dictionary<string, object> from, string key, string fallback = "")
        {
            object value;
            if (from == null || !from.TryGetValue(key, out value))
                return fallback;
            return value as string ?? fallback;
        }

        static double Num(Dictionary<string, object> from, string key, double fallback)
        {
            object value;
            if (from == null || !from.TryGetValue(key, out value) || !(value is double))
                return fallback;
            return (double)value;
        }

        static bool Bool(Dictionary<string, object> from, string key, bool fallback)
        {
            object value;
            if (from == null || !from.TryGetValue(key, out value) || !(value is bool))
                return fallback;
            return (bool)value;
        }
    }

    // The shape picture holds shapes, not colours: it must be read exactly
    // as stored, without colour correction or compression. The artist's own
    // pictures are colours: kept sharp and uncompressed, as painted.
    public class VFXForgeTextureSettings : AssetPostprocessor
    {
        void OnPreprocessTexture()
        {
            if (assetPath.StartsWith(VFXForgeImporter.ImagesFolder, StringComparison.Ordinal))
            {
                var picture = (TextureImporter)assetImporter;
                picture.textureType = TextureImporterType.Default;
                picture.sRGBTexture = true;
                picture.alphaSource = TextureImporterAlphaSource.FromInput;
                picture.alphaIsTransparency = true;
                picture.mipmapEnabled = true;
                picture.wrapMode = TextureWrapMode.Clamp;
                picture.filterMode = FilterMode.Bilinear;
                picture.npotScale = TextureImporterNPOTScale.None;
                picture.textureCompression = TextureImporterCompression.Uncompressed;
                picture.maxTextureSize = 8192;
                return;
            }
            if (assetPath != VFXForgeImporter.AtlasPath)
                return;
            var importer = (TextureImporter)assetImporter;
            importer.textureType = TextureImporterType.Default;
            importer.sRGBTexture = false;
            importer.alphaSource = TextureImporterAlphaSource.FromInput;
            importer.alphaIsTransparency = false;
            importer.mipmapEnabled = true;
            importer.wrapMode = TextureWrapMode.Clamp;
            importer.filterMode = FilterMode.Bilinear;
            importer.npotScale = TextureImporterNPOTScale.None;
            importer.textureCompression = TextureImporterCompression.Uncompressed;
            importer.maxTextureSize = 2048;
        }
    }
}
