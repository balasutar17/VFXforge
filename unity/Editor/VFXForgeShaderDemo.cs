// VFX Forge: builds a small demo from an exported shader.
//
// Menu: Assets > VFX Forge > Create Shader Demo Object. Select an exported material (the .mat
// beside the .shader) and choose it: an object with that material, a Shader Animator already
// set up with the shader's timeline, and, for a shield, a small script that sends test impacts.
using System.IO;
using UnityEditor;
using UnityEngine;

namespace VFXForge.EditorTools
{
    public static class VFXForgeShaderDemo
    {
        [MenuItem("Assets/VFX Forge/Create Shader Demo Object", true)]
        static bool CanCreate() { return Selection.activeObject is Material; }

        [MenuItem("Assets/VFX Forge/Create Shader Demo Object")]
        static void Create()
        {
            Material material = Selection.activeObject as Material;
            if (material == null) return;
            string path = AssetDatabase.GetAssetPath(material);
            string folder = Path.GetDirectoryName(path).Replace('\\', '/');
            string name = Path.GetFileNameWithoutExtension(path);
            string shape = material.HasProperty("_FF_hitRadius") || material.HasProperty("_EM_intensity") || material.HasProperty("_HG_color") || material.HasProperty("_IC_color")
                ? "Sphere" : "Quad";
            PrimitiveType type = shape == "Sphere" ? PrimitiveType.Sphere : PrimitiveType.Quad;
            GameObject go = GameObject.CreatePrimitive(type);
            go.name = name + " Demo";
            Object.DestroyImmediate(go.GetComponent<Collider>());
            Renderer r = go.GetComponent<Renderer>();
            r.sharedMaterial = material;
            VFXForgeShaderAnimator animator = go.AddComponent<VFXForgeShaderAnimator>();
            animator.animationFile = AssetDatabase.LoadAssetAtPath<TextAsset>(folder + "/" + name + ".anim.json");
            if (material.HasProperty("_FF_hitRadius")) go.AddComponent<VFXForgeShaderDemoHits>();
            Undo.RegisterCreatedObjectUndo(go, "Create Shader Demo Object");
            Selection.activeGameObject = go;
        }
    }
}
