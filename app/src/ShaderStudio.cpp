#include "ShaderStudio.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>

#include "vfx/FileIO.h"
#include "vfx/editor/UnityExport.h"

namespace sh = vfx::editor::shader;

namespace {

QString text(const std::string& s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }
std::string utf8(const QString& s) { return s.toUtf8().toStdString(); }
std::filesystem::path toPath(const QString& localFile) { return vfx::pathFromUtf8(utf8(localFile)); }

const char* easingName(sh::Easing e) {
    switch (e) {
        case sh::Easing::Linear: return "linear";
        case sh::Easing::Smooth: return "smooth";
        case sh::Easing::EaseIn: return "easeIn";
        case sh::Easing::EaseOut: return "easeOut";
    }
    return "smooth";
}

sh::Easing easingFrom(const QString& name) {
    if (name == QLatin1String("linear")) return sh::Easing::Linear;
    if (name == QLatin1String("easeIn")) return sh::Easing::EaseIn;
    if (name == QLatin1String("easeOut")) return sh::Easing::EaseOut;
    return sh::Easing::Smooth;
}

std::array<float, 4> defaultOf(const sh::ParamDef& p) {
    if (p.kind == sh::ParamKind::Color) return p.colour;
    return {static_cast<float>(p.def), 0, 0, 0};
}

float clampTo(const sh::ParamDef& p, double v) {
    if (p.kind == sh::ParamKind::Color) return static_cast<float>(v);
    return static_cast<float>(std::clamp(v, p.min, p.max));
}

QString presetFolder() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + QStringLiteral("/shader-presets");
}

QString safeFile(const QString& name) {
    QString out;
    for (const QChar c : name) out += (c.isLetterOrNumber() || c == QLatin1Char(' ') || c == QLatin1Char('-') || c == QLatin1Char('_')) ? c : QLatin1Char('_');
    out = out.trimmed();
    return out.isEmpty() ? QStringLiteral("Shader") : out.left(60);
}

bool contains(const std::string& hay, const QString& needleLower) {
    return text(hay).toLower().contains(needleLower);
}

QImage toImage(const vfx::editor::Picture& p) {
    if (p.width <= 0 || p.height <= 0) return {};
    QImage image(p.rgba.data(), p.width, p.height, p.width * 4, QImage::Format_RGBA8888);
    return image.copy();
}

// One finished frame, one or two panels wide.
QImage drawFrame(const ShaderFrame& f, int width, int height, double* ms) {
    QElapsedTimer timer;
    timer.start();
    sh::PreviewOptions o = f.options;
    o.model = f.model.get();
    QImage out;
    if (f.compare) {
        o.width = std::max(16, width / 2);
        o.height = height;
        const QImage a = toImage(sh::renderPreview(f.other, o));
        const QImage b = toImage(sh::renderPreview(f.settings, o));
        out = QImage(o.width * 2 + 2, height, QImage::Format_RGBA8888);
        out.fill(QColor(60, 62, 70));
        QPainter p(&out);
        p.drawImage(0, 0, a);
        p.drawImage(o.width + 2, 0, b);
    } else {
        o.width = width;
        o.height = height;
        out = toImage(sh::renderPreview(f.settings, o));
    }
    if (ms) *ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
    return out;
}

}  // namespace

// ============================================================================ ShaderStudio

ShaderStudio::ShaderStudio(QObject* parent) : QObject(parent) {
    const sh::Family& f = sh::families().front();
    s_ = sh::presetSettings(f, f.presets.front());
    timer_ = new QTimer(this);
    timer_->setInterval(33);
    connect(timer_, &QTimer::timeout, this, [this] { tick(); });
    clock_.start();
}

ShaderStudio::~ShaderStudio() = default;

void ShaderStudio::setOpen(bool open) {
    if (open == open_) return;
    open_ = open;
    if (open_) {
        lastTick_ = clock_.elapsed();
        timer_->start();
    } else {
        timer_->stop();
    }
    emit openChanged();
}

const sh::Family* ShaderStudio::family() const { return sh::findFamily(s_.family); }
QString ShaderStudio::familyId() const { return QString::fromStdString(s_.family); }
QString ShaderStudio::familyName() const { return family() ? text(family()->name) : QString(); }
QString ShaderStudio::familySummary() const { return family() ? text(family()->summary) : QString(); }
QStringList ShaderStudio::familyLimits() const {
    QStringList out;
    if (family()) {
        for (const std::string& l : family()->limits) out << text(l);
    }
    return out;
}

void ShaderStudio::touch(bool structure, bool animation) {
    ++revision_;
    ++previewRevision_;
    dirty_ = true;
    if (structure) {
        ++structureRevision_;
        emit structureChanged();
    }
    if (animation) {
        ++animationRevision_;
        emit animationChanged();
    }
    emit changed();
    emit previewChanged();
}

void ShaderStudio::say(const QString& message, bool error) {
    status_ = message;
    statusError_ = error;
    emit statusChanged();
}

void ShaderStudio::setReport(const QString& title, const QStringList& lines) {
    reportTitle_ = title;
    report_ = lines;
    emit reportChanged();
}

void ShaderStudio::clearReport() {
    report_.clear();
    reportTitle_.clear();
    emit reportChanged();
}

void ShaderStudio::reportRenderMs(double ms) {
    renderMs_ = renderMs_ <= 0 ? ms : renderMs_ * 0.7 + ms * 0.3;
    emit renderMsChanged();
}

void ShaderStudio::pushUndo() {
    undo_.push_back(s_);
    if (undo_.size() > 100) undo_.erase(undo_.begin());
    redo_.clear();
    emit historyChanged();
}

void ShaderStudio::checkpoint() { pushUndo(); }

void ShaderStudio::adopt(const sh::Settings& s, bool structure, bool clean) {
    s_ = s;
    // A newly opened layer shader selects its top layer; undo and redo keep the selection where it can.
    if (structure) {
        selectedLayer_ = isLayers() ? sh::layerTop(s_) - 1 : -1;
    } else {
        normaliseSelection();
        if (isLayers()) {  // the layers' settings may have changed under the controls
            ++structureRevision_;
            emit structureChanged();
        }
    }
    dirty_ = !clean;
    ++revision_;
    ++previewRevision_;
    ++animationRevision_;
    if (structure) {
        ++structureRevision_;
        emit structureChanged();
    }
    emit animationChanged();
    emit changed();
    emit previewChanged();
    emit historyChanged();
}

void ShaderStudio::undo() {
    if (undo_.empty()) return;
    redo_.push_back(s_);
    const sh::Settings back = undo_.back();
    undo_.pop_back();
    const bool structure = back.family != s_.family;
    adopt(back, structure, false);
}

void ShaderStudio::redo() {
    if (redo_.empty()) return;
    undo_.push_back(s_);
    const sh::Settings next = redo_.back();
    redo_.pop_back();
    const bool structure = next.family != s_.family;
    adopt(next, structure, false);
}

// ---------------------------------------------------------------- properties

void ShaderStudio::setName(const QString& name) {
    const std::string n = utf8(name);
    if (n == s_.name) return;
    s_.name = n;
    dirty_ = true;
    ++revision_;
    emit changed();
}

void ShaderStudio::setShape(const QString& shape) {
    const std::string n = utf8(shape);
    if (n == s_.shape) return;
    s_.shape = n;
    dirty_ = true;
    ++previewRevision_;
    emit previewChanged();
}

