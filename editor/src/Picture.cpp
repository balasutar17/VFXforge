#include "vfx/editor/Picture.h"

#include <algorithm>
#include <cmath>

#include "vfx/FileIO.h"
#include "vfx/editor/Shapes.h"

namespace vfx::editor {

namespace {

struct Edge {
    float a = 0, b = 0, c = 0;
    float at(float x, float y) const { return a * x + b * y + c; }
};

Edge edgeThrough(const SpriteVertex& p, const SpriteVertex& q) {
    return Edge{p.y - q.y, q.x - p.x, p.x * q.y - p.y * q.x};
}

// Draws one triangle of a particle. taken marks the pixels the particle's
// other triangle has already drawn, so the shared diagonal is drawn once.
void drawTriangle(std::vector<float>& canvas, int width, int height, const SpriteVertex& v0,
                  const SpriteVertex& v1, const SpriteVertex& v2, SpriteShape shape,
                  std::vector<std::uint32_t>& taken, std::uint32_t stamp) {
    const float area = edgeThrough(v0, v1).at(v2.x, v2.y);
    if (!(std::fabs(area) > 1e-9f)) {
        return;
    }
    const Edge e0 = edgeThrough(v1, v2), e1 = edgeThrough(v2, v0), e2 = edgeThrough(v0, v1);

    const float minX = std::min({v0.x, v1.x, v2.x}), maxX = std::max({v0.x, v1.x, v2.x});
    const float minY = std::min({v0.y, v1.y, v2.y}), maxY = std::max({v0.y, v1.y, v2.y});
    const int x0 = std::max(0, static_cast<int>(std::floor(minX)));
    const int x1 = std::min(width - 1, static_cast<int>(std::ceil(maxX)));
    const int y0 = std::max(0, static_cast<int>(std::floor(minY)));
    const int y1 = std::min(height - 1, static_cast<int>(std::ceil(maxY)));

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const float cx = static_cast<float>(x) + 0.5f, cy = static_cast<float>(y) + 0.5f;
            float w0 = e0.at(cx, cy) / area, w1 = e1.at(cx, cy) / area, w2 = e2.at(cx, cy) / area;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                continue;
            }
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            if (taken[index] == stamp) {
                continue;
            }
            taken[index] = stamp;

            const float u = w0 * v0.u + w1 * v1.u + w2 * v2.u;
            const float v = w0 * v0.v + w1 * v1.v + w2 * v2.v;
            const ShapeSample sample =
                sampleShape(shape, 2.0f * u - 1.0f, 1.0f - 2.0f * v, v0.aaX, v0.aaY);
            const float cover = sample.cover;
            if (!(cover > 0.0f)) {
                continue;
            }
            float r = v0.r, g = v0.g, b = v0.b;
            applyTone(r, g, b, sample.tone);
            // The one blend rule: source + destination * (1 - source alpha).
            float* d = &canvas[index * 4u];
            const float keep = 1.0f - v0.a * cover;
            d[0] = r * cover + d[0] * keep;
            d[1] = g * cover + d[1] * keep;
            d[2] = b * cover + d[2] * keep;
        }
    }
}

// A fixed pattern of values from -0.5 to 0.5, different at every pixel.
// Nudging each pixel by less than one step before rounding hides the bands
// that a faint, wide glow would otherwise show.
float ditherAt(int x, int y) {
    const float n = 0.06711056f * static_cast<float>(x) + 0.00583715f * static_cast<float>(y);
    const float m = 52.9829189f * (n - std::floor(n));
    return m - std::floor(m) - 0.5f;
}

std::uint8_t toByte(float v, float nudge) {
    v = v * 255.0f + nudge;
    v = v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v);
    return static_cast<std::uint8_t>(std::lround(v));
}

// ------------------------------------------------------------------- PNG

std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0) {
    static std::uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            }
            table[n] = c;
        }
        ready = true;
    }
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
    }
    return ~crc;
}

void put32(std::string& out, std::uint32_t v) {
    out.push_back(static_cast<char>(v >> 24));
    out.push_back(static_cast<char>(v >> 16));
    out.push_back(static_cast<char>(v >> 8));
    out.push_back(static_cast<char>(v));
}

void chunk(std::string& out, const char type[4], const std::string& body) {
    put32(out, static_cast<std::uint32_t>(body.size()));
    std::string typed(type, 4);
    typed += body;
    out += typed;
    put32(out, crc32(reinterpret_cast<const std::uint8_t*>(typed.data()), typed.size()));
}

}  // namespace

