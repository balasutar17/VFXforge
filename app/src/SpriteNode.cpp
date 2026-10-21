#include "SpriteNode.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <set>

#include <QByteArray>
#include <QImage>
#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGRendererInterface>
#include <QSGTexture>

#include "vfx/Program.h"
#include "vfx/editor/Image.h"
#include "vfx/editor/ShapeLibrary.h"

using vfx::editor::SpriteVertex;

namespace {

// What every node of one item shares: its size, and the model's distances.
struct SpriteState {
    QSizeF size;
    SpriteDepth depth;
    QSGTexture* blank = nullptr;  // bound when there is no model, as every sampler must be
};

bool writeUniforms(QSGMaterialShader::RenderState& state, const SpriteState* shared) {
    // Matches the uniform block in the shaders: a 4x4 matrix, opacity, then
    // modelOn, perspective, fade, itemSize (two) and depthRange (two).
    QByteArray* buffer = state.uniformData();
    if (buffer->size() < 68) {
        return false;
    }
    bool changed = false;
    if (state.isMatrixDirty()) {
        const QMatrix4x4 matrix = state.combinedMatrix();
        std::memcpy(buffer->data(), matrix.constData(), 64);
        changed = true;
    }
    if (state.isOpacityDirty()) {
        const float opacity = state.opacity();
        std::memcpy(buffer->data() + 64, &opacity, 4);
        changed = true;
    }
    if (buffer->size() >= 96 && shared) {
        const bool on = shared->depth.texture != nullptr;
        const float values[7] = {on ? 1.0f : 0.0f,
                                 shared->depth.perspective ? 1.0f : 0.0f,
                                 shared->depth.fade,
                                 static_cast<float>(shared->size.width()),
                                 static_cast<float>(shared->size.height()),
                                 shared->depth.nearest,
                                 shared->depth.furthest};
        // modelOn, perspective, fade at 68..79; itemSize at 80; depthRange at 88.
        std::memcpy(buffer->data() + 68, values, 3 * 4);
        std::memcpy(buffer->data() + 80, values + 3, 4 * 4);
        changed = true;
    }
    return changed;
}

void bindDepth(QSGMaterialShader::RenderState& state, QSGTexture** texture, const SpriteState* shared) {
    QSGTexture* t = shared ? (shared->depth.texture ? shared->depth.texture : shared->blank) : nullptr;
    if (t) {
        t->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
    }
    *texture = t;
}

// How a layer's particles are laid over what is behind them, for the modes
// the graphics card can do from the particle and the pixel under it. A
// particle's colour arrives premultiplied (p) with its alpha (a):
//   Normal and Linear Dodge (Add)  p + pixel * (1 - a)   (Add has a = 0)
//   Screen                         p + pixel * (1 - p)
//   Multiply                       p * pixel + pixel * (1 - a)
//   Exclusion                      p * (1 - pixel) + pixel * (1 - p)
// Every other mode needs the pixel itself for its sums, which a card's
// blending cannot give, so the preview draws it as Normal (the picture
// renderer, and so exported frames, draw them all: see editor/Blend.h).
bool applyBlend(QSGMaterialShader::GraphicsPipelineState* ps, vfx::BlendMode mode) {
    using F = QSGMaterialShader::GraphicsPipelineState::BlendFactor;
    ps->blendEnable = true;
    switch (mode) {
        case vfx::BlendMode::Screen:
            ps->srcColor = F::One;
            ps->dstColor = F::OneMinusSrcColor;
            break;
        case vfx::BlendMode::Multiply:
            ps->srcColor = F::DstColor;
            ps->dstColor = F::OneMinusSrcAlpha;
            break;
        case vfx::BlendMode::Exclusion:
            ps->srcColor = F::OneMinusDstColor;
            ps->dstColor = F::OneMinusSrcColor;
            break;
        default:
            ps->srcColor = F::One;
            ps->dstColor = F::OneMinusSrcAlpha;
            break;
    }
    return true;
}

// The built-in shapes. The vertex colours arrive already premultiplied, so
// ordinary and additive particles share one draw call, and the shape is
// chosen per particle inside the shader.
class SpriteMaterial;

class SpriteShader : public QSGMaterialShader {
public:
    SpriteShader() {
        setShaderFileName(VertexStage, QStringLiteral(":/shaders/sprite.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/shaders/sprite.frag.qsb"));
    }
    bool updateUniformData(RenderState& state, QSGMaterial* material, QSGMaterial*) override;
    void updateSampledImage(RenderState& state, int binding, QSGTexture** texture, QSGMaterial* material,
                            QSGMaterial*) override;
    bool updateGraphicsPipelineState(RenderState& state, GraphicsPipelineState* ps, QSGMaterial* material,
                                     QSGMaterial*) override;
};

class SpriteMaterial : public QSGMaterial {
public:
    SpriteMaterial() { setFlag(Blending, true); }
    QSGMaterialType* type() const override {
        static QSGMaterialType type;
        return &type;
    }
    QSGMaterialShader* createShader(QSGRendererInterface::RenderMode) const override {
        return new SpriteShader;
    }
    int compare(const QSGMaterial* other) const override {
        const auto* o = static_cast<const SpriteMaterial*>(other);
        if (mode != o->mode) {
            return mode < o->mode ? -1 : 1;
        }
        return shared == o->shared ? 0 : (shared < o->shared ? -1 : 1);
    }
    const SpriteState* shared = nullptr;
    vfx::BlendMode mode = vfx::BlendMode::Alpha;
};

bool SpriteShader::updateGraphicsPipelineState(RenderState&, GraphicsPipelineState* ps, QSGMaterial* material,
                                               QSGMaterial*) {
    return applyBlend(ps, static_cast<SpriteMaterial*>(material)->mode);
}

bool SpriteShader::updateUniformData(RenderState& state, QSGMaterial* material, QSGMaterial*) {
    return writeUniforms(state, static_cast<SpriteMaterial*>(material)->shared);
}

void SpriteShader::updateSampledImage(RenderState& state, int binding, QSGTexture** texture,
                                      QSGMaterial* material, QSGMaterial*) {
    if (binding == 2) {
        bindDepth(state, texture, static_cast<SpriteMaterial*>(material)->shared);
    }
}

// One of the artist's pictures, or one shape from the shape library.
class ImageMaterial;

class ImageShader : public QSGMaterialShader {
public:
    ImageShader() {
        setShaderFileName(VertexStage, QStringLiteral(":/shaders/sprite.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/shaders/image.frag.qsb"));
    }
    bool updateUniformData(RenderState& state, QSGMaterial* material, QSGMaterial*) override;
    void updateSampledImage(RenderState& state, int binding, QSGTexture** texture,
                            QSGMaterial* material, QSGMaterial*) override;
    bool updateGraphicsPipelineState(RenderState& state, GraphicsPipelineState* ps, QSGMaterial* material,
                                     QSGMaterial*) override;
};

class ImageMaterial : public QSGMaterial {
public:
    ImageMaterial() { setFlag(Blending, true); }
    QSGMaterialType* type() const override {
        static QSGMaterialType type;
        return &type;
    }
    QSGMaterialShader* createShader(QSGRendererInterface::RenderMode) const override {
        return new ImageShader;
    }
    int compare(const QSGMaterial* other) const override {
        const auto* o = static_cast<const ImageMaterial*>(other);
        if (mode != o->mode) {
            return mode < o->mode ? -1 : 1;
        }
        if (texture != o->texture) {
            return texture < o->texture ? -1 : 1;
        }
        return shared == o->shared ? 0 : (shared < o->shared ? -1 : 1);
    }
    QSGTexture* texture = nullptr;
    const SpriteState* shared = nullptr;
    vfx::BlendMode mode = vfx::BlendMode::Alpha;
};

bool ImageShader::updateGraphicsPipelineState(RenderState&, GraphicsPipelineState* ps, QSGMaterial* material,
                                              QSGMaterial*) {
    return applyBlend(ps, static_cast<ImageMaterial*>(material)->mode);
}

bool ImageShader::updateUniformData(RenderState& state, QSGMaterial* material, QSGMaterial*) {
    return writeUniforms(state, static_cast<ImageMaterial*>(material)->shared);
}

void ImageShader::updateSampledImage(RenderState& state, int binding, QSGTexture** texture,
                                     QSGMaterial* material, QSGMaterial*) {
    if (binding == 2) {
        bindDepth(state, texture, static_cast<ImageMaterial*>(material)->shared);
        return;
    }
    if (binding != 1) {
        return;
    }
    auto* m = static_cast<ImageMaterial*>(material);
    if (m->texture) {
        m->texture->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
    }
    *texture = m->texture;
}

const QSGGeometry::AttributeSet& spriteAttributes() {
    static const QSGGeometry::Attribute attributes[] = {
        QSGGeometry::Attribute::createWithAttributeType(0, 2, QSGGeometry::FloatType,
                                                        QSGGeometry::PositionAttribute),
        QSGGeometry::Attribute::createWithAttributeType(1, 2, QSGGeometry::FloatType,
                                                        QSGGeometry::TexCoordAttribute),
        QSGGeometry::Attribute::createWithAttributeType(2, 4, QSGGeometry::FloatType,
                                                        QSGGeometry::ColorAttribute),
        QSGGeometry::Attribute::createWithAttributeType(3, 4, QSGGeometry::FloatType,
                                                        QSGGeometry::UnknownAttribute),
        // Own position on the particle, how far it is eaten away, its own
        // random number.
        QSGGeometry::Attribute::createWithAttributeType(4, 4, QSGGeometry::FloatType,
                                                        QSGGeometry::UnknownAttribute),
        // How soft a dissolving edge is, and how far away the corner is.
        QSGGeometry::Attribute::createWithAttributeType(5, 2, QSGGeometry::FloatType,
                                                        QSGGeometry::UnknownAttribute),
    };
    static const QSGGeometry::AttributeSet set = {6, static_cast<int>(sizeof(SpriteVertex)),
                                                  attributes};
    return set;
}

// The top node. It keeps the uploaded pictures, so a picture is sent to the
// graphics card once, not every frame.
class SpriteRoot : public QSGNode {
public:
    ~SpriteRoot() override {
        // The children's materials point at the textures: drop them first.
        while (QSGNode* child = firstChild()) {
            removeChildNode(child);
            delete child;
        }
    }

