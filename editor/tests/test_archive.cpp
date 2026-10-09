// The byte formats exports are written in. A small inflater here reads the
// compressed data back, so every test is a full round trip.
#include <catch2/catch_amalgamated.hpp>

#include <cstdint>
#include <random>
#include <string>

#include "vfx/editor/Archive.h"

using namespace vfx::editor;

namespace {

// Reads DEFLATE data that uses fixed Huffman codes, which is all ours writes.
class Inflater {
public:
    explicit Inflater(std::string_view in) : in_(in) {}

    std::string run() {
        std::string out;
        bool last = false;
        while (!last) {
            last = bits(1) == 1;
            const unsigned type = bits(2);
            REQUIRE(type == 1u);
            for (;;) {
                const unsigned symbol = literal();
                if (symbol < 256) {
                    out.push_back(static_cast<char>(symbol));
                } else if (symbol == 256) {
                    break;
                } else {
                    static const unsigned base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,
                                                      15, 17, 19, 23, 27, 31, 35, 43, 51,  59,
                                                      67, 83, 99, 115, 131, 163, 195, 227, 258};
                    static const int extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                  2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
                    const unsigned i = symbol - 257;
                    const unsigned length = base[i] + bits(extra[i]);
                    unsigned code = 0;
                    for (int k = 0; k < 5; ++k) {
                        code = (code << 1) | bits(1);
                    }
                    static const unsigned dbase[30] = {1,    2,    3,    4,    5,    7,    9,    13,
                                                       17,   25,   33,   49,   65,   97,   129,  193,
                                                       257,  385,  513,  769,  1025, 1537, 2049, 3073,
                                                       4097, 6145, 8193, 12289, 16385, 24577};
                    static const int dextra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3,  3,  4,  4,  5,  5,  6,
                                                   6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
                    const unsigned distance = dbase[code] + bits(dextra[code]);
                    REQUIRE(distance <= out.size());
                    for (unsigned k = 0; k < length; ++k) {
                        out.push_back(out[out.size() - distance]);
                    }
                }
            }
        }
        return out;
    }

private:
    unsigned bits(int count) {
        unsigned value = 0;
        for (int i = 0; i < count; ++i) {
            REQUIRE(at_ / 8 < in_.size());
            const unsigned bit = (static_cast<std::uint8_t>(in_[at_ / 8]) >> (at_ % 8)) & 1u;
            value |= bit << i;
            ++at_;
        }
        return value;
    }
    unsigned literal() {
        unsigned code = 0;
        for (int length = 1; length <= 9; ++length) {
            code = (code << 1) | bits(1);
            if (length == 7 && code <= 0x17) {
                return 256 + code;
            }
            if (length == 8 && code >= 0x30 && code <= 0xbf) {
                return code - 0x30;
            }
            if (length == 8 && code >= 0xc0 && code <= 0xc7) {
                return 280 + (code - 0xc0);
            }
            if (length == 9 && code >= 0x190) {
                return 144 + (code - 0x190);
            }
        }
        FAIL("not a fixed Huffman code");
        return 0;
    }

    std::string_view in_;
    std::size_t at_ = 0;
};

std::uint32_t read32be(const std::string& s, std::size_t at) {
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[at])) << 24) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[at + 1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[at + 2])) << 8) |
           static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[at + 3]));
}

std::uint32_t read32le(const std::string& s, std::size_t at) {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[at])) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[at + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[at + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[at + 3])) << 24);
}

}  // namespace

TEST_CASE("checksums match their published values", "[archive]") {
    CHECK(crc32("123456789") == 0xcbf43926u);
    CHECK(crc32("") == 0u);
    CHECK(adler32("Wikipedia") == 0x11e60398u);
    CHECK(adler32("") == 1u);
}

