#include "PreviewItems.h"

#include <cmath>
#include <string>

#include "SpriteNode.h"
#include "vfx/Id.h"
#include "vfx/Metadata.h"
#include "vfx/Program.h"
#include "vfx/editor/Session.h"

// ---------------------------------------------------------- PresetPreview

PresetPreview::PresetPreview(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    setClip(true);
}

PresetPreview::~PresetPreview() = default;

void PresetPreview::setPreset(const QString& id) {
    if (id == preset_) {
        return;
    }
    preset_ = id;
    simulation_.reset();
    frame_ = vfx::RenderFrame{};

    const std::string key = id.toStdString();
    if (const vfx::editor::PresetInfo* info = vfx::editor::findPreset(key)) {
        info_ = *info;
        // Fixed IDs: every showing of a preview looks the same.
        vfx::IdGenerator ids(2026);
        const vfx::IdSource newId = [&ids]() { return ids.next(); };
        auto made = vfx::editor::makePreset(key, newId);
        if (made) {
            simulation_ = std::make_unique<vfx::Simulation>(vfx::compileEffect(made.value()));
            // Start at a moment where the effect looks like itself, so a
            // card is never empty when the library opens.
            time_ = info_.previewTime;
            simulation_->seek(static_cast<std::int64_t>(time_ / vfx::kSimulationStep));
            simulation_->extract(frame_);
        }
    }
    emit presetChanged();
    update();
}

void PresetPreview::advance(double seconds) {
    if (!simulation_ || !std::isfinite(seconds) || seconds <= 0.0) {
        return;
    }
    time_ += seconds < 0.1 ? seconds : 0.1;
    const auto target = static_cast<std::int64_t>(time_ / vfx::kSimulationStep);
    const std::int64_t behind = target - simulation_->step();
    if (behind > 0 && behind <= 12) {
        for (std::int64_t i = 0; i < behind; ++i) {
            simulation_->advance();
        }
    } else if (behind != 0) {
        simulation_->seek(target);
    }
    simulation_->extract(frame_);
    update();
}

QSGNode* PresetPreview::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    mesh_.vertices.clear();
    mesh_.indices.clear();
    mesh_.drawn = 0;
    if (simulation_ && width() > 0 && height() > 0) {
        vfx::editor::View view;
        view.width = static_cast<float>(width());
        view.height = static_cast<float>(height());
        view.centerX = info_.viewX;
        view.centerY = info_.viewY;
        view.unitsHigh = info_.viewHeight;
        vfx::editor::buildSpriteMesh(frame_, view, mesh_);
    }
    return updateSpriteNode(old, mesh_);
}

// -------------------------------------------------------------- ShapeIcon

ShapeIcon::ShapeIcon(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemHasContents, true); }

void ShapeIcon::setShape(const QString& name) {
    if (name == shape_) {
        return;
    }
    shape_ = name;
    emit changed();
    update();
}

void ShapeIcon::setColor(const QColor& color) {
    if (color == color_) {
        return;
    }
    color_ = color;
    emit changed();
    update();
}

void ShapeIcon::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        update();
    }
}

QSGNode* ShapeIcon::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    mesh_.vertices.clear();
    mesh_.indices.clear();
    mesh_.drawn = 0;
    if (width() > 0 && height() > 0) {
        // The shape's number is its place in the property's list of options.
        int number = 0;
        const auto& options =
            vfx::Registry::builtin().findModule("sprite")->find("shape")->options;
        const std::string wanted = shape_.toStdString();
        for (std::size_t i = 0; i < options.size(); ++i) {
            if (options[i] == wanted) {
                number = static_cast<int>(i);
            }
        }

        vfx::RenderFrame frame;
        frame.flat = true;
        vfx::SpriteInstance particle;
        particle.size = 2.0f;
        particle.r = static_cast<float>(vfx::editor::srgbToLinear(static_cast<double>(color_.redF())));
        particle.g = static_cast<float>(vfx::editor::srgbToLinear(static_cast<double>(color_.greenF())));
        particle.b = static_cast<float>(vfx::editor::srgbToLinear(static_cast<double>(color_.blueF())));
        particle.a = static_cast<float>(color_.alphaF());
        frame.instances.push_back(particle);
        vfx::RenderBatch batch;
        batch.shape = static_cast<vfx::SpriteShape>(number);
        batch.count = 1;
        frame.batches.push_back(batch);

        vfx::editor::View view;
        view.width = static_cast<float>(width());
        view.height = static_cast<float>(height());
        view.centerX = 0;
        view.centerY = 0;
        view.unitsHigh = 2.0f;
        vfx::editor::buildSpriteMesh(frame, view, mesh_);
    }
    return updateSpriteNode(old, mesh_);
}
