// VFX Forge app: small live pictures used around the window.
#pragma once

#include <cstdint>
#include <memory>

#include <QColor>
#include <QQuickItem>
#include <QString>

#include "vfx/Simulation.h"
#include "vfx/editor/Presets.h"
#include "vfx/editor/SpriteMesh.h"

// One library preset, playing. The library shows one of these per card.
class PresetPreview : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString preset READ preset WRITE setPreset NOTIFY presetChanged)

public:
    explicit PresetPreview(QQuickItem* parent = nullptr);
    ~PresetPreview() override;

    QString preset() const { return preset_; }
    void setPreset(const QString& id);

    // Moves the preview on by this much real time. The library calls this
    // once per displayed frame for every card on screen.
    Q_INVOKABLE void advance(double seconds);

signals:
    void presetChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;

private:
    QString preset_;
    vfx::editor::PresetInfo info_;
    std::unique_ptr<vfx::Simulation> simulation_;
    double time_ = 0.0;
    double quiet_ = 0.0;  // how long nothing has been on screen
    vfx::RenderFrame frame_;
    vfx::editor::SpriteMesh mesh_;
};

// One built-in particle shape, drawn as large as the item allows.
class ShapeIcon : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString shape READ shape WRITE setShape NOTIFY changed)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)

public:
    explicit ShapeIcon(QQuickItem* parent = nullptr);

    QString shape() const { return shape_; }
    void setShape(const QString& name);
    QColor color() const { return color_; }
    void setColor(const QColor& color);

signals:
    void changed();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    QString shape_ = QStringLiteral("soft");
    QColor color_ = QColor(255, 255, 255);
    vfx::editor::SpriteMesh mesh_;
};
