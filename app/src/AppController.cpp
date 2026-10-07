#include "AppController.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <QCoreApplication>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QVariantMap>

#include "vfx/Command.h"
#include "vfx/FileIO.h"
#include "vfx/Path.h"

using vfx::editor::ControlView;

namespace {

QString text(const std::string& s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }

std::string utf8(const QString& s) { return s.toUtf8().toStdString(); }

std::filesystem::path toPath(const QString& localFile) { return vfx::pathFromUtf8(utf8(localFile)); }

double clampTo(const vfx::PropertyDesc& desc, double v) {
    if (!std::isfinite(v)) {
        v = 0.0;
    }
    if (desc.min && v < *desc.min) {
        v = *desc.min;
    }
    if (desc.max && v > *desc.max) {
        v = *desc.max;
    }
    return v;
}

}  // namespace

AppController::AppController(QObject* parent) : QObject(parent) {
    rebuildLayers();
    rebuildControls();
    say(QStringLiteral("Ready. Drag the controls on the right to shape the effect."));
}

QString AppController::version() const { return QCoreApplication::applicationVersion(); }

// -------------------------------------------------------------- reading

QString AppController::effectName() const { return text(session_.effect().name); }

QString AppController::fileName() const {
    if (!session_.hasFile()) {
        return QString();
    }
    return QFileInfo(text(vfx::pathToUtf8(session_.filePath()))).fileName();
}

QString AppController::windowTitle() const {
    QString title = session_.hasFile() ? fileName() : effectName();
    if (session_.dirty()) {
        title += QStringLiteral(" (edited)");
    }
    if (session_.readOnly()) {
        title += QStringLiteral(" (read-only)");
    }
    return title + QStringLiteral(" - VFX Forge");
}

bool AppController::threeD() const { return session_.effect().space == "3d"; }
bool AppController::loop() const { return session_.effect().loop == "loop"; }
double AppController::duration() const { return session_.effect().duration; }
double AppController::frameRate() const { return session_.effect().frameRate; }

int AppController::frameCount() const {
    const double frames = std::floor(session_.effect().duration * session_.effect().frameRate + 1e-6);
    return static_cast<int>(std::clamp(frames, 1.0, 1e9));
}

double AppController::seed() const { return static_cast<double>(session_.effect().seed); }

bool AppController::canUndo() const { return session_.commands().canUndo(); }
bool AppController::canRedo() const { return session_.commands().canRedo(); }
QString AppController::undoName() const { return text(session_.commands().undoName()); }
QString AppController::redoName() const { return text(session_.commands().redoName()); }

QString AppController::selectedLayerName() const {
    const auto& layers = session_.effect().layers;
    if (selected_ < 0 || selected_ >= static_cast<int>(layers.size())) {
        return QString();
    }
    return text(layers[static_cast<std::size_t>(selected_)].name);
}

bool AppController::playing() const { return session_.clock().playing(); }
double AppController::timeScale() const { return session_.clock().timeScale(); }

vfx::Id AppController::selectedLayerId() const {
    const auto& layers = session_.effect().layers;
    if (selected_ < 0 || selected_ >= static_cast<int>(layers.size())) {
        return vfx::Id{};
    }
    return layers[static_cast<std::size_t>(selected_)].id;
}

// ------------------------------------------------------------ refreshing

void AppController::say(const QString& message, bool error) {
    message_ = message;
    messageIsError_ = error;
    emit messageChanged();
}

bool AppController::report(const vfx::Status& status) {
    if (status) {
        return true;
    }
    say(text(status.error().message), true);
    return false;
}

void AppController::rebuildLayers() {
    QVariantList list;
    for (const vfx::Layer& layer : session_.effect().layers) {
        QVariantMap row;
        row.insert(QStringLiteral("name"), text(layer.name));
        row.insert(QStringLiteral("enabled"), layer.enabled);
        list.push_back(row);
    }
    layers_ = std::move(list);

    const int count = static_cast<int>(session_.effect().layers.size());
    selected_ = count == 0 ? -1 : std::clamp(selected_, 0, count - 1);
}

