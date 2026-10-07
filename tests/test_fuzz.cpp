// Fuzz test for the loader: nothing a file contains may crash the program.
//
// Two kinds of damage are tried. Byte-level damage (flipped bits, cut and
// duplicated spans, stray tokens) mostly exercises the "this is not a valid
// file" paths. Structure-aware damage keeps the file valid JSON and swaps
// values for hostile ones, which reaches the validation and repair paths.
//
// Set VFX_FUZZ_ITERATIONS to run more rounds than the default.
#include <catch2/catch_amalgamated.hpp>

#include <cstdlib>
#include <string>
#include <vector>

#include "Json.h"
#include "helpers.h"
#include "vfx/FileIO.h"
#include "vfx/Serialize.h"

using namespace vfx;
using detail::J;
using testing::Rng;

namespace {

int iterations() {
    if (const char* env = std::getenv("VFX_FUZZ_ITERATIONS")) {
        const int n = std::atoi(env);
        if (n > 0) {
            return n;
        }
    }
    return 3000;
}

std::vector<std::string> seedFiles() {
    std::vector<std::string> seeds;
    seeds.push_back(writeEffect(testing::sampleEffect()));
    for (const char* name : {"unknown_fields.vfx", "handwritten.vfx"}) {
        auto bytes = readFile(pathFromUtf8(std::string(VFX_TEST_DATA_DIR) + "/" + name));
        REQUIRE(bytes.ok());
        seeds.push_back(bytes.value());
    }
    return seeds;
}

// Whatever loads must then behave like any other effect: it saves, the saved
// file loads without further repairs, and saving again changes nothing.
void checkLoaded(const LoadedEffect& loaded) {
    const std::string saved = writeEffect(loaded.effect);
    auto again = readEffect(saved);
    REQUIRE(again.ok());
    REQUIRE_FALSE(again.value().repaired);
    REQUIRE_FALSE(again.value().readOnly);
    REQUIRE(writeEffect(again.value().effect) == saved);
}

const char* const kTokens[] = {
    "{", "}", "[", "]", ",", ":", "\"", "null", "true", "false", "-1", "0", "1e999", "-1e999",
    "1e-400", "0.0000001", "\\u0000", "\\", "\xff", "\xc3", "\xf0\x9f", "\xed\xa0\x80",
    "9999999999999999999999", "18446744073709551616", "-9223372036854775809", "NaN", "Infinity",
    "{\"curve\": []}", "{\"random\": [5, 1]}", "\"type\": \"zzz\"", "\"id\": \"m-1\"",
    "\"formatVersion\": 99", "[[[[[[[[", "]]]]]]]]", "\n", "\r\n", "\t", " ", "/*", "//",
};

std::string damageBytes(std::string s, Rng& rng) {
    const std::size_t rounds = 1 + rng.below(4);
    for (std::size_t r = 0; r < rounds && !s.empty(); ++r) {
        const std::size_t at = rng.below(s.size());
        const std::size_t span = std::min(s.size() - at, 1 + rng.below(40));
        switch (rng.below(7)) {
            case 0:
                s[at] = static_cast<char>(static_cast<unsigned char>(s[at]) ^ (1u << rng.below(8)));
                break;
            case 1:
                s.erase(at, span);
                break;
            case 2:
                s.insert(at, s.substr(at, span));
                break;
            case 3:
                s.insert(at, kTokens[rng.below(std::size(kTokens))]);
                break;
            case 4:
                s.resize(at);
                break;
            case 5:
                s[at] = static_cast<char>(rng.below(256));
                break;
            default: {
                const std::size_t other = rng.below(s.size());
                std::swap(s[at], s[other]);
                break;
            }
        }
    }
    return s;
}

void collect(J& node, std::vector<J*>& out) {
    out.push_back(&node);
    if (node.is_structured()) {
        for (auto& child : node) {
            collect(child, out);
        }
    }
}

J hostileValue(Rng& rng, const std::vector<J*>& nodes) {
    switch (rng.below(22)) {
        case 0: return J(nullptr);
        case 1: return J(rng.chance(0.5));
        case 2: return J(0);
        case 3: return J(-1);
        case 4: return J(std::int64_t{9223372036854775807});
        case 5: return J(std::uint64_t{18446744073709551615ull});
        case 6: return J(1e308);
        case 7: return J(-1e308);
        case 8: return J(5e-324);
        case 9: return J(-0.0);
        case 10: return J("");
        case 11: return J(std::string(5000, 'x'));
        case 12: return J("m-0000000000000001");
        case 13: return J("additive");
        case 14: return J::array();
        case 15: return J::object();
        case 16: return J::parse(R"({"random": [5, 1]})");
        case 17: return J::parse(R"({"curve": [[2, 1], [0.5, 1], [0.5, 2], [-1, 1e300]]})");
        case 18: return J::parse(R"({"gradient": [[0.5, [9, -1, 2, 7]], [0.1, [1, 1, 1]]]})");
        case 19: return J::parse(R"([{"time": -5, "count": 99999999999}, {"time": 1}])");
        case 20: return J(rng.between(-1e6, 1e6));
        default: return *nodes[rng.below(nodes.size())];  // a copy of something else in the file
    }
}

std::string damageStructure(const std::string& text, Rng& rng) {
    J root = J::parse(text);
    const std::size_t rounds = 1 + rng.below(5);
    for (std::size_t r = 0; r < rounds; ++r) {
        std::vector<J*> nodes;
        collect(root, nodes);
        J* target = nodes[1 + rng.below(nodes.size() - 1)];  // never the root itself
        switch (rng.below(5)) {
            case 0:
            case 1:
            case 2: {
                J replacement = hostileValue(rng, nodes);
                *target = std::move(replacement);
                break;
            }
            case 3:
                if (target->is_array() && !target->empty()) {
                    // Duplicating an element creates repeated IDs and extra modules.
                    J copy = (*target)[rng.below(target->size())];
                    target->push_back(std::move(copy));
                } else if (target->is_object() && !target->empty()) {
                    auto it = target->begin();
                    std::advance(it, static_cast<std::ptrdiff_t>(rng.below(target->size())));
                    target->erase(it.key());
                }
                break;
            default:
                if (target->is_object()) {
                    static const char* keys[] = {"id", "type", "extra", "curve", "random",
                                                 "gradient", "format", "layers", "modules"};
                    (*target)[keys[rng.below(std::size(keys))]] = hostileValue(rng, nodes);
                } else if (target->is_array()) {
                    target->push_back(hostileValue(rng, nodes));
                }
                break;
        }
    }
    return root.dump(rng.chance(0.5) ? 2 : -1, ' ', false, J::error_handler_t::replace);
}

}  // namespace

