// Photoshop files: reading the flattened copy or the layers, and writing a
// one-layer document. The fixtures were made by psd-tools, an independent
// reader and writer (fixtures/psd/make_fixtures.py), and the expected
// pictures are psd-tools' own composites of them.
#include <catch2/catch_amalgamated.hpp>

#include <cstdlib>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

#include "vfx/editor/Image.h"
#include "vfx/editor/Psd.h"

using namespace vfx::editor;

namespace {

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The largest difference in a channel, counting colour only where the
// pixel can be seen (colour under alpha 0 means nothing).
int largestDifference(const Image& a, const Image& b) {
    REQUIRE(a.width == b.width);
    REQUIRE(a.height == b.height);
    int worst = 0;
    for (std::size_t p = 0; p < a.rgba.size() / 4; ++p) {
        const int alpha = std::min(a.rgba[p * 4 + 3], b.rgba[p * 4 + 3]);
        worst = std::max(worst, std::abs(a.rgba[p * 4 + 3] - b.rgba[p * 4 + 3]));
        if (alpha > 64) {
            for (int c = 0; c < 3; ++c) {
                worst = std::max(worst, std::abs(a.rgba[p * 4 + static_cast<std::size_t>(c)] -
                                                 b.rgba[p * 4 + static_cast<std::size_t>(c)]));
            }
        }
    }
    return worst;
}

Image test(int w, int h) {
    Image image;
    image.width = w;
    image.height = h;
    image.rgba.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t* px = &image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                            static_cast<std::size_t>(x)) * 4u];
            px[0] = static_cast<std::uint8_t>(x * 255 / (w - 1));
            px[1] = static_cast<std::uint8_t>(y * 255 / (h - 1));
            px[2] = 200;
            px[3] = static_cast<std::uint8_t>((x + y) % 3 == 0 ? 0 : (x * 7 + y * 13) % 256);
        }
    }
    return image;
}

}  // namespace

TEST_CASE("Photoshop files made elsewhere read as their maker shows them", "[psd]") {
    for (const char* kind : {"raw", "rle", "zip"}) {
        INFO(kind);
        const std::string base = std::string(VFX_EDITOR_FIXTURES) + "/psd/layers-" + kind;
        const std::string bytes = slurp(base + ".psd");
        REQUIRE(looksLikePsd(bytes));
        // psd-tools keeps a flattened copy too, without see-through parts.
        auto picture = readPsd(bytes);
        REQUIRE(picture);
        CHECK(picture.value().fromFlattenedCopy);
        auto flattened = decodePng(slurp(base + ".merged.png"));
        REQUIRE(flattened);
        CHECK(largestDifference(picture.value().merged, flattened.value()) <= 1);

        // Told the copy is not real, VFX Forge puts the layers together.
        std::string noCopy = bytes;
        const auto at = noCopy.find("8BIM\x04\x21");
        REQUIRE(at != std::string::npos);
        noCopy[at + 4 + 2 + 2 + 4 + 4] = 0;  // hasRealMergedData
        picture = readPsd(noCopy);
        REQUIRE(picture);
        CHECK_FALSE(picture.value().fromFlattenedCopy);
        CHECK(picture.value().merged.width == 48);
        CHECK(picture.value().merged.height == 32);
        REQUIRE(picture.value().layers.size() == 3);
        CHECK(picture.value().layers[0].name == "Red blob");
        CHECK(picture.value().layers[1].opacity == Catch::Approx(128.0 / 255.0));
        CHECK_FALSE(picture.value().layers[2].visible);
        CHECK(picture.value().layers[1].left == 20);
        CHECK(picture.value().layers[1].top == 6);
        auto expected = decodePng(slurp(base + ".expect.png"));
        REQUIRE(expected);
        CHECK(largestDifference(picture.value().merged, expected.value()) <= 3);
    }
}

TEST_CASE("A written Photoshop file reads back the same, both ways", "[psd]") {
    const Image original = test(37, 21);
    const std::string bytes = writePsd(original, "Fire frame");
    auto picture = readPsd(bytes);
    REQUIRE(picture);
    CHECK(picture.value().fromFlattenedCopy);
    CHECK(picture.value().notes.empty());
    CHECK(largestDifference(picture.value().merged, original) <= 2);
    REQUIRE(picture.value().layers.size() == 1);
    CHECK(picture.value().layers[0].name == "Fire frame");
    CHECK(picture.value().layers[0].image.rgba == original.rgba);

    // With the flattened copy marked as not real, the layer is used.
    std::string noCopy = bytes;
    const auto at = noCopy.find("8BIM\x04\x21");
    REQUIRE(at != std::string::npos);
    noCopy[at + 4 + 2 + 2 + 4 + 4] = 0;  // hasRealMergedData
    auto layered = readPsd(noCopy);
    REQUIRE(layered);
    CHECK_FALSE(layered.value().fromFlattenedCopy);
    CHECK_FALSE(layered.value().notes.empty());
    CHECK(largestDifference(layered.value().merged, original) <= 1);

    if (const char* dir = std::getenv("VFX_PSD_OUT")) {
        std::ofstream(std::string(dir) + "/written.psd", std::ios::binary) << bytes;
        std::ofstream(std::string(dir) + "/written.png", std::ios::binary) << encodePng(original);
    }
}

TEST_CASE("Damaged and unsupported Photoshop files are refused with a reason", "[psd]") {
    const std::string bytes = writePsd(test(8, 8), "x");
    for (std::size_t cut : {std::size_t{10}, std::size_t{30}, std::size_t{120}, bytes.size() / 2}) {
        INFO(cut);
        auto picture = readPsd(std::string_view(bytes).substr(0, cut));
        CHECK_FALSE(picture);
    }
    std::string cmyk = bytes;
    cmyk[25] = 4;  // CMYK
    auto refused = readPsd(cmyk);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().message.find("RGB") != std::string::npos);
    CHECK_FALSE(readPsd("not a picture at all, just words"));
}