void AppController::rebuildControls() {
    QVariantList list;
    const vfx::Id layer = selectedLayerId();
    if (layer.valid()) {
        for (const ControlView& c : session_.controls(layer)) {
            QVariantMap row;
            row.insert(QStringLiteral("label"), text(c.label));
            QString kind = QStringLiteral("unsupported");
            if (c.desc) {
                const vfx::PropertyDesc& d = *c.desc;
                row.insert(QStringLiteral("unit"), text(d.unit));
                row.insert(QStringLiteral("help"), text(d.help));
                row.insert(QStringLiteral("uiMin"), d.uiMin.value_or(d.min.value_or(0.0)));
                row.insert(QStringLiteral("uiMax"), d.uiMax.value_or(d.max.value_or(1.0)));
                row.insert(QStringLiteral("hardMin"), d.min.value_or(-1e12));
                row.insert(QStringLiteral("hardMax"), d.max.value_or(1e12));

                if (const auto* number = std::get_if<double>(&c.value)) {
                    kind = QStringLiteral("number");
                    row.insert(QStringLiteral("value"), *number);
                    row.insert(QStringLiteral("canBeRandom"), false);
                } else if (const auto* scalar = std::get_if<vfx::Scalar>(&c.value)) {
                    row.insert(QStringLiteral("canBeRandom"), true);
                    if (scalar->kind == vfx::Scalar::Kind::Constant) {
                        kind = QStringLiteral("number");
                        row.insert(QStringLiteral("value"), scalar->a);
                    } else if (scalar->kind == vfx::Scalar::Kind::Random) {
                        kind = QStringLiteral("range");
                        row.insert(QStringLiteral("from"), scalar->a);
                        row.insert(QStringLiteral("to"), scalar->b);
                    } else {
                        kind = QStringLiteral("curve");
                    }
                } else if (const auto* color = std::get_if<vfx::Color>(&c.value)) {
                    kind = QStringLiteral("color");
                    // The picker works in screen colour; brightness above 1
                    // is the Glow control's job, so it is held to 1 here.
                    QColor shown;
                    shown.setRgbF(static_cast<float>(std::min(1.0, vfx::editor::linearToSrgb(color->r))),
                                  static_cast<float>(std::min(1.0, vfx::editor::linearToSrgb(color->g))),
                                  static_cast<float>(std::min(1.0, vfx::editor::linearToSrgb(color->b))),
                                  static_cast<float>(std::clamp(color->a, 0.0, 1.0)));
                    row.insert(QStringLiteral("color"), shown);
                } else if (const auto* direction = std::get_if<vfx::Vec3>(&c.value)) {
                    kind = QStringLiteral("direction");
                    const vfx::editor::Heading h = vfx::editor::headingFromDirection(*direction);
                    row.insert(QStringLiteral("heading"), h.heading);
                    row.insert(QStringLiteral("tilt"), h.tilt);
                    row.insert(QStringLiteral("threeD"), threeD());
                }
            }
            row.insert(QStringLiteral("kind"), kind);
            list.push_back(row);
        }
    }
    controls_ = std::move(list);
}

void AppController::changed(Refresh what) {
    if (what == Structure) {
        rebuildLayers();  // also keeps the selection on a layer that exists
        emit layersChanged();
        emit selectionChanged();
        rebuildControls();
        emit controlsChanged();
    }
    emit documentChanged();
    emit redraw();
}

// ---------------------------------------------------------------- files

void AppController::newEffect(bool threeD) {
    session_.newEffect(threeD);
    selected_ = 0;
    changed(Structure);
    emit playbackChanged();
    say(threeD ? QStringLiteral("New 3D effect.") : QStringLiteral("New 2D effect."));
}

bool AppController::openFile(const QUrl& file) { return openPath(file.toLocalFile()); }

bool AppController::openPath(const QString& path) {
    if (path.isEmpty()) {
        say(QStringLiteral("That is not a file on this computer."), true);
        return false;
    }
    if (!report(session_.open(toPath(path)))) {
        return false;
    }
    selected_ = 0;
    changed(Structure);
    emit playbackChanged();

    const auto& notes = session_.loadNotes();
    if (session_.readOnly()) {
        say(QStringLiteral("Opened read-only: this file was made by a newer version of VFX Forge. "
                           "Use Save As to keep a copy."),
            true);
    } else if (!notes.empty()) {
        say(QStringLiteral("Opened, with %1 thing(s) repaired. First: %2")
                .arg(static_cast<int>(notes.size()))
                .arg(text(notes.front().message)),
            true);
    } else {
        say(QStringLiteral("Opened %1.").arg(fileName()));
    }
    return true;
}

