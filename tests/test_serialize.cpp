#include <catch2/catch_amalgamated.hpp>

#include <string>

#include "Json.h"
#include "helpers.h"
#include "vfx/FileIO.h"
#include "vfx/Serialize.h"

using namespace vfx;
using testing::sampleEffect;

namespace {

std::string fixture(const char* name) {
    auto bytes = readFile(pathFromUtf8(std::string(VFX_TEST_DATA_DIR) + "/" + name));
    REQUIRE(bytes.ok());
    return bytes.value();
}

// Replaces the first occurrence; fails the test if the text is not there, so
// a test can never silently stop testing what it claims to.
std::string replaced(std::string text, const std::string& from, const std::string& to) {
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}

bool hasNote(const LoadedEffect& loaded, const std::string& fragment) {
    for (const auto& d : loaded.diagnostics) {
        if (d.where.find(fragment) != std::string::npos ||
            d.message.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("An empty effect is written in exactly this form") {
    Effect e;
    e.id = Id{1};
    const std::string expected =
        "{\n"
        "  \"format\": \"vfxforge.effect\",\n"
        "  \"formatVersion\": 1,\n"
        "  \"id\": \"e-0000000000000001\",\n"
        "  \"name\": \"Untitled\",\n"
        "  \"space\": \"2d\",\n"
        "  \"seed\": 1,\n"
        "  \"duration\": 2.0,\n"
        "  \"loop\": \"loop\",\n"
        "  \"frameRate\": 30.0,\n"
        "  \"assets\": [],\n"
        "  \"layers\": []\n"
        "}\n";
    CHECK(writeEffect(e) == expected);
}

TEST_CASE("Writing is deterministic and uses LF line endings only") {
    const Effect e = sampleEffect();
    const std::string a = writeEffect(e);
    CHECK(a == writeEffect(e));
    CHECK(a.find('\r') == std::string::npos);
    CHECK(a.back() == '\n');
    CHECK(a.find('\t') == std::string::npos);
}

TEST_CASE("Save then load then save gives a byte-identical file") {
    const std::string first = writeEffect(sampleEffect());
    auto loaded = readEffect(first);
    REQUIRE(loaded.ok());
    CHECK(loaded.value().diagnostics.empty());
    CHECK_FALSE(loaded.value().repaired);
    CHECK_FALSE(loaded.value().readOnly);
    CHECK(loaded.value().fileVersion == kFormatVersion);
    CHECK(writeEffect(loaded.value().effect) == first);
}

TEST_CASE("The shipped sample is canonical") {
    auto bytes = readFile(pathFromUtf8(std::string(VFX_SAMPLES_DIR) + "/coin_burst.vfx"));
    REQUIRE(bytes.ok());
    auto loaded = readEffect(bytes.value());
    REQUIRE(loaded.ok());
    CHECK(loaded.value().diagnostics.empty());
    CHECK(writeEffect(loaded.value().effect) == bytes.value());
    CHECK(loaded.value().effect.name == "Coin Burst");
}

TEST_CASE("Loading restores every field") {
    const Effect before = sampleEffect();
    auto loaded = readEffect(writeEffect(before));
    REQUIRE(loaded.ok());
    const Effect& after = loaded.value().effect;
    CHECK(after.id == before.id);
    CHECK(after.name == before.name);
    REQUIRE(after.assets.size() == 1);
    CHECK(after.assets[0].id == before.assets[0].id);
    CHECK(after.assets[0].path == "textures/spark.png");
    REQUIRE(after.layers.size() == before.layers.size());
    for (std::size_t l = 0; l < before.layers.size(); ++l) {
        CHECK(after.layers[l].id == before.layers[l].id);
        CHECK(after.layers[l].name == before.layers[l].name);
        REQUIRE(after.layers[l].controls.size() == before.layers[l].controls.size());
        for (std::size_t c = 0; c < before.layers[l].controls.size(); ++c) {
            CHECK(after.layers[l].controls[c].id == before.layers[l].controls[c].id);
            CHECK(after.layers[l].controls[c].label == before.layers[l].controls[c].label);
            REQUIRE(after.layers[l].controls[c].targets.size() == 1);
            CHECK(after.layers[l].controls[c].targets[0].module ==
                  before.layers[l].controls[c].targets[0].module);
            CHECK(after.layers[l].controls[c].targets[0].property ==
                  before.layers[l].controls[c].targets[0].property);
        }
        REQUIRE(after.layers[l].modules.size() == before.layers[l].modules.size());
        for (std::size_t m = 0; m < before.layers[l].modules.size(); ++m) {
            CHECK(after.layers[l].modules[m].id == before.layers[l].modules[m].id);
            CHECK(after.layers[l].modules[m].type == before.layers[l].modules[m].type);
            CHECK(after.layers[l].modules[m].desc == before.layers[l].modules[m].desc);
            CHECK(after.layers[l].modules[m].values == before.layers[l].modules[m].values);
        }
    }
}

TEST_CASE("Numbers survive a round trip exactly") {
    const double tricky[] = {0.1,       0.2 + 0.1, 1.0 / 3.0, 1e-7,  123.456789, 5e-5,
                             0.3333333, 2.5,       100.0,     1e-12, 999.999999999999};
    Effect e = sampleEffect();
    const auto& drag = *Registry::builtin().findModule("motion")->find("drag");
    for (double v : tricky) {
        INFO(v);
        REQUIRE(validateValue(drag, Value(v)).ok());
        for (auto& m : e.layers[0].modules) {
            if (m.type == "motion") {
                *m.find("drag") = v;
            }
        }
        auto loaded = readEffect(writeEffect(e));
        REQUIRE(loaded.ok());
        const auto& motion = testing::moduleOfType(loaded.value().effect.layers[0], "motion");
        CHECK(std::get<double>(*motion.find("drag")) == v);
    }
    // Whole numbers keep a decimal point so a float never turns into an int.
    CHECK(writeEffect(e).find("\"frameRate\": 30.0") != std::string::npos);
}

TEST_CASE("Names with quotes, newlines and non-English text survive") {
    Effect e = sampleEffect();
    e.name = "Étoile \"dorée\"\n\t火花 ✨ \\ end";
    e.layers[0].name = "שלום мир";
    auto loaded = readEffect(writeEffect(e));
    REQUIRE(loaded.ok());
    CHECK(loaded.value().effect.name == e.name);
    CHECK(loaded.value().effect.layers[0].name == e.layers[0].name);
    CHECK(writeEffect(loaded.value().effect) == writeEffect(e));
}

TEST_CASE("Fields and modules from a newer minor revision are kept untouched") {
    const std::string text = fixture("unknown_fields.vfx");
    auto loaded = readEffect(text);
    REQUIRE(loaded.ok());
    const auto& l = loaded.value();
    CHECK_FALSE(l.readOnly);
    CHECK_FALSE(l.repaired);
    CHECK(hasNote(l, "turbulence"));

    REQUIRE(l.effect.layers.size() == 1);
    REQUIRE(l.effect.layers[0].modules.size() == 2);
    const Module& unknown = l.effect.layers[0].modules[1];
    CHECK(unknown.type == "turbulence");
    CHECK_FALSE(unknown.known());
    CHECK(unknown.values.empty());
    CHECK_FALSE(unknown.extra.empty());
    CHECK_FALSE(l.effect.extra.empty());
    CHECK_FALSE(l.effect.layers[0].extra.empty());
    CHECK_FALSE(l.effect.layers[0].modules[0].extra.empty());
    CHECK_FALSE(l.effect.assets[0].extra.empty());

    // The whole point: nothing this version does not understand is lost.
    CHECK(writeEffect(l.effect) == text);
}

TEST_CASE("A hand-written file loads and settles into canonical form") {
    const std::string text = fixture("handwritten.vfx");
    auto loaded = readEffect(text);
    REQUIRE(loaded.ok());
    const auto& e = loaded.value().effect;
    CHECK(loaded.value().diagnostics.empty());
    CHECK(e.name == "By Hand");
    CHECK(e.duration == 3.0);  // written as the integer 3
    CHECK(e.layers.at(0).id == Id{0x7f3a});
    const auto& initial = testing::moduleOfType(e.layers.at(0), "initial");
    // Missing properties take their defaults without complaint.
    CHECK(*initial.find("spread") == Value(30.0));
    CHECK(*initial.find("speed") == Value(Scalar::random(4, 7)));
    CHECK(*initial.find("color") == Value(Color{1, 0.5, 0, 1}));  // three numbers given

    const std::string canonical = writeEffect(e);
    CHECK(canonical != text);
    auto again = readEffect(canonical);
    REQUIRE(again.ok());
    CHECK(writeEffect(again.value().effect) == canonical);
}

TEST_CASE("A file from a newer format version opens read-only") {
    const std::string text =
        replaced(writeEffect(sampleEffect()), "\"formatVersion\": 1", "\"formatVersion\": 2");
    auto loaded = readEffect(text);
    REQUIRE(loaded.ok());
    CHECK(loaded.value().readOnly);
    CHECK(loaded.value().fileVersion == 2);
    CHECK(hasNote(loaded.value(), "newer version"));
}

TEST_CASE("A byte-order mark at the start is tolerated") {
    const std::string text = writeEffect(sampleEffect());
    auto loaded = readEffect("\xEF\xBB\xBF" + text);
    REQUIRE(loaded.ok());
    CHECK(writeEffect(loaded.value().effect) == text);
}

TEST_CASE("Files that are not effects are refused with a readable message") {
    const std::string good = writeEffect(sampleEffect());
    const struct {
        const char* why;
        std::string text;
    } cases[] = {
        {"empty", ""},
        {"not json", "this is not json"},
        {"truncated", good.substr(0, good.size() / 2)},
        {"top-level list", "[1, 2, 3]"},
        {"top-level number", "42"},
        {"no format tag", "{\"id\": \"e-1\"}"},
        {"wrong format tag", replaced(good, "vfxforge.effect", "someone.else")},
        {"format tag not text", replaced(good, "\"vfxforge.effect\"", "17")},
        {"version missing", replaced(good, "  \"formatVersion\": 1,\n", "")},
        {"version zero", replaced(good, "\"formatVersion\": 1", "\"formatVersion\": 0")},
        {"version text", replaced(good, "\"formatVersion\": 1", "\"formatVersion\": \"1\"")},
        {"version fraction", replaced(good, "\"formatVersion\": 1", "\"formatVersion\": 1.5")},
        {"effect id missing", replaced(good, "  \"id\": \"e-", "  \"idx\": \"e-")},
        {"effect id wrong kind", replaced(good, "\"id\": \"e-", "\"id\": \"l-")},
        {"layers not a list",
         "{\"format\": \"vfxforge.effect\", \"formatVersion\": 1, \"id\": \"e-1\", "
         "\"layers\": {\"a\": 1}}"},
        {"assets not a list", replaced(good, "\"assets\": [", "\"assets\": 5, \"x\": [")},
        {"module type missing", replaced(good, "\"type\": \"emission\"", "\"kind\": \"emission\"")},
        {"module type empty", replaced(good, "\"type\": \"emission\"", "\"type\": \"\"")},
        {"layer is a number", replaced(good, "\"layers\": [", "\"layers\": [7,")},
    };
    for (const auto& c : cases) {
        INFO(c.why);
        auto loaded = readEffect(c.text);
        REQUIRE_FALSE(loaded.ok());
        CHECK_FALSE(loaded.error().message.empty());
        // The message is for artists: it must not be raw parser output.
        CHECK(loaded.error().message.find("parse error") == std::string::npos);
    }
}

TEST_CASE("The same ID used twice is refused") {
    Effect e = sampleEffect();
    e.layers[1].modules[0].id = e.layers[0].modules[0].id;
    auto loaded = readEffect(writeEffect(e));
    REQUIRE_FALSE(loaded.ok());
    CHECK(loaded.error().message.find("same ID") != std::string::npos);

    Effect f = sampleEffect();
    f.layers[1].id = f.id;  // different kinds of thing may not share an ID either
    CHECK_FALSE(readEffect(writeEffect(f)).ok());
}

TEST_CASE("Very deep nesting is refused instead of overflowing the stack") {
    std::string bomb = "{\"format\": \"vfxforge.effect\", \"formatVersion\": 1, \"x\": ";
    bomb += std::string(200000, '[');
    bomb += std::string(200000, ']');
    bomb += "}";
    auto loaded = readEffect(bomb);
    REQUIRE_FALSE(loaded.ok());
}

TEST_CASE("Oversized lists are refused") {
    std::string many = "{\"format\": \"vfxforge.effect\", \"formatVersion\": 1, "
                       "\"id\": \"e-1\", \"layers\": [";
    for (int i = 0; i < 5000; ++i) {
        if (i) {
            many += ",";
        }
        many += "{\"id\": \"l-" + std::to_string(i + 10) + "\"}";
    }
    many += "]}";
    auto loaded = readEffect(many);
    REQUIRE_FALSE(loaded.ok());
    CHECK(loaded.error().message.find("more layers") != std::string::npos);
}

TEST_CASE("Bad property values are repaired and reported, never fatal") {
    const std::string good = writeEffect(sampleEffect());

    SECTION("out of range is clamped") {
        auto loaded = readEffect(replaced(good, "\"spread\": 30.0", "\"spread\": 500"));
        REQUIRE(loaded.ok());
        CHECK(loaded.value().repaired);
        CHECK(hasNote(loaded.value(), "spread"));
        const auto& initial = testing::moduleOfType(loaded.value().effect.layers[0], "initial");
        CHECK(*initial.find("spread") == Value(180.0));
    }
    SECTION("wrong type falls back to the default") {
        auto loaded = readEffect(replaced(good, "\"spread\": 30.0", "\"spread\": \"wide\""));
        REQUIRE(loaded.ok());
        CHECK(loaded.value().repaired);
        const auto& initial = testing::moduleOfType(loaded.value().effect.layers[0], "initial");
        CHECK(*initial.find("spread") == Value(30.0));
    }
    SECTION("an unknown option falls back to the default") {
        auto loaded = readEffect(replaced(good, "\"blend\": \"alpha\"", "\"blend\": \"dodge\""));
        REQUIRE(loaded.ok());
        CHECK(loaded.value().repaired);
        const auto& sprite = testing::moduleOfType(loaded.value().effect.layers[0], "sprite");
        CHECK(*sprite.find("blend") == Value(std::string("alpha")));
    }
    SECTION("a curve with points out of order is sorted") {
        auto loaded = readEffect(replaced(good, "\"opacity\": {\"curve\": [[0.0, 1.0], [1.0, 0.0]]}",
                                          "\"opacity\": {\"curve\": [[1.0, 0.0], [0.0, 1.0]]}"));
        REQUIRE(loaded.ok());
        CHECK(loaded.value().repaired);
        const auto& overLife = testing::moduleOfType(loaded.value().effect.layers[0], "overLife");
        CHECK(*overLife.find("opacity") == Value(Scalar::curve({{0, 1}, {1, 0}})));
    }
    SECTION("effect and layer fields are repaired too") {
        auto loaded = readEffect(replaced(replaced(good, "\"duration\": 2.0", "\"duration\": -4"),
                                          "\"enabled\": true", "\"enabled\": \"yes\""));
        REQUIRE(loaded.ok());
        CHECK(loaded.value().repaired);
        CHECK(loaded.value().effect.duration == 0.01);
        CHECK(loaded.value().effect.layers[0].enabled);
    }
    SECTION("after a repair the saved file is clean") {
        auto loaded = readEffect(replaced(good, "\"spread\": 30.0", "\"spread\": 500"));
        REQUIRE(loaded.ok());
        auto again = readEffect(writeEffect(loaded.value().effect));
        REQUIRE(again.ok());
        CHECK_FALSE(again.value().repaired);
        CHECK(again.value().diagnostics.empty());
    }
}

TEST_CASE("A reference to an asset that is not listed is kept and reported") {
    Effect e = sampleEffect();
    e.assets.clear();
    const std::string text = writeEffect(e);
    auto loaded = readEffect(text);
    REQUIRE(loaded.ok());
    CHECK(hasNote(loaded.value(), "texture"));
    CHECK_FALSE(loaded.value().repaired);
    CHECK(writeEffect(loaded.value().effect) == text);
}

TEST_CASE("Asset locations that could escape the project are reported") {
    for (const char* path : {"../secret.png", "/etc/passwd", "C:/Windows/x.png",
                             "textures\\\\spark.png", "textures//spark.png", ""}) {
        INFO(path);
        const std::string text = replaced(writeEffect(sampleEffect()), "textures/spark.png", path);
        auto loaded = readEffect(text);
        REQUIRE(loaded.ok());
        CHECK(hasNote(loaded.value(), "assets[0].path"));
    }
}

TEST_CASE("Two modules of one type in a layer are kept and reported") {
    Effect e = sampleEffect();
    Module copy = e.layers[0].modules[0];
    copy.id = Id{0x777};
    e.layers[0].modules.push_back(copy);
    const std::string text = writeEffect(e);
    auto loaded = readEffect(text);
    REQUIRE(loaded.ok());
    CHECK(hasNote(loaded.value(), "more than one"));
    CHECK(writeEffect(loaded.value().effect) == text);
}

TEST_CASE("Single values convert to text and back") {
    const auto& registry = Registry::builtin();
    for (const auto& m : registry.modules()) {
        for (const auto& p : m.properties) {
            INFO(m.type << "." << p.key);
            auto back = valueFromText(p, valueToText(p.defaultValue));
            REQUIRE(back.ok());
            CHECK(back.value() == p.defaultValue);
        }
    }
    const auto& spread = *registry.findModule("initial")->find("spread");
    CHECK(valueFromText(spread, "45").ok());
    CHECK_FALSE(valueFromText(spread, "500").ok());
    CHECK_FALSE(valueFromText(spread, "\"wide\"").ok());
    CHECK_FALSE(valueFromText(spread, "not json").ok());
    CHECK_FALSE(valueFromText(spread, "").ok());

    const auto& lifetime = *registry.findModule("initial")->find("lifetime");
    CHECK(valueToText(Value(Scalar::random(0.8, 1.4))) == "{\"random\": [0.8, 1.4]}");
    auto parsed = valueFromText(lifetime, "{\"curve\": [[0, 1], [1, 0.5]]}");
    REQUIRE(parsed.ok());
    CHECK(parsed.value() == Value(Scalar::curve({{0, 1}, {1, 0.5}})));
}

TEST_CASE("The schema describes every module and is itself canonical JSON") {
    const std::string schema = schemaText();
    auto j = detail::J::parse(schema, nullptr, false);
    REQUIRE_FALSE(j.is_discarded());
    CHECK(j["modules"].size() == Registry::builtin().modules().size());
    CHECK(j["formatVersion"] == kFormatVersion);
    CHECK(detail::writeCanonical(j) == schema);
    std::size_t properties = 0;
    for (const auto& m : j["modules"]) {
        properties += m["properties"].size();
        for (const auto& p : m["properties"]) {
            CHECK(p.contains("key"));
            CHECK(p.contains("label"));
            CHECK(p.contains("kind"));
            CHECK(p.contains("default"));
        }
    }
    CHECK(properties == 50);
}

TEST_CASE("Migrations run in order, one version at a time") {
    using detail::J;
    using detail::Migration;
    std::vector<int> order;
    const std::vector<Migration> table = {
        [&](J& root) -> Status {
            order.push_back(1);
            root["renamed"] = root["old"];
            root.erase("old");
            return {};
        },
        [&](J& root) -> Status {
            order.push_back(2);
            root["added"] = true;
            return {};
        },
    };

    J root = J::parse(R"({"formatVersion": 1, "old": 5})");
    REQUIRE(detail::runMigrations(root, 1, 3, table).ok());
    CHECK(order == std::vector<int>{1, 2});
    CHECK(root["formatVersion"] == 3);
    CHECK(root["renamed"] == 5);
    CHECK(root["added"] == true);
    CHECK_FALSE(root.contains("old"));

    order.clear();
    J partial = J::parse(R"({"formatVersion": 2})");
    REQUIRE(detail::runMigrations(partial, 2, 3, table).ok());
    CHECK(order == std::vector<int>{2});

    J noop = J::parse(R"({"formatVersion": 3})");
    CHECK(detail::runMigrations(noop, 3, 3, table).ok());

    // A gap in the chain is an error, not a silent skip.
    J stuck = J::parse(R"({"formatVersion": 1})");
    CHECK_FALSE(detail::runMigrations(stuck, 1, 4, table).ok());

    const std::vector<Migration> failing = {
        [](J&) -> Status { return makeError("cannot upgrade"); }};
    J bad = J::parse(R"({"formatVersion": 1})");
    CHECK_FALSE(detail::runMigrations(bad, 1, 2, failing).ok());
    CHECK(bad["formatVersion"] == 1);
}

TEST_CASE("This version has no migrations yet because version 1 is the first") {
    CHECK(kFormatVersion == 1);
    CHECK(detail::builtinMigrations().empty());
}

TEST_CASE("A layer's role and lock are saved only when set") {
    Effect effect = sampleEffect();
    REQUIRE_FALSE(effect.layers.empty());
    // Files written before these existed read back and write out unchanged.
    const std::string plain = writeEffect(effect);
    CHECK(plain.find("\"role\"") == std::string::npos);
    CHECK(plain.find("\"locked\"") == std::string::npos);
    auto old = readEffect(plain);
    REQUIRE(old.ok());
    CHECK(old.value().effect.layers[0].role.empty());
    CHECK_FALSE(old.value().effect.layers[0].locked);

    effect.layers[0].role = "glow";
    effect.layers[0].locked = true;
    const std::string text = writeEffect(effect);
    CHECK(text.find("\"role\": \"glow\"") != std::string::npos);
    CHECK(text.find("\"locked\": true") != std::string::npos);
    auto loaded = readEffect(text);
    REQUIRE(loaded.ok());
    CHECK(loaded.value().diagnostics.empty());
    CHECK(loaded.value().effect.layers[0].role == "glow");
    CHECK(loaded.value().effect.layers[0].locked);
    CHECK(loaded.value().effect.layers[0].extra.empty());
    CHECK(writeEffect(loaded.value().effect) == text);
}