QString ShaderStudio::blend() const { return s_.blend == sh::BlendMode::Additive ? QStringLiteral("additive") : QStringLiteral("alpha"); }

void ShaderStudio::setBlend(const QString& blend) {
    const sh::BlendMode b = blend == QLatin1String("alpha") ? sh::BlendMode::Alpha : sh::BlendMode::Additive;
    if (b == s_.blend) return;
    pushUndo();
    s_.blend = b;
    touch();
}

void ShaderStudio::setQuality(int quality) {
    quality = std::clamp(quality, 0, 3);
    if (quality == s_.quality) return;
    pushUndo();
    s_.quality = quality;
    touch();
}

void ShaderStudio::setBackground(int background) {
    background_ = std::clamp(background, 0, 3);
    ++previewRevision_;
    emit previewChanged();
}

void ShaderStudio::setBloom(bool on) {
    bloom_ = on;
    ++previewRevision_;
    emit previewChanged();
}

void ShaderStudio::setZoom(double zoom) {
    zoom_ = std::clamp(zoom, 0.4, 3.0);
    ++previewRevision_;
    emit previewChanged();
}

void ShaderStudio::setYaw(double yaw) {
    yaw_ = yaw;
    ++previewRevision_;
    emit previewChanged();
}

void ShaderStudio::setPitch(double pitch) {
    pitch_ = std::clamp(pitch, -1.45, 1.45);
    ++previewRevision_;
    emit previewChanged();
}

void ShaderStudio::resetCamera() {
    yaw_ = 0.55;
    pitch_ = 0.30;
    zoom_ = 1.0;
    ++previewRevision_;
    emit previewChanged();
}

void ShaderStudio::setCompareOn(bool on) {
    if (on && !pinned_) pinForCompare();
    compareOn_ = on;
    ++previewRevision_;
    emit previewChanged();
}

void ShaderStudio::pinForCompare() {
    pinned_ = std::make_unique<sh::Settings>(s_);
    say(QStringLiteral("Pinned this look. Compare shows it on the left and the shader you are editing on the right."));
    ++previewRevision_;
    emit previewChanged();
}

QStringList ShaderStudio::shapes() const {
    QStringList out;
    for (const std::string& s : sh::previewShapes()) out << text(s);
    if (model_) out << QStringLiteral("model");
    return out;
}

QStringList ShaderStudio::qualityNames() const {
    QStringList out;
    for (const auto& q : sh::qualityProfiles()) out << text(q.name);
    return out;
}

QString ShaderStudio::qualityNote(int quality) const {
    const auto& q = sh::qualityProfiles();
    return text(q[static_cast<std::size_t>(std::clamp(quality, 0, 3))].note);
}

bool ShaderStudio::loadPreviewModel(const QUrl& file) {
    auto loaded = vfx::editor::loadModel(toPath(file.toLocalFile()));
    if (!loaded) {
        say(text(loaded.error().message), true);
        return false;
    }
    modelBase_ = std::make_shared<const vfx::editor::Model>(std::move(loaded.value()));
    modelOrient_ = 0;
    model_ = modelBase_;
    s_.shape = "model";
    ++previewRevision_;
    emit previewChanged();
    say(QStringLiteral("Your model is the preview shape now. It is only for looking; the Unity material works on any mesh."));
    return true;
}

void ShaderStudio::turnPreviewModel() {
    if (!modelBase_) return;
    modelOrient_ = (modelOrient_ + 1) % vfx::editor::kModelOrientations;
    model_ = modelOrient_ == 0 ? modelBase_
                               : std::make_shared<const vfx::editor::Model>(vfx::editor::reorientModel(*modelBase_, modelOrient_));
    ++previewRevision_;
    emit previewChanged();
    static const char* names[] = {"as it came", "upside down", "turned from Z-up", "turned from Z-down"};
    say(QStringLiteral("Model %1. Press again to try the next way up.").arg(QString::fromLatin1(names[modelOrient_])));
}

void ShaderStudio::clearPreviewModel() {
    model_.reset();
    modelBase_.reset();
    modelOrient_ = 0;
    if (s_.shape == "model") s_.shape = "sphere";
    ++previewRevision_;
    emit previewChanged();
}

// ---------------------------------------------------------------- the library

QStringList ShaderStudio::tags() const {
    return {QStringLiteral("All"), QStringLiteral("Favorites"), QStringLiteral("Recent"), QStringLiteral("Casual"), QStringLiteral("Stylized"),
            QStringLiteral("Realistic"), QStringLiteral("AAA"), QStringLiteral("UI"), QStringLiteral("VFX"), QStringLiteral("Mobile")};
}

bool ShaderStudio::isFavorite(const QString& key) const {
    return QSettings().value(QStringLiteral("shader/favorites")).toStringList().contains(key);
}

void ShaderStudio::toggleFavorite(const QString& key) {
    QSettings settings;
    QStringList list = settings.value(QStringLiteral("shader/favorites")).toStringList();
    if (!list.removeOne(key)) list << key;
    settings.setValue(QStringLiteral("shader/favorites"), list);
    ++libraryRevision_;
    emit libraryChanged();
}

void ShaderStudio::remember(const QString& key) {
    QSettings settings;
    QStringList list = settings.value(QStringLiteral("shader/recent")).toStringList();
    list.removeAll(key);
    list.prepend(key);
    while (list.size() > 8) list.removeLast();
    settings.setValue(QStringLiteral("shader/recent"), list);
    ++libraryRevision_;
    emit libraryChanged();
}

QVariantList ShaderStudio::library(const QString& search, const QString& tag) const {
    const QString needle = search.trimmed().toLower();
    const QStringList favorites = QSettings().value(QStringLiteral("shader/favorites")).toStringList();
    const QStringList recent = QSettings().value(QStringLiteral("shader/recent")).toStringList();
    QVariantList out;
    for (const sh::Family& f : sh::families()) {
        bool tagOk = tag.isEmpty() || tag == QLatin1String("All") || tag == QLatin1String("Favorites") || tag == QLatin1String("Recent");
        for (const std::string& t : f.tags) {
            if (text(t) == tag) tagOk = true;
        }
        if (!tagOk) continue;
        QVariantList presets;
        for (const sh::Preset& p : f.presets) {
            const QString key = text(f.id) + QLatin1Char('/') + text(p.id);
            if (tag == QLatin1String("Favorites") && !favorites.contains(key)) continue;
            if (tag == QLatin1String("Recent") && !recent.contains(key)) continue;
            if (!needle.isEmpty() && !contains(p.name, needle) && !contains(p.description, needle) && !contains(f.name, needle) &&
                !contains(f.summary, needle)) {
                continue;
            }
            QVariantMap m;
            m[QStringLiteral("family")] = text(f.id);
            m[QStringLiteral("id")] = text(p.id);
            m[QStringLiteral("name")] = text(p.name);
            m[QStringLiteral("description")] = text(p.description);
            m[QStringLiteral("key")] = key;
            m[QStringLiteral("favorite")] = favorites.contains(key);
            presets << m;
        }
        if (presets.isEmpty()) continue;
        QVariantMap fm;
        fm[QStringLiteral("id")] = text(f.id);
        fm[QStringLiteral("name")] = text(f.name);
        fm[QStringLiteral("number")] = f.number;
        fm[QStringLiteral("summary")] = text(f.summary);
        fm[QStringLiteral("presets")] = presets;
        out << fm;
    }
    return out;
}