    struct Uploaded {
        std::shared_ptr<const vfx::editor::Image> image;
        std::unique_ptr<QSGTexture> texture;
    };
    std::map<std::uint64_t, Uploaded> textures;
    SpriteState shared;
    std::unique_ptr<QSGTexture> blank;
};

// How many library shapes' pictures a node keeps on the graphics card
// before it starts letting go of the ones not on screen.
constexpr std::size_t kShapeTexturesKept = 12;

QSGTexture* textureFor(SpriteRoot& root, vfx::Id id, const vfx::editor::ImageSet* images,
                       QQuickWindow* window, int shapePixels) {
    if (!window) {
        return nullptr;
    }
    // A shape from the library brings its own picture; anything else is one
    // of the artist's.
    vfx::ShapeEdit edit;  // how the shape's own design has been changed, if it has
    const int shape = vfx::editor::shapeFromTextureId(id, &edit);
    std::shared_ptr<const vfx::editor::Image> image =
        shape >= 0 ? vfx::editor::shapePicture(shape, shapePixels, edit) : images ? images->shared(id) : nullptr;
    if (!image || image->width <= 0 || image->height <= 0) {
        return nullptr;
    }
    auto& slot = root.textures[id.value];
    if (slot.image != image || !slot.texture) {
        QImage pixels(image->rgba.data(), image->width, image->height, image->width * 4,
                      QImage::Format_RGBA8888);
        // Premultiplied, so smoothing between pixels never darkens edges. (A
        // shape's picture is opaque everywhere, so this leaves it as it is.)
        const QImage ready = pixels.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        QQuickWindow::CreateTextureOptions options;
        options |= QQuickWindow::TextureHasAlphaChannel;
        options |= QQuickWindow::TextureHasMipmaps;
        QSGTexture* texture = window->createTextureFromImage(ready, options);
        if (!texture) {
            return nullptr;
        }
        texture->setFiltering(QSGTexture::Linear);
        texture->setMipmapFiltering(QSGTexture::Linear);
        texture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
        texture->setVerticalWrapMode(QSGTexture::ClampToEdge);
        slot.texture.reset(texture);
        slot.image = std::move(image);
    }
    return slot.texture.get();
}

QSGGeometryNode* makeRunNode() {
    auto* node = new QSGGeometryNode;
    auto* geometry = new QSGGeometry(spriteAttributes(), 0, 0, QSGGeometry::UnsignedIntType);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    geometry->setVertexDataPattern(QSGGeometry::StreamPattern);
    geometry->setIndexDataPattern(QSGGeometry::StreamPattern);
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry, true);
    node->setMaterial(new SpriteMaterial);
    node->setFlag(QSGNode::OwnsMaterial, true);
    return node;
}

}  // namespace

