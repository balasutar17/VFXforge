// VFX Forge: exact VFX Forge colours in a URP project in Linear colour.
//
// In Linear colour, overlapping particles add up in linear light, so glow
// looks dimmer and more coloured than in VFX Forge, where it adds up as the
// screen shows it. This renderer feature draws VFX Forge particles into a
// buffer of screen values, where they blend exactly as in VFX Forge, and
// lays that buffer over the scene. Other particles and the scene are left as
// they are. In a project in Gamma colour there is nothing to do.
//
// It is added to the project's URP renderers when the VFX Forge files are
// first imported, and by VFX Forge > Use Exact VFX Forge Colours (URP).
// Remove it from a renderer (its Renderer Features list) to go back to
// Unity's own blending. URP 17 (Unity 6) or newer.
using UnityEngine;
using UnityEngine.Experimental.Rendering;
using UnityEngine.Rendering;
using UnityEngine.Rendering.RenderGraphModule;
using UnityEngine.Rendering.Universal;

public class VFXForgeColours : ScriptableRendererFeature
{
    public const string ShaderName = "VFX Forge/Particle";
    const int CompositePass = 2;

    [Tooltip("The VFX Forge particle shader (found by name when empty).")]
    public Shader shader;

    // Unity allows these only once the feature is enabled, not while it is
    // being made, so they live outside the feature itself.
    static class Ids
    {
        public static readonly int GammaOn = Shader.PropertyToID("_VFXForgeGammaOn");
        public static readonly ShaderTagId GammaTag = new ShaderTagId("VFXForgeGamma");
    }

    Material composite;
    SwitchPass switchOn;
    DrawPass draw;

    // Sets or clears _VFXForgeGammaOn, which keeps the particles' ordinary
    // pass from drawing while this feature draws them.
    class SwitchPass : ScriptableRenderPass
    {
        class Data { public float value; }
        readonly float value;

        public SwitchPass(float value, RenderPassEvent at)
        {
            this.value = value;
            renderPassEvent = at;
        }

        public override void RecordRenderGraph(RenderGraph graph, ContextContainer frame)
        {
            using (var builder = graph.AddUnsafePass<Data>(value > 0.5f ? "VFX Forge colours on" : "VFX Forge colours off", out var data))
            {
                data.value = value;
                builder.AllowPassCulling(false);
                builder.AllowGlobalStateModification(true);
                builder.SetRenderFunc((Data d, UnsafeGraphContext ctx) => ctx.cmd.SetGlobalFloat(Ids.GammaOn, d.value));
            }
        }
    }

    class DrawPass : ScriptableRenderPass
    {
        class DrawData { public RendererListHandle list; }
        class CompositeData { public TextureHandle buffer; public Material material; }
        class OffData { }

        public Material composite;

        public DrawPass()
        {
            renderPassEvent = RenderPassEvent.AfterRenderingTransparents;
            requiresIntermediateTexture = true;
        }

