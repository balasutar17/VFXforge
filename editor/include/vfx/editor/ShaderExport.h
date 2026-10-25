// VFX Forge editor: exporting a shader to Unity.
//
// A shader made in the Shader tab becomes real Unity assets:
//   Assets/VFXForge/Shaders/<Name>/<Name>.shader     the shader: the shared shader source with
//                                                    one property for every parameter
//   Assets/VFXForge/Shaders/<Name>/<Name>.shader.meta   fixes the shader's GUID so the material
//                                                    can point at it
//   Assets/VFXForge/Shaders/<Name>/<Name>.mat        the material, with every value set
//   Assets/VFXForge/Shaders/<Name>/<Name>.vfxshader  the settings, to open again in VFX Forge
//   Assets/VFXForge/Shaders/<Name>/<Name>.anim.json  the timeline, for the runtime script
//   Assets/VFXForge/Shaders/<Name>/README.txt        setup steps and what is not supported
//   Assets/VFXForge/Runtime/VFXForgeShaderAnimator.cs   plays the timeline, triggers hits and bursts
//   Assets/VFXForge/Editor/VFXForgeShaderDemo.cs        menu: builds a demo object with the material
//
// The shader is plain CG for the Built-in render pipeline; in URP it draws through the
// "SRPDefaultUnlit" pass, like VFX Forge's other shaders. There is no HDRP version.
#pragma once

#include <string>
#include <vector>

#include "vfx/Result.h"
#include "vfx/editor/ShaderLib.h"
#include "vfx/editor/UnityFiles.h"

namespace vfx::editor::shader {

// The shared shader source, as embedded at build time.
const std::string& shaderCoreSource();

struct ShaderExport {
    std::string name;                 // the safe file name, "Hologram_Globe"
    std::string folder;               // "Shaders/Hologram_Globe/"
    std::vector<UnityFile> files;     // paths relative to Assets/VFXForge/
    std::vector<std::string> notes;   // what is not supported or needs a step in Unity
};

// The name made safe for a file and a Unity shader menu.
std::string safeName(const std::string& name);

// The .shader text alone.
std::string generateShader(const Settings& settings);
// The .mat text, for a shader with the given GUID.
std::string generateMaterial(const Settings& settings, const std::string& shaderGuid);
// The timeline, as the runtime script reads it.
std::string generateAnimationJson(const Settings& settings);
// A stable 32-digit GUID made from the shader's name.
std::string shaderGuid(const std::string& name);

// A stable GUID for one of the shader's other assets ("mat", "prefab", "anim"), made from its name.
std::string assetGuid(const std::string& name, const std::string& kind);
// The GUID fixed for the Shader Animator script's .meta, which the prefab points at.
std::string animatorScriptGuid();

// The Unity prefab text: an object with the material on a mesh, and, when the shader has a
// timeline, the Shader Animator already pointing at its .anim.json. Empty for UI and
// particle shaders, which belong on an Image or a Particle System, not a mesh.
std::string generatePrefab(const Settings& settings, const std::string& animatorGuid);

// `animatorGuid`: the GUID of the Shader Animator script when the project already has it
// (read from its .meta); empty uses the fixed one this export also writes a .meta for.
Result<ShaderExport> exportShader(const Settings& settings, const std::string& animatorGuid = {});

}  // namespace vfx::editor::shader