QSGNode* updateSpriteNode(QSGNode* old, const vfx::editor::SpriteMesh& mesh,
                          const vfx::editor::ImageSet* images, QQuickWindow* window, int shapePixels,
                          QSizeF itemSize, const SpriteDepth* depth) {
    auto* root = static_cast<SpriteRoot*>(old);
    if (!root) {
        root = new SpriteRoot;
    }
    // What every node of the item shares, for its shaders.
    if (!root->blank && window) {
        QImage nothing(1, 1, QImage::Format_RGBA8888);
        nothing.fill(Qt::transparent);
        root->blank.reset(window->createTextureFromImage(nothing));
    }
    root->shared.blank = root->blank.get();
    root->shared.size = itemSize;
    root->shared.depth = depth ? *depth : SpriteDepth{};

    // The artist's pictures that are gone, or have changed, are let go. A
    // library shape's picture never changes; those are kept from frame to
    // frame (a burst that is between two of its bursts will want its shape
    // again in a moment) and let go only once there are many, and then only
    // the ones nothing on screen is using.
    std::set<std::uint64_t> wanted;
    std::size_t shapesKept = 0;
    for (const auto& run : mesh.runs) {
        wanted.insert(run.texture.value);
    }
    for (const auto& kept : root->textures) {
        shapesKept += vfx::editor::shapeFromTextureId(vfx::Id{kept.first}) >= 0 ? 1 : 0;
    }
    bool letGo = false;
    for (auto it = root->textures.begin(); it != root->textures.end();) {
        bool keep = false;
        if (vfx::editor::shapeFromTextureId(vfx::Id{it->first}) >= 0) {
            keep = shapesKept <= kShapeTexturesKept || wanted.count(it->first) > 0;
        } else {
            keep = images && images->shared(vfx::Id{it->first}) == it->second.image;
        }
        if (keep) {
            ++it;
        } else {
            it = root->textures.erase(it);
            letGo = true;
        }
    }
    if (letGo) {
        // No material may go on pointing at a picture that has been let go:
        // each is given its picture afresh below.
        for (QSGNode* node = root->firstChild(); node; node = node->nextSibling()) {
            if (auto* image = dynamic_cast<ImageMaterial*>(static_cast<QSGGeometryNode*>(node)->material())) {
                image->texture = nullptr;
            }
        }
    }

    // The runs, drawn in order. A run with no picture, or whose picture could
    // not be uploaded, is drawn with the shapes.
    std::vector<vfx::editor::SpriteRun> runs = mesh.runs;
    if (runs.empty() && !mesh.indices.empty()) {
        runs.push_back(vfx::editor::SpriteRun{vfx::Id{}, 0,
                                              static_cast<std::uint32_t>(mesh.indices.size())});
    }

    // One child node per run, reused from frame to frame.
    int existing = root->childCount();
    while (existing > static_cast<int>(runs.size())) {
        QSGNode* last = root->lastChild();
        root->removeChildNode(last);
        delete last;
        --existing;
    }
    while (existing < static_cast<int>(runs.size())) {
        root->appendChildNode(makeRunNode());
        ++existing;
    }

    auto* child = static_cast<QSGGeometryNode*>(root->firstChild());
    for (const auto& run : runs) {
        QSGTexture* texture =
            run.texture.valid() ? textureFor(*root, run.texture, images, window, shapePixels) : nullptr;

        // Swap the material when the run changes between shape and picture.
        auto* image = dynamic_cast<ImageMaterial*>(child->material());
        if (texture && !image) {
            image = new ImageMaterial;
            child->setMaterial(image);
            child->markDirty(QSGNode::DirtyMaterial);
        } else if (!texture && image) {
            child->setMaterial(new SpriteMaterial);
            child->markDirty(QSGNode::DirtyMaterial);
            image = nullptr;
        }
        if (image && image->texture != texture) {
            image->texture = texture;
            child->markDirty(QSGNode::DirtyMaterial);
        }
        // Its shaders read the item's size and the model's distances, which
        // may change from frame to frame. The card lays the run down in the
        // layer's blend mode when it can do that by itself (see applyBlend).
        const vfx::BlendMode mode = vfx::blendByCard(run.blend) ? run.blend : vfx::BlendMode::Alpha;
        if (image) {
            image->shared = &root->shared;
            image->mode = mode;
        } else if (auto* shape = dynamic_cast<SpriteMaterial*>(child->material())) {
            shape->shared = &root->shared;
            shape->mode = mode;
        }
        child->markDirty(QSGNode::DirtyMaterial);

        // A run's particles use a block of the vertices that is all its own.
        const std::uint32_t firstVertex = run.firstIndex / 6 * 4;
        const std::uint32_t vertexCount = run.indexCount / 6 * 4;
        QSGGeometry* geometry = child->geometry();
        geometry->allocate(static_cast<int>(vertexCount), static_cast<int>(run.indexCount));
        auto* vertices = static_cast<SpriteVertex*>(geometry->vertexData());
        std::memcpy(vertices, mesh.vertices.data() + firstVertex, vertexCount * sizeof(SpriteVertex));
        if (!texture && run.texture.valid()) {
            // The picture is not there: show its quads as soft dots, never blank.
            for (std::uint32_t i = 0; i < vertexCount; ++i) {
                if (vertices[i].shape == vfx::editor::kPictureShape) {
                    vertices[i].shape = 0.0f;
                    vertices[i].u = (i % 4 == 1 || i % 4 == 2) ? 1.0f : 0.0f;
                    vertices[i].v = (i % 4 == 2 || i % 4 == 3) ? 0.0f : 1.0f;
                } else if (vertices[i].shape >= static_cast<float>(vfx::kSpriteShapeCount)) {
                    vertices[i].shape = 0.0f;  // a library shape: its corners are already laid out right
                }
                // (One of the first thirty-five that has been changed falls
                // back to itself as drawn: the shape shader knows it.)
            }
        }
        auto* indices = static_cast<std::uint32_t*>(geometry->indexData());
        for (std::uint32_t i = 0; i < run.indexCount; ++i) {
            indices[i] = mesh.indices[run.firstIndex + i] - firstVertex;
        }
        child->markDirty(QSGNode::DirtyGeometry);
        child = static_cast<QSGGeometryNode*>(child->nextSibling());
    }
    return root;
}