TEST_CASE("compressed data reads back exactly", "[archive]") {
    std::mt19937 rng(7);
    std::vector<std::string> samples = {
        "",
        "a",
        "abc",
        std::string(100000, '\0'),
        std::string(70000, 'x') + "tail",
    };
    std::string noise(20000, '\0');
    for (char& c : noise) {
        c = static_cast<char>(rng() & 0xffu);
    }
    samples.push_back(noise);
    // Picture-like: mostly empty rows with the odd repeated pattern.
    std::string rows;
    for (int y = 0; y < 300; ++y) {
        std::string row(1200, '\0');
        if (y % 7 == 0) {
            for (std::size_t x = 400; x < 700; ++x) {
                row[x] = static_cast<char>(x * 3 + static_cast<std::size_t>(y));
            }
        }
        rows += row;
    }
    samples.push_back(rows);

    for (const std::string& data : samples) {
        INFO("size " << data.size());
        const std::string packed = deflate(data, 1200);
        CHECK(Inflater(packed).run() == data);
        CHECK(Inflater(deflate(data)).run() == data);
    }
    // Runs and repeated rows shrink a great deal.
    CHECK(deflate(rows, 1200).size() < rows.size() / 20);
    CHECK(deflate(std::string(100000, '\0')).size() < 1000);
}

TEST_CASE("zlib and gzip wrappers are well formed", "[archive]") {
    const std::string data = "VFX Forge VFX Forge VFX Forge";
    const std::string z = zlibCompress(data);
    CHECK(static_cast<std::uint8_t>(z[0]) == 0x78);
    CHECK((static_cast<unsigned>(static_cast<std::uint8_t>(z[0])) * 256 + static_cast<std::uint8_t>(z[1])) % 31 == 0);
    CHECK(Inflater(std::string_view(z).substr(2, z.size() - 6)).run() == data);
    CHECK(read32be(z, z.size() - 4) == adler32(data));

    const std::string g = gzipCompress(data);
    CHECK(static_cast<std::uint8_t>(g[0]) == 0x1f);
    CHECK(static_cast<std::uint8_t>(g[1]) == 0x8b);
    CHECK(Inflater(std::string_view(g).substr(10, g.size() - 18)).run() == data);
    CHECK(read32le(g, g.size() - 8) == crc32(data));
    CHECK(read32le(g, g.size() - 4) == data.size());
}

TEST_CASE("tar archives have valid headers", "[archive]") {
    const std::string longPath =
        "0123456789abcdef0123456789abcdef/asset folder with a long name/and more/" + std::string(60, 'z') + ".png";
    const std::string tar = makeTar({{"one/asset", "hello"}, {longPath, std::string(600, 'q')}});
    REQUIRE(tar.size() == 512 + 512 + 512 + 1024 + 1024);
    CHECK(tar.substr(0, 9) == "one/asset");
    CHECK(tar.substr(257, 5) == "ustar");
    CHECK(tar.substr(124, 11) == "00000000005");  // size, octal
    CHECK(tar.substr(512, 5) == "hello");

    // Every header's checksum is right.
    for (std::size_t at : {std::size_t{0}, std::size_t{1024}}) {
        unsigned sum = 0;
        for (std::size_t i = 0; i < 512; ++i) {
            sum += (i >= 148 && i < 156) ? 32u : static_cast<std::uint8_t>(tar[at + i]);
        }
        CHECK(std::stoul(tar.substr(at + 148, 6), nullptr, 8) == sum);
    }
    // The long path is split into prefix and name.
    const std::string prefix = tar.substr(1024 + 345, 155).c_str();
    const std::string name = tar.substr(1024, 100).c_str();
    CHECK(prefix + "/" + name == longPath);
}

TEST_CASE("PNG images are well formed for every channel count", "[archive]") {
    for (int channels = 1; channels <= 4; ++channels) {
        INFO(channels);
        std::string pixels(static_cast<std::size_t>(5 * 3 * channels), '\x40');
        const std::string png = encodePngImage(5, 3, channels, pixels);
        CHECK(png.substr(1, 3) == "PNG");
        CHECK(read32be(png, 16) == 5u);
        CHECK(read32be(png, 20) == 3u);
        static const int types[5] = {0, 0, 4, 2, 6};
        CHECK(static_cast<int>(png[25]) == types[channels]);
        // Unpack the image data and check it.
        const std::size_t idat = png.find("IDAT");
        REQUIRE(idat != std::string::npos);
        const std::uint32_t length = read32be(png, idat - 4);
        const std::string z = png.substr(idat + 4, length);
        const std::string raw = Inflater(std::string_view(z).substr(2, z.size() - 6)).run();
        REQUIRE(raw.size() == 3u * (1u + 5u * static_cast<unsigned>(channels)));
        CHECK(raw[0] == '\0');
        CHECK(raw[1] == '\x40');
    }
}
