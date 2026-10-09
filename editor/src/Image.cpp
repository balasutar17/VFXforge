#include "vfx/editor/Image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#include "vfx/FileIO.h"
#include "vfx/editor/Archive.h"

namespace vfx::editor {

// ----------------------------------------------------------------- inflate

namespace {

// Canonical Huffman decoding tables, built from code lengths (RFC 1951 3.2.2).
struct Huffman {
    std::array<std::uint16_t, 16> count{};    // codes of each length
    std::array<std::uint16_t, 320> symbol{};  // symbols ordered by code
};

// False when the lengths describe more codes than fit.
bool build(Huffman& h, const std::uint8_t* lengths, int n) {
    h.count.fill(0);
    for (int i = 0; i < n; ++i) {
        ++h.count[lengths[i]];
    }
    h.count[0] = 0;
    int left = 1;
    for (int len = 1; len < 16; ++len) {
        left <<= 1;
        left -= h.count[static_cast<std::size_t>(len)];
        if (left < 0) {
            return false;
        }
    }
    std::array<std::uint16_t, 16> offset{};
    for (int len = 1; len < 15; ++len) {
        offset[static_cast<std::size_t>(len + 1)] =
            static_cast<std::uint16_t>(offset[static_cast<std::size_t>(len)] +
                                       h.count[static_cast<std::size_t>(len)]);
    }
    for (int i = 0; i < n; ++i) {
        if (lengths[i] != 0) {
            h.symbol[offset[lengths[i]]++] = static_cast<std::uint16_t>(i);
        }
    }
    return true;
}

class Inflate {
public:
    Inflate(std::string_view in, std::string& out) : in_(in), out_(out) {}

    bool run() {
        bool last = false;
        while (!last) {
            last = bits(1) == 1;
            const int type = bits(2);
            bool ok = false;
            if (type == 0) {
                ok = stored();
            } else if (type == 1) {
                ok = fixed();
            } else if (type == 2) {
                ok = dynamic();
            }
            if (!ok || bad_) {
                return false;
            }
        }
        return true;
    }

private:
    int bits(int need) {
        std::uint32_t value = buffer_;
        while (count_ < need) {
            if (at_ >= in_.size()) {
                bad_ = true;
                return 0;
            }
            value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(in_[at_++])) << count_;
            count_ += 8;
        }
        buffer_ = value >> need;
        count_ -= need;
        return static_cast<int>(value & ((1u << need) - 1u));
    }

    bool stored() {
        buffer_ = 0;
        count_ = 0;
        if (at_ + 4 > in_.size()) {
            return false;
        }
        const auto byte = [&](std::size_t i) { return static_cast<std::uint8_t>(in_[i]); };
        const unsigned len = byte(at_) | (byte(at_ + 1) << 8);
        const unsigned nlen = byte(at_ + 2) | (byte(at_ + 3) << 8);
        at_ += 4;
        if (len != (~nlen & 0xffffu) || at_ + len > in_.size()) {
            return false;
        }
        out_.append(in_.data() + at_, len);
        at_ += len;
        return true;
    }

    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= bits(1);
            if (bad_) {
                return -1;
            }
            const int n = h.count[static_cast<std::size_t>(len)];
            if (code - n < first) {
                return h.symbol[static_cast<std::size_t>(index + (code - first))];
            }
            index += n;
            first += n;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }

    bool codes(const Huffman& lit, const Huffman& dist) {
        static const std::uint16_t lbase[29] = {3,  4,  5,  6,  7,  8,  9,  10,  11,  13,
                                                15, 17, 19, 23, 27, 31, 35, 43,  51,  59,
                                                67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const std::uint8_t lextra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const std::uint16_t dbase[30] = {1,    2,    3,    4,    5,    7,     9,     13,
                                                17,   25,   33,   49,   65,   97,    129,   193,
                                                257,  385,  513,  769,  1025, 1537,  2049,  3073,
                                                4097, 6145, 8193, 12289, 16385, 24577};
        static const std::uint8_t dextra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int symbol = decode(lit);
            if (symbol < 0) {
                return false;
            }
            if (symbol < 256) {
                out_.push_back(static_cast<char>(symbol));
                continue;
            }
            if (symbol == 256) {
                return true;
            }
            symbol -= 257;
            if (symbol >= 29) {
                return false;
            }
            const std::size_t length = lbase[symbol] + static_cast<std::size_t>(bits(lextra[symbol]));
            const int d = decode(dist);
            if (d < 0 || d >= 30) {
                return false;
            }
            const std::size_t distance = dbase[d] + static_cast<std::size_t>(bits(dextra[d]));
            if (bad_ || distance > out_.size()) {
                return false;
            }
            const std::size_t from = out_.size() - distance;
            for (std::size_t k = 0; k < length; ++k) {
                out_.push_back(out_[from + k]);
            }
        }
    }