void ShaderStudio::loadPreset(const QString& familyId, const QString& presetId) {
    const sh::Family* f = sh::findFamily(utf8(familyId));
    if (!f) return;
    const sh::Preset* p = sh::findPreset(*f, utf8(presetId));
    if (!p) return;
    pushUndo();
    const bool structure = f->id != s_.family;
    sh::Settings next = sh::presetSettings(*f, *p);
    if (s_.quality != next.quality) next.quality = s_.quality;
    file_.clear();
    playhead_ = 0;
    adopt(next, structure, true);
    emit fileChanged();
    emit playheadChanged();
    remember(familyId + QLatin1Char('/') + presetId);
    say(QStringLiteral("%1: %2.").arg(text(f->name), text(p->name)));
}

QVariantList ShaderStudio::userPresets() const {
    QVariantList out;
    const QDir dir(presetFolder());
    for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.vfxshader")}, QDir::Files, QDir::Name)) {
        QVariantMap m;
        m[QStringLiteral("name")] = info.completeBaseName();
        auto bytes = vfx::readFile(toPath(info.absoluteFilePath()));
        QString family;
        if (bytes) {
            auto read = sh::readSettings(bytes.value());
            if (read) {
                const sh::Family* f = sh::findFamily(read.value().settings.family);
                if (f) family = text(f->name);
            }
        }
        m[QStringLiteral("family")] = family;
        out << m;
    }
    return out;
}

void ShaderStudio::saveUserPreset(const QString& name) {
    const QString clean = safeFile(name);
    QDir().mkpath(presetFolder());
    sh::Settings copy = s_;
    copy.name = utf8(name.trimmed().isEmpty() ? clean : name.trimmed());
    const auto saved = vfx::writeFileAtomic(toPath(presetFolder() + QLatin1Char('/') + clean + QStringLiteral(".vfxshader")), sh::writeSettings(copy));
    if (!saved) {
        say(text(saved.error().message), true);
        return;
    }
    // The shader carries the name it was saved under, so a Unity export is named the same.
    if (s_.name != copy.name) {
        s_.name = copy.name;
        ++revision_;
        emit changed();
    }
    ++libraryRevision_;
    emit libraryChanged();
    say(QStringLiteral("Saved \"%1\" to My presets.").arg(clean));
}

void ShaderStudio::loadUserPreset(const QString& name) {
    auto bytes = vfx::readFile(toPath(presetFolder() + QLatin1Char('/') + safeFile(name) + QStringLiteral(".vfxshader")));
    if (!bytes) {
        say(text(bytes.error().message), true);
        return;
    }
    auto read = sh::readSettings(bytes.value());
    if (!read) {
        say(text(read.error().message), true);
        return;
    }
    pushUndo();
    const bool structure = read.value().settings.family != s_.family;
    file_.clear();
    playhead_ = 0;
    adopt(read.value().settings, structure, true);
    emit fileChanged();
    emit playheadChanged();
    say(QStringLiteral("Opened \"%1\".").arg(name));
}

void ShaderStudio::deleteUserPreset(const QString& name) {
    QFile::remove(presetFolder() + QLatin1Char('/') + safeFile(name) + QStringLiteral(".vfxshader"));
    ++libraryRevision_;
    emit libraryChanged();
}

// ---------------------------------------------------------------- the controls

QVariantList ShaderStudio::controls(const QString& search, bool advanced) const {
    QVariantList out;
    const sh::Family* f = family();
    if (!f) return out;
    const QString needle = search.trimmed().toLower();
    // A layer shader shows one layer's settings at a time (or the shader's own, when none is selected).
    const bool layered = isLayers();
    const int layerFirst = layered ? sh::layerParamIndex(*f, 0, "el") : 0;
    const sh::ElementInfo* element = nullptr;
    if (layered && selectedLayer_ >= 0) {
        const int el = static_cast<int>(s_.values[static_cast<std::size_t>(layerFirst + selectedLayer_ * sh::kLayerStride)][0] + 0.5f);
        if (el >= 0 && static_cast<std::size_t>(el) < sh::layerElements().size()) element = &sh::layerElements()[static_cast<std::size_t>(el)];
    }
    for (const sh::ParamDef& p : f->params) {
        std::string label = p.label;
        if (layered) {
            const bool inLayers = p.index >= layerFirst;
            if (selectedLayer_ < 0 && inLayers) continue;
            if (selectedLayer_ >= 0) {
                const int rel = p.index - (layerFirst + selectedLayer_ * sh::kLayerStride);
                if (rel < 0 || rel >= sh::kLayerStride) continue;
                if (rel == sh::kLOn) continue;  // the eye in the stack does this
                // The part of the key after "L3_": what this setting is called for this element.
                const std::string shortKey = p.key.substr(p.key.find('_') + 1);
                if (element) {
                    bool hidden = false;
                    for (const std::string& h : element->hide) hidden = hidden || h == shortKey;
                    if (hidden && needle.isEmpty()) continue;
                    for (const auto& [k, l] : element->labels) {
                        if (k == shortKey) label = l;
                    }
                }
                if (rel == sh::kLEl) label = "Element";
            }
        }
        if (!advanced && p.advanced && needle.isEmpty()) continue;
        if (!needle.isEmpty() && !contains(label, needle) && !contains(p.tip, needle) && !contains(p.key, needle)) continue;
        QVariantMap m;
        m[QStringLiteral("index")] = p.index;
        m[QStringLiteral("key")] = text(p.key);
        m[QStringLiteral("label")] = text(label);
        m[QStringLiteral("tip")] = text(p.tip);
        m[QStringLiteral("kind")] = p.kind == sh::ParamKind::Color ? QStringLiteral("color")
                                    : p.kind == sh::ParamKind::Choice ? QStringLiteral("choice") : QStringLiteral("number");
        m[QStringLiteral("min")] = p.min;
        m[QStringLiteral("max")] = p.max;
        m[QStringLiteral("advanced")] = p.advanced;
        m[QStringLiteral("common")] = p.common;
        QStringList choices;
        for (const std::string& c : p.choices) choices << text(c);
        m[QStringLiteral("choices")] = choices;
        out << m;
    }
    return out;
}

// ---------------------------------------------------------------- layers

bool ShaderStudio::isLayers() const { return s_.family == "layers"; }

void ShaderStudio::setSelectedLayer(int slot) {
    if (!isLayers()) slot = -1;
    slot = std::clamp(slot, -1, sh::kLayerCount - 1);
    if (slot == selectedLayer_) return;
    selectedLayer_ = slot;
    ++structureRevision_;
    emit structureChanged();
}

void ShaderStudio::normaliseSelection() {
    if (!isLayers()) {
        selectedLayer_ = -1;
        return;
    }
    selectedLayer_ = std::min(selectedLayer_, sh::layerTop(s_) - 1);
}

