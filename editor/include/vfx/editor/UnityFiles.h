// VFX Forge editor: the Unity-side files, built into the program.
//
// The sources live in unity/ at the top of this repository. The build turns
// them into text inside the program, so an export can always write the
// exact files this version of VFX Forge was built with.
#pragma once

#include <string>
#include <vector>

namespace vfx::editor {

struct UnityFile {
    std::string path;  // relative to Assets/VFXForge/
    std::string text;
};

const std::vector<UnityFile>& unityHelperSources();

}  // namespace vfx::editor