    bool fixed() {
        static Huffman lit, dist;
        static const bool ready = [] {
            std::uint8_t lengths[288];
            int i = 0;
            for (; i < 144; ++i) lengths[i] = 8;
            for (; i < 256; ++i) lengths[i] = 9;
            for (; i < 280; ++i) lengths[i] = 7;
            for (; i < 288; ++i) lengths[i] = 8;
            build(lit, lengths, 288);
            std::uint8_t d[30];
            std::fill(std::begin(d), std::end(d), std::uint8_t{5});
            build(dist, d, 30);
            return true;
        }();
        (void)ready;
        return codes(lit, dist);
    }

    bool dynamic() {
        const int nlen = bits(5) + 257;
        const int ndist = bits(5) + 1;
        const int ncode = bits(4) + 4;
        if (bad_ || nlen > 286 || ndist > 30) {
            return false;
        }
        static const std::uint8_t order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                               11, 4,  12, 3, 13, 2, 14, 1, 15};
        std::uint8_t lengths[320] = {};
        for (int i = 0; i < ncode; ++i) {
            lengths[order[i]] = static_cast<std::uint8_t>(bits(3));
        }
        Huffman lencode;
        if (!build(lencode, lengths, 19)) {
            return false;
        }
        int index = 0;
        while (index < nlen + ndist) {
            int symbol = decode(lencode);
            if (symbol < 0) {
                return false;
            }
            if (symbol < 16) {
                lengths[index++] = static_cast<std::uint8_t>(symbol);
                continue;
            }
            std::uint8_t value = 0;
            int repeat = 0;
            if (symbol == 16) {
                if (index == 0) {
                    return false;
                }
                value = lengths[index - 1];
                repeat = 3 + bits(2);
            } else if (symbol == 17) {
                repeat = 3 + bits(3);
            } else {
                repeat = 11 + bits(7);
            }
            if (bad_ || index + repeat > nlen + ndist) {
                return false;
            }
            while (repeat--) {
                lengths[index++] = value;
            }
        }
        if (lengths[256] == 0) {
            return false;
        }
        Huffman lit, dist;
        if (!build(lit, lengths, nlen) || !build(dist, lengths + nlen, ndist)) {
            return false;
        }
        return codes(lit, dist);
    }

    std::string_view in_;
    std::string& out_;
    std::size_t at_ = 0;
    std::uint32_t buffer_ = 0;
    int count_ = 0;
    bool bad_ = false;
};

std::uint32_t be32(std::string_view s, std::size_t at) {
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[at])) << 24) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[at + 1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[at + 2])) << 8) |
           static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[at + 3]));
}

}  // namespace

Result<std::string> inflate(std::string_view deflated, std::size_t sizeHint) {
    std::string out;
    out.reserve(sizeHint);
    Inflate reader(deflated, out);
    if (!reader.run()) {
        return makeError("The compressed data is damaged.");
    }
    return out;
}

Result<std::string> zlibDecompress(std::string_view zlib, std::size_t sizeHint) {
    if (zlib.size() < 6) {
        return makeError("The compressed data is damaged.", "too short");
    }
    const auto cmf = static_cast<std::uint8_t>(zlib[0]);
    const auto flg = static_cast<std::uint8_t>(zlib[1]);
    if ((cmf & 0x0f) != 8 || ((cmf << 8) | flg) % 31 != 0 || (flg & 0x20) != 0) {
        return makeError("The compressed data is damaged.", "not zlib data");
    }
    auto out = inflate(zlib.substr(2), sizeHint);
    if (!out.ok()) {
        return out;
    }
    // The checksum at the end is not checked: a damaged picture is better
    // shown than refused, and the decoder never reads out of bounds.
    return out;
}

// --------------------------------------------------------------------- PNG