        public override void RecordRenderGraph(RenderGraph graph, ContextContainer frame)
        {
            var resources = frame.Get<UniversalResourceData>();
            var camera = frame.Get<UniversalCameraData>();
            var rendering = frame.Get<UniversalRenderingData>();
            var lights = frame.Get<UniversalLightData>();

            if (composite != null && !resources.isActiveTargetBackBuffer)
            {
                TextureDesc desc = graph.GetTextureDesc(resources.activeColorTexture);
                desc.name = "VFX Forge particles";
#if UNITY_6000_1_OR_NEWER
                desc.format = GraphicsFormat.R8G8B8A8_UNorm;
#else
                desc.colorFormat = GraphicsFormat.R8G8B8A8_UNorm;
#endif
                desc.clearBuffer = true;
                desc.clearColor = Color.clear;
                desc.bindTextureMS = false;
                TextureHandle buffer = graph.CreateTexture(desc);

                using (var builder = graph.AddRasterRenderPass<DrawData>("VFX Forge particles", out var data))
                {
                    DrawingSettings drawing = RenderingUtils.CreateDrawingSettings(
                        Ids.GammaTag, rendering, camera, lights, SortingCriteria.CommonTransparent);
                    var filtering = new FilteringSettings(RenderQueueRange.transparent);
                    var list = new RendererListParams(rendering.cullResults, drawing, filtering);
                    data.list = graph.CreateRendererList(list);
                    builder.UseRendererList(data.list);
                    builder.SetRenderAttachment(buffer, 0, AccessFlags.Write);
                    builder.SetRenderAttachmentDepth(resources.activeDepthTexture, AccessFlags.Read);
                    builder.SetRenderFunc((DrawData d, RasterGraphContext ctx) => ctx.cmd.DrawRendererList(d.list));
                }

                using (var builder = graph.AddRasterRenderPass<CompositeData>("VFX Forge particles onto the scene", out var data))
                {
                    data.buffer = buffer;
                    data.material = composite;
                    builder.UseTexture(buffer, AccessFlags.Read);
                    builder.SetRenderAttachment(resources.activeColorTexture, 0, AccessFlags.ReadWrite);
                    builder.SetRenderFunc((CompositeData d, RasterGraphContext ctx) =>
                        Blitter.BlitTexture(ctx.cmd, d.buffer, new Vector4(1, 1, 0, 0), d.material, CompositePass));
                }
            }

            using (var builder = graph.AddUnsafePass<OffData>("VFX Forge colours off", out var off))
            {
                builder.AllowPassCulling(false);
                builder.AllowGlobalStateModification(true);
                builder.SetRenderFunc((OffData d, UnsafeGraphContext ctx) => ctx.cmd.SetGlobalFloat(Ids.GammaOn, 0f));
            }
        }
    }

    public override void Create()
    {
        Shader s = shader != null ? shader : Shader.Find(ShaderName);
        if (composite == null && s != null)
            composite = CoreUtils.CreateEngineMaterial(s);
        switchOn = new SwitchPass(1f, RenderPassEvent.BeforeRenderingTransparents);
        draw = new DrawPass { composite = composite };
    }

    public override void AddRenderPasses(ScriptableRenderer renderer, ref RenderingData renderingData)
    {
        // While Unity rebuilds the shader it briefly has fewer passes; the
        // particles then draw the ordinary way.
        if (QualitySettings.activeColorSpace != ColorSpace.Linear || composite == null ||
            composite.passCount <= CompositePass)
            return;
        CameraType type = renderingData.cameraData.cameraType;
        if (type == CameraType.Preview || type == CameraType.Reflection)
            return;
        renderer.EnqueuePass(switchOn);
        renderer.EnqueuePass(draw);
    }

    protected override void Dispose(bool disposing)
    {
        CoreUtils.Destroy(composite);
        composite = null;
    }
}

#if UNITY_EDITOR
public static class VFXForgeColoursSetup
{
    const string Menu = "VFX Forge/Use Exact VFX Forge Colours (URP)";

    // Once per project: add the feature when VFX Forge's files arrive, so an
    // exported effect looks as in VFX Forge without a further step.
    [UnityEditor.InitializeOnLoadMethod]
    static void FirstTime()
    {
        UnityEditor.EditorApplication.delayCall += () =>
        {
            string marker = System.IO.Path.Combine("ProjectSettings", "VFXForgeColours.txt");
            if (System.IO.File.Exists(marker))
                return;
            if (QualitySettings.activeColorSpace != ColorSpace.Linear)
                return;
            int added = AddEverywhere();
            System.IO.File.WriteAllText(marker,
                "VFX Forge added its colour feature to this project's URP renderers once.\n" +
                "Delete this file to have it added again.\n");
            if (added > 0)
                Debug.Log("VFX Forge: added VFX Forge Colours to " + added +
                          " URP renderer(s), so effects blend as in VFX Forge in Linear colour. " +
                          "Remove it from a renderer's Renderer Features to use Unity's own blending.");
        };
    }

    // A renderer whose feature list holds an entry with no script (VFX Forge's
    // files were deleted and brought back, so the script has a new identity)
    // shows "is missing RendererFeatures". Say how to fix it, once per load.
    [UnityEditor.InitializeOnLoadMethod]
    static void WarnAboutBrokenFeatures()
    {
        UnityEditor.EditorApplication.delayCall += () =>
        {
            int broken = 0;
            foreach (string guid in UnityEditor.AssetDatabase.FindAssets("t:UniversalRendererData"))
            {
                string path = UnityEditor.AssetDatabase.GUIDToAssetPath(guid);
                var data = path.StartsWith("Assets/") ? UnityEditor.AssetDatabase.LoadAssetAtPath<UniversalRendererData>(path) : null;
                if (data != null)
                    broken += data.rendererFeatures.FindAll(f => f == null).Count;
            }
            if (broken > 0)
                Debug.LogWarning("VFX Forge: " + broken + " renderer feature(s) have lost their script. If one was VFX Forge Colours, " +
                                 "choose VFX Forge > Use Exact VFX Forge Colours (URP): it removes the empty entries and adds the feature again.");
        };
    }