bool AppController::save() {
    if (!session_.hasFile()) {
        return false;
    }
    if (!report(session_.save())) {
        return false;
    }
    emit documentChanged();
    say(QStringLiteral("Saved %1.").arg(fileName()));
    return true;
}

bool AppController::saveAs(const QUrl& file) {
    QString path = file.toLocalFile();
    if (path.isEmpty()) {
        say(QStringLiteral("That is not a place on this computer."), true);
        return false;
    }
    if (!path.endsWith(QStringLiteral(".vfx"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".vfx");
    }
    if (!report(session_.saveAs(toPath(path)))) {
        return false;
    }
    emit documentChanged();
    say(QStringLiteral("Saved %1.").arg(fileName()));
    return true;
}

// ---------------------------------------------------------------- edits

void AppController::undo() {
    session_.endEdit();
    const QString name = undoName();
    if (canUndo() && report(session_.undo())) {
        changed(Structure);
        say(QStringLiteral("Undid: %1").arg(name));
    }
}

void AppController::redo() {
    session_.endEdit();
    const QString name = redoName();
    if (canRedo() && report(session_.redo())) {
        changed(Structure);
        say(QStringLiteral("Redid: %1").arg(name));
    }
}

void AppController::beginEdit(const QString& name) { session_.beginEdit(utf8(name)); }

void AppController::endEdit() {
    session_.endEdit();
    emit documentChanged();
}

void AppController::setEffectName(const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == effectName()) {
        emit documentChanged();  // puts the old name back in the field
        return;
    }
    report(session_.set(vfx::Path::effect("name"), vfx::Value(utf8(trimmed))));
    changed(Values);
}

void AppController::setLoop(bool on) {
    if (on == loop()) {
        return;
    }
    report(session_.set(vfx::Path::effect("loop"), vfx::Value(std::string(on ? "loop" : "once"))));
    changed(Values);
    emit playbackChanged();
}

void AppController::setDuration(double seconds) {
    const vfx::Effect& effect = session_.effect();
    if (!std::isfinite(seconds)) {
        emit documentChanged();
        return;
    }
    seconds = std::clamp(seconds, 0.05, 3600.0);
    if (seconds == effect.duration) {
        emit documentChanged();
        return;
    }
    // Layers that ran for the whole effect keep running for the whole effect.
    std::vector<vfx::CommandPtr> steps;
    steps.push_back(std::make_unique<vfx::SetPropertyCommand>(vfx::Path::effect("duration"),
                                                              vfx::Value(seconds)));
    for (const vfx::Layer& layer : effect.layers) {
        if (layer.start == 0.0 && layer.duration == effect.duration) {
            steps.push_back(std::make_unique<vfx::SetPropertyCommand>(
                vfx::Path::layerField(layer.id, "duration"), vfx::Value(seconds)));
        }
    }
    report(session_.commands().push(
        std::make_unique<vfx::CompositeCommand>("Change Duration", std::move(steps))));
    changed(Values);
}

void AppController::setFrameRate(double framesPerSecond) {
    if (!std::isfinite(framesPerSecond)) {
        emit documentChanged();
        return;
    }
    framesPerSecond = std::clamp(framesPerSecond, 1.0, 240.0);
    report(session_.set(vfx::Path::effect("frameRate"), vfx::Value(framesPerSecond)));
    changed(Values);
}

void AppController::setSeed(double value) {
    if (!std::isfinite(value)) {
        emit documentChanged();
        return;
    }
    const auto whole = static_cast<std::int64_t>(std::clamp(std::floor(value), 0.0, 4294967295.0));
    report(session_.set(vfx::Path::effect("seed"), vfx::Value(whole)));
    changed(Values);
}

void AppController::newVariation() {
    setSeed(static_cast<double>(QRandomGenerator::global()->bounded(1u, 1000000u)));
    say(QStringLiteral("New variation. Undo brings the last one back."));
}

