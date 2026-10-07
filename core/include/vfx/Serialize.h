// VFX Forge core: the .vfx text format.
//
// A .vfx file is canonical UTF-8 JSON: fixed key order, shortest round-trip
// numbers, LF line endings. Saving an unchanged effect gives a byte-identical
// file, and a one-slider change is a one-line diff.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "vfx/Effect.h"
#include "vfx/Metadata.h"
#include "vfx/Result.h"
#include "vfx/Value.h"

namespace vfx {

inline constexpr int kFormatVersion = 1;
inline constexpr const char* kFormatTag = "vfxforge.effect";
inline constexpr std::size_t kMaxFileBytes = 64u * 1024u * 1024u;

// Something the loader noticed that did not stop the load.
struct Diagnostic {
    std::string where;    // for example "layers[0].modules[2].gravity"
    std::string message;  // readable by an artist
};

struct LoadedEffect {
    Effect effect;
    std::vector<Diagnostic> diagnostics;

    int fileVersion = kFormatVersion;

    // True when the file was written by a newer version of VFX Forge. It can
    // be viewed, but must not be saved over.
    bool readOnly = false;

    // True when at least one value was invalid and was replaced or clamped.
    // The caller should tell the artist before saving.
    bool repaired = false;
};

// Canonical text for an effect. Always succeeds for an effect built through
// commands; values are validated on the way in, not here.
std::string writeEffect(const Effect& effect);

// Parses .vfx text. Structural damage (not JSON, wrong format tag, missing or
// duplicate IDs) is an error. Bad property values are repaired and reported.
Result<LoadedEffect> readEffect(std::string_view text);

// One value as JSON text, and back. Used by the command-line tool and, later,
// by scripting.
std::string valueToText(const Value& value);
Result<Value> valueFromText(const PropertyDesc& desc, std::string_view text);

// The built-in module metadata as JSON text.
std::string schemaText();

}  // namespace vfx