    // Takes out feature entries that have no script, and their ids in the
    // renderer's map, so that the feature can be added afresh.
    static int RemoveEmptyFeatures(UniversalRendererData data)
    {
        var so = new UnityEditor.SerializedObject(data);
        var list = so.FindProperty("m_RendererFeatures");
        var map = so.FindProperty("m_RendererFeatureMap");
        int removed = 0;
        for (int i = list.arraySize - 1; i >= 0; i--)
        {
            if (list.GetArrayElementAtIndex(i).objectReferenceValue != null)
                continue;
            list.DeleteArrayElementAtIndex(i);
            if (map != null && i < map.arraySize)
                map.DeleteArrayElementAtIndex(i);
            removed++;
        }
        if (removed > 0)
        {
            so.ApplyModifiedProperties();
            UnityEditor.EditorUtility.SetDirty(data);
        }
        return removed;
    }

    [UnityEditor.MenuItem(Menu)]
    static void AddFromMenu()
    {
        int repaired = 0;
        foreach (string guid in UnityEditor.AssetDatabase.FindAssets("t:UniversalRendererData"))
        {
            string path = UnityEditor.AssetDatabase.GUIDToAssetPath(guid);
            var data = path.StartsWith("Assets/") ? UnityEditor.AssetDatabase.LoadAssetAtPath<UniversalRendererData>(path) : null;
            if (data != null)
                repaired += RemoveEmptyFeatures(data);
        }
        if (repaired > 0)
            Debug.Log("VFX Forge: removed " + repaired + " renderer feature entr" + (repaired == 1 ? "y" : "ies") + " whose script was missing.");
        int added = AddEverywhere();
        Debug.Log(added > 0
            ? "VFX Forge: added VFX Forge Colours to " + added + " URP renderer(s)."
            : "VFX Forge: every URP renderer in this project already has VFX Forge Colours.");
        if (QualitySettings.activeColorSpace != ColorSpace.Linear)
            Debug.Log("VFX Forge: this project uses Gamma colour, where effects already blend as in VFX Forge.");
    }

    static int AddEverywhere()
    {
        int added = 0;
        foreach (string guid in UnityEditor.AssetDatabase.FindAssets("t:UniversalRendererData"))
        {
            string path = UnityEditor.AssetDatabase.GUIDToAssetPath(guid);
            if (!path.StartsWith("Assets/"))
                continue;
            var data = UnityEditor.AssetDatabase.LoadAssetAtPath<UniversalRendererData>(path);
            if (data == null || data.rendererFeatures.Exists(f => f is VFXForgeColours))
                continue;
            var feature = ScriptableObject.CreateInstance<VFXForgeColours>();
            feature.name = "VFX Forge Colours";
            feature.shader = Shader.Find(VFXForgeColours.ShaderName);
            feature.SetActive(true);
            UnityEditor.AssetDatabase.AddObjectToAsset(feature, data);
            UnityEditor.AssetDatabase.TryGetGUIDAndLocalFileIdentifier(feature, out string _, out long id);

            var so = new UnityEditor.SerializedObject(data);
            var list = so.FindProperty("m_RendererFeatures");
            var map = so.FindProperty("m_RendererFeatureMap");
            list.arraySize++;
            list.GetArrayElementAtIndex(list.arraySize - 1).objectReferenceValue = feature;
            if (map != null)
            {
                map.arraySize++;
                map.GetArrayElementAtIndex(map.arraySize - 1).longValue = id;
            }
            so.ApplyModifiedProperties();
            data.SetDirty();
            UnityEditor.EditorUtility.SetDirty(data);
            added++;
        }
        UnityEditor.AssetDatabase.SaveAssets();
        return added;
    }
}
#endif