Result<Image> decodePng(std::string_view bytes) {
    static const char signature[8] = {'\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n'};
    if (bytes.size() < 8 || std::memcmp(bytes.data(), signature, 8) != 0) {
        return makeError("This is not a PNG picture.");
    }
    std::uint32_t width = 0, height = 0;
    int depth = 0, colorType = -1, interlace = 0;
    std::string data;
    std::string palette;
    std::string transparency;
    bool header = false;

    std::size_t at = 8;
    while (at + 12 <= bytes.size()) {
        const std::uint32_t length = be32(bytes, at);
        if (length > bytes.size() - at - 12) {
            return makeError("The PNG picture is damaged.", "a chunk runs past the end");
        }
        const std::string_view type = bytes.substr(at + 4, 4);
        const std::string_view body = bytes.substr(at + 8, length);
        at += 12 + static_cast<std::size_t>(length);
        if (type == "IHDR") {
            if (length < 13) {
                return makeError("The PNG picture is damaged.", "short header");
            }
            width = be32(body, 0);
            height = be32(body, 4);
            depth = static_cast<std::uint8_t>(body[8]);
            colorType = static_cast<std::uint8_t>(body[9]);
            interlace = static_cast<std::uint8_t>(body[12]);
            header = true;
        } else if (type == "PLTE") {
            palette.assign(body);
        } else if (type == "tRNS") {
            transparency.assign(body);
        } else if (type == "IDAT") {
            data.append(body);
        } else if (type == "IEND") {
            break;
        }
    }
    if (!header) {
        return makeError("The PNG picture is damaged.", "no header");
    }
    if (width == 0 || height == 0 || width > 8192 || height > 8192) {
        return makeError("The picture is too large.",
                         "pictures can be at most 8192 pixels wide and high");
    }
    int channels = 0;
    switch (colorType) {
        case 0: channels = 1; break;
        case 2: channels = 3; break;
        case 3: channels = 1; break;
        case 4: channels = 2; break;
        case 6: channels = 4; break;
        default: return makeError("The PNG picture is damaged.", "unknown colour type");
    }
    const bool depthOk = depth == 8 || depth == 16 ||
                         ((colorType == 0 || colorType == 3) &&
                          (depth == 1 || depth == 2 || depth == 4));
    if (!depthOk || (colorType == 3 && depth == 16) || interlace > 1) {
        return makeError("The PNG picture is damaged.", "unusual bit depth");
    }
    if (colorType == 3 && palette.size() < 3) {
        return makeError("The PNG picture is damaged.", "no palette");
    }

    const std::size_t bitsPerPixel = static_cast<std::size_t>(channels * depth);
    const std::size_t bpp = std::max<std::size_t>(1, bitsPerPixel / 8);  // filter step

    // The seven interlace passes, or one pass covering every pixel.
    struct Pass {
        int x0, y0, dx, dy;
    };
    static const Pass adam7[7] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
                                  {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
    static const Pass whole[1] = {{0, 0, 1, 1}};
    const Pass* passes = interlace ? adam7 : whole;
    const int passCount = interlace ? 7 : 1;

    std::size_t expected = 0;
    for (int p = 0; p < passCount; ++p) {
        const std::size_t pw = (width - static_cast<std::uint32_t>(passes[p].x0) +
                                static_cast<std::uint32_t>(passes[p].dx) - 1) /
                               static_cast<std::uint32_t>(passes[p].dx);
        const std::size_t ph = (height - static_cast<std::uint32_t>(passes[p].y0) +
                                static_cast<std::uint32_t>(passes[p].dy) - 1) /
                               static_cast<std::uint32_t>(passes[p].dy);
        if (pw && ph) {
            expected += ph * (1 + (pw * bitsPerPixel + 7) / 8);
        }
    }
    auto raw = zlibDecompress(data, expected);
    if (!raw.ok()) {
        return makeError("The PNG picture is damaged.", raw.error().message);
    }
    std::string& filtered = raw.value();
    if (filtered.size() < expected) {
        return makeError("The PNG picture is damaged.", "the pixel data is cut short");
    }

    Image image;
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.rgba.assign(static_cast<std::size_t>(width) * height * 4u, 0);

    // Transparency for grey and RGB pictures: one colour that is see-through.
    auto key16 = [&](std::size_t i) -> int {
        if (transparency.size() < i * 2 + 2) {
            return -1;
        }
        return (static_cast<std::uint8_t>(transparency[i * 2]) << 8) |
               static_cast<std::uint8_t>(transparency[i * 2 + 1]);
    };

    std::size_t offset = 0;
    std::vector<std::uint8_t> previous, current;
    for (int p = 0; p < passCount; ++p) {
        const Pass& pass = passes[p];
        const std::size_t pw = (width - static_cast<std::uint32_t>(pass.x0) +
                                static_cast<std::uint32_t>(pass.dx) - 1) /
                               static_cast<std::uint32_t>(pass.dx);
        const std::size_t ph = (height - static_cast<std::uint32_t>(pass.y0) +
                                static_cast<std::uint32_t>(pass.dy) - 1) /
                               static_cast<std::uint32_t>(pass.dy);
        if (pw == 0 || ph == 0) {
            continue;
        }
        const std::size_t stride = (pw * bitsPerPixel + 7) / 8;
        previous.assign(stride, 0);
        current.assign(stride, 0);
        for (std::size_t row = 0; row < ph; ++row) {
            const auto filter = static_cast<std::uint8_t>(filtered[offset]);
            const auto* src = reinterpret_cast<const std::uint8_t*>(filtered.data() + offset + 1);
            offset += 1 + stride;
            for (std::size_t i = 0; i < stride; ++i) {
                const int a = i >= bpp ? current[i - bpp] : 0;
                const int b = previous[i];
                const int c = i >= bpp ? previous[i - bpp] : 0;
                int predicted = 0;
                switch (filter) {
                    case 0: predicted = 0; break;
                    case 1: predicted = a; break;
                    case 2: predicted = b; break;
                    case 3: predicted = (a + b) / 2; break;
                    case 4: {
                        const int pp = a + b - c;
                        const int pa = std::abs(pp - a), pb = std::abs(pp - b), pc = std::abs(pp - c);
                        predicted = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                        break;
                    }
                    default:
                        return makeError("The PNG picture is damaged.", "unknown row filter");
                }
                current[i] = static_cast<std::uint8_t>(src[i] + predicted);
            }

            const std::size_t y = static_cast<std::size_t>(pass.y0) + row * static_cast<std::size_t>(pass.dy);
            for (std::size_t col = 0; col < pw; ++col) {
                const std::size_t x = static_cast<std::size_t>(pass.x0) + col * static_cast<std::size_t>(pass.dx);
                // Sample k of this pixel, scaled to 0..255, and its raw value.
                auto sample = [&](int k, int& rawValue) -> std::uint8_t {
                    const std::size_t bit = (col * static_cast<std::size_t>(channels) +
                                             static_cast<std::size_t>(k)) *
                                            static_cast<std::size_t>(depth);
                    if (depth == 16) {
                        rawValue = (current[bit / 8] << 8) | current[bit / 8 + 1];
                        return current[bit / 8];
                    }
                    if (depth == 8) {
                        rawValue = current[bit / 8];
                        return current[bit / 8];
                    }
                    const int shift = 8 - depth - static_cast<int>(bit % 8);
                    rawValue = (current[bit / 8] >> shift) & ((1 << depth) - 1);
                    return static_cast<std::uint8_t>(rawValue * 255 / ((1 << depth) - 1));
                };
                std::uint8_t* out = &image.rgba[(y * width + x) * 4u];
                int r0 = 0, r1 = 0, r2 = 0, r3 = 0;
                switch (colorType) {
                    case 0: {
                        const std::uint8_t g = sample(0, r0);
                        out[0] = out[1] = out[2] = g;
                        out[3] = r0 == key16(0) ? 0 : 255;
                        break;
                    }
                    case 2: {
                        out[0] = sample(0, r0);
                        out[1] = sample(1, r1);
                        out[2] = sample(2, r2);
                        out[3] = (r0 == key16(0) && r1 == key16(1) && r2 == key16(2)) ? 0 : 255;
                        break;
                    }
                    case 3: {
                        sample(0, r0);
                        const std::size_t i = static_cast<std::size_t>(r0);
                        if (i * 3 + 2 < palette.size()) {
                            out[0] = static_cast<std::uint8_t>(palette[i * 3]);
                            out[1] = static_cast<std::uint8_t>(palette[i * 3 + 1]);
                            out[2] = static_cast<std::uint8_t>(palette[i * 3 + 2]);
                        }
                        out[3] = i < transparency.size() ? static_cast<std::uint8_t>(transparency[i]) : 255;
                        break;
                    }
                    case 4: {
                        const std::uint8_t g = sample(0, r0);
                        out[0] = out[1] = out[2] = g;
                        out[3] = sample(1, r1);
                        break;
                    }
                    default: {
                        out[0] = sample(0, r0);
                        out[1] = sample(1, r1);
                        out[2] = sample(2, r2);
                        out[3] = sample(3, r3);
                        break;
                    }
                }
            }
            std::swap(previous, current);
        }
    }
    return image;
}

std::string encodePng(const Image& image) {
    const std::string_view pixels(reinterpret_cast<const char*>(image.rgba.data()), image.rgba.size());
    return encodePngImage(image.width, image.height, 4, pixels);
}

// -------------------------------------------------------------------- mips

MipChain makeMipChain(const Image& image) {
    MipChain chain;
    Image first = image;
    for (std::size_t i = 0; i + 3 < first.rgba.size(); i += 4) {
        const unsigned a = first.rgba[i + 3];
        for (int c = 0; c < 3; ++c) {
            first.rgba[i + static_cast<std::size_t>(c)] =
                static_cast<std::uint8_t>((first.rgba[i + static_cast<std::size_t>(c)] * a + 127u) / 255u);
        }
    }
    chain.levels.push_back(std::move(first));
    while (chain.levels.back().width > 1 || chain.levels.back().height > 1) {
        const Image& big = chain.levels.back();
        Image small;
        small.width = std::max(1, big.width / 2);
        small.height = std::max(1, big.height / 2);
        small.rgba.resize(static_cast<std::size_t>(small.width) * small.height * 4u);
        for (int y = 0; y < small.height; ++y) {
            for (int x = 0; x < small.width; ++x) {
                for (int c = 0; c < 4; ++c) {
                    unsigned sum = 0;
                    for (int k = 0; k < 4; ++k) {
                        const int bx = std::min(big.width - 1, x * 2 + (k & 1));
                        const int by = std::min(big.height - 1, y * 2 + (k >> 1));
                        sum += big.rgba[(static_cast<std::size_t>(by) * big.width + bx) * 4u +
                                        static_cast<std::size_t>(c)];
                    }
                    small.rgba[(static_cast<std::size_t>(y) * small.width + x) * 4u +
                               static_cast<std::size_t>(c)] = static_cast<std::uint8_t>((sum + 2u) / 4u);
                }
            }
        }
        chain.levels.push_back(std::move(small));
    }
    return chain;
}

namespace {

void sampleLevel(const Image& image, float u, float v, float out[4]) {
    const float fx = u * static_cast<float>(image.width) - 0.5f;
    const float fy = v * static_cast<float>(image.height) - 0.5f;
    const float x0f = std::floor(fx), y0f = std::floor(fy);
    const float tx = fx - x0f, ty = fy - y0f;
    const int x0 = static_cast<int>(x0f), y0 = static_cast<int>(y0f);
    auto at = [&](int x, int y, int c) {
        x = std::clamp(x, 0, image.width - 1);
        y = std::clamp(y, 0, image.height - 1);
        return static_cast<float>(image.rgba[(static_cast<std::size_t>(y) * image.width + x) * 4u +
                                             static_cast<std::size_t>(c)]);
    };
    for (int c = 0; c < 4; ++c) {
        const float top = at(x0, y0, c) + (at(x0 + 1, y0, c) - at(x0, y0, c)) * tx;
        const float bottom = at(x0, y0 + 1, c) + (at(x0 + 1, y0 + 1, c) - at(x0, y0 + 1, c)) * tx;
        out[c] = (top + (bottom - top) * ty) / 255.0f;
    }
}

}  // namespace

void sampleMips(const MipChain& mips, float u, float v, float footprint, float out[4]) {
    if (mips.levels.empty()) {
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        return;
    }
    const float last = static_cast<float>(mips.levels.size() - 1);
    const float level = footprint > 1.0f ? std::min(std::log2(footprint), last) : 0.0f;
    const auto lower = static_cast<std::size_t>(level);
    sampleLevel(mips.levels[lower], u, v, out);
    const float blend = level - static_cast<float>(lower);
    if (blend > 0.0f && lower + 1 < mips.levels.size()) {
        float next[4];
        sampleLevel(mips.levels[lower + 1], u, v, next);
        for (int c = 0; c < 4; ++c) {
            out[c] += (next[c] - out[c]) * blend;
        }
    }
}

// ------------------------------------------------------------------ naming

std::string imageAssetPath(std::string_view originalName, std::string_view pngBytes) {
    // The name without folders or extension.
    std::string_view name = originalName;
    if (const auto slash = name.find_last_of("/\\"); slash != std::string_view::npos) {
        name.remove_prefix(slash + 1);
    }
    if (const auto dot = name.find_last_of('.'); dot != std::string_view::npos && dot > 0) {
        name = name.substr(0, dot);
    }
    std::string clean;
    for (const char ch : name) {
        const auto c = static_cast<unsigned char>(ch);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            clean.push_back(static_cast<char>(c));
        } else if (c >= 'A' && c <= 'Z') {
            clean.push_back(static_cast<char>(c - 'A' + 'a'));
        } else if (!clean.empty() && clean.back() != '-') {
            clean.push_back('-');
        }
        if (clean.size() >= 40) {
            break;
        }
    }
    while (!clean.empty() && clean.back() == '-') {
        clean.pop_back();
    }
    if (clean.empty()) {
        clean = "image";
    }
    char hex[9];
    std::snprintf(hex, sizeof hex, "%08x", crc32(pngBytes) ^ static_cast<std::uint32_t>(pngBytes.size()));
    return std::string(kImagesFolder) + "/" + clean + "-" + hex + ".png";
}

