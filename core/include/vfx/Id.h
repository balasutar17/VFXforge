// VFX Forge core: stable identifiers.
//
// Every effect, layer, module, control and asset carries a random 64-bit ID
// assigned once at creation. IDs are never positional and never reused, so
// undo, file diffs, selection and scripted edits all refer to the same thing.
#pragma once

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

#include "vfx/Result.h"

namespace vfx {

struct Id {
    std::uint64_t value = 0;

    bool valid() const { return value != 0; }
    auto operator<=>(const Id&) const = default;
};

// Text form is "<prefix>-<16 lowercase hex digits>", for example
// "l-00c0ffee12345678". The prefix says what kind of thing the ID names:
// e effect, l layer, m module, c control, a asset.
std::string formatId(char prefix, Id id);

// Accepts 1 to 16 hex digits in either case so hand-written files work.
Result<Id> parseId(std::string_view text, char prefix);

// Deterministic for a given seed, so tests are repeatable.
class IdGenerator {
public:
    explicit IdGenerator(std::uint64_t seed);
    static IdGenerator fromEntropy();

    Id next();

private:
    std::uint64_t state_;
};

}  // namespace vfx
