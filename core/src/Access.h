// Internal: raw reads and writes by property path. Not part of the public API.
// Commands use these after validating; nothing else may call writeValue.
#pragma once

#include "vfx/Effect.h"
#include "vfx/Metadata.h"
#include "vfx/Path.h"
#include "vfx/Value.h"

namespace vfx::detail {

const PropertyDesc* describePath(const Effect& effect, const Path& path);
bool readValue(const Effect& effect, const Path& path, Value& out);
bool writeValue(Effect& effect, const Path& path, const Value& value);

}  // namespace vfx::detail
