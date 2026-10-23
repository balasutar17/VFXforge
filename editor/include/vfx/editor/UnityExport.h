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
//   Assets/VFXForge/Models/<name>.vfxmodel     the 3D model the effect was made on, if the
//                                              artist chose to take it along
//
// Unity turns each .vfxforge file into a prefab as soon as it appears, and
// rebuilds it whenever the file is exported again, so scenes that use the
// effect pick up the change.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "vfx/Effect.h"
#include "vfx/Program.h"
#include "vfx/Result.h"
#include "vfx/editor/Archive.h"
#include "vfx/editor/Model.h"

namespace vfx::editor {

inline constexpr int kUnityFormatVersion = 1;

// The atlas: each of the first thirty-five shapes in a grid. Coverage in alpha, toon tone in
// red (and blue; 0.5 means no tone), gloss in green.
inline constexpr int kAtlasColumns = 8;
inline constexpr int kAtlasRows = 5;
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

// The sounds the effect's layers play, read from the project folder (library
// sounds are made again if missing), as "Assets/VFXForge/Sounds/<file>.wav".
// They use the same structure as pictures.
std::vector<UnityPicture> unitySounds(const Effect& effect, const std::filesystem::path& folder);

// The 3D model an effect was made on, where it stands in the effect. It
// travels with the effect when asked, so the effect looks in Unity as it
// does in VFX Forge: the model in place, particles hidden behind it.
struct UnityModel {
    const Model* model = nullptr;
    ModelPlace place;
};
// More triangles than this are not sent (the file would be very large).
inline constexpr std::size_t kUnityMaxModelTriangles = 400'000;

// The model as the file Unity reads: already fitted, turned and placed as
// in the effect's world, and mirrored for Unity's left-handed space. Empty
// when there is nothing to send.
std::string unityModelBytes(const UnityModel& model);
std::string unityModelPath(const Effect& effect);  // "Assets/VFXForge/Models/<name>.vfxmodel"

// Unity's particle shader draws two blends: Normal and Linear Dodge (Add).
// A layer set to any other of Photoshop's modes is drawn there with the
// nearer of the two (see blendForUnity). One line for each such layer, in
// words an artist reads: "Spark Trail: Overlay is drawn as Normal in Unity".
std::vector<std::string> unityBlendNotes(const Effect& effect);

// Layers made on a model (the Mesh shape) whose look Unity cannot match
// exactly: a scan or ripple band (Unity's mesh shape has no band), and a
// silhouette outline (fixed to the camera it was made from).
std::vector<std::string> unityMeshNotes(const Effect& effect);

// The effect, translated into Unity Particle System terms, as JSON text.
// modelPath, when not empty, is the model file the effect shows.
std::string unityDescription(const Effect& effect, const std::vector<UnityPicture>& pictures = {},
                             const std::vector<UnityPicture>& sounds = {}, const std::string& modelPath = {});

// A file name for an effect: its name with anything awkward replaced.
std::string unityFileStem(const Effect& effect);

// The shape atlas as a PNG.
std::string unityShapeAtlasPng();

// A shape from the library, and any shape whose design has been changed,
// travels as a picture of its own, laid out like one tile of the atlas, and
// only the ones an effect uses are sent. A changed shape's settings are in
// its file name, so two layers with the same change share one picture.
struct UnityShape {
    int shape = 0;
    ShapeEdit edit;
};
std::string unityShapePicturePath(int shape, const ShapeEdit& edit = {});  // "Assets/VFXForge/Shapes/<name>.png"
std::string unityShapePicturePng(int shape, const ShapeEdit& edit = {});
// The shapes that travel this way for an effect's drawn layers, each once,
// in order of file name.
std::vector<UnityShape> unityShapePictures(const Effect& effect);

// Every file an export writes, with paths relative to the Unity project
// folder (each starts with "Assets/VFXForge/").
// pictureFolder is where the effect's own pictures are kept.
std::vector<TarEntry> unityExportFiles(const Effect& effect,
                                       const std::filesystem::path& pictureFolder = {},
                                       const UnityModel* model = nullptr);

// True when the folder looks like the top of a Unity project.
bool isUnityProject(const std::filesystem::path& folder);

// Writes the files into a Unity project. Fails, writing nothing, when the
// folder is not a Unity project.
Status exportToUnityProject(const Effect& effect, const std::filesystem::path& projectFolder,
                            std::filesystem::path* written = nullptr,
                            const std::filesystem::path& pictureFolder = {},
                            const UnityModel* model = nullptr);

// The same files as a .unitypackage, to import with Assets > Import Package.
std::string makeUnityPackage(const std::vector<TarEntry>& files);
Status exportUnityPackage(const Effect& effect, const std::filesystem::path& file,
                          const std::filesystem::path& pictureFolder = {}, const UnityModel* model = nullptr);

}  // namespace vfx::editor
