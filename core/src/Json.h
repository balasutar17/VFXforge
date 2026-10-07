// Internal: the one place the JSON library is visible. Not part of the public
// API, so the library can be swapped without touching anything else.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "vfx/Result.h"

namespace vfx::detail {

// Ordered so keys keep the order they were written in.
using J = nlohmann::ordered_json;

// Deterministic text: two-space indent, short values kept on one line, LF
// line endings, one trailing newline.
std::string writeCanonical(const J& root);

// One value on one line, in the same number and string format.
std::string writeInline(const J& value);

// True when arrays and objects nest deeper than maxDepth. Checked without
// recursion so a hostile file cannot overflow the stack.
bool nestsDeeperThan(const J& root, int maxDepth);

// A migration upgrades a file's JSON by exactly one format version.
// table[0] upgrades version 1 to 2, table[1] upgrades 2 to 3, and so on.
using Migration = std::function<Status(J&)>;
const std::vector<Migration>& builtinMigrations();
Status runMigrations(J& root, int fromVersion, int toVersion,
                     const std::vector<Migration>& table);

}  // namespace vfx::detail