QVariantList ShaderStudio::layers() const {
    QVariantList out;
    const sh::Family* f = family();
    if (!isLayers() || !f) return out;
    const auto& list = sh::layerElements();
    for (int slot = sh::layerTop(s_) - 1; slot >= 0; --slot) {
        const auto at = [&](const char* key) -> const std::array<float, 4>& { return s_.values[static_cast<std::size_t>(sh::layerParamIndex(*f, slot, key))]; };
        const int el = static_cast<int>(at("el")[0] + 0.5f);
        const int bm = static_cast<int>(at("blend")[0] + 0.5f);
        QVariantMap m;
        m[QStringLiteral("slot")] = slot;
        m[QStringLiteral("name")] = text(sh::layerName(s_, slot));
        m[QStringLiteral("element")] = el >= 0 && static_cast<std::size_t>(el) < list.size() ? text(list[static_cast<std::size_t>(el)].name) : QString();
        m[QStringLiteral("group")] = el >= 0 && static_cast<std::size_t>(el) < list.size() ? text(list[static_cast<std::size_t>(el)].group) : QString();
        m[QStringLiteral("empty")] = el <= 0;
        m[QStringLiteral("visible")] = at("on")[0] > 0.5f;
        m[QStringLiteral("blend")] = bm >= 0 && static_cast<std::size_t>(bm) < sh::layerBlendNames().size() ? text(sh::layerBlendNames()[static_cast<std::size_t>(bm)]) : QString();
        const auto& c = at("colA");
        const auto& c2 = at("colB");
        const auto shown = [](const std::array<float, 4>& v) {
            const float boost = std::max({1.0f, v[0], v[1], v[2]});
            return QColor::fromRgbF(std::clamp(v[0] / boost, 0.0f, 1.0f), std::clamp(v[1] / boost, 0.0f, 1.0f), std::clamp(v[2] / boost, 0.0f, 1.0f));
        };
        m[QStringLiteral("colourA")] = shown(c);
        m[QStringLiteral("colourB")] = shown(c2);
        out << m;
    }
    return out;
}

QVariantList ShaderStudio::elements() const {
    QVariantList out;
    const auto& list = sh::layerElements();
    for (std::size_t i = 1; i < list.size(); ++i) {
        QVariantMap m;
        m[QStringLiteral("index")] = static_cast<int>(i);
        m[QStringLiteral("name")] = text(list[i].name);
        m[QStringLiteral("group")] = text(list[i].group);
        m[QStringLiteral("tip")] = text(list[i].tip);
        out << m;
    }
    return out;
}

bool ShaderStudio::addLayer(int element) {
    if (!isLayers()) return false;
    sh::Settings next = s_;
    if (!sh::layerAdd(next, element)) {
        say(QStringLiteral("All eight layers are in use. Remove or hide one first."), true);
        return false;
    }
    pushUndo();
    s_ = next;
    selectedLayer_ = sh::layerTop(s_) - 1;
    touch(true, true);
    say(QStringLiteral("Added %1.").arg(text(sh::layerName(s_, selectedLayer_))));
    return true;
}

void ShaderStudio::removeLayer(int slot) {
    sh::Settings next = s_;
    if (!sh::layerRemove(next, slot)) return;
    pushUndo();
    s_ = next;
    normaliseSelection();
    touch(true, true);
}

void ShaderStudio::duplicateLayer(int slot) {
    sh::Settings next = s_;
    if (!sh::layerDuplicate(next, slot)) {
        say(QStringLiteral("All eight layers are in use. Remove or hide one first."), true);
        return;
    }
    pushUndo();
    s_ = next;
    selectedLayer_ = slot + 1;
    touch(true, true);
}

void ShaderStudio::moveLayer(int slot, int delta) {
    sh::Settings next = s_;
    if (!sh::layerMove(next, slot, delta)) return;
    pushUndo();
    s_ = next;
    selectedLayer_ = slot + delta;
    touch(true, true);
}

void ShaderStudio::toggleLayer(int slot) {
    const sh::Family* f = family();
    if (!isLayers() || !f || slot < 0 || slot >= sh::layerTop(s_)) return;
    const int at = sh::layerParamIndex(*f, slot, "on");
    if (at < 0) return;
    pushUndo();
    float& v = s_.values[static_cast<std::size_t>(at)][0];
    v = v > 0.5f ? 0.0f : 1.0f;
    touch(true);
}

void ShaderStudio::renameLayer(int slot, const QString& name) {
    if (!isLayers() || slot < 0 || slot >= sh::layerTop(s_)) return;
    pushUndo();
    s_.layerNames.resize(static_cast<std::size_t>(sh::kLayerCount));
    const QString trimmed = name.trimmed().left(40);
    // A name the same as the element's own is no name at all.
    s_.layerNames[static_cast<std::size_t>(slot)].clear();
    if (!trimmed.isEmpty() && utf8(trimmed) != sh::layerName(s_, slot)) s_.layerNames[static_cast<std::size_t>(slot)] = utf8(trimmed);
    touch(true, true);
}

void ShaderStudio::newLayerShader() { loadPreset(QStringLiteral("layers"), QStringLiteral("blank")); }

double ShaderStudio::number(int index) const {
    const sh::Family* f = family();
    if (!f || index < 0 || index >= static_cast<int>(s_.values.size())) return 0;
    const auto at = sh::valuesAt(s_, sh::animationTime(s_.animation, playhead_));
    return at[static_cast<std::size_t>(index)][0];
}

QVariantMap ShaderStudio::colorInfo(int index) const {
    QVariantMap m;
    if (index < 0 || index >= static_cast<int>(s_.values.size())) return m;
    const auto& v = s_.values[static_cast<std::size_t>(index)];
    const float boost = std::max({1.0f, v[0], v[1], v[2]});
    const QColor c = QColor::fromRgbF(std::clamp(v[0] / boost, 0.0f, 1.0f), std::clamp(v[1] / boost, 0.0f, 1.0f), std::clamp(v[2] / boost, 0.0f, 1.0f));
    m[QStringLiteral("h")] = c.hsvHueF() < 0 ? 0.0 : c.hsvHueF();
    m[QStringLiteral("s")] = c.hsvSaturationF();
    m[QStringLiteral("v")] = c.valueF();
    m[QStringLiteral("boost")] = static_cast<double>(boost);
    m[QStringLiteral("a")] = static_cast<double>(v[3]);
    return m;
}

QColor ShaderStudio::colorShown(int index) const {
    if (index < 0 || index >= static_cast<int>(s_.values.size())) return QColor();
    const auto& v = s_.values[static_cast<std::size_t>(index)];
    const float boost = std::max({1.0f, v[0], v[1], v[2]});
    return QColor::fromRgbF(std::clamp(v[0] / boost, 0.0f, 1.0f), std::clamp(v[1] / boost, 0.0f, 1.0f), std::clamp(v[2] / boost, 0.0f, 1.0f));
}

bool ShaderStudio::isDefault(int index) const {
    const sh::Family* f = family();
    if (!f || index < 0 || index >= static_cast<int>(f->params.size())) return true;
    const sh::ParamDef& p = f->params[static_cast<std::size_t>(index)];
    const auto d = defaultOf(p);
    const auto& v = s_.values[static_cast<std::size_t>(index)];
    const int n = p.kind == sh::ParamKind::Color ? 4 : 1;
    for (int k = 0; k < n; ++k) {
        if (std::fabs(v[static_cast<std::size_t>(k)] - d[static_cast<std::size_t>(k)]) > 1e-4f) return false;
    }
    return !isAnimated(index);
}

bool ShaderStudio::isAnimated(int index) const {
    const sh::Family* f = family();
    if (!f || index < 0 || index >= static_cast<int>(f->params.size())) return false;
    for (const sh::Track& t : s_.animation.tracks) {
        if (t.param == f->params[static_cast<std::size_t>(index)].key) return !t.keys.empty();
    }
    return false;
}