Picture drawPicture(const SpriteMesh& mesh, int width, int height, ScreenColor background) {
    Picture picture;
    picture.width = width > 0 ? width : 0;
    picture.height = height > 0 ? height : 0;
    const std::size_t pixels =
        static_cast<std::size_t>(picture.width) * static_cast<std::size_t>(picture.height);
    picture.rgba.resize(pixels * 4u);
    if (pixels == 0) {
        return picture;
    }

    std::vector<float> canvas(pixels * 4u);
    for (std::size_t i = 0; i < pixels; ++i) {
        canvas[i * 4u] = background.r;
        canvas[i * 4u + 1] = background.g;
        canvas[i * 4u + 2] = background.b;
    }

    std::vector<std::uint32_t> taken(pixels, 0u);
    std::uint32_t stamp = 0;
    for (std::size_t i = 0; i + 5 < mesh.indices.size(); i += 6) {
        ++stamp;
        for (std::size_t t = 0; t < 2; ++t) {
            const SpriteVertex& v0 = mesh.vertices[mesh.indices[i + t * 3]];
            const SpriteVertex& v1 = mesh.vertices[mesh.indices[i + t * 3 + 1]];
            const SpriteVertex& v2 = mesh.vertices[mesh.indices[i + t * 3 + 2]];
            const int number = static_cast<int>(std::lround(v0.shape));
            const auto shape = static_cast<SpriteShape>(
                number >= 0 && number < kSpriteShapeCount ? number : 0);
            drawTriangle(canvas, picture.width, picture.height, v0, v1, v2, shape, taken, stamp);
        }
    }

    for (std::size_t i = 0; i < pixels; ++i) {
        const float nudge = ditherAt(static_cast<int>(i % static_cast<std::size_t>(picture.width)),
                                     static_cast<int>(i / static_cast<std::size_t>(picture.width)));
        picture.rgba[i * 4u] = toByte(canvas[i * 4u], nudge);
        picture.rgba[i * 4u + 1] = toByte(canvas[i * 4u + 1], nudge);
        picture.rgba[i * 4u + 2] = toByte(canvas[i * 4u + 2], nudge);
        picture.rgba[i * 4u + 3] = 255;
    }
    return picture;
}

void paste(Picture& onto, const Picture& piece, int left, int top) {
    for (int y = 0; y < piece.height; ++y) {
        const int oy = top + y;
        if (oy < 0 || oy >= onto.height) {
            continue;
        }
        for (int x = 0; x < piece.width; ++x) {
            const int ox = left + x;
            if (ox < 0 || ox >= onto.width) {
                continue;
            }
            const std::uint8_t* from = piece.pixel(x, y);
            std::uint8_t* to = &onto.rgba[(static_cast<std::size_t>(oy) * static_cast<std::size_t>(onto.width) +
                                           static_cast<std::size_t>(ox)) * 4u];
            std::copy(from, from + 4, to);
        }
    }
}

std::string encodePng(const Picture& picture) {
    std::string out("\x89PNG\r\n\x1a\n", 8);

    std::string header;
    put32(header, static_cast<std::uint32_t>(picture.width));
    put32(header, static_cast<std::uint32_t>(picture.height));
    header += std::string("\x08\x06\x00\x00\x00", 5);  // 8 bits, RGBA, no interlace
    chunk(out, "IHDR", header);

    // The image data: each row starts with a zero ("no filter") byte.
    std::string raw;
    const std::size_t rowBytes = static_cast<std::size_t>(picture.width) * 4u;
    raw.reserve((rowBytes + 1u) * static_cast<std::size_t>(picture.height));
    for (int y = 0; y < picture.height; ++y) {
        raw.push_back('\0');
        raw.append(reinterpret_cast<const char*>(picture.rgba.data()) + static_cast<std::size_t>(y) * rowBytes,
                   rowBytes);
    }

    // Wrapped as "stored" deflate blocks: correct, simply not compressed.
    std::string data("\x78\x01", 2);
    std::size_t at = 0;
    do {
        const std::size_t n = std::min<std::size_t>(65535u, raw.size() - at);
        const bool last = at + n >= raw.size();
        data.push_back(last ? '\x01' : '\x00');
        data.push_back(static_cast<char>(n & 0xffu));
        data.push_back(static_cast<char>(n >> 8));
        data.push_back(static_cast<char>(~n & 0xffu));
        data.push_back(static_cast<char>((~n >> 8) & 0xffu));
        data.append(raw, at, n);
        at += n;
    } while (at < raw.size());
    std::uint32_t a = 1, b = 0;  // Adler-32 of the raw data
    for (const char c : raw) {
        a = (a + static_cast<std::uint8_t>(c)) % 65521u;
        b = (b + a) % 65521u;
    }
    put32(data, (b << 16) | a);
    chunk(out, "IDAT", data);
    chunk(out, "IEND", std::string());
    return out;
}

Status writePng(const std::filesystem::path& path, const Picture& picture) {
    if (picture.width <= 0 || picture.height <= 0) {
        return makeError("There is no picture to save.");
    }
    return writeFileAtomic(path, encodePng(picture));
}

}  // namespace vfx::editor
