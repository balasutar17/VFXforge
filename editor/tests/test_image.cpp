// Reading the artist's own pictures.
#include <catch2/catch_amalgamated.hpp>

#include <filesystem>
#include <random>
#include <string>

#include "vfx/FileIO.h"
#include "vfx/editor/Archive.h"
#include "vfx/editor/Image.h"

using namespace vfx;
using namespace vfx::editor;

namespace {

std::string fixture(const std::string& name) {
    auto bytes = readFile(pathFromUtf8(std::string(VFX_EDITOR_FIXTURES) + "/" + name));
    REQUIRE(bytes.ok());
    return bytes.value();
}

}  // namespace

TEST_CASE("PNG pictures of every kind decode to the same pixels", "[image]") {
    // Each fixture was written by another program and its pixels checked
    // against a second decoder; the .rgba file next to it holds the answer.
    for (const char* name : {"rgba8", "rgb8", "grey8", "greyalpha8", "grey1", "palette4",
                             "palette-trns", "rgba16", "rgba8-interlaced", "rgb-key"}) {
        INFO(name);
        auto image = decodePng(fixture(std::string(name) + ".png"));
        REQUIRE(image.ok());
        CHECK(image.value().width == 13);
        CHECK(image.value().height == 11);
        const std::string expected = fixture(std::string(name) + ".rgba");
        REQUIRE(image.value().rgba.size() == expected.size());
        CHECK(std::string(image.value().rgba.begin(), image.value().rgba.end()) == expected);
    }
}

TEST_CASE("Our own PNGs read back exactly", "[image]") {
    Image image;
    image.width = 37;
    image.height = 5;
    std::mt19937 rng(3);
    for (int i = 0; i < 37 * 5 * 4; ++i) {
        image.rgba.push_back(static_cast<std::uint8_t>(rng() & 0xff));
    }
    auto back = decodePng(encodePng(image));
    REQUIRE(back.ok());
    CHECK(back.value().rgba == image.rgba);
}

TEST_CASE("The inflater reads stored, fixed and dynamic blocks", "[image]") {
    // A stored block, written by hand.
    const std::string stored = std::string("\x01\x05\x00\xfa\xff", 5) + "hello";
    auto out = inflate(stored);
    REQUIRE(out.ok());
    CHECK(out.value() == "hello");

    // Fixed blocks, from our own writer.
    std::string text;
    for (int i = 0; i < 5000; ++i) {
        text += static_cast<char>('a' + (i * 7) % 26);
    }
    out = zlibDecompress(zlibCompress(text));
    REQUIRE(out.ok());
    CHECK(out.value() == text);
    // Dynamic blocks are what the fixtures above are made of.
}

TEST_CASE("Damaged pictures are refused, never read past the end", "[image]") {
    CHECK_FALSE(decodePng("").ok());
    CHECK_FALSE(decodePng("not a picture at all").ok());
    const std::string good = fixture("rgba8.png");
    for (std::size_t cut = 0; cut < good.size(); cut += 7) {
        (void)decodePng(good.substr(0, cut));  // must not crash
    }
    std::mt19937 rng(11);
    for (int round = 0; round < 3000; ++round) {
        std::string bad = good;
        const int flips = 1 + static_cast<int>(rng() % 4);
        for (int k = 0; k < flips; ++k) {
            bad[8 + rng() % (bad.size() - 8)] = static_cast<char>(rng() & 0xff);
        }
        auto image = decodePng(bad);
        if (image.ok()) {
            CHECK(image.value().rgba.size() ==
                  static_cast<std::size_t>(image.value().width) * image.value().height * 4u);
        }
    }
    for (int round = 0; round < 3000; ++round) {
        std::string junk(1 + rng() % 200, '\0');
        for (char& c : junk) {
            c = static_cast<char>(rng() & 0xff);
        }
        (void)inflate(junk);
    }
}

TEST_CASE("Pictures get tidy names that never collide", "[image]") {
    const std::string a = "first picture", b = "second picture";
    const std::string pa = imageAssetPath("C:\\Art\\Fire Sheet.PNG", a);
    CHECK(pa.rfind("images/fire-sheet-", 0) == 0);
    CHECK(pa.size() == std::string("images/fire-sheet-12345678.png").size());
    CHECK(imageAssetPath("/Users/me/Fire Sheet.png", a) == pa);  // same picture, same name
    CHECK(imageAssetPath("/Users/me/Fire Sheet.png", b) != pa);
    CHECK(imageAssetPath("???.png", a).rfind("images/image-", 0) == 0);
    CHECK(imageAssetPath("Étoile ★ 2.png", a).rfind("images/toile-2-", 0) == 0);
}

TEST_CASE("An image set loads an effect's pictures from its folder", "[image]") {
    const auto folder = std::filesystem::temp_directory_path() / "vfxforge_image_set_test";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder / "images");
    REQUIRE(writeFileAtomic(folder / "images" / "a.png", fixture("rgba8.png")).ok());

    Effect effect;
    Asset a;
    a.id = Id{101};
    a.path = "images/a.png";
    Asset missing;
    missing.id = Id{102};
    missing.path = "images/gone.png";
    effect.assets = {a, missing};

    ImageSet set;
    auto problems = set.sync(effect, folder);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].find("gone.png") != std::string::npos);
    REQUIRE(set.has(Id{101}));
    CHECK_FALSE(set.has(Id{102}));
    CHECK(set.find(Id{101})->width == 13);

    // Syncing again keeps what is loaded, and says nothing changed.
    const auto revision = set.revision();
    const Image* before = set.find(Id{101});
    problems = set.sync(effect, folder);
    CHECK(set.find(Id{101}) == before);
    CHECK(set.revision() == revision);

    // An asset that is removed is forgotten.
    effect.assets = {missing};
    (void)set.sync(effect, folder);
    CHECK_FALSE(set.has(Id{101}));
    CHECK(set.revision() != revision);
    std::filesystem::remove_all(folder);
}
