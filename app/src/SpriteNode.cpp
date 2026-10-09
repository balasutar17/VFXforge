#include "SpriteNode.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>

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

#include "vfx/editor/Image.h"

using vfx::editor::SpriteVertex;

namespace {

bool writeMatrixAndOpacity(QSGMaterialShader::RenderState& state) {
    // Matches the uniform block in the shaders: a 4x4 matrix, then opacity.
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
    return changed;
}

// The built-in shapes. The vertex colours arrive already premultiplied, so
// ordinary and additive particles share one draw call, and the shape is
// chosen per particle inside the shader.
class SpriteShader : public QSGMaterialShader {
public:
    SpriteShader() {
        setShaderFileName(VertexStage, QStringLiteral(":/shaders/sprite.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/shaders/sprite.frag.qsb"));
    }
    bool updateUniformData(RenderState& state, QSGMaterial*, QSGMaterial*) override {
        return writeMatrixAndOpacity(state);
    }
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
    int compare(const QSGMaterial*) const override { return 0; }
};

// One of the artist's pictures.
class ImageMaterial;

class ImageShader : public QSGMaterialShader {
public:
    ImageShader() {
        setShaderFileName(VertexStage, QStringLiteral(":/shaders/sprite.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/shaders/image.frag.qsb"));
    }
    bool updateUniformData(RenderState& state, QSGMaterial*, QSGMaterial*) override {
        return writeMatrixAndOpacity(state);
    }
    void updateSampledImage(RenderState& state, int binding, QSGTexture** texture,
                            QSGMaterial* material, QSGMaterial*) override;
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
        return texture == o->texture ? 0 : (texture < o->texture ? -1 : 1);
    }
    QSGTexture* texture = nullptr;
};

void ImageShader::updateSampledImage(RenderState& state, int binding, QSGTexture** texture,
                                     QSGMaterial* material, QSGMaterial*) {
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
        QSGGeometry::Attribute::createWithAttributeType(3, 3, QSGGeometry::FloatType,
                                                        QSGGeometry::UnknownAttribute),
    };
    static const QSGGeometry::AttributeSet set = {4, static_cast<int>(sizeof(SpriteVertex)),
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
};

QSGTexture* textureFor(SpriteRoot& root, vfx::Id id, const vfx::editor::ImageSet* images,
                       QQuickWindow* window) {
    if (!images || !window) {
        return nullptr;
    }
    std::shared_ptr<const vfx::editor::Image> image = images->shared(id);
    if (!image || image->width <= 0 || image->height <= 0) {
        return nullptr;
    }
    auto& slot = root.textures[id.value];
    if (slot.image != image || !slot.texture) {
        QImage pixels(image->rgba.data(), image->width, image->height, image->width * 4,
                      QImage::Format_RGBA8888);
        // Premultiplied, so smoothing between pixels never darkens edges.
        const QImage ready = pixels.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        QSGTexture* texture = window->createTextureFromImage(
            ready, QQuickWindow::TextureHasAlphaChannel | QQuickWindow::TextureHasMipmaps);
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
                          const vfx::editor::ImageSet* images, QQuickWindow* window) {
    auto* root = static_cast<SpriteRoot*>(old);
    if (!root) {
        root = new SpriteRoot;
    }

    // Pictures that are gone, or have changed, are let go.
    for (auto it = root->textures.begin(); it != root->textures.end();) {
        const auto now = images ? images->shared(vfx::Id{it->first}) : nullptr;
        it = now == it->second.image ? std::next(it) : root->textures.erase(it);
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
            run.texture.valid() ? textureFor(*root, run.texture, images, window) : nullptr;

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
                if (vertices[i].shape < 0.0f) {
                    vertices[i].shape = 0.0f;
                    vertices[i].u = (i % 4 == 1 || i % 4 == 2) ? 1.0f : 0.0f;
                    vertices[i].v = (i % 4 == 2 || i % 4 == 3) ? 0.0f : 1.0f;
                }
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
