#include <utility>

#include "Json.h"

namespace vfx::detail {

namespace {

// A value with no objects anywhere inside it: a number, string, bool, null,
// or an array of such values.
bool isFlat(const J& j) {
    if (j.is_object()) {
        return false;
    }
    if (j.is_array()) {
        for (const auto& element : j) {
            if (!isFlat(element)) {
                return false;
            }
        }
    }
    return true;
}

// Small objects such as {"random": [0.8, 1.4]} or {"time": 0.0, "count": 24}
// stay on one line. Anything larger gets one key per line.
bool isInlineObject(const J& j) {
    if (!j.is_object() || j.size() > 2) {
        return false;
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (!isFlat(it.value())) {
            return false;
        }
    }
    return true;
}

bool isInlineArray(const J& j) {
    for (const auto& element : j) {
        if (!isFlat(element) && !isInlineObject(element)) {
            return false;
        }
    }
    return true;
}

void appendScalar(std::string& out, const J& j) {
    // Shortest text that reads back to the same number. Invalid UTF-8 can
    // never reach here through commands or the loader; replace is a backstop
    // so writing can never throw.
    out += j.dump(-1, ' ', false, J::error_handler_t::replace);
}

void appendInline(std::string& out, const J& j) {
    if (j.is_array()) {
        out += '[';
        bool first = true;
        for (const auto& element : j) {
            if (!first) {
                out += ", ";
            }
            first = false;
            appendInline(out, element);
        }
        out += ']';
    } else if (j.is_object()) {
        out += '{';
        bool first = true;
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (!first) {
                out += ", ";
            }
            first = false;
            appendScalar(out, J(it.key()));
            out += ": ";
            appendInline(out, it.value());
        }
        out += '}';
    } else {
        appendScalar(out, j);
    }
}

void append(std::string& out, const J& j, int depth) {
    const bool inlineValue = (j.is_array() && isInlineArray(j)) ||
                             (j.is_object() && (j.empty() || isInlineObject(j))) ||
                             j.is_primitive();
    if (inlineValue) {
        appendInline(out, j);
        return;
    }
    const std::string pad(static_cast<std::size_t>(depth + 1) * 2, ' ');
    const std::string closePad(static_cast<std::size_t>(depth) * 2, ' ');
    if (j.is_array()) {
        out += "[\n";
        bool first = true;
        for (const auto& element : j) {
            if (!first) {
                out += ",\n";
            }
            first = false;
            out += pad;
            append(out, element, depth + 1);
        }
        out += '\n';
        out += closePad;
        out += ']';
    } else {
        out += "{\n";
        bool first = true;
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (!first) {
                out += ",\n";
            }
            first = false;
            out += pad;
            appendScalar(out, J(it.key()));
            out += ": ";
            append(out, it.value(), depth + 1);
        }
        out += '\n';
        out += closePad;
        out += '}';
    }
}

}  // namespace

std::string writeCanonical(const J& root) {
    std::string out;
    append(out, root, 0);
    out += '\n';
    return out;
}

std::string writeInline(const J& value) {
    std::string out;
    appendInline(out, value);
    return out;
}

bool nestsDeeperThan(const J& root, int maxDepth) {
    std::vector<std::pair<const J*, int>> stack;
    stack.emplace_back(&root, 1);
    while (!stack.empty()) {
        const auto [node, depth] = stack.back();
        stack.pop_back();
        if (!node->is_structured()) {
            continue;
        }
        if (depth > maxDepth) {
            return true;
        }
        for (const auto& child : *node) {
            stack.emplace_back(&child, depth + 1);
        }
    }
    return false;
}

const std::vector<Migration>& builtinMigrations() {
    // Format version 1 is the first, so there is nothing to migrate from yet.
    // The first change that old readers would get wrong adds an entry here
    // and raises kFormatVersion.
    static const std::vector<Migration> table;
    return table;
}

Status runMigrations(J& root, int fromVersion, int toVersion,
                     const std::vector<Migration>& table) {
    for (int version = fromVersion; version < toVersion; ++version) {
        const auto index = static_cast<std::size_t>(version - 1);
        if (version < 1 || index >= table.size() || !table[index]) {
            return makeError("This file is from a version that can no longer be opened.",
                             "no migration from format version " + std::to_string(version));
        }
        if (auto s = table[index](root); !s) {
            return s;
        }
        root["formatVersion"] = version + 1;
    }
    return {};
}

}  // namespace vfx::detail
