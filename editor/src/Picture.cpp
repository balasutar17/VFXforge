#include "vfx/editor/Picture.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "vfx/FileIO.h"
#include "vfx/editor/Archive.h"
#include "vfx/editor/Blend.h"
#include "vfx/editor/Image.h"
#include "vfx/editor/Model.h"
#include "vfx/editor/ShapeLibrary.h"
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
                  std::vector<std::uint32_t>& taken, std::uint32_t stamp, bool keepAlpha,
                  const MipChain* picture, const ShapeEdit& edit, const ModelPicture* model, BlendMode blend) {
    const float area = edgeThrough(v0, v1).at(v2.x, v2.y);
    if (!(std::fabs(area) > 1e-9f)) {
        return;
    }
    const Edge e0 = edgeThrough(v1, v2), e1 = edgeThrough(v2, v0), e2 = edgeThrough(v0, v1);

    // How many of the picture's pixels one screen pixel covers.
    float footprint = 1.0f;
    if (picture && !picture->levels.empty()) {
        const auto w = static_cast<float>(picture->levels[0].width);
        const auto h = static_cast<float>(picture->levels[0].height);
        const float dudx = (e0.a * v0.u + e1.a * v1.u + e2.a * v2.u) / area * w;
        const float dvdx = (e0.a * v0.v + e1.a * v1.v + e2.a * v2.v) / area * h;
        const float dudy = (e0.b * v0.u + e1.b * v1.u + e2.b * v2.u) / area * w;
        const float dvdy = (e0.b * v0.v + e1.b * v1.v + e2.b * v2.v) / area * h;
        footprint = std::max(std::sqrt(dudx * dudx + dvdx * dvdx),
                             std::sqrt(dudy * dudy + dvdy * dvdy));
    }

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
            // Colour is smoothed across the triangle (it changes along a trail).
            const float vr = w0 * v0.r + w1 * v1.r + w2 * v2.r;
            const float vg = w0 * v0.g + w1 * v1.g + w2 * v2.g;
            const float vb = w0 * v0.b + w1 * v1.b + w2 * v2.b;
            const float va = w0 * v0.a + w1 * v1.a + w2 * v2.a;
            // Being eaten away: what is left of this point, and the glow
            // along the eaten edge.
            Dissolved eaten;
            if (v0.dissolve > 0.0f) {
                eaten = dissolveAt(w0 * v0.ownX + w1 * v1.ownX + w2 * v2.ownX,
                                   w0 * v0.ownY + w1 * v1.ownY + w2 * v2.ownY, v0.dissolve, v0.seed, v0.soft, v0.edge);
                if (!(eaten.keep > 0.0f)) {
                    continue;
                }
            }
            if (model) {
                // Behind the model, pixel by pixel.
                const float depth = depthFromKey(w0 * v0.depth + w1 * v1.depth + w2 * v2.depth, model->flat);
                eaten.keep *= model->seen(cx, cy, depth, static_cast<float>(width), static_cast<float>(height));
                if (!(eaten.keep > 0.0f)) {
                    continue;
                }
            }
            if (picture) {
                // The painted picture, premultiplied, tinted by the particle.
                float t[4];
                sampleMips(*picture, u, v, footprint, t);
                if (!(t[3] > 0.0f) && !(t[0] + t[1] + t[2] > 0.0f)) {
                    continue;
                }
                float r = vr * t[0], g = vg * t[1], b = vb * t[2];
                const float pa = va * t[3] * eaten.keep;
                applyShine(r, g, b, va * t[3], eaten.rim);
                r *= eaten.keep;
                g *= eaten.keep;
                b *= eaten.keep;
                float* d = &canvas[index * 4u];
                blendOver(blend, r, g, b, pa, d, blendNoise(x, y));
                if (keepAlpha) {
                    const float own = va > 0.0f ? pa
                                                   : std::min(1.0f, std::max({r, g, b}));
                    d[3] = own + d[3] * (1.0f - own);
                }
                continue;
            }
            const ShapeSample sample =
                v0.shape < -1.5f ? ribbonSample(u, v)
                                 : sampleShapeEdited(static_cast<int>(shape), edit, 2.0f * u - 1.0f, 1.0f - 2.0f * v, v0.aaX,
                                                     v0.aaY);
            const float cover = sample.cover * eaten.keep;
            if (!(cover > 0.0f)) {
                continue;
            }
            float r = vr, g = vg, b = vb;
            applyTone(r, g, b, sample.tone);
            applyShine(r, g, b, va, std::max(sample.shine, eaten.rim));
            // Normal's rule is source + destination * (1 - source alpha); the
            // other modes are in Blend.h.
            float* d = &canvas[index * 4u];
            blendOver(blend, r * cover, g * cover, b * cover, va * cover, d, blendNoise(x, y));
            if (keepAlpha) {
                // Coverage, for pictures with a see-through background. Light
                // that is added (alpha 0) still has to show when the picture
                // is laid over something, so it counts by its brightness.
                const float own = va > 0.0f ? va * cover
                                               : std::min(1.0f, std::max({r, g, b})) * cover;
                d[3] = own + d[3] * (1.0f - own);
            }
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

}  // namespace

namespace {

Picture render(const SpriteMesh& mesh, int width, int height, ScreenColor background, bool transparent,
               const ImageSet* images, const Picture* under = nullptr, const ModelPicture* hideBehind = nullptr) {
    Picture out;
    out.width = width > 0 ? width : 0;
    out.height = height > 0 ? height : 0;
    const std::size_t pixels =
        static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height);
    out.rgba.resize(pixels * 4u);
    if (pixels == 0) {
        return out;
    }