void AppController::selectLayer(int index) {
    const int count = static_cast<int>(session_.effect().layers.size());
    if (index < 0 || index >= count || index == selected_) {
        return;
    }
    session_.endEdit();
    selected_ = index;
    emit selectionChanged();
    rebuildControls();
    emit controlsChanged();
}

void AppController::addLayer() {
    const int number = static_cast<int>(session_.effect().layers.size()) + 1;
    vfx::Id created;
    if (report(session_.addEmitter("Layer " + std::to_string(number), &created))) {
        selected_ = static_cast<int>(session_.effect().layers.size()) - 1;
        changed(Structure);
        say(QStringLiteral("Added a layer."));
    }
}

void AppController::removeLayer(int index) {
    const auto& layers = session_.effect().layers;
    if (index < 0 || index >= static_cast<int>(layers.size())) {
        return;
    }
    const QString name = text(layers[static_cast<std::size_t>(index)].name);
    if (report(session_.removeLayer(layers[static_cast<std::size_t>(index)].id))) {
        if (selected_ > index) {
            --selected_;
        }
        changed(Structure);
        say(QStringLiteral("Removed %1. Undo brings it back.").arg(name));
    }
}

void AppController::renameLayer(int index, const QString& name) {
    const auto& layers = session_.effect().layers;
    if (index < 0 || index >= static_cast<int>(layers.size())) {
        return;
    }
    const QString trimmed = name.trimmed();
    const vfx::Layer& layer = layers[static_cast<std::size_t>(index)];
    if (!trimmed.isEmpty() && trimmed != text(layer.name)) {
        report(session_.set(vfx::Path::layerField(layer.id, "name"), vfx::Value(utf8(trimmed))));
    }
    changed(Structure);
}

void AppController::setLayerEnabled(int index, bool enabled) {
    const auto& layers = session_.effect().layers;
    if (index < 0 || index >= static_cast<int>(layers.size())) {
        return;
    }
    const vfx::Layer& layer = layers[static_cast<std::size_t>(index)];
    if (layer.enabled != enabled) {
        report(session_.set(vfx::Path::layerField(layer.id, "enabled"), vfx::Value(enabled)));
    }
    changed(Structure);
}

bool AppController::control(int index, ControlView& out) const {
    const vfx::Id layer = selectedLayerId();
    if (!layer.valid()) {
        return false;
    }
    std::vector<ControlView> all = session_.controls(layer);
    if (index < 0 || index >= static_cast<int>(all.size()) ||
        !all[static_cast<std::size_t>(index)].desc) {
        return false;
    }
    out = std::move(all[static_cast<std::size_t>(index)]);
    return true;
}

void AppController::applyControl(int index, const vfx::Value& value) {
    ControlView c;
    if (!control(index, c)) {
        return;
    }
    report(session_.setControl(c.layer, c.control, value));
    // The control that is being dragged already shows the new value, so the
    // list of controls is deliberately not rebuilt here.
    changed(Values);
}

void AppController::setControlNumber(int index, double value) {
    ControlView c;
    if (!control(index, c)) {
        return;
    }
    value = clampTo(*c.desc, value);
    if (c.desc->kind == vfx::ValueKind::Scalar) {
        applyControl(index, vfx::Value(vfx::Scalar::constant(value)));
    } else if (c.desc->kind == vfx::ValueKind::Float) {
        applyControl(index, vfx::Value(value));
    }
}

void AppController::setControlRange(int index, double from, double to) {
    ControlView c;
    if (!control(index, c) || c.desc->kind != vfx::ValueKind::Scalar) {
        return;
    }
    from = clampTo(*c.desc, from);
    to = clampTo(*c.desc, to);
    if (from > to) {
        std::swap(from, to);
    }
    applyControl(index, vfx::Value(vfx::Scalar::random(from, to)));
}

