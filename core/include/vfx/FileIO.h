// VFX Forge core: reading and writing files safely.
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include "vfx/Effect.h"
#include "vfx/Result.h"
#include "vfx/Serialize.h"

namespace vfx {

// Builds a path from UTF-8 text on every platform.
std::filesystem::path pathFromUtf8(std::string_view text);
std::string pathToUtf8(const std::filesystem::path& path);

// Writes a temporary file beside the target, flushes it to disk, then swaps
// it into place in one step. A crash part-way leaves the old file intact.
Status writeFileAtomic(const std::filesystem::path& path, std::string_view bytes);

Result<std::string> readFile(const std::filesystem::path& path,
                             std::size_t maxBytes = kMaxFileBytes);

Status saveEffect(const std::filesystem::path& path, const Effect& effect);
Result<LoadedEffect> loadEffect(const std::filesystem::path& path);

}  // namespace vfx
