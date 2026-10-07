// VFX Forge app: the viewport, where the effect is drawn.
#pragma once

#include <QPointF>
#include <QQuickItem>

#include "AppController.h"
#include "vfx/editor/SpriteMesh.h"

class ViewportItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(AppController* controller READ controller WRITE setController NOTIFY controllerChanged)
    // Where the effect's origin sits in the viewport, for the marker.
    Q_PROPERTY(QPointF origin READ origin NOTIFY viewChanged)
    Q_PROPERTY(bool originVisible READ originVisible NOTIFY viewChanged)
    Q_PROPERTY(int drawnCount READ drawnCount NOTIFY drawnCountChanged)

public:
    explicit ViewportItem(QQuickItem* parent = nullptr);

    AppController* controller() const { return controller_; }
    void setController(AppController* controller);

    QPointF origin() const;
    bool originVisible() const;
    int drawnCount() const { return drawnCount_; }

    // Mouse handling lives in the window; these do the camera arithmetic.
    Q_INVOKABLE void dragBy(double dx, double dy, bool secondary);
    Q_INVOKABLE void zoomBy(double steps, double atX, double atY);
    Q_INVOKABLE void resetView();

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

    AppController* controller_ = nullptr;
    vfx::editor::View view_;
    vfx::editor::SpriteMesh mesh_;
    int drawnCount_ = 0;
};
