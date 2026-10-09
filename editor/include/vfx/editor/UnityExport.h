// VFX Forge editor: exporting an effect to Unity.
//
// An effect becomes a Unity prefab built from Unity's own Particle System,
// one child system per layer, drawn with a small VFX Forge shader that
// knows the built-in shapes. Everything that needs judgement (how a VFX
// Forge setting maps onto a Unity one) happens here, in tested C++. The
// Unity side only reads the result and sets fields; it holds no rules.
//
// What travels to the Unity project:
//   Assets/VFXForge/Editor/...           the importer that builds prefabs
//   Assets/VFXForge/Shaders/...          the particle shader
//   Assets/VFXForge/Textures/...         the shape atlas
//   Assets/VFXForge/Images/...           the artist's own pictures, if any
//   Assets/VFXForge/Effects/<name>.vfxforge   one per exported effect
//
// Unity turns each .vfxforge file into a prefab as soon as it appears, and
// rebuilds it whenever the file is exported again, so scenes that use the
// effect pick up the change.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "vfx/Effect.h"
#include "vfx/Result.h"
#include "vfx/editor/Archive.h"

namespace vfx::editor {

inline constexpr int kUnityFormatVersion = 1;

// The atlas: every built-in shape in a grid, coverage in alpha and toon tone
// in grey (0.5 means no tone).
inline constexpr int kAtlasColumns = 8;
inline constexpr int kAtlasRows = 4;
inline constexpr int kAtlasTile = 256;

// One of the artist's pictures, as it travels to Unity.
struct UnityPicture {
    Id asset;
    std::string path;  // in the Unity project: "Assets/VFXForge/Images/<file>.png"
    std::string png;
};

// The pictures the effect's drawn layers use, read from the folder they are
// kept in (the effect's project folder). A picture that can't be read is
// left out, and its layer is exported with its Shape instead.
std::vector<UnityPicture> unityPictures(const Effect& effect, const std::filesystem::path& folder);

// The effect, translated into Unity Particle System terms, as JSON text.
std::string unityDescription(const Effect& effect, const std::vector<UnityPicture>& pictures = {});

// A file name for an effect: its name with anything awkward replaced.
std::string unityFileStem(const Effect& effect);

// The shape atlas as a PNG.
std::string unityShapeAtlasPng();

// Every file an export writes, with paths relative to the Unity project
// folder (each starts with "Assets/VFXForge/").
// pictureFolder is where the effect's own pictures are kept.
std::vector<TarEntry> unityExportFiles(const Effect& effect,
                                       const std::filesystem::path& pictureFolder = {});

// True when the folder looks like the top of a Unity project.
bool isUnityProject(const std::filesystem::path& folder);

// Writes the files into a Unity project. Fails, writing nothing, when the
// folder is not a Unity project.
Status exportToUnityProject(const Effect& effect, const std::filesystem::path& projectFolder,
                            std::filesystem::path* written = nullptr,
                            const std::filesystem::path& pictureFolder = {});

// The same files as a .unitypackage, to import with Assets > Import Package.
std::string makeUnityPackage(const std::vector<TarEntry>& files);
Status exportUnityPackage(const Effect& effect, const std::filesystem::path& file,
                          const std::filesystem::path& pictureFolder = {});

}  // namespace vfx::editor