void AppController::setControlRandom(int index, bool random) {
    ControlView c;
    if (!control(index, c) || c.desc->kind != vfx::ValueKind::Scalar) {
        return;
    }
    const auto& now = std::get<vfx::Scalar>(c.value);
    vfx::Scalar next = now;
    if (random && now.kind == vfx::Scalar::Kind::Constant) {
        // Open up a range around the current value so the look barely moves.
        const double low = clampTo(*c.desc, now.a * 0.75);
        const double high = clampTo(*c.desc, now.a * 1.25);
        next = vfx::Scalar::random(std::min(low, high), std::max(low, high));
    } else if (!random && now.kind == vfx::Scalar::Kind::Random) {
        next = vfx::Scalar::constant(clampTo(*c.desc, (now.a + now.b) * 0.5));
    } else {
        return;
    }
    session_.endEdit();
    report(session_.setControl(c.layer, c.control, vfx::Value(next)));
    rebuildControls();
    emit controlsChanged();
    changed(Values);
}

void AppController::setControlColor(int index, const QColor& color) {
    ControlView c;
    if (!control(index, c) || c.desc->kind != vfx::ValueKind::Color || !color.isValid()) {
        return;
    }
    vfx::Color linear;
    linear.r = vfx::editor::srgbToLinear(static_cast<double>(color.redF()));
    linear.g = vfx::editor::srgbToLinear(static_cast<double>(color.greenF()));
    linear.b = vfx::editor::srgbToLinear(static_cast<double>(color.blueF()));
    linear.a = std::clamp(static_cast<double>(color.alphaF()), 0.0, 1.0);
    applyControl(index, vfx::Value(linear));
}

void AppController::setControlDirection(int index, double heading, double tilt) {
    ControlView c;
    if (!control(index, c) || c.desc->kind != vfx::ValueKind::Vec3) {
        return;
    }
    if (!std::isfinite(heading) || !std::isfinite(tilt)) {
        return;
    }
    applyControl(index, vfx::Value(vfx::editor::directionFromHeading(
                            vfx::editor::Heading{heading, threeD() ? tilt : 0.0})));
}

// ------------------------------------------------------------- playback

void AppController::togglePlay() {
    if (playing()) {
        pause();
    } else {
        play();
    }
}

void AppController::play() {
    session_.clock().play();
    emit playbackChanged();
}

void AppController::pause() {
    session_.clock().pause();
    emit playbackChanged();
}

void AppController::restart() {
    session_.clock().stop();
    session_.clock().play();
    emit playbackChanged();
}

void AppController::seek(double seconds) {
    session_.clock().seek(seconds);
    emit redraw();
}

void AppController::stepFrames(int frames) {
    session_.clock().stepFrames(frames);
    emit playbackChanged();
    emit redraw();
}

void AppController::setTimeScale(double scale) {
    session_.clock().setTimeScale(scale);
    emit playbackChanged();
}

void AppController::tick(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) {
        seconds = 0.0;
    }
    session_.tick(seconds);

    // A one-shot effect stops itself at its end.
    const bool nowPlaying = playing();
    if (nowPlaying != wasPlaying_) {
        wasPlaying_ = nowPlaying;
        emit playbackChanged();
    }

    fpsWindow_ += seconds;
    ++fpsFrames_;
    bool statsChanged = false;
    if (fpsWindow_ >= 0.5) {
        const int fps = static_cast<int>(std::lround(fpsFrames_ / fpsWindow_));
        statsChanged = fps != fps_;
        fps_ = fps;
        fpsWindow_ = 0.0;
        fpsFrames_ = 0;

        // Checked twice a second, not every frame: it only changes on edits.
        QString warning;
        const auto capped = session_.cappedLayers();
        if (!capped.empty()) {
            const vfx::Layer* layer = vfx::findLayer(session_.effect(), capped.front());
            warning = QStringLiteral("%1 wants more particles than the safety limit of %2, so some are left out.")
                          .arg(layer ? text(layer->name) : QStringLiteral("A layer"))
                          .arg(vfx::kMaxParticlesPerEmitter);
        }
        if (warning != warning_) {
            warning_ = warning;
            statsChanged = true;
        }
    }

    const double time = session_.clock().displayTime();
    const int frame = static_cast<int>(session_.clock().displayFrame());
    const int particles = static_cast<int>(session_.particleCount());
    if (statsChanged || time != time_ || frame != frame_ || particles != particles_) {
        time_ = time;
        frame_ = frame;
        particles_ = particles;
        emit frameChanged();
    }
    emit redraw();
}
