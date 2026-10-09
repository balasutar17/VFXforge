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

// The effect, translated into Unity Particle System terms, as JSON text.
std::string unityDescription(const Effect& effect);

// A file name for an effect: its name with anything awkward replaced.
std::string unityFileStem(const Effect& effect);

// The shape atlas as a PNG.
std::string unityShapeAtlasPng();

// Every file an export writes, with paths relative to the Unity project
// folder (each starts with "Assets/VFXForge/").
std::vector<TarEntry> unityExportFiles(const Effect& effect);

// True when the folder looks like the top of a Unity project.
bool isUnityProject(const std::filesystem::path& folder);

// Writes the files into a Unity project. Fails, writing nothing, when the
// folder is not a Unity project.
Status exportToUnityProject(const Effect& effect, const std::filesystem::path& projectFolder,
                            std::filesystem::path* written = nullptr);

// The same files as a .unitypackage, to import with Assets > Import Package.
std::string makeUnityPackage(const std::vector<TarEntry>& files);
Status exportUnityPackage(const Effect& effect, const std::filesystem::path& file);

}  // namespace vfx::editor
