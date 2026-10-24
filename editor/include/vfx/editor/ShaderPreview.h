// VFX Forge editor: the live preview of a shader.
//
// The shader maths is the same source Unity gets (editor/shaders/ShaderCore.hlsli), run here as
// C++ on a small mesh: a sphere, cube, plane, tube, crystal or cone, a sprite shape, a particle
// quad, or the artist's own model. The picture is deterministic: the same settings and time give
// the same pixels, so scrubbing the timeline is exact.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "vfx/editor/Model.h"
#include "vfx/editor/Picture.h"
#include "vfx/editor/ShaderLib.h"

namespace vfx::editor::shader {

struct PreviewOptions {
    int width = 320, height = 320;
    double time = 0;          // the playhead, in seconds
    float yaw = 0.55f;        // the orbit camera, radians
    float pitch = 0.30f;
    float zoom = 1.0f;
    int background = 0;       // 0 dark, 1 grey, 2 light, 3 checker
    bool bloom = true;
    std::string shape;        // empty: the shader's own; "model" uses `model`
    const Model* model = nullptr;
    double hitAge = -1;       // seconds since a shield hit; negative: the shader's own test impact
};

// The index each parameter has in the code the preview runs, by macro name ("HG_color"). The
// tests compare it with the library's own tables, so the two cannot drift apart.
const std::map<std::string, int>& previewIndices();

// The preview shapes by name.
const std::vector<std::string>& previewShapes();

Picture renderPreview(const Settings& settings, const PreviewOptions& options);

}  // namespace vfx::editor::shader