bool ShaderStudio::hasKeyHere(int index) const {
    const sh::Family* f = family();
    if (!f || index < 0 || index >= static_cast<int>(f->params.size())) return false;
    const double now = sh::animationTime(s_.animation, playhead_);
    for (const sh::Track& t : s_.animation.tracks) {
        if (t.param != f->params[static_cast<std::size_t>(index)].key) continue;
        for (const sh::Key& k : t.keys) {
            if (std::fabs(k.time - now) < 0.04) return true;
        }
    }
    return false;
}

static void putKey(sh::Animation& a, const std::string& param, double time, float value, sh::Easing easing) {
    sh::Track* track = nullptr;
    for (sh::Track& t : a.tracks) {
        if (t.param == param) track = &t;
    }
    if (!track) {
        a.tracks.push_back({param, {}});
        track = &a.tracks.back();
    }
    for (sh::Key& k : track->keys) {
        if (std::fabs(k.time - time) < 0.04) {
            k.value = value;
            return;
        }
    }
    track->keys.push_back({time, value, easing});
    std::sort(track->keys.begin(), track->keys.end(), [](const sh::Key& x, const sh::Key& y) { return x.time < y.time; });
}

void ShaderStudio::setNumber(int index, double value) {
    const sh::Family* f = family();
    if (!f || index < 0 || index >= static_cast<int>(f->params.size())) return;
    const sh::ParamDef& p = f->params[static_cast<std::size_t>(index)];
    const float v = clampTo(p, value);
    if (isAnimated(index)) {
        putKey(s_.animation, p.key, sh::animationTime(s_.animation, playhead_), v, sh::Easing::Smooth);
        touch(false, true);
    } else {
        s_.values[static_cast<std::size_t>(index)][0] = v;
        touch();
    }
}

void ShaderStudio::setChoice(int index, int choice) {
    const sh::Family* f = family();
    if (!f || index < 0 || index >= static_cast<int>(f->params.size())) return;
    const sh::ParamDef& p = f->params[static_cast<std::size_t>(index)];
    pushUndo();
    s_.values[static_cast<std::size_t>(index)][0] = static_cast<float>(std::clamp(choice, 0, std::max(0, static_cast<int>(p.choices.size()) - 1)));
    // Changing a layer's element changes which settings it shows and what they are called.
    touch(isLayers());
}

void ShaderStudio::setColorHsv(int index, double hue, double saturation, double value, double boost) {
    if (index < 0 || index >= static_cast<int>(s_.values.size())) return;
    const QColor c = QColor::fromHsvF(std::clamp(hue, 0.0, 0.999), std::clamp(saturation, 0.0, 1.0), std::clamp(value, 0.0, 1.0));
    const float b = static_cast<float>(std::clamp(boost, 1.0, 16.0));
    auto& v = s_.values[static_cast<std::size_t>(index)];
    v[0] = static_cast<float>(c.redF()) * b;
    v[1] = static_cast<float>(c.greenF()) * b;
    v[2] = static_cast<float>(c.blueF()) * b;
    touch();
}

void ShaderStudio::resetControl(int index) {
    const sh::Family* f = family();
    if (!f || index < 0 || index >= static_cast<int>(f->params.size())) return;
    const sh::ParamDef& p = f->params[static_cast<std::size_t>(index)];
    pushUndo();
    s_.values[static_cast<std::size_t>(index)] = defaultOf(p);
    auto& tracks = s_.animation.tracks;
    tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [&](const sh::Track& t) { return t.param == p.key; }), tracks.end());
    touch(false, true);
}

void ShaderStudio::resetAll() {
    const sh::Family* f = family();
    if (!f) return;
    const sh::Preset* p = sh::findPreset(*f, s_.preset);
    pushUndo();
    sh::Settings next = p ? sh::presetSettings(*f, *p) : sh::defaultSettings(*f);
    next.name = s_.name;
    next.quality = s_.quality;
    adopt(next, false, false);
    say(QStringLiteral("Back to the preset's own values."));
}

void ShaderStudio::colorVariant(double degrees) {
    const sh::Family* f = family();
    if (!f) return;
    pushUndo();
    for (const sh::ParamDef& p : f->params) {
        if (p.kind != sh::ParamKind::Color) continue;
        auto& v = s_.values[static_cast<std::size_t>(p.index)];
        const float boost = std::max({1.0f, v[0], v[1], v[2]});
        QColor c = QColor::fromRgbF(std::clamp(v[0] / boost, 0.0f, 1.0f), std::clamp(v[1] / boost, 0.0f, 1.0f), std::clamp(v[2] / boost, 0.0f, 1.0f));
        float h, s, val, a;
        c.getHsvF(&h, &s, &val, &a);
        if (h < 0) continue;  // a grey has no hue to turn
        h = std::fmod(h + static_cast<float>(degrees) / 360.0f + 1.0f, 1.0f);
        c = QColor::fromHsvF(h, s, val);
        v[0] = static_cast<float>(c.redF()) * boost;
        v[1] = static_cast<float>(c.greenF()) * boost;
        v[2] = static_cast<float>(c.blueF()) * boost;
    }
    touch();
}

// ---------------------------------------------------------------- the timeline

double ShaderStudio::timelineTime() const { return sh::animationTime(s_.animation, playhead_); }

void ShaderStudio::setPlayhead(double seconds) {
    playhead_ = std::max(0.0, seconds);
    ++previewRevision_;
    emit playheadChanged();
    emit previewChanged();
}

void ShaderStudio::setDuration(double seconds) {
    seconds = std::clamp(seconds, 0.5, 60.0);
    if (std::fabs(seconds - s_.animation.duration) < 1e-6) return;
    pushUndo();
    s_.animation.duration = seconds;
    touch(false, true);
}

void ShaderStudio::setPlaybackSpeed(double speed) {
    s_.animation.speed = std::clamp(speed, 0.05, 4.0);
    touch(false, true);
}

QString ShaderStudio::loopMode() const {
    switch (s_.animation.loop) {
        case sh::LoopMode::Loop: return QStringLiteral("loop");
        case sh::LoopMode::PingPong: return QStringLiteral("pingpong");
        case sh::LoopMode::Once: return QStringLiteral("once");
    }
    return QStringLiteral("loop");
}

void ShaderStudio::setLoopMode(const QString& mode) {
    const sh::LoopMode m = mode == QLatin1String("pingpong") ? sh::LoopMode::PingPong : mode == QLatin1String("once") ? sh::LoopMode::Once : sh::LoopMode::Loop;
    if (m == s_.animation.loop) return;
    s_.animation.loop = m;
    touch(false, true);
}

void ShaderStudio::play() {
    if (s_.animation.loop == sh::LoopMode::Once && playhead_ >= s_.animation.duration) playhead_ = 0;
    playing_ = true;
    emit playingChanged();
}

void ShaderStudio::pause() {
    playing_ = false;
    emit playingChanged();
}

void ShaderStudio::stop() {
    playing_ = false;
    playhead_ = 0;
    emit playingChanged();
    ++previewRevision_;
    emit playheadChanged();
    emit previewChanged();
}

void ShaderStudio::tick() {
    const qint64 now = clock_.elapsed();
    const double dt = std::min(0.25, static_cast<double>(now - lastTick_) / 1000.0);
    lastTick_ = now;
    bool changed = false;
    if (playing_) {
        playhead_ += dt * s_.animation.speed;
        if (s_.animation.loop == sh::LoopMode::Once && playhead_ >= s_.animation.duration) {
            playhead_ = s_.animation.duration;
            playing_ = false;
            emit playingChanged();
        }
        changed = true;
    }
    if (hitActive_) {
        if (hitClock_.elapsed() > 6000) hitActive_ = false;
        changed = true;
    }
    if (changed) {
        ++previewRevision_;
        emit playheadChanged();
        emit previewChanged();
    }
}

