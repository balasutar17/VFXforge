#include <catch2/catch_amalgamated.hpp>

#include <set>

#include "vfx/Id.h"
#include "vfx/Path.h"

using namespace vfx;

TEST_CASE("IDs format as a prefix and sixteen hex digits") {
    CHECK(formatId('l', Id{0x00c0ffee12345678ull}) == "l-00c0ffee12345678");
    CHECK(formatId('m', Id{1}) == "m-0000000000000001");
    CHECK(formatId('e', Id{0xffffffffffffffffull}) == "e-ffffffffffffffff");
}

TEST_CASE("IDs parse back to the same value") {
    for (std::uint64_t v : {1ull, 0xabcull, 0x00c0ffee12345678ull, 0xffffffffffffffffull}) {
        auto parsed = parseId(formatId('a', Id{v}), 'a');
        REQUIRE(parsed.ok());
        CHECK(parsed.value().value == v);
    }
}

TEST_CASE("Short and upper-case IDs are accepted so hand-written files work") {
    auto a = parseId("l-7f3a", 'l');
    REQUIRE(a.ok());
    CHECK(a.value().value == 0x7f3a);
    auto b = parseId("l-7F3A", 'l');
    REQUIRE(b.ok());
    CHECK(b.value() == a.value());
}

TEST_CASE("Malformed IDs are rejected") {
    for (const char* bad : {"", "l", "l-", "m-7f3a", "l-0", "l-0000", "l-xyz", "l-12345678901234567",
                            "l 7f3a", "-7f3a", "l-7f3a ", "l--1"}) {
        INFO(bad);
        CHECK_FALSE(parseId(bad, 'l').ok());
    }
}

TEST_CASE("The ID generator never repeats and never gives zero") {
    IdGenerator ids(7);
    std::set<std::uint64_t> seen;
    for (int i = 0; i < 100000; ++i) {
        const Id id = ids.next();
        REQUIRE(id.valid());
        REQUIRE(seen.insert(id.value).second);
    }
}

TEST_CASE("The ID generator is repeatable for a given seed") {
    IdGenerator a(99), b(99), c(100);
    const Id first = a.next();
    CHECK(first == b.next());
    CHECK(first != c.next());
}

TEST_CASE("Property paths round-trip through text") {
    const Id layer{0x1111}, module{0x2222};
    const Path paths[] = {
        Path::effect("duration"),
        Path::layerField(layer, "name"),
        Path::property(layer, module, "gravity"),
    };
    CHECK(paths[0].str() == "effect/duration");
    CHECK(paths[1].str() == "layer/l-0000000000001111/name");
    CHECK(paths[2].str() == "layer/l-0000000000001111/module/m-0000000000002222/gravity");
    for (const auto& p : paths) {
        auto parsed = Path::parse(p.str());
        REQUIRE(parsed.ok());
        CHECK(parsed.value() == p);
    }
    CHECK(paths[0].isEffectField());
    CHECK(paths[1].isLayerField());
    CHECK(paths[2].isModuleProperty());
}

TEST_CASE("Malformed property paths are rejected") {
    for (const char* bad : {"", "effect", "effect/", "/duration", "effect//duration",
                            "layer/l-1", "layer/m-1/name", "layer/l-1/module/m-2",
                            "layer/l-1/module/l-2/x", "layer/l-1/thing/m-2/x", "layers/l-1/name",
                            "effect/a/b", "layer/l-1/module/m-2/x/y"}) {
        INFO(bad);
        CHECK_FALSE(Path::parse(bad).ok());
    }
}
