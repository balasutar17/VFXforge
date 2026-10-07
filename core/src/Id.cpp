#include "vfx/Id.h"

#include <random>

namespace vfx {

std::string formatId(char prefix, Id id) {
    static const char digits[] = "0123456789abcdef";
    std::string out(18, '0');
    out[0] = prefix;
    out[1] = '-';
    for (std::size_t i = 0; i < 16; ++i) {
        out[2 + i] = digits[(id.value >> (60 - 4 * i)) & 0xF];
    }
    return out;
}

Result<Id> parseId(std::string_view text, char prefix) {
    auto fail = [&]() {
        return makeError("\"" + std::string(text) + "\" is not a valid ID.",
                         std::string("expected '") + prefix + "-' followed by 1 to 16 hex digits");
    };
    if (text.size() < 3 || text.size() > 18 || text[0] != prefix || text[1] != '-') {
        return fail();
    }
    std::uint64_t value = 0;
    for (char c : text.substr(2)) {
        std::uint64_t digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<std::uint64_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<std::uint64_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<std::uint64_t>(c - 'A' + 10);
        } else {
            return fail();
        }
        value = (value << 4) | digit;
    }
    if (value == 0) {
        return fail();
    }
    return Id{value};
}

IdGenerator::IdGenerator(std::uint64_t seed) : state_(seed) {}

IdGenerator IdGenerator::fromEntropy() {
    std::random_device device;
    const std::uint64_t high = device();
    const std::uint64_t low = device();
    return IdGenerator((high << 32) ^ low);
}

Id IdGenerator::next() {
    // splitmix64: every state gives a distinct output, so a generator never
    // repeats itself within 2^64 calls.
    for (;;) {
        state_ += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        if (z != 0) {
            return Id{z};
        }
    }
}

}  // namespace vfx
