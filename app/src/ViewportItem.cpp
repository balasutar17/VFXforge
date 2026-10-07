#include "ViewportItem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <QByteArray>
#include <QMatrix4x4>
#include <QMetaObject>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGRendererInterface>

using vfx::editor::SpriteVertex;

namespace {

// ---------------------------------------------------------------- shader
//
// One material draws every particle. The vertex colours arrive already
// premultiplied, so ordinary and additive particles share one draw call.

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
    };
    static const QSGGeometry::AttributeSet set = {3, static_cast<int>(sizeof(SpriteVertex)),
                                                  attributes};
    return set;
}

}  // namespace

// ------------------------------------------------------------------ item

ViewportItem::ViewportItem(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
}

void ViewportItem::setController(AppController* controller) {
    if (controller == controller_) {
        return;
    }
    if (controller_) {
        disconnect(controller_, nullptr, this, nullptr);
    }
    controller_ = controller;
    if (controller_) {
        connect(controller_, &AppController::redraw, this, &QQuickItem::update);
        // The origin marker depends on whether the effect is 2D or 3D.
        connect(controller_, &AppController::documentChanged, this, &ViewportItem::viewChanged);
    }
    emit controllerChanged();
    emit viewChanged();
    update();
}

bool ViewportItem::flat() const { return !controller_ || !controller_->threeD(); }

vfx::editor::View ViewportItem::currentView() const {
    vfx::editor::View view = view_;
    view.width = static_cast<float>(width());
    view.height = static_cast<float>(height());
    return view;
}

QPointF ViewportItem::origin() const {
    float x = 0, y = 0, scale = 0;
    if (!vfx::editor::projectPoint(currentView(), flat(), 0, 0, 0, x, y, scale)) {
        return QPointF(-1000, -1000);
    }
    return QPointF(static_cast<double>(x), static_cast<double>(y));
}

bool ViewportItem::originVisible() const {
    float x = 0, y = 0, scale = 0;
    return vfx::editor::projectPoint(currentView(), flat(), 0, 0, 0, x, y, scale) && x >= 0 &&
           y >= 0 && static_cast<double>(x) <= width() && static_cast<double>(y) <= height();
}

void ViewportItem::dragBy(double dx, double dy, bool secondary) {
    if (height() <= 0) {
        return;
    }
    if (flat()) {
        // Pan: the world follows the pointer.
        const double unitsPerPixel = static_cast<double>(view_.unitsHigh) / height();
        view_.centerX -= static_cast<float>(dx * unitsPerPixel);
        view_.centerY += static_cast<float>(dy * unitsPerPixel);
    } else if (secondary) {
        // Pan the orbit target up and down and sideways across the screen.
        const double unitsPerPixel = static_cast<double>(view_.distance) / height();
        const double yaw = static_cast<double>(view_.yaw) * 3.14159265358979323846 / 180.0;
        view_.targetX -= static_cast<float>(dx * unitsPerPixel * std::cos(yaw));
        view_.targetZ += static_cast<float>(dx * unitsPerPixel * std::sin(yaw));
        view_.targetY += static_cast<float>(dy * unitsPerPixel);
    } else {
        // Orbit.
        view_.yaw = static_cast<float>(std::fmod(static_cast<double>(view_.yaw) - dx * 0.4, 360.0));
        view_.pitch = static_cast<float>(
            std::clamp(static_cast<double>(view_.pitch) + dy * 0.4, -85.0, 85.0));
    }
    emit viewChanged();
    update();
}

void ViewportItem::zoomBy(double steps, double atX, double atY) {
    if (!std::isfinite(steps) || height() <= 0 || width() <= 0) {
        return;
    }
    const double factor = std::pow(0.88, std::clamp(steps, -10.0, 10.0));
    if (flat()) {
        // Keep the world point under the pointer where it is.
        const double before = static_cast<double>(view_.unitsHigh) / height();
        const double unitsHigh = std::clamp(static_cast<double>(view_.unitsHigh) * factor, 0.2, 2000.0);
        const double after = unitsHigh / height();
        const double offX = atX - width() * 0.5;
        const double offY = atY - height() * 0.5;
        view_.centerX += static_cast<float>(offX * (before - after));
        view_.centerY -= static_cast<float>(offY * (before - after));
        view_.unitsHigh = static_cast<float>(unitsHigh);
    } else {
        view_.distance =
            static_cast<float>(std::clamp(static_cast<double>(view_.distance) * factor, 0.5, 2000.0));
    }
    emit viewChanged();
    update();
}

void ViewportItem::resetView() {
    view_ = vfx::editor::View{};
    emit viewChanged();
    update();
}

void ViewportItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        emit viewChanged();
        update();
    }
}

QSGNode* ViewportItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    // Runs on the render thread while the main thread is held still, so the
    // session's frame can be read here safely.
    auto* node = static_cast<QSGGeometryNode*>(old);

    mesh_.vertices.clear();
    mesh_.indices.clear();
    mesh_.drawn = 0;
    if (controller_ && width() > 0 && height() > 0) {
        vfx::editor::buildSpriteMesh(controller_->session().frame(), currentView(), mesh_);
    }

    const int drawn = static_cast<int>(mesh_.drawn);
    if (drawn != drawnCount_) {
        drawnCount_ = drawn;
        QMetaObject::invokeMethod(this, &ViewportItem::drawnCountChanged, Qt::QueuedConnection);
    }

    if (mesh_.vertices.empty()) {
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
    geometry->allocate(static_cast<int>(mesh_.vertices.size()),
                       static_cast<int>(mesh_.indices.size()));
    std::memcpy(geometry->vertexData(), mesh_.vertices.data(),
                mesh_.vertices.size() * sizeof(SpriteVertex));
    std::memcpy(geometry->indexData(), mesh_.indices.data(),
                mesh_.indices.size() * sizeof(std::uint32_t));
    node->markDirty(QSGNode::DirtyGeometry);
    return node;
}