void ShaderStudio::triggerHit() {
    hitClock_.start();
    hitActive_ = true;
    if (!playing_) {
        ++previewRevision_;
        emit previewChanged();
    }
}

bool ShaderStudio::hasHit() const { return s_.family == "shield"; }

void ShaderStudio::toggleKey(int index) {
    const sh::Family* f = family();
    if (!f || index < 0 || index >= static_cast<int>(f->params.size())) return;
    const sh::ParamDef& p = f->params[static_cast<std::size_t>(index)];
    if (p.kind != sh::ParamKind::Number) return;
    pushUndo();
    const double now = sh::animationTime(s_.animation, playhead_);
    for (sh::Track& t : s_.animation.tracks) {
        if (t.param != p.key) continue;
        for (std::size_t i = 0; i < t.keys.size(); ++i) {
            if (std::fabs(t.keys[i].time - now) < 0.04) {
                t.keys.erase(t.keys.begin() + static_cast<std::ptrdiff_t>(i));
                if (t.keys.empty()) {
                    s_.animation.tracks.erase(std::remove_if(s_.animation.tracks.begin(), s_.animation.tracks.end(),
                                                             [&](const sh::Track& x) { return x.keys.empty(); }),
                                              s_.animation.tracks.end());
                }
                touch(false, true);
                return;
            }
        }
    }
    putKey(s_.animation, p.key, now, static_cast<float>(number(index)), sh::Easing::Smooth);
    touch(false, true);
}

void ShaderStudio::removeTrack(int index) {
    const sh::Family* f = family();
    if (!f || index < 0 || index >= static_cast<int>(f->params.size())) return;
    pushUndo();
    const std::string key = f->params[static_cast<std::size_t>(index)].key;
    auto& tracks = s_.animation.tracks;
    tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [&](const sh::Track& t) { return t.param == key; }), tracks.end());
    touch(false, true);
}

QVariantList ShaderStudio::tracks() const {
    QVariantList out;
    const sh::Family* f = family();
    if (!f) return out;
    for (const sh::Track& t : s_.animation.tracks) {
        const sh::ParamDef* p = sh::findParam(*f, t.param);
        if (!p) continue;
        QVariantMap m;
        m[QStringLiteral("index")] = p->index;
        QString label = text(p->label);
        if (isLayers() && p->key.size() > 3 && p->key[0] == 'L' && p->key[2] == '_') {
            // "Ring · Size": which layer a track belongs to.
            const int slot = p->key[1] - '0';
            label = text(sh::layerName(s_, slot)) + QStringLiteral(" · ") + label;
        }
        m[QStringLiteral("label")] = label;
        QVariantList keys;
        for (const sh::Key& k : t.keys) {
            QVariantMap km;
            km[QStringLiteral("time")] = k.time;
            km[QStringLiteral("value")] = static_cast<double>(k.value);
            km[QStringLiteral("easing")] = QString::fromLatin1(easingName(k.easing));
            keys << km;
        }
        m[QStringLiteral("keys")] = keys;
        out << m;
    }
    return out;
}

void ShaderStudio::setKeyEasing(int index, int key, const QString& easing) {
    const sh::Family* f = family();
    if (!f || index < 0 || index >= static_cast<int>(f->params.size())) return;
    for (sh::Track& t : s_.animation.tracks) {
        if (t.param != f->params[static_cast<std::size_t>(index)].key) continue;
        if (key < 0 || key >= static_cast<int>(t.keys.size())) return;
        pushUndo();
        t.keys[static_cast<std::size_t>(key)].easing = easingFrom(easing);
        touch(false, true);
        return;
    }
}

void ShaderStudio::removeKey(int index, int key) {
    const sh::Family* f = family();
    if (!f || index < 0 || index >= static_cast<int>(f->params.size())) return;
    for (sh::Track& t : s_.animation.tracks) {
        if (t.param != f->params[static_cast<std::size_t>(index)].key) continue;
        if (key < 0 || key >= static_cast<int>(t.keys.size())) return;
        pushUndo();
        t.keys.erase(t.keys.begin() + key);
        s_.animation.tracks.erase(std::remove_if(s_.animation.tracks.begin(), s_.animation.tracks.end(), [](const sh::Track& x) { return x.keys.empty(); }),
                                  s_.animation.tracks.end());
        touch(false, true);
        return;
    }
}

void ShaderStudio::moveKey(int index, int key, double time) {
    const sh::Family* f = family();
    if (!f || index < 0 || index >= static_cast<int>(f->params.size())) return;
    for (sh::Track& t : s_.animation.tracks) {
        if (t.param != f->params[static_cast<std::size_t>(index)].key) continue;
        if (key < 0 || key >= static_cast<int>(t.keys.size())) return;
        t.keys[static_cast<std::size_t>(key)].time = std::clamp(time, 0.0, s_.animation.duration);
        std::sort(t.keys.begin(), t.keys.end(), [](const sh::Key& x, const sh::Key& y) { return x.time < y.time; });
        touch(false, true);
        return;
    }
}

// The animation modes are recipes on the shared parameters: no preset has its own animation code.
QStringList ShaderStudio::animationModes() const {
    return {QStringLiteral("Pulse"),   QStringLiteral("Flow"),   QStringLiteral("Rotate"), QStringLiteral("Scroll"),   QStringLiteral("Dissolve"),
            QStringLiteral("Expand"),  QStringLiteral("Flicker"), QStringLiteral("Hit reaction"), QStringLiteral("Keyframes"), QStringLiteral("Noise")};
}

QString ShaderStudio::animationModeTip(const QString& mode) const {
    if (mode == QLatin1String("Pulse")) return QStringLiteral("Makes the glow swell and fade.");
    if (mode == QLatin1String("Flow")) return QStringLiteral("Makes the pattern flow.");
    if (mode == QLatin1String("Rotate")) return QStringLiteral("Turns the pattern round the object.");
    if (mode == QLatin1String("Scroll")) return QStringLiteral("Scrolls the pattern: scanlines, hexagons or the UI colours, whichever this shader has.");
    if (mode == QLatin1String("Dissolve")) return QStringLiteral("Keyframes Reveal so the shader dissolves away and back.");
    if (mode == QLatin1String("Expand")) return QStringLiteral("Keyframes Reveal so the shader grows in and stays.");
    if (mode == QLatin1String("Flicker")) return QStringLiteral("Irregular flickering of the light.");
    if (mode == QLatin1String("Hit reaction")) return QStringLiteral("Shield: sends an impact now. Others: keyframes a quick pulse spike.");
    if (mode == QLatin1String("Keyframes")) return QStringLiteral("Adds keys that swell the main glow up and down over the timeline.");
    if (mode == QLatin1String("Noise")) return QStringLiteral("More turbulence and random variation in the pattern.");
    return {};
}

