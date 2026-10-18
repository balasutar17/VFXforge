// VFX Forge: how the pictures in Assets/VFXForge are imported.
//
// The shape picture, the shape-library pictures and the artist's own
// pictures must come in as plain 2D textures with their own alpha, exactly
// as painted, whatever a project's texture presets would otherwise make of
// them (a project whose default turns PNGs into sprites or light cookies
// would leave every particle a white square). Shape pictures are data (how
// much of the particle shows, its tone and gloss), not colour; the artist's
// pictures are colour.
using UnityEditor;
using UnityEngine;

namespace VFXForge.EditorTools
{
    public class VFXForgeTextures : AssetPostprocessor
    {
        // Raise to import every VFX Forge picture again with new settings.
        public override uint GetVersion() { return 1; }

        void OnPreprocessTexture()
        {
            if (!assetPath.StartsWith("Assets/VFXForge/"))
                return;
            var importer = assetImporter as TextureImporter;
            if (importer == null)
                return;
            bool colour = assetPath.StartsWith(VFXForgeImporter.ImagesFolder);
            importer.textureType = TextureImporterType.Default;
            importer.textureShape = TextureImporterShape.Texture2D;
            importer.sRGBTexture = colour;
            importer.alphaSource = TextureImporterAlphaSource.FromInput;
            importer.alphaIsTransparency = colour;
            importer.mipmapEnabled = true;
            importer.wrapMode = TextureWrapMode.Clamp;
            importer.filterMode = FilterMode.Bilinear;
            importer.anisoLevel = 1;
            importer.npotScale = TextureImporterNPOTScale.None;
            importer.textureCompression = TextureImporterCompression.Uncompressed;
            importer.maxTextureSize = 4096;
            importer.isReadable = false;
        }
    }
}
