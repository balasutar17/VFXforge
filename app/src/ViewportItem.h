// VFX Forge app: the viewport, where the effect is drawn.
#pragma once

#include <QPointF>
#include <QQuickItem>
#include <QTimer>
#include <QVariantMap>

#include "AppController.h"
#include "vfx/editor/Model.h"
#include "vfx/editor/SpriteMesh.h"

class ViewportItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(AppController* controller READ controller WRITE setController NOTIFY controllerChanged)
    // Where the effect's origin sits in the viewport, for the marker.
    Q_PROPERTY(QPointF origin READ origin NOTIFY viewChanged)
    Q_PROPERTY(bool originVisible READ originVisible NOTIFY viewChanged)
    // How many pixels one world unit covers at the origin.
    Q_PROPERTY(double unitScale READ unitScale NOTIFY viewChanged)
    Q_PROPERTY(int drawnCount READ drawnCount NOTIFY drawnCountChanged)

public:
    explicit ViewportItem(QQuickItem* parent = nullptr);

    AppController* controller() const { return controller_; }
    void setController(AppController* controller);

    QPointF origin() const;
    bool originVisible() const;
    double unitScale() const;
    int drawnCount() const { return drawnCount_; }

    // Mouse handling lives in the window; these do the camera arithmetic.
    Q_INVOKABLE void dragBy(double dx, double dy, bool secondary);
    Q_INVOKABLE void zoomBy(double steps, double atX, double atY);
    Q_INVOKABLE void resetView();
    // Shows the given world point in the middle, with this many units top to bottom.
    Q_INVOKABLE void frame(double x, double y, double unitsHigh);
    // The camera as it is now, for exports that should show the same view.
    Q_INVOKABLE QVariantMap viewState() const;
    // Where a world point lands in the viewport: {x, y, scale (pixels per
    // unit there), ok (false when it is behind the camera)}.
    Q_INVOKABLE QVariantMap project(double x, double y, double z) const;

signals:
    void controllerChanged();
    void viewChanged();
    void drawnCountChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    vfx::editor::View currentView() const;
    bool flat() const;

    // Marks the camera as moving: while it moves, the model is drawn
    // quickly at lower quality, and properly once it has been still a moment.
    void moved();

    AppController* controller_ = nullptr;
    vfx::editor::View view_;
    vfx::editor::SpriteMesh mesh_;
    int drawnCount_ = 0;

    // The model as last drawn, and what it was drawn for.
    vfx::editor::ModelPicture modelPicture_;
    struct ModelKey {
        const vfx::editor::Model* model = nullptr;
        vfx::editor::ModelPlace place;
        vfx::editor::View view;
        bool flat = true;
        int width = 0, height = 0, smooth = 0;
    } modelKey_;
    bool modelFresh_ = false;  // drawn since the texture was made
    bool moving_ = false;
    QTimer* settle_ = nullptr;
};