// ---------------------------------------------------------------- ImageSet

const Image* ImageSet::find(Id asset) const {
    const auto it = images_.find(asset.value);
    return it == images_.end() ? nullptr : it->second.image.get();
}

std::shared_ptr<const Image> ImageSet::shared(Id asset) const {
    const auto it = images_.find(asset.value);
    return it == images_.end() ? nullptr : it->second.image;
}

const MipChain* ImageSet::mips(Id asset) const {
    const auto it = images_.find(asset.value);
    if (it == images_.end() || !it->second.image) {
        return nullptr;
    }
    if (!it->second.mips) {
        // Made on first use: only exports need them.
        it->second.mips = std::make_shared<const MipChain>(makeMipChain(*it->second.image));
    }
    return it->second.mips.get();
}

void ImageSet::put(Id asset, std::shared_ptr<const Image> image, std::string path) {
    images_[asset.value] = Entry{std::move(image), nullptr, std::move(path)};
    ++revision_;
}

std::vector<std::string> ImageSet::sync(const Effect& effect, const std::filesystem::path& folder) {
    std::vector<std::string> problems;
    std::map<std::uint64_t, Entry> next;
    for (const Asset& asset : effect.assets) {
        if (asset.kind != "texture") {
            continue;
        }
        const auto old = images_.find(asset.id.value);
        if (old != images_.end() && old->second.path == asset.path && old->second.image) {
            next[asset.id.value] = old->second;
            continue;
        }
        const auto failed = failed_.find(asset.id.value);
        if (failed != failed_.end() && failed->second.first == asset.path) {
            problems.push_back(failed->second.second);
            continue;
        }
        auto fail = [&](std::string problem) {
            failed_[asset.id.value] = {asset.path, problem};
            problems.push_back(std::move(problem));
        };
        if (folder.empty()) {
            fail("The picture \"" + asset.path + "\" can't be found.");
            continue;
        }
        auto bytes = readFile(folder / pathFromUtf8(asset.path));
        if (!bytes.ok()) {
            fail("The picture \"" + asset.path + "\" can't be found next to the effect.");
            continue;
        }
        auto image = decodePng(bytes.value());
        if (!image.ok()) {
            fail("The picture \"" + asset.path + "\" can't be read: " + image.error().message);
            continue;
        }
        failed_.erase(asset.id.value);
        next[asset.id.value] =
            Entry{std::make_shared<const Image>(std::move(image.value())), nullptr, asset.path};
        ++revision_;
    }
    if (next.size() != images_.size()) {
        ++revision_;
    }
    images_ = std::move(next);
    return problems;
}

}  // namespace vfx::editor
