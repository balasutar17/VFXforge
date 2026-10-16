#include "ViewportItem.h"

#include <algorithm>
#include <cmath>

#include <QMetaObject>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <limits>
#include <memory>

#include "SpriteNode.h"

// ------------------------------------------------------------------ item

ViewportItem::ViewportItem(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    settle_ = new QTimer(this);
    settle_->setSingleShot(true);
    settle_->setInterval(180);
    connect(settle_, &QTimer::timeout, this, [this]() {
        moving_ = false;
        update();
    });
}

void ViewportItem::moved() {
    moving_ = true;
    settle_->start();
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
        connect(controller_, &AppController::modelChanged, this, &QQuickItem::update);
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

QVariantMap ViewportItem::project(double x, double y, double z) const {
    float px = 0, py = 0, scale = 0;
    const bool ok = vfx::editor::projectPoint(currentView(), flat(), static_cast<float>(x), static_cast<float>(y),
                                              static_cast<float>(z), px, py, scale);
    QVariantMap out;
    out.insert(QStringLiteral("x"), static_cast<double>(px));
    out.insert(QStringLiteral("y"), static_cast<double>(py));
    out.insert(QStringLiteral("scale"), static_cast<double>(scale));
    out.insert(QStringLiteral("ok"), ok);
    return out;
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
    moved();
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
    moved();
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

void ViewportItem::frame(double x, double y, double unitsHigh) {
    view_ = vfx::editor::View{};
    if (std::isfinite(x) && std::isfinite(y) && std::isfinite(unitsHigh) && unitsHigh > 0.01) {
        view_.centerX = static_cast<float>(x);
        view_.centerY = static_cast<float>(y);
        view_.unitsHigh = static_cast<float>(unitsHigh);
        view_.targetX = static_cast<float>(x);
        view_.targetY = static_cast<float>(y);
        view_.distance = static_cast<float>(unitsHigh * 1.45);
    }
    emit viewChanged();
    update();
}

QVariantMap ViewportItem::viewState() const {
    QVariantMap out;
    out.insert(QStringLiteral("centerX"), static_cast<double>(view_.centerX));
    out.insert(QStringLiteral("centerY"), static_cast<double>(view_.centerY));
    out.insert(QStringLiteral("unitsHigh"), static_cast<double>(view_.unitsHigh));
    out.insert(QStringLiteral("targetX"), static_cast<double>(view_.targetX));
    out.insert(QStringLiteral("targetY"), static_cast<double>(view_.targetY));
    out.insert(QStringLiteral("targetZ"), static_cast<double>(view_.targetZ));
    out.insert(QStringLiteral("yaw"), static_cast<double>(view_.yaw));
    out.insert(QStringLiteral("pitch"), static_cast<double>(view_.pitch));
    out.insert(QStringLiteral("distance"), static_cast<double>(view_.distance));
    out.insert(QStringLiteral("fieldOfView"), static_cast<double>(view_.fieldOfView));
    return out;
}

double ViewportItem::unitScale() const {
    float x = 0, y = 0, scale = 0;
    if (!vfx::editor::projectPoint(currentView(), flat(), 0, 0, 0, x, y, scale)) {
        return 0.0;
    }
    return static_cast<double>(scale);
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

namespace {

// The viewport's nodes: the model, when there is one, under the particles.
class ViewportNode : public QSGNode {
public:
    ~ViewportNode() override {
        while (QSGNode* child = firstChild()) {
            removeChildNode(child);
            delete child;
        }
    }
    QSGSimpleTextureNode* model = nullptr;
    std::unique_ptr<QSGTexture> modelTexture;
    std::unique_ptr<QSGTexture> depthTexture;  // the model's distances, for the particle shaders
    SpriteDepth depth;
    QSGNode* sprites = nullptr;
};

bool sameView(const vfx::editor::View& a, const vfx::editor::View& b) {
    return a.width == b.width && a.height == b.height && a.centerX == b.centerX && a.centerY == b.centerY &&
           a.unitsHigh == b.unitsHigh && a.targetX == b.targetX && a.targetY == b.targetY && a.targetZ == b.targetZ &&
           a.yaw == b.yaw && a.pitch == b.pitch && a.distance == b.distance && a.fieldOfView == b.fieldOfView;
}

bool samePlace(const vfx::editor::ModelPlace& a, const vfx::editor::ModelPlace& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.turn == b.turn && a.height == b.height;
}

}  // namespace

QSGNode* ViewportItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    // Runs on the render thread while the main thread is held still, so the
    // session's frame and the controller's model can be read here safely.
    auto* root = static_cast<ViewportNode*>(old);
    if (!root) {
        root = new ViewportNode;
    }
    mesh_.vertices.clear();
    mesh_.indices.clear();
    mesh_.runs.clear();
    mesh_.drawn = 0;
    const vfx::editor::ImageSet* images = controller_ ? &controller_->session().images() : nullptr;
    const std::shared_ptr<const vfx::editor::Model> model = controller_ ? controller_->model() : nullptr;
    const vfx::editor::View view = currentView();

    // The model, drawn again only when it, its place or the camera changes.
    // While the camera moves it is drawn at the viewport's own size and
    // without smoothing, which is several times quicker.
    if (model && width() > 0 && height() > 0 && window()) {
        const double ratio = std::clamp(static_cast<double>(window()->effectiveDevicePixelRatio()), 1.0, 2.0);
        ModelKey key;
        key.model = model.get();
        key.place = controller_->modelPlace();
        key.view = view;
        key.flat = controller_->session().frame().flat;
        key.width = static_cast<int>(std::lround(width() * (moving_ ? 1.0 : ratio)));
        key.height = static_cast<int>(std::lround(height() * (moving_ ? 1.0 : ratio)));
        key.smooth = moving_ || ratio > 1.5 ? 1 : 2;
        if (key.model != modelKey_.model || !samePlace(key.place, modelKey_.place) || !sameView(key.view, modelKey_.view) ||
            key.flat != modelKey_.flat || key.width != modelKey_.width || key.height != modelKey_.height ||
            key.smooth != modelKey_.smooth) {
            modelPicture_ = vfx::editor::drawModel(*model, key.place, view, key.flat, key.width, key.height, key.smooth);
            modelKey_ = key;
            modelFresh_ = true;
        }
    } else {
        modelPicture_ = vfx::editor::ModelPicture{};
        modelKey_ = ModelKey{};
    }
    const bool hide = model && controller_ && controller_->hideBehindModel() && !modelPicture_.empty();

    if (controller_ && width() > 0 && height() > 0) {
        vfx::editor::buildSpriteMesh(controller_->session().frame(), view, mesh_, images);
    }
    const int drawn = static_cast<int>(mesh_.drawn);
    if (drawn != drawnCount_) {
        drawnCount_ = drawn;
        QMetaObject::invokeMethod(this, &ViewportItem::drawnCountChanged, Qt::QueuedConnection);
    }

    if (!modelPicture_.empty()) {
        if (!root->model) {
            root->model = new QSGSimpleTextureNode;
            root->model->setFiltering(QSGTexture::Linear);
            root->prependChildNode(root->model);
            modelFresh_ = true;
        }
        if (modelFresh_) {
            const vfx::editor::Picture& p = modelPicture_.picture;
            const QImage pixels(p.rgba.data(), p.width, p.height, p.width * 4, QImage::Format_RGBA8888);
            QSGTexture* texture = window()->createTextureFromImage(
                pixels.convertToFormat(QImage::Format_RGBA8888_Premultiplied), QQuickWindow::TextureHasAlphaChannel);
            if (texture) {
                texture->setFiltering(QSGTexture::Linear);
                root->model->setTexture(texture);
                root->modelTexture.reset(texture);  // the old one goes now that nothing uses it
            }
            // Its distances, sixteen bits in red and green between the
            // nearest and the furthest, blue where there is any model.
            float nearest = std::numeric_limits<float>::max(), furthest = std::numeric_limits<float>::lowest();
            for (const float d : modelPicture_.depth) {
                if (std::isfinite(d)) {
                    nearest = std::min(nearest, d);
                    furthest = std::max(furthest, d);
                }
            }
            root->depthTexture.reset();
            if (nearest <= furthest) {
                const float range = std::max(furthest - nearest, 1e-3f);
                QImage distances(p.width, p.height, QImage::Format_RGBA8888);
                for (int y = 0; y < p.height; ++y) {
                    uchar* line = distances.scanLine(y);
                    for (int x = 0; x < p.width; ++x) {
                        const float d = modelPicture_.depth[static_cast<std::size_t>(y) * static_cast<std::size_t>(p.width) +
                                                            static_cast<std::size_t>(x)];
                        uchar* out = line + x * 4;
                        if (std::isfinite(d)) {
                            const auto q = static_cast<unsigned>(std::lround(std::clamp((d - nearest) / range, 0.0f, 1.0f) * 65535.0f));
                            out[0] = static_cast<uchar>(q >> 8);
                            out[1] = static_cast<uchar>(q & 0xffu);
                            out[2] = 255;
                        } else {
                            out[0] = out[1] = out[2] = 0;
                        }
                        out[3] = 255;
                    }
                }
                if (QSGTexture* t = window()->createTextureFromImage(distances)) {
                    t->setFiltering(QSGTexture::Nearest);
                    root->depthTexture.reset(t);
                }
                root->depth.nearest = nearest;
                root->depth.furthest = nearest + range;
            }
            root->depth.perspective = !modelKey_.flat;
            root->depth.fade = vfx::editor::kBehindFade;
            modelFresh_ = false;
        }
        root->model->setRect(boundingRect());
    } else if (root->model) {
        root->removeChildNode(root->model);
        delete root->model;
        root->model = nullptr;
        root->modelTexture.reset();
        root->depthTexture.reset();
    }

    SpriteDepth depth = root->depth;
    depth.texture = hide && root->model ? root->depthTexture.get() : nullptr;
    QSGNode* sprites = updateSpriteNode(root->sprites, mesh_, images, window(), 512, size(), &depth);
    if (sprites != root->sprites) {
        if (root->sprites) {
            root->removeChildNode(root->sprites);
            delete root->sprites;
        }
        root->appendChildNode(sprites);
        root->sprites = sprites;
    }
    return root;
}