void ShaderStudio::applyAnimationMode(const QString& mode) {
    const sh::Family* f = family();
    if (!f) return;
    const double d = s_.animation.duration;
    auto setIf = [&](const char* key, double v) {
        const sh::ParamDef* p = sh::findParam(*f, key);
        if (p && p->kind == sh::ParamKind::Number) s_.values[static_cast<std::size_t>(p->index)][0] = clampTo(*p, v);
    };
    auto keyIf = [&](const char* key, double t, double v, sh::Easing e) {
        const sh::ParamDef* p = sh::findParam(*f, key);
        if (p && p->kind == sh::ParamKind::Number) putKey(s_.animation, p->key, t, clampTo(*p, v), e);
    };
    auto clearTrack = [&](const char* key) {
        auto& tr = s_.animation.tracks;
        tr.erase(std::remove_if(tr.begin(), tr.end(), [&](const sh::Track& t) { return t.param == key; }), tr.end());
    };
    if (mode == QLatin1String("Hit reaction") && s_.family == "shield") {
        triggerHit();
        say(QStringLiteral("Sent an impact. The shield also repeats a test impact every few seconds (Test impact every)."));
        return;
    }
    pushUndo();
    if (mode == QLatin1String("Pulse")) {
        setIf("pulseAmp", 0.5);
        setIf("pulseFreq", 1.5);
    } else if (mode == QLatin1String("Flow")) {
        setIf("flowSpeed", 1.0);
    } else if (mode == QLatin1String("Rotate")) {
        setIf("rotateSpeed", 0.8);
    } else if (mode == QLatin1String("Scroll")) {
        setIf("flowSpeed", -1.2);
        setIf("scanSpeed", 2.0);
        setIf("hexScroll", 0.8);
        setIf("scrollU", 0.4);
    } else if (mode == QLatin1String("Dissolve")) {
        clearTrack("reveal");
        keyIf("reveal", 0, 1, sh::Easing::Smooth);
        keyIf("reveal", d * 0.5, 0, sh::Easing::Smooth);
        keyIf("reveal", d, 1, sh::Easing::Smooth);
    } else if (mode == QLatin1String("Expand")) {
        clearTrack("reveal");
        keyIf("reveal", 0, 0, sh::Easing::EaseOut);
        keyIf("reveal", d * 0.45, 1, sh::Easing::Linear);
        keyIf("reveal", d, 1, sh::Easing::Linear);
    } else if (mode == QLatin1String("Flicker")) {
        setIf("flicker", 0.5);
    } else if (mode == QLatin1String("Hit reaction")) {
        clearTrack("pulseAmp");
        keyIf("pulseAmp", 0, 0.2, sh::Easing::Smooth);
        keyIf("pulseAmp", d * 0.06, 1.0, sh::Easing::EaseOut);
        keyIf("pulseAmp", d * 0.4, 0.2, sh::Easing::Smooth);
    } else if (mode == QLatin1String("Keyframes")) {
        const char* names[] = {"intensity", "glowIntensity", "edgeHighlight"};
        for (const char* n : names) {
            const sh::ParamDef* p = sh::findParam(*f, n);
            if (!p) continue;
            const double base = s_.values[static_cast<std::size_t>(p->index)][0];
            clearTrack(n);
            keyIf(n, 0, base * 0.6, sh::Easing::Smooth);
            keyIf(n, d * 0.5, base * 1.4, sh::Easing::Smooth);
            keyIf(n, d, base * 0.6, sh::Easing::Smooth);
            break;
        }
    } else if (mode == QLatin1String("Noise")) {
        setIf("noiseAmt", 0.8);
        setIf("distortion", 0.6);
        setIf("turbulence", 1.2);
    } else {
        return;
    }
    touch(false, true);
    say(QStringLiteral("%1: %2").arg(mode, animationModeTip(mode)));
}

// ---------------------------------------------------------------- the file and Unity

QString ShaderStudio::fileName() const { return file_.isEmpty() ? QString() : QFileInfo(file_).fileName(); }

QString ShaderStudio::lastUnityProject() const { return QSettings().value(QStringLiteral("export/unityProject")).toString(); }

void ShaderStudio::newShader() {
    const sh::Family* f = family();
    if (!f) return;
    pushUndo();
    sh::Settings next = sh::defaultSettings(*f);
    next.quality = s_.quality;
    file_.clear();
    playhead_ = 0;
    adopt(next, false, true);
    emit fileChanged();
    emit playheadChanged();
    say(QStringLiteral("A new %1 shader with the family's default values.").arg(text(f->name)));
}

void ShaderStudio::duplicate() {
    pushUndo();
    s_.name += " copy";
    file_.clear();
    touch();
    emit fileChanged();
    say(QStringLiteral("Duplicated. Save it to keep the copy."));
}

bool ShaderStudio::openFile(const QUrl& file) {
    const QString path = file.toLocalFile();
    auto bytes = vfx::readFile(toPath(path));
    if (!bytes) {
        say(text(bytes.error().message), true);
        return false;
    }
    auto read = sh::readSettings(bytes.value());
    if (!read) {
        say(text(read.error().message), true);
        return false;
    }
    pushUndo();
    const bool structure = read.value().settings.family != s_.family;
    file_ = path;
    playhead_ = 0;
    adopt(read.value().settings, structure, true);
    emit fileChanged();
    emit playheadChanged();
    if (!read.value().notes.empty()) {
        QStringList lines;
        for (const std::string& n : read.value().notes) lines << text(n);
        setReport(QStringLiteral("Opened with changes"), lines);
    }
    say(QStringLiteral("Opened %1.").arg(QFileInfo(path).fileName()));
    return true;
}

bool ShaderStudio::saveAs(const QUrl& file) {
    QString path = file.toLocalFile();
    if (path.isEmpty()) {
        say(QStringLiteral("That is not a place on this computer."), true);
        return false;
    }
    if (!path.endsWith(QStringLiteral(".vfxshader"), Qt::CaseInsensitive)) path += QStringLiteral(".vfxshader");
    const auto saved = vfx::writeFileAtomic(toPath(path), sh::writeSettings(s_));
    if (!saved) {
        say(text(saved.error().message), true);
        return false;
    }
    file_ = path;
    dirty_ = false;
    ++revision_;
    emit changed();
    emit fileChanged();
    say(QStringLiteral("Saved %1.").arg(QFileInfo(path).fileName()));
    return true;
}

bool ShaderStudio::save() {
    if (file_.isEmpty()) return false;
    return saveAs(QUrl::fromLocalFile(file_));
}

void ShaderStudio::validate() {
    QStringList lines;
    const std::vector<std::string> notes = sh::validateSettings(s_);
    for (const std::string& n : notes) lines << text(n);
    for (const std::string& n : family()->limits) lines << text(n);
    lines << QStringLiteral("The preview runs the shader's shared source on the CPU. It shows how the shader looks, not how fast it runs on a phone or console: measure that in Unity on the target device.");
    setReport(QStringLiteral("Check before exporting"), lines);
}