TEST_CASE("Fuzz: byte-level damage never crashes the loader") {
    const auto seeds = seedFiles();
    Rng rng(20261003);
    int loadedCount = 0, refusedCount = 0;
    const int n = iterations();
    for (int i = 0; i < n; ++i) {
        const std::string damaged = damageBytes(seeds[rng.below(seeds.size())], rng);
        auto loaded = readEffect(damaged);
        if (loaded.ok()) {
            ++loadedCount;
            checkLoaded(loaded.value());
        } else {
            ++refusedCount;
            REQUIRE_FALSE(loaded.error().message.empty());
        }
    }
    // Both outcomes must actually occur or the test is not testing much.
    CHECK(loadedCount > n / 50);
    CHECK(refusedCount > n / 50);
}

TEST_CASE("Fuzz: hostile but well-formed files never crash the loader") {
    const auto seeds = seedFiles();
    Rng rng(771);
    int loadedCount = 0, refusedCount = 0, repairedCount = 0;
    const int n = iterations();
    for (int i = 0; i < n; ++i) {
        const std::string damaged = damageStructure(seeds[rng.below(seeds.size())], rng);
        auto loaded = readEffect(damaged);
        if (loaded.ok()) {
            ++loadedCount;
            repairedCount += loaded.value().repaired ? 1 : 0;
            checkLoaded(loaded.value());
        } else {
            ++refusedCount;
            REQUIRE_FALSE(loaded.error().message.empty());
        }
    }
    CHECK(loadedCount > n / 20);
    CHECK(refusedCount > n / 20);
    CHECK(repairedCount > n / 50);
}

TEST_CASE("Fuzz: single values from hostile text never crash") {
    Rng rng(5);
    std::vector<J*> none;
    J dummy = 1;
    none.push_back(&dummy);
    const int n = iterations();
    for (int i = 0; i < n; ++i) {
        const std::string value = hostileValue(rng, none).dump(-1, ' ', false,
                                                                J::error_handler_t::replace);
        for (const auto& m : Registry::builtin().modules()) {
            for (const auto& p : m.properties) {
                auto parsed = valueFromText(p, value);
                if (parsed.ok()) {
                    REQUIRE(validateValue(p, parsed.value()).ok());
                }
            }
        }
    }
}
