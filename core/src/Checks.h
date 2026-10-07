// Internal: whole-object validation shared by commands and the loader.
#pragma once

#include <string_view>

#include "vfx/Effect.h"
#include "vfx/Result.h"

namespace vfx::detail {

// Relative, forward slashes only, no "..", no drive letters, no empty parts.
bool isPortableRelativePath(std::string_view path);

Status validateAsset(const Asset& asset);
Status validateModule(const Module& module);
Status validateLayer(const Layer& layer);

}  // namespace vfx::detail