bool ShaderStudio::exportToUnity(const QUrl& folder) {
    const QString path = folder.toLocalFile();
    if (path.isEmpty()) {
        say(QStringLiteral("That is not a folder on this computer."), true);
        return false;
    }
    std::filesystem::path at = toPath(QFileInfo(path).absoluteFilePath());
    std::filesystem::path project;
    for (std::filesystem::path p = at; !p.empty(); p = p.parent_path()) {
        if (vfx::editor::isUnityProject(p)) {
            project = p;
            break;
        }
        if (p == p.parent_path()) break;
    }
    if (project.empty()) {
        say(QStringLiteral("That folder is not inside a Unity project. Choose the project's folder: the one that holds Assets and ProjectSettings."), true);
        return false;
    }
    const std::filesystem::path root = project / "Assets" / "VFXForge";
    // If the project already has the Shader Animator script, its own GUID is the one to point the prefab at.
    const std::filesystem::path animatorMeta = root / "Runtime" / "VFXForgeShaderAnimator.cs.meta";
    std::string animatorGuid;
    if (auto old = vfx::readFile(animatorMeta)) {
        const std::string& t = old.value();
        const auto at = t.find("guid: ");
        if (at != std::string::npos && t.size() >= at + 6 + 32) animatorGuid = t.substr(at + 6, 32);
    }
    auto made = sh::exportShader(s_, animatorGuid);
    if (!made) {
        say(text(made.error().message), true);
        return false;
    }
    for (const vfx::editor::UnityFile& f : made.value().files) {
        const std::filesystem::path target = root / vfx::pathFromUtf8(f.path);
        std::error_code exists;
        if (f.path == "Runtime/VFXForgeShaderAnimator.cs.meta" && std::filesystem::exists(target, exists)) continue;  // keep the project's own GUID
        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
        const auto written = vfx::writeFileAtomic(target, f.text);
        if (!written) {
            say(text(written.error().message), true);
            return false;
        }
    }
    const QString projectPath = text(vfx::pathToUtf8(project));
    QSettings().setValue(QStringLiteral("export/unityProject"), projectPath);
    QStringList lines;
    lines << QStringLiteral("Written to Assets/VFXForge/%1 in %2.").arg(text(made.value().folder), QFileInfo(projectPath).fileName());
    lines << QStringLiteral("Drag %1.prefab into a scene, or use %1.mat on a mesh, Particle System or UI Image. The Shader Animator plays the timeline and gameplay events.").arg(text(made.value().name));
    lines << QStringLiteral("Not opened in Unity by VFX Forge: if Unity shows a shader error, the message is what is needed to fix it.");
    for (const std::string& n : made.value().notes) lines << text(n);
    setReport(QStringLiteral("Exported to Unity"), lines);
    say(QStringLiteral("Exported %1 to %2.").arg(text(made.value().name), QFileInfo(projectPath).fileName()));
    emit statusChanged();
    return true;
}

ShaderFrame ShaderStudio::frame() const {
    ShaderFrame f;
    f.settings = s_;
    f.compare = compareOn_ && pinned_ != nullptr;
    if (pinned_) f.other = *pinned_;
    f.options.time = playhead_;
    f.options.yaw = static_cast<float>(yaw_);
    f.options.pitch = static_cast<float>(pitch_);
    f.options.zoom = static_cast<float>(zoom_);
    f.options.background = background_;
    f.options.bloom = bloom_;
    f.options.shape = s_.shape;
    f.options.hitAge = hitActive_ ? static_cast<double>(hitClock_.elapsed()) / 1000.0 : -1.0;
    f.model = model_;
    return f;
}

// ============================================================================ the preview item

ShaderPreviewItem::ShaderPreviewItem(QQuickItem* parent) : QQuickPaintedItem(parent) {}

void ShaderPreviewItem::setStudio(ShaderStudio* studio) {
    if (studio == studio_) return;
    if (studio_) disconnect(studio_, nullptr, this, nullptr);
    studio_ = studio;
    if (studio_) connect(studio_, &ShaderStudio::previewChanged, this, [this] { schedule(); });
    if (studio_) connect(studio_, &ShaderStudio::playingChanged, this, [this] { schedule(); });
    emit studioChanged();
    schedule();
}

void ShaderPreviewItem::setMaxSize(int size) {
    maxSize_ = std::clamp(size, 128, 1024);
    emit studioChanged();
}

void ShaderPreviewItem::geometryChange(const QRectF& next, const QRectF& previous) {
    QQuickPaintedItem::geometryChange(next, previous);
    schedule();
}

void ShaderPreviewItem::schedule() {
    if (!studio_ || width() < 8 || height() < 8) return;
    if (busy_) {
        again_ = true;
        return;
    }
    busy_ = true;
    again_ = false;
    // A still picture goes straight to the large size (it steps back down if drawing is slow).
    if (studio_ && !studio_->playing() && side_ < 800) side_ = std::min(std::max(maxSize_, 900), 800);
    const double aspect = width() / height();
    int w = aspect >= 1 ? side_ : static_cast<int>(std::lround(side_ * aspect));
    int h = aspect >= 1 ? static_cast<int>(std::lround(side_ / aspect)) : side_;
    w = std::clamp(w, 64, 1024);
    h = std::clamp(h, 64, 1024);
    const ShaderFrame frame = studio_->frame();
    QPointer<ShaderPreviewItem> self(this);
    QThreadPool::globalInstance()->start(QRunnable::create([self, frame, w, h] {
        double ms = 0;
        QImage image = drawFrame(frame, w, h, &ms);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, image, ms] {
                if (!self) return;
                self->image_ = image;
                self->busy_ = false;
                if (self->studio_) self->studio_->reportRenderMs(ms);
                // Keep the picture size to what the computer can draw about fifteen times a second.
                // Playing, favour speed; paused, a still picture can afford more detail so edges stay smooth.
                const bool still = self->studio_ && !self->studio_->playing();
                const int cap = still ? std::max(self->maxSize_, 900) : self->maxSize_;
                const double slow = still ? 220 : 80, fast = still ? 90 : 30;
                if (ms > slow && self->side_ > 160) self->side_ = std::max(160, static_cast<int>(self->side_ * 0.85));
                else if (ms < fast && self->side_ < cap) self->side_ = std::min(cap, static_cast<int>(self->side_ * 1.12) + 1);
                else if (!still && self->side_ > self->maxSize_) self->side_ = self->maxSize_;
                self->update();
                if (self->again_) self->schedule();
            },
            Qt::QueuedConnection);
    }));
}

void ShaderPreviewItem::paint(QPainter* painter) {
    painter->fillRect(boundingRect(), QColor(11, 12, 15));
    if (image_.isNull()) return;
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->drawImage(boundingRect(), image_);
}

// ============================================================================ the library thumbnails

ShaderThumb::ShaderThumb(QQuickItem* parent) : QQuickPaintedItem(parent) {}

void ShaderThumb::setFamily(const QString& family) {
    if (family == family_) return;
    family_ = family;
    load();
    emit changed();
}

void ShaderThumb::setPreset(const QString& preset) {
    if (preset == preset_) return;
    preset_ = preset;
    load();
    emit changed();
}

void ShaderThumb::load() {
    static QHash<QString, QImage> cache;
    const QString key = family_ + QLatin1Char('/') + preset_;
    if (family_.isEmpty() || preset_.isEmpty()) return;
    auto it = cache.constFind(key);
    if (it == cache.constEnd()) {
        QImage image;
        const sh::Family* f = sh::findFamily(utf8(family_));
        const sh::Preset* p = f ? sh::findPreset(*f, utf8(preset_)) : nullptr;
        if (f && p) {
            sh::Settings s = sh::presetSettings(*f, *p);
            s.quality = 0;
            sh::PreviewOptions o;
            o.width = o.height = 72;
            o.time = 1.2;
            image = toImage(sh::renderPreview(s, o));
        }
        it = cache.insert(key, image);
    }
    image_ = it.value();
    update();
}

void ShaderThumb::paint(QPainter* painter) {
    painter->fillRect(boundingRect(), QColor(11, 12, 15));
    if (!image_.isNull()) {
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawImage(boundingRect(), image_);
    }
}
