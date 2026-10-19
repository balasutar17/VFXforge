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
    [ScriptedImporter(9, "vfxforge")]
    public class VFXForgeImporter : ScriptedImporter
    {
        public const string ShaderPath = "Assets/VFXForge/Shaders/VFXForgeParticle.shader";
        public const string AtlasPath = "Assets/VFXForge/Textures/VFXForgeShapes.png";
        public const string ImagesFolder = "Assets/VFXForge/Images/";
        public const string ShapesFolder = "Assets/VFXForge/Shapes/";

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
                // A shape from the library comes as a picture of its own,
                // laid out like one tile of the atlas.
                Texture2D layerAtlas = atlas;
                float layerColumns = columns, layerRows = rows;
                string shapePath = Str(Obj(layer, "render"), "shapePicture", "");
                if (shapePath.Length > 0)
                {
                    ctx.DependsOnArtifact(shapePath);
                    Texture2D shapePicture = AssetDatabase.LoadAssetAtPath<Texture2D>(shapePath);
                    if (shapePicture != null)
                    {
                        layerAtlas = shapePicture;
                        layerColumns = 1f;
                        layerRows = 1f;
                    }
                    else
                        ctx.LogImportWarning("The shape picture " + shapePath + " is missing, so the layer \"" + child.name + "\" draws a soft dot. Export from VFX Forge again to restore it.");
                }
                Material trailMaterial;
                Mesh pathMesh;
                Material material = BuildLayer(system, layer, duration, loop, shader, layerAtlas, layerColumns, layerRows,
                                               picture, out trailMaterial, out pathMesh);
                if (pathMesh != null)
                    ctx.AddObjectToAsset("path " + i, pathMesh);
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
                // Held frames: VFXForgeHold moves this layer on only when a
                // new drawing is due, so it plays "on twos" or "on threes".
                float hold = (float)Num(layer, "hold", 0);
                if (hold > 0f)
                {
                    var holder = child.AddComponent<VFXForge.VFXForgeHold>();
                    holder.timer = topSystem;
                    holder.drawingsPerSecond = hold;
                }
                child.SetActive(Bool(layer, "enabled", true));
            }

            // The 3D model the effect was made on, when it came along: drawn
            // by the VFX Forge model shader, which writes depth so particles
            // behind it are hidden, as in VFX Forge.
            var modelInfo = Obj(root, "model");
            if (modelInfo != null)
                AddModel(ctx, top, Str(modelInfo, "file", ""));

            ctx.AddObjectToAsset("effect", top);
            ctx.SetMainObject(top);
        }

        const string ModelShaderPath = "Assets/VFXForge/Shaders/VFXForgeModel.shader";

        static void AddModel(AssetImportContext ctx, GameObject top, string path)
        {
            if (path.Length == 0)
                return;
            string full = Path.Combine(Path.GetDirectoryName(Application.dataPath), path);
            ctx.DependsOnSourceAsset(path);
            ctx.DependsOnArtifact(ModelShaderPath);
            if (!File.Exists(full))
            {
                ctx.LogImportWarning("The model " + path + " is missing, so the effect has no model. Export from VFX Forge again to restore it.");
                return;
            }
            byte[] data = File.ReadAllBytes(full);
            if (data.Length < 16 || data[0] != 'V' || data[1] != 'F' || data[2] != 'X' || data[3] != 'M')
            {
                ctx.LogImportWarning("The model " + path + " is not a VFX Forge model.");
                return;
            }
            int vertexCount = (int)System.BitConverter.ToUInt32(data, 8);
            int indexCount = (int)System.BitConverter.ToUInt32(data, 12);
            int at = 16;
            long need = 16L + vertexCount * 24L + (((long)vertexCount * 3 + 3) & ~3L) + indexCount * 4L;
            if (vertexCount <= 0 || indexCount <= 0 || data.Length < need)
            {
                ctx.LogImportWarning("The model " + path + " is cut short.");
                return;
            }
            var positions = new Vector3[vertexCount];
            var normals = new Vector3[vertexCount];
            var colors = new Color32[vertexCount];
            for (int i = 0; i < vertexCount; i++, at += 12)
                positions[i] = new Vector3(System.BitConverter.ToSingle(data, at), System.BitConverter.ToSingle(data, at + 4), System.BitConverter.ToSingle(data, at + 8));
            for (int i = 0; i < vertexCount; i++, at += 12)
                normals[i] = new Vector3(System.BitConverter.ToSingle(data, at), System.BitConverter.ToSingle(data, at + 4), System.BitConverter.ToSingle(data, at + 8));
            for (int i = 0; i < vertexCount; i++, at += 3)
                colors[i] = new Color32(data[at], data[at + 1], data[at + 2], 255);
            at = (int)(16L + vertexCount * 24L + (((long)vertexCount * 3 + 3) & ~3L));
            var triangles = new int[indexCount];
            for (int i = 0; i < indexCount; i++, at += 4)
                triangles[i] = (int)System.BitConverter.ToUInt32(data, at);

            var mesh = new Mesh { name = "Model" };
            if (vertexCount > 65000)
                mesh.indexFormat = UnityEngine.Rendering.IndexFormat.UInt32;
            mesh.vertices = positions;
            mesh.normals = normals;
            mesh.colors32 = colors;
            mesh.triangles = triangles;
            mesh.RecalculateBounds();

            Shader shader = AssetDatabase.LoadAssetAtPath<Shader>(ModelShaderPath);
            if (shader == null)
                shader = Shader.Find("VFX Forge/Model");
            if (shader == null)
                ctx.LogImportWarning("The VFX Forge model shader is missing from Assets/VFXForge/Shaders. Export from VFX Forge again to restore it.");
            var material = new Material(shader != null ? shader : Shader.Find("Hidden/InternalErrorShader")) { name = "Model" };

            // Positions are already in the effect's world: this object stays at the top's origin.
            var modelObject = new GameObject("Model");
            modelObject.transform.SetParent(top.transform, false);
            modelObject.AddComponent<MeshFilter>().sharedMesh = mesh;
            modelObject.AddComponent<MeshRenderer>().sharedMaterial = material;
            ctx.AddObjectToAsset("model mesh", mesh);
            ctx.AddObjectToAsset("model material", material);
        }

        static Material BuildLayer(ParticleSystem system, Dictionary<string, object> layer, float duration,
                                   bool loop, Shader shader, Texture2D atlas, float columns, float rows,
                                   Texture2D picture, out Material trailMaterial, out Mesh pathMesh)
        {
            bool turnAlong = false;
            trailMaterial = null;
            pathMesh = null;
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
            else if (type == "path")
                pathMesh = PathShape(system, shapeInfo, layer, duration, out turnAlong);
            else
                shape.shapeType = ParticleSystemShapeType.Circle;
            if (type != "path")
            {
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
            }
            else
            {
                shape.position = Vec(shapeInfo, "position", Vector3.zero);
            }

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
            // Speed over Life and Orbit both live in Velocity over Lifetime.
            bool eases = layer.ContainsKey("speedModifier") && layer["speedModifier"] != null;
            var orbitInfo = Obj(layer, "orbit");
            if (eases || orbitInfo != null)
            {
                var velocity = system.velocityOverLifetime;
                velocity.enabled = true;
                velocity.space = ParticleSystemSimulationSpace.Local;
                velocity.x = 0f;
                velocity.y = 0f;
                velocity.z = 0f;
                velocity.speedModifier = eases ? Curve(layer, "speedModifier", 1) : new ParticleSystem.MinMaxCurve(1f);
                if (orbitInfo != null)
                {
                    // Turning about the axis that points at the viewer (or, for
                    // a layer on the ground, about the upright), round the
                    // layer's own position.
                    Vector3 center = Vec(orbitInfo, "center", Vector3.zero);
                    velocity.orbitalX = 0f;
                    velocity.orbitalY = (float)Num(orbitInfo, "y", 0);
                    velocity.orbitalZ = (float)Num(orbitInfo, "z", 0);
                    velocity.orbitalOffsetX = center.x;
                    velocity.orbitalOffsetY = center.y;
                    velocity.orbitalOffsetZ = center.z;
                    velocity.radial = 0f;

                    // VFX Forge turns a particle's heading with it, so one
                    // sent out from the centre runs straight out along a
                    // turning spoke. Unity keeps the heading and would carry
                    // it round in a circle instead, and its Speed Modifier
                    // would slow the turning too; moving out from the centre
                    // (Radial) instead of by its own speed gives VFX Forge's path.
                    string kind = Str(shapeInfo, "type", "");
                    Vector3 from = Vec(shapeInfo, "position", Vector3.zero);
                    bool fromCentre = (from - center).sqrMagnitude < 1e-8f &&
                                      Num(shapeInfo, "radius", 1) <= 0.001 &&
                                      (kind == "sphere" || (kind == "cone" && Num(shapeInfo, "angle", 1) <= 0.0));
                    if (fromCentre && List(layer, "force").Count == 0 && !(layer.ContainsKey("drag") && layer["drag"] != null))
                    {
                        var moving = system.main;
                        ParticleSystem.MinMaxCurve speed = moving.startSpeed;
                        float low = speed.mode == ParticleSystemCurveMode.TwoConstants ? speed.constantMin : speed.constant;
                        float high = speed.mode == ParticleSystemCurveMode.TwoConstants ? speed.constantMax : speed.constant;
                        AnimationCurve ease = eases ? ScaledCurve(velocity.speedModifier, 1f) : AnimationCurve.Constant(0f, 1f, 1f);
                        velocity.radial = Mathf.Approximately(low, high)
                            ? new ParticleSystem.MinMaxCurve(low, ease)
                            : new ParticleSystem.MinMaxCurve(1f, ScaledCurve(ease, low), ScaledCurve(ease, high));
                        velocity.speedModifier = new ParticleSystem.MinMaxCurve(1f);
                        moving.startSpeed = 0f;
                        if (kind == "cone")
                        {
                            // Out from just off the centre, the way it was aimed.
                            var shapeModule = system.shape;
                            shapeModule.position = from + Quaternion.Euler(shapeModule.rotation) * Vector3.forward * 0.001f;
                        }
                    }
                }
            }
            var noiseInfo = Obj(layer, "noise");
            if (noiseInfo != null)
            {
                // Turbulence: Unity's own Noise module carries the particles
                // about. Its swirls are Unity's, so the wandering is as strong
                // and as wide as in VFX Forge but does not follow the same paths.
                var noise = system.noise;
                noise.enabled = true;
                noise.quality = ParticleSystemNoiseQuality.High;
                noise.octaveCount = 1;
                noise.damping = false;
                noise.frequency = (float)Num(noiseInfo, "frequency", 0.5);
                noise.scrollSpeed = (float)Num(noiseInfo, "scrollSpeed", 0);
                noise.positionAmount = 1f;
                noise.rotationAmount = 0f;
                noise.sizeAmount = 0f;
                float strength = (float)Num(noiseInfo, "strength", 0);
                noise.separateAxes = true;
                noise.strengthX = strength;
                noise.strengthY = strength;
                noise.strengthZ = Bool(noiseInfo, "flat", true) ? 0f : strength;
            }

            // ---- over each particle's life
            var squashInfo = Obj(layer, "squash");
            if (squashInfo != null && squashInfo.ContainsKey("x"))
            {
                // Squash and stretch: width and height each follow their own
                // curve, which already holds the size over life.
                var size = system.sizeOverLifetime;
                size.enabled = true;
                size.separateAxes = true;
                size.x = Curve(squashInfo, "x", 1);
                size.y = Curve(squashInfo, "y", 1);
                size.z = Curve(squashInfo, "z", 1);
            }
            else if (layer.ContainsKey("sizeOverLife") && layer["sizeOverLife"] != null)
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
            // Colours as VFX Forge picked them, in either colour space.
#if UNITY_2022_1_OR_NEWER
            renderer.applyActiveColorSpace = true;
#endif
            string mode = Str(renderInfo, "mode", "billboard");
            if (turnAlong)
            {
                // Turned along a path: a still particle drawn as a streak
                // that keeps its own length, pointed along the path by a
                // drift too slow to see.
                renderer.renderMode = ParticleSystemRenderMode.Stretch;
                renderer.velocityScale = 0f;
                renderer.lengthScale = 1f;
                renderer.cameraVelocityScale = 0f;
                mode = "stretched";
            }
            else if (mode == "stretched")
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
                float tilt = (float)Num(renderInfo, "tilt", 0);
                if (tilt > 0f)
                {
                    // Lying back, as if on the ground: the particle is fixed in
                    // the effect's own space, spins about its own face (Z) and
                    // is then leaned back about its sideways axis (X). Unity
                    // applies Z before X, which is the order wanted.
                    renderer.alignment = ParticleSystemRenderSpace.Local;
                    main.startRotation3D = true;
                    main.startRotationX = tilt * Mathf.Deg2Rad;
                    main.startRotationY = 0f;
                    main.startRotationZ = Curve(layer, "startRotation", 0);
                    if (layer.ContainsKey("spin") && layer["spin"] != null)
                    {
                        var turning = system.rotationOverLifetime;
                        turning.enabled = true;
                        turning.separateAxes = true;
                        turning.x = 0f;
                        turning.y = 0f;
                        turning.z = Curve(layer, "spin", 0);
                    }
                }
            }
            // Far ones first in 3D, as VFX Forge draws them; otherwise newest on top.
            renderer.sortMode = Str(renderInfo, "sort", "") == "distance" ? ParticleSystemSortMode.Distance
                                                                          : ParticleSystemSortMode.YoungestInFront;
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

            // ---- dissolve: eaten away in ragged patches toward the end of its life
            var dissolveInfo = Obj(renderInfo, "dissolve");
            if (dissolveInfo != null)
            {
                material.SetFloat("_Dissolve", (float)Num(dissolveInfo, "share", 0));
                material.SetFloat("_DissolveSoft", (float)Num(dissolveInfo, "soft", 0.15));
                material.SetFloat("_DissolveEdge", (float)Num(dissolveInfo, "edge", 0));
                material.SetFloat("_SheetColumns", sheet.enabled ? sheet.numTilesX : 1f);
                material.SetFloat("_SheetRows", sheet.enabled ? sheet.numTilesY : 1f);
                // The shader needs to know how far through its life each
                // particle is, and a number of its own so that no two are
                // eaten the same way. Both ride along with the picture position.
                renderer.SetActiveVertexStreams(new List<ParticleSystemVertexStream>
                {
                    ParticleSystemVertexStream.Position,
                    ParticleSystemVertexStream.Color,
                    ParticleSystemVertexStream.UV,
                    ParticleSystemVertexStream.AgePercent,
                    ParticleSystemVertexStream.StableRandomX,
                });
            }
            renderer.sharedMaterial = material;
            return material;
        }

        // ---------------------------------------------------------- paths

        // A path layer emits from a mesh that follows the path. Without a
        // band, from its edges: each segment is a triangle folded flat onto
        // it, so its edges run along the path. With a band, from a ribbon
        // that wide; filled, from the inside of the outline. Normals carry
        // the aim. Running and drawing on step round the edges in order at
        // the path's own speed (Unity counts that speed in edges a second).
        // A curve's values (times its multiplier) scaled by factor.
        static AnimationCurve ScaledCurve(ParticleSystem.MinMaxCurve c, float factor)
        {
            AnimationCurve source = c.mode == ParticleSystemCurveMode.Curve ? c.curve : AnimationCurve.Constant(0f, 1f, c.constant);
            float m = c.mode == ParticleSystemCurveMode.Curve ? c.curveMultiplier : 1f;
            return ScaledCurve(source, factor * m);
        }

        static AnimationCurve ScaledCurve(AnimationCurve source, float factor)
        {
            var keys = source.keys;
            for (int k = 0; k < keys.Length; k++)
            {
                keys[k].value *= factor;
                keys[k].inTangent *= factor;
                keys[k].outTangent *= factor;
            }
            return new AnimationCurve(keys);
        }

        static bool Convex(List<Vector2> ring)
        {
            int sign = 0;
            for (int i = 0; i < ring.Count; i++)
            {
                Vector2 a = ring[i], b = ring[(i + 1) % ring.Count], c = ring[(i + 2) % ring.Count];
                float cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
                if (Mathf.Abs(cross) < 1e-9f)
                    continue;
                int now = cross > 0f ? 1 : -1;
                if (sign != 0 && now != sign)
                    return false;
                sign = now;
            }
            return true;
        }

        // The same line through n + 1 points spaced evenly along it.
        static List<Vector2> EvenSteps(List<Vector2> points, int n)
        {
            var length = new List<float> { 0f };
            for (int i = 1; i < points.Count; i++)
                length.Add(length[i - 1] + (points[i] - points[i - 1]).magnitude);
            float total = length[length.Count - 1];
            if (total <= 1e-8f)
                return points;
            var even = new List<Vector2>(n + 1);
            int at = 0;
            for (int k = 0; k <= n; k++)
            {
                float d = total * k / n;
                while (at + 2 < length.Count && length[at + 1] < d)
                    at++;
                float span = length[at + 1] - length[at];
                float f = span > 1e-12f ? Mathf.Clamp01((d - length[at]) / span) : 0f;
                even.Add(Vector2.Lerp(points[at], points[at + 1], f));
            }
            return even;
        }

        static Mesh PathShape(ParticleSystem system, Dictionary<string, object> info, Dictionary<string, object> layer,
                              float duration, out bool turnAlong)
        {
            turnAlong = false;
            var raw = List(info, "points");
            var points = new List<Vector2>();
            foreach (var item in raw)
            {
                var p = item as List<object>;
                if (p != null && p.Count >= 2)
                    points.Add(new Vector2((float)(double)p[0], (float)(double)p[1]));
            }
            var shape = system.shape;
            if (points.Count < 2)
            {
                shape.shapeType = ParticleSystemShapeType.Circle;
                shape.radius = 0.0001f;
                return null;
            }
            bool closed = Bool(info, "closed", true);
            bool ground = Bool(info, "ground", false);
            bool fill = Bool(info, "fill", false);
            float band = (float)Num(info, "band", 0);
            float outward = (float)Num(info, "outward", 1);
            string aim = Str(info, "aim", "direction");
            bool align = Bool(info, "align", false);
            float travel = (float)Num(info, "travel", 0);
            bool draw = Bool(info, "draw", false);
            Vector3 dir = Vec(info, "direction", Vector3.up);
            // Unity's own turning (Align to Direction) tips a facing particle
            // out of the picture rather than turning it in it, so a still
            // particle turned along the path is drawn as a streak instead.
            var renderInfo = Obj(layer, "render");
            if (align && aim == "direction" && MaxValue(system.main.startSpeed) <= 0f &&
                Str(renderInfo, "mode", "billboard") == "billboard" && Num(renderInfo, "tilt", 0) <= 0)
            {
                turnAlong = true;
                align = false;
                aim = "along";
                var main = system.main;
                main.startSpeed = 0.001f;
            }
            if (travel < 0f)
            {
                points.Reverse();
                outward = -outward;
                travel = -travel;
            }
            // In the plane: the layer's own (x, y), or the ground's (x, z),
            // away from the front being +Z in Unity.
            System.Func<Vector2, Vector3> place = v => ground ? new Vector3(v.x, 0f, v.y) : new Vector3(v.x, v.y, 0f);

            var vertices = new List<Vector3>();
            var normals = new List<Vector3>();
            var triangles = new List<int>();
            System.Func<Vector2, Vector2, Vector3> aimOf = (tangent, normal) =>
            {
                if (aim == "outward")
                    return place(normal);
                if (aim == "along")
                    return place(tangent);
                return dir;
            };
            if (fill && closed)
            {
                // The inside of the outline, cut into triangles.
                var ring = new List<Vector2>(points);
                if (ring.Count > 1 && (ring[0] - ring[ring.Count - 1]).sqrMagnitude < 1e-10f)
                    ring.RemoveAt(ring.Count - 1);
                foreach (var v in ring)
                {
                    vertices.Add(place(v));
                    normals.Add(dir);
                }
                if (Convex(ring))
                {
                    // Spokes from the middle: a regular outline gives triangles
                    // of one size, so particles spread evenly however Unity
                    // picks among them.
                    Vector2 middle = Vector2.zero;
                    foreach (var v in ring)
                        middle += v;
                    middle /= ring.Count;
                    int centre = vertices.Count;
                    vertices.Add(place(middle));
                    normals.Add(dir);
                    for (int k = 0; k < ring.Count; k++)
                        triangles.AddRange(new[] { centre, k, (k + 1) % ring.Count });
                }
                else
                {
                    triangles.AddRange(Triangulate(ring));
                }
            }
            else
            {
                // Cut into equal steps, so that Unity, which picks or steps
                // through edges one by one whatever their length, spreads
                // particles evenly along the path, as VFX Forge does.
                if (closed && (points[0] - points[points.Count - 1]).sqrMagnitude > 1e-10f)
                    points.Add(points[0]);
                points = EvenSteps(points, Mathf.Clamp(4 * (points.Count - 1), 256, 2048));
                for (int i = 0; i + 1 < points.Count; i++)
                {
                    Vector2 a = points[i], b = points[i + 1];
                    Vector2 t = (b - a).sqrMagnitude > 1e-12f ? (b - a).normalized : Vector2.right;
                    Vector2 n = new Vector2(-t.y, t.x) * outward;
                    Vector3 facing = aimOf(t, n);
                    int at = vertices.Count;
                    if (band > 0f)
                    {
                        Vector2 h = new Vector2(-t.y, t.x) * (0.5f * band);
                        vertices.Add(place(a - h)); vertices.Add(place(b - h));
                        vertices.Add(place(a + h)); vertices.Add(place(b + h));
                        for (int k = 0; k < 4; k++)
                            normals.Add(facing);
                        triangles.AddRange(new[] { at, at + 1, at + 2, at + 1, at + 3, at + 2 });
                    }
                    else
                    {
                        // One line per step: Unity emits along a mesh's lines
                        // in order, with nothing doubling back.
                        vertices.Add(place(a)); vertices.Add(place(b));
                        normals.Add(facing); normals.Add(facing);
                        triangles.Add(at); triangles.Add(at + 1);
                    }
                }
            }
            var mesh = new Mesh();
            mesh.name = system.gameObject.name + " path";
            mesh.indexFormat = vertices.Count > 65000 ? UnityEngine.Rendering.IndexFormat.UInt32 : UnityEngine.Rendering.IndexFormat.UInt16;
            mesh.SetVertices(vertices);
            mesh.SetNormals(normals);
            bool lines = !fill && band <= 0f;
            mesh.SetIndices(triangles, lines ? MeshTopology.Lines : MeshTopology.Triangles, 0);
            mesh.RecalculateBounds();

            shape.shapeType = ParticleSystemShapeType.Mesh;
            shape.mesh = mesh;
            shape.useMeshColors = false;
            shape.normalOffset = 0f;
            shape.alignToDirection = align;
            shape.randomDirectionAmount = (float)Num(info, "randomDirection", 0);
            shape.scale = Vector3.one;
            shape.rotation = Vector3.zero;
            if (fill || band > 0f)
            {
                shape.meshShapeType = ParticleSystemMeshShapeType.Triangle;
            }
            else
            {
                shape.meshShapeType = ParticleSystemMeshShapeType.Edge;
                if (travel > 0f)
                {
                    // Unity steps round the mesh's edges this many a second;
                    // each step of the path is one line.
                    int edges = Mathf.Max(1, triangles.Count / 2);
                    shape.meshSpawnMode = ParticleSystemShapeMultiModeValue.Loop;
                    shape.meshSpawnSpeed = new ParticleSystem.MinMaxCurve(travel * edges);
                }
                else
                {
                    // Anywhere along it. VFX Forge spaces a line's particles
                    // evenly, each a golden-ratio step on from the one
                    // before, so a line of many particles has no gaps;
                    // stepping round at that pace does the same here.
                    // Particles sent in bursts would all land on one spot
                    // that way, so those are placed at random.
                    float rate = (float)Num(layer, "rate", 0);
                    var bursts = List(layer, "bursts");
                    int edges = Mathf.Max(1, triangles.Count / 2);
                    if (rate > 0f && (bursts == null || bursts.Count == 0))
                    {
                        shape.meshSpawnMode = ParticleSystemShapeMultiModeValue.Loop;
                        shape.meshSpawnSpeed = new ParticleSystem.MinMaxCurve(rate * edges * 0.618034f);
                    }
                    else
                    {
                        shape.meshSpawnMode = ParticleSystemShapeMultiModeValue.Random;
                    }
                }
            }
            if (draw && travel > 0f)
            {
                // Drawn on once: what is drawn stays until the layer ends,
                // and nothing more is sent once the path is drawn.
                var main = system.main;
                float drawTime = 1f / travel;
                main.startLifetime = Mathf.Max(MaxValue(main.startLifetime), duration);
                var emission = system.emission;
                float rate = MaxValue(emission.rateOverTime);
                var stop = new AnimationCurve(new Keyframe(0f, 1f), new Keyframe(Mathf.Clamp01(drawTime / Mathf.Max(0.01f, duration)), 1f),
                                              new Keyframe(Mathf.Clamp01(drawTime / Mathf.Max(0.01f, duration)) + 0.0001f, 0f), new Keyframe(1f, 0f));
                for (int k = 0; k < stop.length; k++)
                {
                    AnimationUtility.SetKeyLeftTangentMode(stop, k, AnimationUtility.TangentMode.Constant);
                    AnimationUtility.SetKeyRightTangentMode(stop, k, AnimationUtility.TangentMode.Constant);
                }
                emission.rateOverTime = new ParticleSystem.MinMaxCurve(rate, stop);
            }
            return mesh;
        }

        static float MaxValue(ParticleSystem.MinMaxCurve c)
        {
            switch (c.mode)
            {
                case ParticleSystemCurveMode.Constant: return c.constant;
                case ParticleSystemCurveMode.TwoConstants: return Mathf.Max(c.constantMin, c.constantMax);
                case ParticleSystemCurveMode.Curve:
                {
                    float most = 0f;
                    foreach (var k in c.curve.keys)
                        most = Mathf.Max(most, k.value);
                    return most * c.curveMultiplier;
                }
                default:
                {
                    float most = 0f;
                    foreach (var k in c.curveMax.keys)
                        most = Mathf.Max(most, k.value);
                    return most * c.curveMultiplier;
                }
            }
        }

        // Ear clipping: a simple polygon, either way round, into triangles.
        static List<int> Triangulate(List<Vector2> ring)
        {
            var result = new List<int>();
            int n = ring.Count;
            if (n < 3)
                return result;
            float area = 0f;
            for (int i = 0, j = n - 1; i < n; j = i++)
                area += ring[j].x * ring[i].y - ring[i].x * ring[j].y;
            var order = new List<int>();
            for (int i = 0; i < n; i++)
                order.Add(area >= 0f ? i : n - 1 - i);
            int guard = 0;
            while (order.Count > 3 && guard++ < 10000)
            {
                bool clipped = false;
                for (int i = 0; i < order.Count; i++)
                {
                    int ia = order[(i + order.Count - 1) % order.Count], ib = order[i], ic = order[(i + 1) % order.Count];
                    Vector2 a = ring[ia], b = ring[ib], c = ring[ic];
                    if ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) <= 0f)
                        continue;  // not convex here
                    bool empty = true;
                    for (int k = 0; k < order.Count && empty; k++)
                    {
                        int ip = order[k];
                        if (ip == ia || ip == ib || ip == ic)
                            continue;
                        if (Inside(ring[ip], a, b, c))
                            empty = false;
                    }
                    if (!empty)
                        continue;
                    result.AddRange(new[] { ia, ib, ic });
                    order.RemoveAt(i);
                    clipped = true;
                    break;
                }
                if (!clipped)
                    break;  // not simple: what is left is dropped
            }
            if (order.Count == 3)
                result.AddRange(new[] { order[0], order[1], order[2] });
            return result;
        }

        static bool Inside(Vector2 p, Vector2 a, Vector2 b, Vector2 c)
        {
            float d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
            float d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y);
            float d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
            bool neg = d1 < 0f || d2 < 0f || d3 < 0f, pos = d1 > 0f || d2 > 0f || d3 > 0f;
            return !(neg && pos);
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
            // Opacity across the layer's time: a gradient Unity reads across
            // the system's duration, its alpha the opacity at each moment.
            var overTime = List(layer, "startAlphaOverTime");
            if (overTime.Count >= 2)
            {
                Color c = list[0] is List<object> ? ToColor(list[0] as List<object>) : ToColor(list);
                if (list[0] is List<object> && list.Count >= 2)
                    c.a = 0.5f * (c.a + ToColor(list[1] as List<object>).a);
                var alphas = new List<GradientAlphaKey>();
                foreach (var item in overTime)
                {
                    var k = item as List<object>;
                    if (k != null && k.Count >= 2 && alphas.Count < 8)
                        alphas.Add(new GradientAlphaKey(c.a * (float)(double)k[1], (float)(double)k[0]));
                }
                var gradient = new Gradient();
                gradient.SetKeys(new[] { new GradientColorKey(new Color(c.r, c.g, c.b), 0f), new GradientColorKey(new Color(c.r, c.g, c.b), 1f) },
                                 alphas.ToArray());
                return new ParticleSystem.MinMaxGradient(gradient);
            }
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

    // The shape picture, and the pictures of shapes from the library, hold
    // shapes, not colours: they must be read exactly as stored, without
    // colour correction or compression. The artist's own
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
            if (assetPath != VFXForgeImporter.AtlasPath &&
                !assetPath.StartsWith(VFXForgeImporter.ShapesFolder, StringComparison.Ordinal))
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