    std::vector<float> canvas(pixels * 4u, 0.0f);
    if (!transparent) {
        for (std::size_t i = 0; i < pixels; ++i) {
            canvas[i * 4u] = background.r;
            canvas[i * 4u + 1] = background.g;
            canvas[i * 4u + 2] = background.b;
        }
        if (under && under->width == out.width && under->height == out.height) {
            // The picture underneath, laid over the background.
            for (std::size_t i = 0; i < pixels; ++i) {
                const float a = static_cast<float>(under->rgba[i * 4u + 3]) / 255.0f;
                for (std::size_t c = 0; c < 3; ++c) {
                    canvas[i * 4u + c] += (static_cast<float>(under->rgba[i * 4u + c]) / 255.0f - canvas[i * 4u + c]) * a;
                }
            }
        }
    }

    std::vector<std::uint32_t> taken(pixels, 0u);
    std::uint32_t stamp = 0;
    // Which picture each particle uses, from the runs.
    std::size_t run = 0;
    for (std::size_t i = 0; i + 5 < mesh.indices.size(); i += 6) {
        ++stamp;
        while (run < mesh.runs.size() &&
               i >= static_cast<std::size_t>(mesh.runs[run].firstIndex) + mesh.runs[run].indexCount) {
            ++run;
        }
        const MipChain* picture = nullptr;
        if (run < mesh.runs.size() && mesh.runs[run].texture.valid() && images &&
            mesh.vertices[mesh.indices[i]].shape == kPictureShape) {
            picture = images->mips(mesh.runs[run].texture);
        }
        // A shape that has been changed says how in the ID of its run.
        ShapeEdit edit;
        if (run < mesh.runs.size() && mesh.runs[run].texture.valid()) {
            shapeFromTextureId(mesh.runs[run].texture, &edit);
        }
        const BlendMode blend = run < mesh.runs.size() ? mesh.runs[run].blend : BlendMode::Alpha;
        for (std::size_t t = 0; t < 2; ++t) {
            const SpriteVertex& v0 = mesh.vertices[mesh.indices[i + t * 3]];
            const SpriteVertex& v1 = mesh.vertices[mesh.indices[i + t * 3 + 1]];
            const SpriteVertex& v2 = mesh.vertices[mesh.indices[i + t * 3 + 2]];
            const int number = static_cast<int>(std::lround(v0.shape));
            const auto shape = static_cast<SpriteShape>(number >= 0 && number < shapeCount() ? number : 0);
            drawTriangle(canvas, out.width, out.height, v0, v1, v2, shape, taken, stamp,
                         transparent, picture, edit, hideBehind, blend);
        }
    }

    for (std::size_t i = 0; i < pixels; ++i) {
        const float nudge = ditherAt(static_cast<int>(i % static_cast<std::size_t>(out.width)),
                                     static_cast<int>(i / static_cast<std::size_t>(out.width)));
        float r = canvas[i * 4u], g = canvas[i * 4u + 1], b = canvas[i * 4u + 2];
        float a = 1.0f;
        if (transparent) {
            // Stored colours are premultiplied; PNG wants them straight.
            a = std::min(canvas[i * 4u + 3], 1.0f);
            if (a > 1e-4f) {
                r /= a;
                g /= a;
                b /= a;
            } else {
                r = g = b = a = 0.0f;
            }
        }
        out.rgba[i * 4u] = toByte(r, nudge);
        out.rgba[i * 4u + 1] = toByte(g, nudge);
        out.rgba[i * 4u + 2] = toByte(b, nudge);
        out.rgba[i * 4u + 3] = transparent ? toByte(a, a > 0.0f && a < 1.0f ? nudge : 0.0f) : 255;
    }
    return out;
}

}  // namespace

Picture drawPicture(const SpriteMesh& mesh, int width, int height, ScreenColor background,
                    const ImageSet* images) {
    return render(mesh, width, height, background, false, images);
}

Picture drawPictureOver(const SpriteMesh& mesh, const ModelPicture& model, bool hideBehind, int width, int height,
                        ScreenColor background, const ImageSet* images) {
    // The model's picture may be sharper than the result: laid over it at
    // the result's size.
    Picture under;
    const Picture* use = &model.picture;
    if (model.picture.width != width || model.picture.height != height) {
        under.width = width;
        under.height = height;
        under.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0);
        if (!model.empty()) {
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const int sx = x * model.picture.width / width, sy = y * model.picture.height / height;
                    std::memcpy(&under.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                             static_cast<std::size_t>(x)) * 4u],
                                model.picture.pixel(sx, sy), 4);
                }
            }
        }
        use = &under;
    }
    return render(mesh, width, height, background, false, images, use,
                  hideBehind && !model.empty() ? &model : nullptr);
}

Picture drawPictureClear(const SpriteMesh& mesh, int width, int height, const ImageSet* images) {
    return render(mesh, width, height, ScreenColor{0, 0, 0}, true, images);
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
    return encodePngImage(picture.width, picture.height, 4,
                          std::string_view(reinterpret_cast<const char*>(picture.rgba.data()),
                                           picture.rgba.size()));
}

Status writePng(const std::filesystem::path& path, const Picture& picture) {
    if (picture.width <= 0 || picture.height <= 0) {
        return makeError("There is no picture to save.");
    }
    return writeFileAtomic(path, encodePng(picture));
}

}  // namespace vfx::editor
