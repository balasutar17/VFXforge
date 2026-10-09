#include "vfx/editor/Archive.h"

#include <algorithm>
#include <cstring>

namespace vfx::editor {

std::uint32_t crc32(std::string_view bytes, std::uint32_t crc) {
    static const auto table = [] {
        std::vector<std::uint32_t> t(256);
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            }
            t[n] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (const char ch : bytes) {
        crc = table[(crc ^ static_cast<std::uint8_t>(ch)) & 0xffu] ^ (crc >> 8);
    }
    return ~crc;
}

std::uint32_t adler32(std::string_view bytes) {
    std::uint32_t a = 1, b = 0;
    for (const char ch : bytes) {
        a = (a + static_cast<std::uint8_t>(ch)) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

namespace {

// Writes bits least significant first, as DEFLATE wants.
class BitWriter {
public:
    void bits(std::uint32_t value, int count) {
        for (int i = 0; i < count; ++i) {
            buffer_ |= ((value >> i) & 1u) << used_;
            if (++used_ == 8) {
                out_.push_back(static_cast<char>(buffer_));
                buffer_ = 0;
                used_ = 0;
            }
        }
    }
    // Huffman codes are defined most significant bit first.
    void code(std::uint32_t value, int count) {
        for (int i = count - 1; i >= 0; --i) {
            bits((value >> i) & 1u, 1);
        }
    }
    std::string finish() {
        if (used_ > 0) {
            out_.push_back(static_cast<char>(buffer_));
            buffer_ = 0;
            used_ = 0;
        }
        return std::move(out_);
    }

private:
    std::string out_;
    std::uint32_t buffer_ = 0;
    int used_ = 0;
};

void literal(BitWriter& w, unsigned value) {
    if (value < 144) {
        w.code(0x30 + value, 8);
    } else if (value < 256) {
        w.code(0x190 + (value - 144), 9);
    } else if (value < 280) {
        w.code(value - 256, 7);
    } else {
        w.code(0xc0 + (value - 280), 8);
    }
}

// A copy of length bytes from distance bytes back (3..258, 1..32768).
void match(BitWriter& w, unsigned length, unsigned distance) {
    static const unsigned base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                      31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const int extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                  2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    int i = 28;
    while (base[i] > length) {
        --i;
    }
    literal(w, 257u + static_cast<unsigned>(i));
    w.bits(length - base[i], extra[i]);

    static const unsigned dbase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                       33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                       1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const int dextra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                   6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    int d = 29;
    while (dbase[d] > distance) {
        --d;
    }
    w.code(static_cast<std::uint32_t>(d), 5);
    w.bits(distance - dbase[d], dextra[d]);
}

}  // namespace

std::string deflate(std::string_view bytes, std::size_t rowLength) {
    BitWriter w;
    w.bits(1, 1);  // the only block, and the last
    w.bits(1, 2);  // fixed Huffman codes
    const std::size_t n = bytes.size();
    const bool rows = rowLength > 0 && rowLength <= 32768;
    std::size_t at = 0;
    while (at < n) {
        // The longer of: the byte before repeated, or the row above repeated.
        std::size_t best = 0, from = 0;
        if (at >= 1) {
            std::size_t len = 0;
            while (at + len < n && len < 258 && bytes[at + len] == bytes[at - 1 + len]) {
                ++len;
            }
            best = len;
            from = 1;
        }
        if (rows && at >= rowLength) {
            std::size_t len = 0;
            while (at + len < n && len < 258 && bytes[at + len] == bytes[at - rowLength + len]) {
                ++len;
            }
            if (len > best) {
                best = len;
                from = rowLength;
            }
        }
        if (best >= 3) {
            match(w, static_cast<unsigned>(best), static_cast<unsigned>(from));
            at += best;
        } else {
            literal(w, static_cast<std::uint8_t>(bytes[at]));
            ++at;
        }
    }
    literal(w, 256);  // end of block
    return w.finish();
}

namespace {

void put32be(std::string& out, std::uint32_t v) {
    out.push_back(static_cast<char>(v >> 24));
    out.push_back(static_cast<char>(v >> 16));
    out.push_back(static_cast<char>(v >> 8));
    out.push_back(static_cast<char>(v));
}

void put32le(std::string& out, std::uint32_t v) {
    out.push_back(static_cast<char>(v));
    out.push_back(static_cast<char>(v >> 8));
    out.push_back(static_cast<char>(v >> 16));
    out.push_back(static_cast<char>(v >> 24));
}

void chunk(std::string& out, const char type[4], const std::string& body) {
    put32be(out, static_cast<std::uint32_t>(body.size()));
    std::string typed(type, 4);
    typed += body;
    out += typed;
    put32be(out, crc32(typed));
}

void octal(char* field, std::size_t width, std::uint64_t value) {
    // width - 1 digits and a terminating zero byte.
    std::memset(field, '0', width - 1);
    field[width - 1] = '\0';
    for (std::size_t i = width - 1; i > 0 && value > 0; --i) {
        field[i - 1] = static_cast<char>('0' + (value & 7u));
        value >>= 3;
    }
}

}  // namespace

std::string zlibCompress(std::string_view bytes, std::size_t rowLength) {
    std::string out("\x78\x01", 2);
    out += deflate(bytes, rowLength);
    put32be(out, adler32(bytes));
    return out;
}

std::string gzipCompress(std::string_view bytes) {
    std::string out("\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\xff", 10);
    out += deflate(bytes, 0);
    put32le(out, crc32(bytes));
    put32le(out, static_cast<std::uint32_t>(bytes.size()));
    return out;
}

std::string makeTar(const std::vector<TarEntry>& entries) {
    std::string out;
    for (const TarEntry& entry : entries) {
        char header[512];
        std::memset(header, 0, sizeof header);
        std::string name = entry.path, prefix;
        if (name.size() > 100) {
            // ustar splits long paths at a slash: up to 155 bytes, then 100.
            const std::size_t cut = name.rfind('/', 155);
            if (cut != std::string::npos && name.size() - cut - 1 <= 100) {
                prefix = name.substr(0, cut);
                name = name.substr(cut + 1);
            }
        }
        std::memcpy(header, name.data(), std::min<std::size_t>(name.size(), 100));
        octal(header + 100, 8, 0644);
        octal(header + 108, 8, 0);
        octal(header + 116, 8, 0);
        octal(header + 124, 12, entry.bytes.size());
        octal(header + 136, 12, 0);
        std::memset(header + 148, ' ', 8);  // checksum is summed as spaces
        header[156] = '0';                  // a regular file
        std::memcpy(header + 257, "ustar", 6);
        header[263] = '0';
        header[264] = '0';
        std::memcpy(header + 345, prefix.data(), std::min<std::size_t>(prefix.size(), 155));
        unsigned sum = 0;
        for (const char c : header) {
            sum += static_cast<std::uint8_t>(c);
        }
        octal(header + 148, 7, sum);
        header[155] = ' ';
        out.append(header, sizeof header);
        out += entry.bytes;
        out.append((512 - entry.bytes.size() % 512) % 512, '\0');
    }
    out.append(1024, '\0');  // two empty blocks end the archive
    return out;
}

std::string encodePngImage(int width, int height, int channels, std::string_view pixels) {
    std::string out("\x89PNG\r\n\x1a\n", 8);
    static const char colourType[5] = {0, 0, 4, 2, 6};
    std::string header;
    put32be(header, static_cast<std::uint32_t>(width));
    put32be(header, static_cast<std::uint32_t>(height));
    header.push_back('\x08');
    header.push_back(colourType[channels >= 1 && channels <= 4 ? channels : 4]);
    header += std::string("\x00\x00\x00", 3);
    chunk(out, "IHDR", header);

    const std::size_t rowBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
    std::string raw;
    raw.reserve((rowBytes + 1u) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        raw.push_back('\0');  // no filter: flat areas stay flat for the compressor
        raw.append(pixels.substr(static_cast<std::size_t>(y) * rowBytes, rowBytes));
    }
    chunk(out, "IDAT", zlibCompress(raw, rowBytes + 1u));
    chunk(out, "IEND", std::string());
    return out;
}

}  // namespace vfx::editor
