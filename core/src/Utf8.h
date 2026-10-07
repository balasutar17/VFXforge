// Internal: UTF-8 checks. Not part of the public API.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace vfx::detail {

// Strict UTF-8: rejects overlong forms, surrogates and values above U+10FFFF.
inline bool isValidUtf8(std::string_view s) {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        const unsigned char c = p[i];
        if (c < 0x80) {
            ++i;
            continue;
        }
        std::size_t need = 0;
        unsigned int cp = 0;
        if ((c & 0xE0) == 0xC0) {
            need = 1;
            cp = c & 0x1Fu;
        } else if ((c & 0xF0) == 0xE0) {
            need = 2;
            cp = c & 0x0Fu;
        } else if ((c & 0xF8) == 0xF0) {
            need = 3;
            cp = c & 0x07u;
        } else {
            return false;
        }
        if (i + need >= n) {
            return false;  // truncated sequence
        }
        for (std::size_t k = 1; k <= need; ++k) {
            const unsigned char cc = p[i + k];
            if ((cc & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if ((need == 1 && cp < 0x80) || (need == 2 && cp < 0x800) || (need == 3 && cp < 0x10000)) {
            return false;
        }
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }
        i += need + 1;
    }
    return true;
}

// Shortens to at most maxBytes without cutting a character in half.
inline std::string truncateUtf8(std::string_view s, std::size_t maxBytes) {
    if (s.size() <= maxBytes) {
        return std::string(s);
    }
    std::size_t end = maxBytes;
    while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) {
        --end;
    }
    return std::string(s.substr(0, end));
}

}  // namespace vfx::detail
