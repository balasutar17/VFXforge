#include "SpriteNode.h"

#include <cstdint>
#include <cstring>

#include <QByteArray>
#include <QMatrix4x4>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGRendererInterface>

using vfx::editor::SpriteVertex;

namespace {

// One material draws every particle. The vertex colours arrive already
// premultiplied, so ordinary and additive particles share one draw call,
// and the shape is chosen per particle inside the shader.
class SpriteShader : public QSGMaterialShader {
public:
    SpriteShader() {
        setShaderFileName(VertexStage, QStringLiteral(":/shaders/sprite.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/shaders/sprite.frag.qsb"));
    }

    bool updateUniformData(RenderState& state, QSGMaterial*, QSGMaterial*) override {
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

}  // namespace

QSGNode* updateSpriteNode(QSGNode* old, const vfx::editor::SpriteMesh& mesh) {
    auto* node = static_cast<QSGGeometryNode*>(old);
    if (mesh.vertices.empty()) {
        delete node;
        return nullptr;
    }

    if (!node) {
        node = new QSGGeometryNode;
        auto* geometry = new QSGGeometry(spriteAttributes(), 0, 0, QSGGeometry::UnsignedIntType);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry->setVertexDataPattern(QSGGeometry::StreamPattern);
        geometry->setIndexDataPattern(QSGGeometry::StreamPattern);
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry, true);
        node->setMaterial(new SpriteMaterial);
        node->setFlag(QSGNode::OwnsMaterial, true);
    }

    QSGGeometry* geometry = node->geometry();
    geometry->allocate(static_cast<int>(mesh.vertices.size()), static_cast<int>(mesh.indices.size()));
    std::memcpy(geometry->vertexData(), mesh.vertices.data(),
                mesh.vertices.size() * sizeof(SpriteVertex));
    std::memcpy(geometry->indexData(), mesh.indices.data(),
                mesh.indices.size() * sizeof(std::uint32_t));
    node->markDirty(QSGNode::DirtyGeometry);
    return node;
}
