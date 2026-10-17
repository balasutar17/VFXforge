#include "AppController.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QCoreApplication>
#include <QImage>
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QSettings>
#include <QVariantMap>

#include "vfx/Command.h"
#include "vfx/FileIO.h"
#include "vfx/Id.h"
#include "vfx/Path.h"
#include "vfx/Metadata.h"
#include "vfx/editor/CurveTools.h"
#include "vfx/editor/Frames.h"
#include "vfx/editor/Audio.h"
#include "vfx/editor/Image.h"
#include "vfx/editor/SoundLibrary.h"
#include "vfx/editor/Presets.h"
#include "vfx/editor/Refine.h"
#include "vfx/editor/Selection.h"
#include "vfx/editor/ShapeLibrary.h"
#include "vfx/editor/UnityExport.h"

using vfx::editor::ControlView;

namespace {

QString text(const std::string& s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }

std::string utf8(const QString& s) { return s.toUtf8().toStdString(); }

std::filesystem::path toPath(const QString& localFile) { return vfx::pathFromUtf8(utf8(localFile)); }

// The words shown for one choice of a property. Files store short lower-case
// names; artists see these.
QString optionLabel(const vfx::PropertyDesc& desc, const std::string& option) {
    if (desc.key == "blend") {
        return option == "additive" ? QStringLiteral("Glow") : QStringLiteral("Normal");
    }
    QString label = text(option);
    if (!label.isEmpty()) {
        label[0] = label[0].toUpper();
    }
    return label;
}

// ---- curves, gradients and bursts, to and from the window's lists

QVariantList keysToList(const std::vector<vfx::CurveKey>& keys) {
    QVariantList list;
    for (const vfx::CurveKey& k : keys) {
        QVariantMap point;
        point.insert(QStringLiteral("t"), k.t);
        point.insert(QStringLiteral("v"), k.v);
        point.insert(QStringLiteral("smooth"), k.smooth);
        list.push_back(point);
    }
    return list;
}

std::vector<vfx::CurveKey> keysFromList(const QVariantList& list) {
    std::vector<vfx::CurveKey> keys;
    for (const QVariant& item : list) {
        const QVariantMap point = item.toMap();
        keys.push_back(vfx::CurveKey{point.value(QStringLiteral("t")).toDouble(),
                                     point.value(QStringLiteral("v")).toDouble(),
                                     point.value(QStringLiteral("smooth")).toBool()});
    }
    return keys;
}

// A linear colour as the gradient editor shows it: a screen colour, and how
// many times brighter than that the real one is.
QVariantMap stopToMap(double t, const vfx::Color& c) {
    const double most = std::max({c.r, c.g, c.b, 1.0});
    QColor shown;
    shown.setRgbF(static_cast<float>(std::clamp(vfx::editor::linearToSrgb(c.r / most), 0.0, 1.0)),
                  static_cast<float>(std::clamp(vfx::editor::linearToSrgb(c.g / most), 0.0, 1.0)),
                  static_cast<float>(std::clamp(vfx::editor::linearToSrgb(c.b / most), 0.0, 1.0)), 1.0f);
    QVariantMap stop;
    stop.insert(QStringLiteral("t"), t);
    stop.insert(QStringLiteral("color"), shown);
    stop.insert(QStringLiteral("bright"), most);
    stop.insert(QStringLiteral("alpha"), std::clamp(c.a, 0.0, 1.0));
    return stop;
}

vfx::Gradient gradientFromList(const QVariantList& list) {
    vfx::Gradient gradient;
    for (const QVariant& item : list) {
        const QVariantMap stop = item.toMap();
        const QColor shown = stop.value(QStringLiteral("color")).value<QColor>();
        double bright = stop.value(QStringLiteral("bright"), 1.0).toDouble();
        bright = std::isfinite(bright) ? std::clamp(bright, 1.0, 1000.0) : 1.0;
        vfx::GradientKey key;
        key.t = stop.value(QStringLiteral("t")).toDouble();
        if (shown.isValid()) {
            key.color.r = vfx::editor::srgbToLinear(static_cast<double>(shown.redF())) * bright;
            key.color.g = vfx::editor::srgbToLinear(static_cast<double>(shown.greenF())) * bright;
            key.color.b = vfx::editor::srgbToLinear(static_cast<double>(shown.blueF())) * bright;
        }
        key.color.a = stop.value(QStringLiteral("alpha"), 1.0).toDouble();
        gradient.keys.push_back(key);
    }
    return vfx::editor::tidyGradient(std::move(gradient));
}

QVariantList gradientToList(const vfx::Gradient& gradient) {
    QVariantList list;
    for (const vfx::GradientKey& k : gradient.keys) {
        list.push_back(stopToMap(k.t, k.color));
    }
    return list;
}

QVariantList burstsToList(const vfx::BurstList& bursts) {
    QVariantList list;
    for (const vfx::Burst& b : bursts.items) {
        QVariantMap item;
        item.insert(QStringLiteral("time"), b.time);
        item.insert(QStringLiteral("count"), static_cast<double>(b.count));
        list.push_back(item);
    }
    return list;
}

// What the three numbers of a vector setting are called.
QStringList vectorLabels(const ControlView& c) {
    if (c.moduleType == "shape" && c.desc->key == "size") {
        return {QStringLiteral("Width"), QStringLiteral("Height"), QStringLiteral("Depth")};
    }
    return {QStringLiteral("Across"), QStringLiteral("Up"), QStringLiteral("Toward you")};
}

// Settings the Advanced list leaves out because a richer control already
// looks after them: the picture and its sprite-sheet grid belong to the
// Shape control, and the whole Sound module to the Sound control.
bool handledElsewhere(const ControlView& c) {
    if (c.moduleType == "sound") {
        return true;
    }
    if (c.moduleType == "sprite") {
        for (const char* key : {"texture", "columns", "rows", "frames", "animate", "fps", "randomStart"}) {
            if (c.desc->key == key) {
                return true;
            }
        }
    }
    return c.desc->kind == vfx::ValueKind::Asset || c.desc->kind == vfx::ValueKind::Points;
}

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
    audio_ = new AudioPlayer(this);
    soundOn_ = QSettings().value(QStringLiteral("sound/on"), true).toBool();
    rebuildLayers();
    rebuildControls();
    loadBackdrop();
    say(QStringLiteral("Ready. Pick an effect from the library, or shape this one with the controls on the right."));
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
        row.insert(QStringLiteral("locked"), layer.locked);
        row.insert(QStringLiteral("role"), text(layer.role));
        row.insert(QStringLiteral("id"), text(vfx::formatId('l', layer.id)));
        row.insert(QStringLiteral("start"), layer.start);
        row.insert(QStringLiteral("group"), text(layer.group));
        list.push_back(row);
    }
    // Each layer of a group knows whether it is the first of its run (the
    // list shows the group's heading above it) and how many there are.
    for (int i = 0; i < list.size(); ++i) {
        QVariantMap row = list[i].toMap();
        const QString group = row.value(QStringLiteral("group")).toString();
        const bool first = !group.isEmpty() &&
                           (i == 0 || list[i - 1].toMap().value(QStringLiteral("group")).toString() != group);
        int count = 0, shown = 0;
        double start = 1e9;
        for (const vfx::Layer& layer : session_.effect().layers) {
            if (text(layer.group) == group) {
                ++count;
                shown += layer.enabled ? 1 : 0;
                start = std::min(start, layer.start);
            }
        }
        row.insert(QStringLiteral("groupFirst"), first);
        row.insert(QStringLiteral("groupCount"), group.isEmpty() ? 0 : count);
        row.insert(QStringLiteral("groupShown"), shown > 0);
        row.insert(QStringLiteral("groupStart"), group.isEmpty() ? 0.0 : start);
        list[i] = row;
    }
    layers_ = std::move(list);

    const int count = static_cast<int>(session_.effect().layers.size());
    selected_ = count == 0 ? -1 : std::clamp(selected_, 0, count - 1);
    syncSelection();

    // The list as shown: a heading for each group as its layers begin,
    // nested ones indented under their parents; the layers inside a folded
    // group are left out.
    const vfx::Effect& e = session_.effect();
    const vfx::editor::ResolvedSelection chosen = selectedNothing_ ? vfx::editor::ResolvedSelection{}
                                                                    : session_.resolve(selection_);
    auto isChosen = [&](vfx::Id id) {
        return std::find(chosen.targets.begin(), chosen.targets.end(), id) != chosen.targets.end() ||
               std::find(chosen.locked.begin(), chosen.locked.end(), id) != chosen.locked.end();
    };
    auto hiddenBy = [&](const std::string& path) {
        // Inside a folded group (not counting the group itself).
        for (std::string p = vfx::editor::parentGroup(path); !p.empty(); p = vfx::editor::parentGroup(p)) {
            if (folded_.contains(text(p))) {
                return true;
            }
        }
        return false;
    };
    QVariantList rows;
    std::string open;  // the group path the previous layer was in
    for (std::size_t i = 0; i < e.layers.size(); ++i) {
        const vfx::Layer& layer = e.layers[i];
        // Headings for each step of this layer's path not already open.
        std::string shared = open;
        while (!shared.empty() && !vfx::editor::inGroup(layer, shared)) {
            shared = vfx::editor::parentGroup(shared);
        }
        std::vector<std::string> steps;
        for (std::string p = layer.group; p.size() > shared.size(); p = vfx::editor::parentGroup(p)) {
            steps.insert(steps.begin(), p);
        }
        for (const std::string& path : steps) {
            if (hiddenBy(path)) {
                continue;
            }
            int members = 0, shown = 0, locked = 0;
            double start = 1e9;
            for (const vfx::Layer& l : e.layers) {
                if (vfx::editor::inGroup(l, path)) {
                    ++members;
                    shown += l.enabled ? 1 : 0;
                    locked += l.locked ? 1 : 0;
                    start = std::min(start, l.start);
                }
            }
            QVariantMap row;
            row.insert(QStringLiteral("kind"), QStringLiteral("group"));
            row.insert(QStringLiteral("path"), text(path));
            row.insert(QStringLiteral("name"), text(vfx::editor::groupName(path)));
            row.insert(QStringLiteral("depth"), vfx::editor::groupDepth(path) - 1);
            row.insert(QStringLiteral("count"), members);
            row.insert(QStringLiteral("shown"), shown > 0);
            row.insert(QStringLiteral("locked"), locked == members);
            row.insert(QStringLiteral("start"), start);
            row.insert(QStringLiteral("folded"), folded_.contains(text(path)));
            row.insert(QStringLiteral("selected"), !selectedNothing_ && std::find(selection_.groups.begin(),
                                                                                  selection_.groups.end(), path) !=
                                                                            selection_.groups.end());
            rows.push_back(row);
        }
        open = layer.group;
        if (hiddenBy(layer.group + "/x") ) {
            continue;  // inside a folded group
        }
        QVariantMap row = layers_[static_cast<int>(i)].toMap();
        row.insert(QStringLiteral("kind"), QStringLiteral("layer"));
        row.insert(QStringLiteral("index"), static_cast<int>(i));
        row.insert(QStringLiteral("depth"), vfx::editor::groupDepth(layer.group));
        row.insert(QStringLiteral("selected"), !selectedNothing_ && isChosen(layer.id));
        row.insert(QStringLiteral("active"), static_cast<int>(i) == selected_ && !selectedNothing_);
        rows.push_back(row);
    }
    layerRows_ = std::move(rows);
}

std::vector<ControlView> AppController::views() const {
    const vfx::Id layer = selectedLayerId();
    if (!layer.valid()) {
        return {};
    }
    return advanced_ ? session_.properties(layer) : session_.controls(layer);
}

// Rebuilds the list of controls and says whether the panel should stay
// scrolled where it was: yes when it still shows the same layer in the same
// view (a value changed form, something was undone), no when it shows
// something else.
void AppController::refreshControls() {
    emit controlsAboutToChange();
    const vfx::Id layer = selectedLayerId();
    const bool same = layer.valid() && layer == shownLayer_ && advanced_ == shownAdvanced_;
    rebuildControls();
    shownLayer_ = layer;
    shownAdvanced_ = advanced_;
    emit controlsChanged();
    emit controlsPlace(same);
}

void AppController::setAdvanced(bool advanced) {
    if (advanced == advanced_) {
        return;
    }
    session_.endEdit();
    advanced_ = advanced;
    refreshControls();
}

namespace {

QVariantMap shapeRow(const vfx::editor::CurveShape& shape) {
    QVariantMap row;
    row.insert(QStringLiteral("key"), QString::fromLatin1(shape.key));
    row.insert(QStringLiteral("label"), QString::fromUtf8(shape.label));
    row.insert(QStringLiteral("group"), QString::fromUtf8(shape.group));
    row.insert(QStringLiteral("help"), QString::fromUtf8(shape.help));
    return row;
}

// The one-click changes the main window offers, by their keys in Refine.h.
const char* const kMotionChanges[] = {"on-twos", "smooth-timing", "squash-stretch",
                                      "anticipate", "overshoot", "curl", "smooth-loop"};

}  // namespace

// The shapes that suit any setting. (A curve's own row lists the ones
// offered for it, which may include some made for that setting alone.)
QVariantList AppController::curveShapes() const {
    QVariantList list;
    for (const vfx::editor::CurveShape& shape : vfx::editor::curveShapes()) {
        if (!shape.only) {
            list.push_back(shapeRow(shape));
        }
    }
    return list;
}

QVariantList AppController::motionChanges() const {
    QVariantList list;
    for (const char* key : kMotionChanges) {
        if (const vfx::editor::RefineInfo* info = vfx::editor::findRefinement(key)) {
            QVariantMap row;
            row.insert(QStringLiteral("key"), QString::fromLatin1(info->key));
            row.insert(QStringLiteral("label"), QString::fromUtf8(info->label));
            row.insert(QStringLiteral("help"), QString::fromUtf8(info->help));
            list.push_back(row);
        }
    }
    return list;
}

QString AppController::loopJump() const {
    const std::vector<std::string> names = vfx::editor::loopJumps(session_.effect());
    if (names.empty()) {
        return {};
    }
    QString list;
    for (std::size_t i = 0; i < names.size(); ++i) {
        list += (i == 0 ? QString() : i + 1 == names.size() ? QStringLiteral(" and ") : QStringLiteral(", ")) +
                QString::fromStdString(names[i]);
    }
    return list + (names.size() == 1 ? QStringLiteral(" jumps") : QStringLiteral(" jump")) +
           QStringLiteral(" back when the loop starts again: it turns part of a turn each loop. "
                          "Click to make it turn a whole number of times.");
}

void AppController::applyMotionChange(const QString& key) {
    const vfx::editor::RefineInfo* info = vfx::editor::findRefinement(utf8(key));
    if (!info) {
        return;
    }
    session_.endEdit();
    vfx::Document& document = session_.document();
    const vfx::IdSource newId = [&document]() { return document.newId(); };
    std::string said;
    const auto next = vfx::editor::refine(session_.effect(), info->id, nullptr, newId, &said);
    if (!next) {
        say(text(next.error().message), true);
        return;
    }
    // Nothing to do (every layer locked, or none of the kind it changes):
    // say so, and leave the undo history alone.
    if (vfx::writeEffect(next.value()) == vfx::writeEffect(session_.effect())) {
        say(text(said));
        return;
    }
    if (!report(session_.applyEffect(next.value(), info->label))) {
        return;
    }
    changed(Structure);
    emit playbackChanged();
    say(text(said));
}

void AppController::rebuildControls() {
    QVariantList list;
    const vfx::Id layer = selectedLayerId();
    if (layer.valid()) {
        const std::vector<ControlView> all = views();
        const vfx::Layer* selected = vfx::findLayer(session_.effect(), layer);
        std::string section;
        for (std::size_t at = 0; at < all.size(); ++at) {
            const ControlView& c = all[at];
            if (advanced_ && c.desc && handledElsewhere(c)) {
                continue;
            }
            if (advanced_ && c.section != section) {
                section = c.section;
                QVariantMap header;
                header.insert(QStringLiteral("kind"), QStringLiteral("section"));
                header.insert(QStringLiteral("label"), text(section));
                header.insert(QStringLiteral("section"), text(section));
                header.insert(QStringLiteral("control"), -1);
                list.push_back(header);
            }
            QVariantMap row;
            row.insert(QStringLiteral("label"), text(c.label));
            row.insert(QStringLiteral("control"), static_cast<int>(at));
            row.insert(QStringLiteral("section"), advanced_ ? text(c.section) : QString());
            QString kind = QStringLiteral("unsupported");
            if (c.desc) {
                const vfx::PropertyDesc& d = *c.desc;
                row.insert(QStringLiteral("unit"), text(d.unit));
                row.insert(QStringLiteral("help"), text(d.help));
                row.insert(QStringLiteral("uiMin"), d.uiMin.value_or(d.min.value_or(0.0)));
                row.insert(QStringLiteral("uiMax"), d.uiMax.value_or(d.max.value_or(1.0)));
                row.insert(QStringLiteral("hardMin"), d.min.value_or(-1e12));
                row.insert(QStringLiteral("hardMax"), d.max.value_or(1e12));
                row.insert(QStringLiteral("canBeCurve"), false);
                row.insert(QStringLiteral("whole"), false);

                if (const auto* number = std::get_if<double>(&c.value)) {
                    kind = QStringLiteral("number");
                    row.insert(QStringLiteral("value"), *number);
                    row.insert(QStringLiteral("canBeRandom"), false);
                } else if (const auto* whole = std::get_if<std::int64_t>(&c.value)) {
                    kind = QStringLiteral("number");
                    row.insert(QStringLiteral("value"), static_cast<double>(*whole));
                    row.insert(QStringLiteral("canBeRandom"), false);
                    row.insert(QStringLiteral("whole"), true);
                } else if (const auto* flag = std::get_if<bool>(&c.value)) {
                    kind = QStringLiteral("flag");
                    row.insert(QStringLiteral("value"), *flag);
                } else if (const auto* scalar = std::get_if<vfx::Scalar>(&c.value)) {
                    row.insert(QStringLiteral("canBeRandom"), true);
                    // Turning a number into a curve is an Advanced matter;
                    // a value that already is one can be edited anywhere.
                    row.insert(QStringLiteral("canBeCurve"), advanced_);
                    if (scalar->kind == vfx::Scalar::Kind::Constant) {
                        kind = QStringLiteral("number");
                        row.insert(QStringLiteral("value"), scalar->a);
                    } else if (scalar->kind == vfx::Scalar::Kind::Random) {
                        kind = QStringLiteral("range");
                        row.insert(QStringLiteral("from"), scalar->a);
                        row.insert(QStringLiteral("to"), scalar->b);
                    } else {
                        kind = QStringLiteral("curve");
                        row.insert(QStringLiteral("keys"), keysToList(scalar->keys));
                        // What the curve runs across: each particle's life,
                        // or the layer's own time.
                        const bool overLife =
                            c.moduleType == "overLife" ||
                            (c.moduleType == "motion" && (d.key == "spin" || d.key == "speedOverLife"));
                        row.insert(QStringLiteral("axis"), overLife ? QStringLiteral("life") : QStringLiteral("layer"));
                        // A setting that only runs from 0 to 1 is always
                        // drawn on that whole range.
                        row.insert(QStringLiteral("fixedRange"),
                                   d.min && d.max && *d.min == 0.0 && *d.max == 1.0);
                        // The ready-made shapes for this setting: its own
                        // first (a stretch that settles, a wind-up), then
                        // the ones that suit anything.
                        QVariantList shapes;
                        for (const vfx::editor::CurveShape* shape : vfx::editor::curveShapesFor(c.moduleType, d)) {
                            shapes.push_back(shapeRow(*shape));
                        }
                        row.insert(QStringLiteral("shapes"), shapes);
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
                } else if (const auto* gradient = std::get_if<vfx::Gradient>(&c.value)) {
                    kind = QStringLiteral("gradient");
                    row.insert(QStringLiteral("stops"), gradientToList(*gradient));
                } else if (const auto* bursts = std::get_if<vfx::BurstList>(&c.value)) {
                    kind = QStringLiteral("bursts");
                    row.insert(QStringLiteral("items"), burstsToList(*bursts));
                    row.insert(QStringLiteral("layerLength"), selected ? selected->duration : 0.0);
                } else if (const auto* choice = std::get_if<std::string>(&c.value);
                           choice && d.kind == vfx::ValueKind::Enum) {
                    // The particle's shape is picked from pictures; any other
                    // choice (where particles appear, how they blend) from words.
                    const bool picture = d.key == "shape" && c.moduleType == "sprite";
                    kind = picture ? QStringLiteral("shape") : QStringLiteral("choice");
                    if (picture) {
                        row.insert(QStringLiteral("picture"), pictureInfo());
                    }
                    row.insert(QStringLiteral("value"), text(*choice));
                    QVariantList options;
                    for (const std::string& option : d.options) {
                        QVariantMap entry;
                        entry.insert(QStringLiteral("value"), text(option));
                        entry.insert(QStringLiteral("label"), optionLabel(d, option));
                        if (picture) {
                            // There are many shapes: each has a name an
                            // artist would use, and a group it is found in.
                            const int number = vfx::editor::shapeNumber(option);
                            if (number >= 0) {
                                const vfx::editor::ShapeInfo& shape = vfx::editor::shapeInfo(number);
                                entry.insert(QStringLiteral("label"), QString::fromUtf8(shape.label));
                                entry.insert(QStringLiteral("category"), QString::fromUtf8(shape.category));
                                entry.insert(QStringLiteral("edits"), shapeEdits(number));
                            }
                        }
                        options.push_back(entry);
                    }
                    row.insert(QStringLiteral("options"), options);
                    if (picture) {
                        QVariantList categories;
                        for (const std::string& category : vfx::editor::shapeCategories()) {
                            categories.push_back(text(category));
                        }
                        row.insert(QStringLiteral("categories"), categories);
                    }
                } else if (const auto* vector = std::get_if<vfx::Vec3>(&c.value)) {
                    if (d.key == "direction") {
                        kind = QStringLiteral("direction");
                        const vfx::editor::Heading h = vfx::editor::headingFromDirection(*vector);
                        row.insert(QStringLiteral("heading"), h.heading);
                        row.insert(QStringLiteral("tilt"), h.tilt);
                    } else {
                        kind = QStringLiteral("vector");
                        row.insert(QStringLiteral("x"), vector->x);
                        row.insert(QStringLiteral("y"), vector->y);
                        row.insert(QStringLiteral("z"), vector->z);
                        row.insert(QStringLiteral("names"), vectorLabels(c));
                    }
                    row.insert(QStringLiteral("threeD"), threeD());
                }
            }
            row.insert(QStringLiteral("kind"), kind);
            list.push_back(row);
        }
        // The layer's sound, if it has one, or the offer of one.
        if (advanced_) {
            QVariantMap header;
            header.insert(QStringLiteral("kind"), QStringLiteral("section"));
            header.insert(QStringLiteral("label"), QStringLiteral("Sound"));
            header.insert(QStringLiteral("section"), QStringLiteral("Sound"));
            header.insert(QStringLiteral("control"), -1);
            list.push_back(header);
        }
        QVariantMap sound;
        sound.insert(QStringLiteral("kind"), QStringLiteral("sound"));
        sound.insert(QStringLiteral("label"), QStringLiteral("Sound"));
        sound.insert(QStringLiteral("sound"), soundInfo());
        sound.insert(QStringLiteral("control"), -1);
        sound.insert(QStringLiteral("section"), advanced_ ? QStringLiteral("Sound") : QString());
        list.push_back(sound);
    }
    controls_ = std::move(list);
}

// ------------------------------------------------------------- selection

// After anything that may have changed the layers: the selection keeps only
// what still exists, and follows the active layer when something else chose
// it (adding, removing, opening).
void AppController::syncSelection() {
    const vfx::Effect& e = session_.effect();
    const vfx::Id active = selectedLayerId();
    std::vector<vfx::Id> kept;
    for (const vfx::Id id : selection_.layers) {
        if (vfx::findLayer(e, id)) {
            kept.push_back(id);
        }
    }
    selection_.layers = kept;
    const std::vector<std::string> paths = vfx::editor::groupPaths(e);
    std::vector<std::string> keptGroups;
    for (const std::string& g : selection_.groups) {
        if (std::find(paths.begin(), paths.end(), g) != paths.end()) {
            keptGroups.push_back(g);
        }
    }
    selection_.groups = keptGroups;
    if (active != selectionFor_) {
        // The active layer was changed from elsewhere: select it alone.
        selection_ = {};
        if (active.valid()) {
            selection_.layers.push_back(active);
        }
        selectedNothing_ = false;
        pivotShift_ = {};
    }
    selection_.active = active;
    selectionFor_ = active;
    if (selection_.empty() && active.valid() && !selectedNothing_) {
        selection_.layers.push_back(active);
    }
}

// After a click changed the selection.
void AppController::selectionEdited() {
    selectionFor_ = selectedLayerId();
    selection_.active = selectionFor_;
    pivotShift_ = {};
    session_.endEdit();
    rebuildLayers();
    emit layersChanged();
    emit selectionChanged();
    emit gizmoChanged();
    refreshControls();
}

void AppController::clickLayer(int index, int modifiers) {
    const auto& layers = session_.effect().layers;
    if (index < 0 || index >= static_cast<int>(layers.size())) {
        return;
    }
    const vfx::Id id = layers[static_cast<std::size_t>(index)].id;
    const bool add = (modifiers & (int(Qt::ControlModifier) | int(Qt::MetaModifier))) != 0;
    const bool range = (modifiers & int(Qt::ShiftModifier)) != 0;
    selectedNothing_ = false;
    if (range && selected_ >= 0) {
        // Everything between the active layer and this one, in the list's order.
        const int from = std::min(selected_, index), to = std::max(selected_, index);
        if (!add) {
            selection_.layers.clear();
            selection_.groups.clear();
        }
        for (int i = from; i <= to; ++i) {
            const vfx::Id each = layers[static_cast<std::size_t>(i)].id;
            if (std::find(selection_.layers.begin(), selection_.layers.end(), each) == selection_.layers.end()) {
                selection_.layers.push_back(each);
            }
        }
    } else if (add) {
        const auto at = std::find(selection_.layers.begin(), selection_.layers.end(), id);
        if (at != selection_.layers.end()) {
            selection_.layers.erase(at);
            if (index == selected_ && !selection_.layers.empty()) {
                index = vfx::layerIndex(session_.effect(), selection_.layers.back());
            }
        } else {
            selection_.layers.push_back(id);
        }
    } else {
        selection_ = {};
        selection_.layers.push_back(id);
    }
    selected_ = index;
    selectionEdited();
}

void AppController::clickGroup(const QString& path, int modifiers) {
    const std::string g = utf8(path);
    const bool add = (modifiers & (int(Qt::ControlModifier) | int(Qt::MetaModifier))) != 0;
    if (add) {
        const auto at = std::find(selection_.groups.begin(), selection_.groups.end(), g);
        if (at != selection_.groups.end()) {
            selection_.groups.erase(at);
        } else {
            selection_.groups.push_back(g);
        }
    } else {
        selection_ = {};
        selection_.groups.push_back(g);
    }
    selectedNothing_ = false;
    // Its first layer is the active one, so its settings are shown.
    const auto& layers = session_.effect().layers;
    for (std::size_t i = 0; i < layers.size(); ++i) {
        if (vfx::editor::inGroup(layers[i], g)) {
            selected_ = static_cast<int>(i);
            break;
        }
    }
    selectionEdited();
}

void AppController::selectNone() {
    if (selection_.empty() && selectedNothing_) {
        return;
    }
    selection_ = {};
    selectedNothing_ = true;
    selectionEdited();
    say(QStringLiteral("Nothing selected: the handles are hidden. Click a layer to select it."));
}

void AppController::clickViewport(int layerIndex, int modifiers) {
    if (layerIndex < 0) {
        if ((modifiers & (int(Qt::ControlModifier) | int(Qt::MetaModifier) | int(Qt::ShiftModifier))) == 0) {
            selectNone();
        }
        return;
    }
    clickLayer(layerIndex, modifiers);
}

void AppController::toggleFold(const QString& path) {
    if (folded_.contains(path)) {
        folded_.remove(path);
    } else {
        folded_.insert(path);
    }
    rebuildLayers();
    emit layersChanged();
}

QString AppController::selectedGroup() const {
    return selection_.layers.empty() && selection_.groups.size() == 1 ? text(selection_.groups.front()) : QString();
}

int AppController::selectionCount() const {
    return static_cast<int>(session_.resolve(selection_).targets.size());
}

void AppController::groupSelection() {
    const vfx::editor::ResolvedSelection r = session_.resolve(selection_);
    std::vector<vfx::Id> ids = r.targets;
    ids.insert(ids.end(), r.locked.begin(), r.locked.end());
    if (ids.empty()) {
        say(QStringLiteral("Select the layers to put in a group first."), true);
        return;
    }
    std::string made;
    session_.endEdit();
    if (!report(session_.groupLayers(ids, "Group", &made))) {
        return;
    }
    selection_ = {};
    selection_.groups.push_back(made);
    const vfx::Layer* first = nullptr;
    for (const vfx::Layer& l : session_.effect().layers) {
        if (vfx::editor::inGroup(l, made)) {
            first = &l;
            break;
        }
    }
    selected_ = first ? vfx::layerIndex(session_.effect(), first->id) : selected_;
    selectionFor_ = selectedLayerId();
    changed(Structure);
    say(QStringLiteral("Made %1 layers into the group “%2”. Double-click its name to rename it.")
            .arg(static_cast<int>(ids.size()))
            .arg(text(vfx::editor::groupName(made))));
}

void AppController::ungroupSelection() {
    const QString g = selectedGroup();
    if (g.isEmpty()) {
        say(QStringLiteral("Select a group to ungroup it."), true);
        return;
    }
    ungroup(g);
}

// ------------------------------------------------------------- handles

void AppController::setGizmoMode(const QString& mode) {
    if (mode != gizmoMode_ && (mode == QStringLiteral("move") || mode == QStringLiteral("turn") ||
                               mode == QStringLiteral("size"))) {
        gizmoMode_ = mode;
        emit gizmoChanged();
    }
}

void AppController::setPivotMode(const QString& mode) {
    if (mode != pivotMode_ && (mode == QStringLiteral("center") || mode == QStringLiteral("active") ||
                                mode == QStringLiteral("individual"))) {
        pivotMode_ = mode;
        pivotShift_ = {};
        emit gizmoChanged();
    }
}

void AppController::setGizmoSpace(const QString& space) {
    if (space != gizmoSpace_ && (space == QStringLiteral("world") || space == QStringLiteral("local"))) {
        gizmoSpace_ = space;
        emit gizmoChanged();
    }
}

vfx::Vec3 AppController::currentPivot(const std::vector<vfx::Id>& targets) const {
    vfx::Vec3 p = session_.pivotOf(targets);
    if (pivotMode_ == QStringLiteral("active")) {
        const vfx::Id active = selectedLayerId();
        if (std::find(targets.begin(), targets.end(), active) != targets.end()) {
            p = session_.pivotOf({active});
        }
    }
    return vfx::Vec3{p.x + pivotShift_.x, p.y + pivotShift_.y, p.z + pivotShift_.z};
}

// The gizmo's three axes: the world's, or the active layer's own (its
// Direction is its up; right is across that in the picture).
std::vector<vfx::Vec3> AppController::gizmoAxes() const {
    std::vector<vfx::Vec3> world{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    if (gizmoSpace_ != QStringLiteral("local")) {
        return world;
    }
    const vfx::Layer* active = vfx::findLayer(session_.effect(), selectedLayerId());
    if (!active) {
        return world;
    }
    vfx::Vec3 up{0, 1, 0};
    for (const vfx::Module& m : active->modules) {
        if (m.type == "initial") {
            if (const vfx::Value* v = m.find("direction"); v && std::holds_alternative<vfx::Vec3>(*v)) {
                up = std::get<vfx::Vec3>(*v);
            }
        }
    }
    if (!threeD()) {
        up.z = 0;
    }
    const double len = std::sqrt(up.x * up.x + up.y * up.y + up.z * up.z);
    if (!(len > 1e-9)) {
        return world;
    }
    up = vfx::Vec3{up.x / len, up.y / len, up.z / len};
    // Right: across up and the way the picture faces (Z); when up is along Z, across X.
    vfx::Vec3 ref = std::fabs(up.z) > 0.99 ? vfx::Vec3{1, 0, 0} : vfx::Vec3{0, 0, 1};
    vfx::Vec3 right{up.y * ref.z - up.z * ref.y, up.z * ref.x - up.x * ref.z, up.x * ref.y - up.y * ref.x};
    const double rl = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
    right = vfx::Vec3{right.x / rl, right.y / rl, right.z / rl};
    const vfx::Vec3 front{right.y * up.z - right.z * up.y, right.z * up.x - right.x * up.z, right.x * up.y - right.y * up.x};
    return {right, up, front};
}

QVariantMap AppController::gizmo() const {
    QVariantMap g;
    const vfx::editor::ResolvedSelection r = selectedNothing_ ? vfx::editor::ResolvedSelection{}
                                                               : session_.resolve(selection_);
    // Every layer's emitter, for clicking on in the viewport.
    QVariantList markers;
    const auto& layers = session_.effect().layers;
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const vfx::Layer& l = layers[i];
        const vfx::Vec3 p = session_.pivotOf({l.id});
        QVariantMap m;
        m.insert(QStringLiteral("x"), p.x);
        m.insert(QStringLiteral("y"), p.y);
        m.insert(QStringLiteral("z"), p.z);
        m.insert(QStringLiteral("index"), static_cast<int>(i));
        m.insert(QStringLiteral("selected"),
                 std::find(r.targets.begin(), r.targets.end(), l.id) != r.targets.end() ||
                     std::find(r.locked.begin(), r.locked.end(), l.id) != r.locked.end());
        markers.push_back(m);
    }
    g.insert(QStringLiteral("markers"), markers);
    const bool lockedOnly = r.targets.empty() && !r.locked.empty();
    g.insert(QStringLiteral("visible"), !r.targets.empty() || lockedOnly);
    g.insert(QStringLiteral("locked"), lockedOnly);
    g.insert(QStringLiteral("lockedCount"), static_cast<int>(r.locked.size()));
    if (r.targets.empty() && !lockedOnly) {
        return g;
    }
    const vfx::Vec3 p = lockedOnly ? session_.pivotOf(r.locked) : currentPivot(r.targets);
    g.insert(QStringLiteral("x"), p.x);
    g.insert(QStringLiteral("y"), p.y);
    g.insert(QStringLiteral("z"), p.z);
    QVariantList axes;
    for (const vfx::Vec3& a : gizmoAxes()) {
        axes.push_back(QVariantList{a.x, a.y, a.z});
    }
    g.insert(QStringLiteral("axes"), axes);
    // What the handles change, in words.
    QString label;
    if (!selection_.groups.empty() && selection_.layers.empty() && selection_.groups.size() == 1) {
        label = text(vfx::editor::groupName(selection_.groups.front()));
    } else if (r.targets.size() == 1) {
        label = text(vfx::findLayer(session_.effect(), r.targets.front())->name);
    } else {
        label = QStringLiteral("%1 layers").arg(static_cast<int>(r.targets.size()));
    }
    g.insert(QStringLiteral("label"), label);
    g.insert(QStringLiteral("count"), static_cast<int>(r.targets.size()));
    g.insert(QStringLiteral("dragging"), session_.transforming());
    return g;
}

bool AppController::beginGizmo(const QString& kind) {
    const vfx::editor::ResolvedSelection r = selectedNothing_ ? vfx::editor::ResolvedSelection{}
                                                               : session_.resolve(selection_);
    if (r.targets.empty()) {
        if (!r.locked.empty()) {
            say(QStringLiteral("Locked: unlock it to move, turn or size it."), true);
        }
        return false;
    }
    const QString what = gizmo().value(QStringLiteral("label")).toString();
    const QString verb = kind == QStringLiteral("turn") ? QStringLiteral("Turn")
                         : kind == QStringLiteral("size") ? QStringLiteral("Size")
                                                         : QStringLiteral("Move");
    gizmoPivot_ = currentPivot(r.targets);
    gizmoKind_ = kind;
    if (!report(session_.beginTransform(r.targets, utf8(verb + QLatin1Char(' ') + what)))) {
        return false;
    }
    emit gizmoChanged();
    return true;
}

void AppController::dragGizmo(const QVariantMap& change) {
    if (!session_.transforming()) {
        return;
    }
    auto vec = [&](const char* key, double fallback) {
        const QVariantList v = change.value(QString::fromLatin1(key)).toList();
        if (v.size() != 3) {
            return vfx::Vec3{fallback, fallback, fallback};
        }
        return vfx::Vec3{v[0].toDouble(), v[1].toDouble(), v[2].toDouble()};
    };
    vfx::editor::LayerTransform t;
    t.pivot = gizmoPivot_;
    t.move = vec("move", 0.0);
    t.turn = change.value(QStringLiteral("turn"), 0.0).toDouble();
    const int axis = std::clamp(change.value(QStringLiteral("axis"), 1).toInt(), 0, 2);
    if (gizmoSpace_ == QStringLiteral("local")) {
        t.axis = 3;
        t.axisVector = gizmoAxes()[static_cast<std::size_t>(axis)];
    } else {
        t.axis = threeD() ? axis : 2;
    }
    t.factor = std::clamp(change.value(QStringLiteral("factor"), 1.0).toDouble(), 0.02, 50.0);
    const vfx::Vec3 sc = vec("scale", 1.0);
    t.scale = vfx::Vec3{std::clamp(sc.x, 0.02, 50.0), std::clamp(sc.y, 0.02, 50.0), std::clamp(sc.z, 0.02, 50.0)};
    t.individual = pivotMode_ == QStringLiteral("individual");
    gizmoTransform_ = t;
    if (!report(session_.updateTransform(t))) {
        session_.cancelTransform();
    }
    emit documentChanged();
    emit redraw();
    emit gizmoChanged();
}

void AppController::endGizmo() {
    if (!session_.transforming()) {
        return;
    }
    session_.endTransform();
    changed(Structure);
    const vfx::editor::LayerTransform& t = gizmoTransform_;
    if (gizmoKind_ == QStringLiteral("turn")) {
        say(QStringLiteral("Turned %1°. Undo turns it back.").arg(t.turn, 0, 'f', 0));
    } else if (gizmoKind_ == QStringLiteral("size")) {
        const double shown = t.factor != 1.0 ? t.factor : std::max({t.scale.x, t.scale.y, t.scale.z}) != 1.0
                                                               ? std::max({t.scale.x, t.scale.y, t.scale.z})
                                                               : std::min({t.scale.x, t.scale.y, t.scale.z});
        say(QStringLiteral("Now %1% of the size. Undo puts it back.").arg(shown * 100.0, 0, 'f', 0));
    } else {
        say(QStringLiteral("Moved. Undo puts it back."));
    }
    gizmoTransform_ = {};
    emit gizmoChanged();
}

void AppController::cancelGizmo() {
    session_.cancelTransform();
    gizmoTransform_ = {};
    changed(Structure);
    emit gizmoChanged();
    say(QStringLiteral("Put back as it was."));
}

void AppController::movePivot(double dx, double dy, double dz) {
    pivotShift_ = vfx::Vec3{pivotShift_.x + dx, pivotShift_.y + dy, pivotShift_.z + dz};
    emit gizmoChanged();
}

void AppController::resetPivot() {
    pivotShift_ = {};
    emit gizmoChanged();
}

void AppController::turnSelection(double degrees) {
    const vfx::editor::ResolvedSelection r = session_.resolve(selection_);
    if (r.targets.empty()) {
        return;
    }
    vfx::editor::LayerTransform t;
    t.pivot = currentPivot(r.targets);
    t.turn = degrees;
    t.individual = pivotMode_ == QStringLiteral("individual");
    session_.endEdit();
    groupDone(session_.transformLayers(r.targets, t, "Turn"), {});
}

void AppController::changed(Refresh what) {
    if (what == Structure) {
        rebuildLayers();  // also keeps the selection on a layer that exists
        emit layersChanged();
        emit selectionChanged();
        refreshControls();
    }
    emit documentChanged();
    emit gizmoChanged();
    emit redraw();
}

// ---------------------------------------------------------------- files

void AppController::newEffect(bool threeD) {
    session_.newEffect(threeD);
    selected_ = 0;
    loadBackdrop();
    changed(Structure);
    emit playbackChanged();
    emit viewSuggested(0.0, 0.0, 0.0);
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
    loadBackdrop();
    changed(Structure);
    emit playbackChanged();
    emit viewSuggested(0.0, 0.0, 0.0);

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
    if (index < 0 || index >= count) {
        return;
    }
    clickLayer(index, 0);
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

void AppController::setLayerLocked(int index, bool locked) {
    const auto& layers = session_.effect().layers;
    if (index < 0 || index >= static_cast<int>(layers.size())) {
        return;
    }
    const vfx::Layer& layer = layers[static_cast<std::size_t>(index)];
    const QString name = text(layer.name);
    if (layer.locked != locked) {
        if (report(session_.set(vfx::Path::layerField(layer.id, "locked"), vfx::Value(locked)))) {
            say(locked ? QStringLiteral("%1 is locked: Refine and rebuilding from a reference will leave it alone.").arg(name)
                       : QStringLiteral("%1 is unlocked.").arg(name));
        }
    }
    changed(Structure);
}

void AppController::moveLayer(int index, int by) {
    const auto& layers = session_.effect().layers;
    const int count = static_cast<int>(layers.size());
    const int target = index + by;
    if (index < 0 || index >= count || target < 0 || target >= count || by == 0) {
        return;
    }
    if (report(session_.moveLayer(layers[static_cast<std::size_t>(index)].id, target))) {
        if (selected_ == index) {
            selected_ = target;
        } else if (selected_ == target) {
            selected_ = index;
        }
        changed(Structure);
    }
}

void AppController::duplicateLayer(int index) {
    const auto& layers = session_.effect().layers;
    if (index < 0 || index >= static_cast<int>(layers.size())) {
        return;
    }
    const QString name = text(layers[static_cast<std::size_t>(index)].name);
    if (report(session_.duplicateLayer(layers[static_cast<std::size_t>(index)].id))) {
        selected_ = index + 1;
        changed(Structure);
        say(QStringLiteral("Copied %1.").arg(name));
    }
}

void AppController::effectChangedOutside(const QString& message, bool error) {
    changed(Structure);
    emit playbackChanged();
    if (!message.isEmpty()) {
        say(message, error);
    }
}

bool AppController::control(int index, ControlView& out) const {
    const vfx::Id layer = selectedLayerId();
    if (!layer.valid()) {
        return false;
    }
    std::vector<ControlView> all = views();
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
    } else if (c.desc->kind == vfx::ValueKind::Int) {
        applyControl(index, vfx::Value(static_cast<std::int64_t>(std::llround(value))));
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
    refreshControls();
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

void AppController::setControlChoice(int index, const QString& option) {
    ControlView c;
    if (!control(index, c) || c.desc->kind != vfx::ValueKind::Enum) {
        return;
    }
    session_.endEdit();
    applyControl(index, vfx::Value(utf8(option)));
}

// What can be changed about a shape, as the sliders the Shape control
// shows for it: which setting each one is, what it is called, how far it
// goes and where it stands for the shape as drawn.
QVariantList AppController::shapeEdits(int number) const {
    QVariantList list;
    const vfx::editor::ShapeEditInfo& info = vfx::editor::shapeEditInfo(number);
    const auto add = [&](const char* key, const QString& label, double from, double to, double own, bool whole,
                         const QString& tip) {
        QVariantMap one;
        one.insert(QStringLiteral("key"), QString::fromLatin1(key));
        one.insert(QStringLiteral("label"), label);
        one.insert(QStringLiteral("from"), from);
        one.insert(QStringLiteral("to"), to);
        one.insert(QStringLiteral("own"), own);
        one.insert(QStringLiteral("whole"), whole);
        one.insert(QStringLiteral("tip"), tip);
        list.push_back(one);
    };
    if (info.count()) {
        add("shapeCount", QString::fromUtf8(info.countLabel), info.countLowest, info.countHighest, info.countOwn, true,
            QStringLiteral("How many it has."));
    }
    if (info.thickness) {
        add("shapeThickness", QStringLiteral("Thickness"), -1, 1, 0, false,
            QStringLiteral("Left is thinner, right is thicker. 0 is the shape as it was drawn."));
    }
    if (info.sharpness) {
        add("shapeSharpness", QStringLiteral("Pointiness"), -1, 1, 0, false,
            QStringLiteral("Left is blunter, right is longer, thinner points. 0 is the shape as it was drawn."));
    }
    if (info.round) {
        add("shapeRound", QStringLiteral("Roundness"), info.sharperToo ? -1 : 0, 1, 0, false,
            info.sharperToo
                ? QStringLiteral("Left is sharper corners, right is rounder. 0 is the shape as it was drawn.")
                : QStringLiteral("Right is rounder corners. 0 is the shape as it was drawn."));
    }
    return list;
}

// Picking a shape gives that shape as it was drawn: settings left over from
// the last one (seven points, extra thick) are put back, in the same undo
// step as the change of shape.
void AppController::chooseShape(int index, const QString& option) {
    ControlView c;
    if (!control(index, c) || c.desc->kind != vfx::ValueKind::Enum) {
        return;
    }
    session_.endEdit();
    const vfx::Id sprite = spriteModuleId();
    const QVariantMap now = pictureInfo();
    session_.beginEdit("Change Shape");
    const bool chosen = report(session_.setControl(c.layer, c.control, vfx::Value(utf8(option))));
    if (chosen && sprite.valid()) {
        if (now.value(QStringLiteral("shapeCount")).toDouble() != 0.0) {
            (void)session_.set(vfx::Path::property(c.layer, sprite, "shapeCount"), vfx::Value(std::int64_t{0}));
        }
        for (const char* key : {"shapeThickness", "shapeSharpness", "shapeRound"}) {
            if (now.value(QString::fromLatin1(key)).toDouble() != 0.0) {
                (void)session_.set(vfx::Path::property(c.layer, sprite, key), vfx::Value(0.0));
            }
        }
    }
    session_.endEdit();
    changed(Values);
}

void AppController::setControlFlag(int index, bool on) {
    ControlView c;
    if (!control(index, c) || c.desc->kind != vfx::ValueKind::Bool) {
        return;
    }
    session_.endEdit();
    applyControl(index, vfx::Value(on));
}

void AppController::setControlVector(int index, double x, double y, double z) {
    ControlView c;
    if (!control(index, c) || c.desc->kind != vfx::ValueKind::Vec3) {
        return;
    }
    applyControl(index, vfx::Value(vfx::Vec3{clampTo(*c.desc, x), clampTo(*c.desc, y), clampTo(*c.desc, z)}));
}

void AppController::setControlMode(int index, const QString& mode) {
    ControlView c;
    if (!control(index, c) || c.desc->kind != vfx::ValueKind::Scalar) {
        return;
    }
    const auto& now = std::get<vfx::Scalar>(c.value);
    // The one number that best stands for what is there now.
    const std::vector<vfx::CurveKey> line = vfx::editor::curveFrom(now);
    double level = line.front().v;
    if (now.kind == vfx::Scalar::Kind::Curve) {
        // A curve's highest point (by size), with its sign.
        for (const vfx::CurveKey& k : line) {
            if (std::fabs(k.v) > std::fabs(level)) {
                level = k.v;
            }
        }
    }
    vfx::Scalar next = now;
    if (mode == QStringLiteral("fixed") && now.kind != vfx::Scalar::Kind::Constant) {
        next = vfx::Scalar::constant(clampTo(*c.desc, level));
    } else if (mode == QStringLiteral("random") && now.kind != vfx::Scalar::Kind::Random) {
        const double low = clampTo(*c.desc, level * 0.75), high = clampTo(*c.desc, level * 1.25);
        next = vfx::Scalar::random(std::min(low, high), std::max(low, high));
    } else if (mode == QStringLiteral("curve") && now.kind != vfx::Scalar::Kind::Curve) {
        next = vfx::Scalar::curve(vfx::editor::tidyCurve(line, *c.desc));
    } else {
        return;
    }
    session_.endEdit();
    report(session_.setControl(c.layer, c.control, vfx::Value(next)));
    refreshControls();
    changed(Values);
}

QVariantList AppController::setControlCurve(int index, const QVariantList& keys) {
    ControlView c;
    if (!control(index, c) || c.desc->kind != vfx::ValueKind::Scalar) {
        return keys;
    }
    const std::vector<vfx::CurveKey> tidy = vfx::editor::tidyCurve(keysFromList(keys), *c.desc);
    applyControl(index, vfx::Value(vfx::Scalar::curve(tidy)));
    return keysToList(tidy);
}

QVariantList AppController::applyCurveShape(int index, const QString& shape) {
    ControlView c;
    const vfx::editor::CurveShape* found = vfx::editor::findCurveShape(utf8(shape));
    if (!control(index, c) || c.desc->kind != vfx::ValueKind::Scalar || !found) {
        return {};
    }
    const vfx::Scalar next = vfx::editor::shaped(*found, std::get<vfx::Scalar>(c.value), *c.desc);
    session_.endEdit();
    applyControl(index, vfx::Value(next));
    say(QStringLiteral("%1 now follows the shape \u201c%2\u201d.").arg(text(c.label), QString::fromUtf8(found->label)));
    return keysToList(next.keys);
}

QVariantList AppController::curveLine(int index, const QVariantList& keys) const {
    ControlView c;
    std::vector<vfx::CurveKey> points = keysFromList(keys);
    if (control(index, c) && c.desc->kind == vfx::ValueKind::Scalar) {
        points = vfx::editor::tidyCurve(std::move(points), *c.desc);
    }
    QVariantList line;
    for (const vfx::CurveKey& k : vfx::flattenCurve(points)) {
        QVariantMap point;
        point.insert(QStringLiteral("t"), k.t);
        point.insert(QStringLiteral("v"), k.v);
        line.push_back(point);
    }
    return line;
}

QVariantList AppController::setControlGradient(int index, const QVariantList& stops) {
    ControlView c;
    if (!control(index, c) || c.desc->kind != vfx::ValueKind::Gradient) {
        return stops;
    }
    const vfx::Gradient gradient = gradientFromList(stops);
    applyControl(index, vfx::Value(gradient));
    return gradientToList(gradient);
}

QVariantList AppController::gradientStrip(const QVariantList& stops, int count) const {
    const vfx::Gradient gradient = gradientFromList(stops);
    count = std::clamp(count, 2, 256);
    QVariantList strip;
    for (int i = 0; i < count; ++i) {
        const vfx::Color c = vfx::editor::gradientAt(gradient, (static_cast<double>(i) + 0.5) / count);
        // As bright as the screen can show it: anything hotter is white.
        QColor shown;
        shown.setRgbF(static_cast<float>(std::clamp(vfx::editor::linearToSrgb(std::min(c.r, 1.0)), 0.0, 1.0)),
                      static_cast<float>(std::clamp(vfx::editor::linearToSrgb(std::min(c.g, 1.0)), 0.0, 1.0)),
                      static_cast<float>(std::clamp(vfx::editor::linearToSrgb(std::min(c.b, 1.0)), 0.0, 1.0)),
                      static_cast<float>(std::clamp(c.a, 0.0, 1.0)));
        strip.push_back(shown);
    }
    return strip;
}

QVariantMap AppController::gradientStopAt(const QVariantList& stops, double t) const {
    t = std::isfinite(t) ? std::clamp(t, 0.0, 1.0) : 0.0;
    return stopToMap(t, vfx::editor::gradientAt(gradientFromList(stops), t));
}

QVariantList AppController::setControlBursts(int index, const QVariantList& items) {
    ControlView c;
    if (!control(index, c) || c.desc->kind != vfx::ValueKind::Bursts) {
        return items;
    }
    vfx::BurstList bursts;
    for (const QVariant& item : items) {
        const QVariantMap row = item.toMap();
        const double count = row.value(QStringLiteral("count")).toDouble();
        bursts.items.push_back(vfx::Burst{row.value(QStringLiteral("time")).toDouble(),
                                          std::isfinite(count) ? static_cast<std::int64_t>(std::llround(count)) : 0});
    }
    const vfx::Layer* layer = vfx::findLayer(session_.effect(), c.layer);
    bursts = vfx::editor::tidyBursts(std::move(bursts), layer ? layer->duration : 0.0);
    session_.endEdit();
    applyControl(index, vfx::Value(bursts));
    return burstsToList(bursts);
}

// ---------------------------------------------------------------- sound

vfx::Id AppController::soundModuleId() const {
    const vfx::Layer* layer = vfx::findLayer(session_.effect(), selectedLayerId());
    if (!layer) {
        return vfx::Id{};
    }
    for (const vfx::Module& m : layer->modules) {
        if (m.type == "sound") {
            return m.id;
        }
    }
    return vfx::Id{};
}

QVariantMap AppController::soundInfo() const {
    QVariantMap info;
    info.insert(QStringLiteral("has"), false);
    const vfx::Layer* layer = vfx::findLayer(session_.effect(), selectedLayerId());
    const vfx::Module* m = nullptr;
    if (layer) {
        for (const vfx::Module& candidate : layer->modules) {
            if (candidate.type == "sound") {
                m = &candidate;
            }
        }
    }
    if (!m) {
        return info;
    }
    auto number = [&](const char* key, double fallback) {
        const vfx::Value* v = m->find(key);
        const auto* d = v ? std::get_if<double>(v) : nullptr;
        return d ? *d : fallback;
    };
    auto flag = [&](const char* key) {
        const vfx::Value* v = m->find(key);
        return v && std::holds_alternative<bool>(*v) && std::get<bool>(*v);
    };
    for (const char* key : {"delay", "volume", "pitch", "pan", "fadeIn", "fadeOut", "trimStart", "length",
                            "randomPitch", "randomVolume"}) {
        info.insert(QString::fromLatin1(key), number(key, std::string_view(key) == "volume" ? 1.0 : 0.0));
    }
    info.insert(QStringLiteral("loop"), flag("loop"));
    info.insert(QStringLiteral("mute"), flag("mute"));
    const vfx::Value* play = m->find("play");
    info.insert(QStringLiteral("play"), play && std::holds_alternative<std::string>(*play)
                                            ? text(std::get<std::string>(*play))
                                            : QStringLiteral("start"));
    const vfx::Value* ref = m->find("sound");
    const auto* asset = ref ? std::get_if<vfx::AssetRef>(ref) : nullptr;
    const vfx::Asset* file = asset ? vfx::findAsset(session_.effect(), asset->id) : nullptr;
    if (!file) {
        return info;
    }
    info.insert(QStringLiteral("has"), true);
    QString name = text(file->path);
    name = name.mid(name.lastIndexOf(QLatin1Char('/')) + 1);
    if (const std::string id = vfx::editor::librarySoundId(file->path); !id.empty()) {
        if (const auto* lib = vfx::editor::findLibrarySound(id)) {
            name = text(lib->name) + QStringLiteral(" (library)");
        }
    }
    info.insert(QStringLiteral("name"), name);
    if (const vfx::editor::Sound* sound = session_.sounds().find(asset->id)) {
        info.insert(QStringLiteral("seconds"), sound->seconds());
        QVariantList peaks;
        for (const float p : vfx::editor::soundPeaks(*sound, 120)) {
            peaks.push_back(static_cast<double>(p));
        }
        info.insert(QStringLiteral("peaks"), peaks);
        info.insert(QStringLiteral("problem"), QString());
    } else {
        info.insert(QStringLiteral("seconds"), 0.0);
        info.insert(QStringLiteral("peaks"), QVariantList());
        info.insert(QStringLiteral("problem"), QStringLiteral("Loading the sound…"));
    }
    return info;
}

void AppController::setSoundOn(bool on) {
    if (on == soundOn_) {
        return;
    }
    soundOn_ = on;
    QSettings().setValue(QStringLiteral("sound/on"), on);
    emit soundOnChanged();
    say(on ? QStringLiteral("Sound on.") : QStringLiteral("Sound off: effects play silently."));
}

bool AppController::soundAvailable() const { return audio_ && audio_->available(); }

QVariantList AppController::librarySounds() const {
    QVariantList list;
    for (const vfx::editor::LibrarySound& s : vfx::editor::librarySounds()) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), text(s.id));
        row.insert(QStringLiteral("name"), text(s.name));
        row.insert(QStringLiteral("category"), text(s.category));
        row.insert(QStringLiteral("description"), text(s.description));
        row.insert(QStringLiteral("loops"), s.loops);
        list.push_back(row);
    }
    return list;
}

bool AppController::useSoundBytes(const std::string& wav, const QString& name) {
    const vfx::Id layer = selectedLayerId();
    if (!layer.valid()) {
        say(QStringLiteral("Pick a layer first."), true);
        return false;
    }
    if (!report(session_.useSound(layer, utf8(name), wav))) {
        return false;
    }
    session_.tick(0.0);  // load it now, so the panel can show its waveform
    changed(Structure);
    say(QStringLiteral("%1 plays when this layer starts. Choose \"Every burst\" to play it with each burst.")
            .arg(name));
    return true;
}

bool AppController::useSoundFile(const QUrl& file) { return useSoundPath(file.toLocalFile()); }

bool AppController::useSoundPath(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        say(QStringLiteral("That sound could not be opened."), true);
        return false;
    }
    const QByteArray raw = file.readAll();
    const std::string bytes(raw.constData(), static_cast<std::size_t>(raw.size()));
    const QString name = QFileInfo(path).fileName();
    auto decoded = vfx::editor::decodeSound(bytes);
    if (decoded.ok()) {
        // Kept as it is, unless it is an AIFF: projects keep WAV.
        const bool wav = bytes.size() >= 4 && bytes.compare(0, 4, "RIFF") == 0;
        return useSoundBytes(wav ? bytes : vfx::editor::encodeWav(decoded.value()), name);
    }

    // Anything else: the system's decoder turns it into samples.
    auto* decoder = new QAudioDecoder(this);
    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Float);
    decoder->setAudioFormat(format);
    decoder->setSource(QUrl::fromLocalFile(path));
    auto sound = std::make_shared<vfx::editor::Sound>();
    sound->sampleRate = 0;
    importingSound_ = true;
    emit importingSoundChanged();
    say(QStringLiteral("Reading %1…").arg(name));
    connect(decoder, &QAudioDecoder::bufferReady, this, [decoder, sound]() {
        const QAudioBuffer buffer = decoder->read();
        const QAudioFormat f = buffer.format();
        const int channels = f.channelCount();
        if (!buffer.isValid() || channels <= 0) {
            return;
        }
        if (sound->sampleRate == 0) {
            sound->sampleRate = f.sampleRate();
            sound->channels = channels >= 2 ? 2 : 1;
        }
        const qsizetype frames = buffer.frameCount();
        for (qsizetype i = 0; i < frames; ++i) {
            for (int c = 0; c < sound->channels; ++c) {
                const qsizetype at = i * channels + c;
                float v = 0.0f;
                switch (f.sampleFormat()) {
                    case QAudioFormat::Float: v = buffer.constData<float>()[at]; break;
                    case QAudioFormat::Int16: v = buffer.constData<qint16>()[at] / 32768.0f; break;
                    case QAudioFormat::Int32: v = static_cast<float>(buffer.constData<qint32>()[at] / 2147483648.0); break;
                    case QAudioFormat::UInt8: v = (buffer.constData<quint8>()[at] - 128) / 128.0f; break;
                    default: break;
                }
                sound->samples.push_back(v);
            }
        }
    });
    connect(decoder, &QAudioDecoder::finished, this, [this, decoder, sound, name]() {
        importingSound_ = false;
        emit importingSoundChanged();
        decoder->deleteLater();
        if (sound->samples.empty() || sound->sampleRate <= 0) {
            say(QStringLiteral("%1 has no sound in it that can be read.").arg(name), true);
            return;
        }
        useSoundBytes(vfx::editor::encodeWav(*sound), name);
    });
    connect(decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this,
            [this, decoder, name](QAudioDecoder::Error) {
                importingSound_ = false;
                emit importingSoundChanged();
                say(QStringLiteral("%1 could not be read: %2").arg(name, decoder->errorString()), true);
                decoder->deleteLater();
            });
    decoder->start();
    return true;
}

bool AppController::useLibrarySound(const QString& id) {
    const vfx::Id layer = selectedLayerId();
    if (!report(session_.useLibrarySound(layer, utf8(id)))) {
        return false;
    }
    session_.tick(0.0);
    changed(Structure);
    const auto* lib = vfx::editor::findLibrarySound(utf8(id));
    say(QStringLiteral("%1 added to this layer.").arg(lib ? text(lib->name) : id));
    return true;
}

void AppController::clearSound() {
    if (report(session_.clearSound(selectedLayerId()))) {
        changed(Structure);
        say(QStringLiteral("Sound removed from this layer."));
    }
}

void AppController::previewLibrarySound(const QString& id) {
    auto made = vfx::editor::makeLibrarySound(utf8(id));
    if (made.ok()) {
        audio_->preview(std::make_shared<const vfx::editor::Sound>(std::move(made.value())));
    }
}

void AppController::previewLayerSound() {
    const vfx::Layer* layer = vfx::findLayer(session_.effect(), selectedLayerId());
    if (!layer) {
        return;
    }
    for (const vfx::Module& m : layer->modules) {
        if (const auto* ref = m.type == "sound" ? std::get_if<vfx::AssetRef>(m.find("sound")) : nullptr) {
            audio_->preview(session_.sounds().shared(ref->id));
        }
    }
}

void AppController::stopPreview() { audio_->stopPreview(); }

void AppController::setSoundNumber(const QString& key, double value) {
    const vfx::Id module = soundModuleId();
    const vfx::ModuleTypeDesc* desc = vfx::Registry::builtin().findModule("sound");
    const vfx::PropertyDesc* p = desc ? desc->find(utf8(key)) : nullptr;
    if (!module.valid() || !p) {
        return;
    }
    if (report(session_.set(vfx::Path::property(selectedLayerId(), module, utf8(key)), vfx::Value(clampTo(*p, value))))) {
        changed(Values);
    }
}

void AppController::setSoundChoice(const QString& key, const QString& value) {
    const vfx::Id module = soundModuleId();
    if (module.valid() &&
        report(session_.set(vfx::Path::property(selectedLayerId(), module, utf8(key)), vfx::Value(utf8(value))))) {
        changed(Values);
    }
}

void AppController::setSoundFlag(const QString& key, bool value) {
    const vfx::Id module = soundModuleId();
    if (module.valid() &&
        report(session_.set(vfx::Path::property(selectedLayerId(), module, utf8(key)), vfx::Value(value)))) {
        changed(Values);
    }
}

// ------------------------------------------------------------- pictures

vfx::Id AppController::spriteModuleId() const {
    const vfx::Layer* layer = vfx::findLayer(session_.effect(), selectedLayerId());
    if (!layer) {
        return vfx::Id{};
    }
    for (const vfx::Module& m : layer->modules) {
        if (m.type == "sprite") {
            return m.id;
        }
    }
    return vfx::Id{};
}

QVariantMap AppController::pictureInfo() const {
    QVariantMap info;
    const vfx::Layer* layer = vfx::findLayer(session_.effect(), selectedLayerId());
    const vfx::Module* sprite = nullptr;
    if (layer) {
        for (const vfx::Module& m : layer->modules) {
            if (m.type == "sprite") {
                sprite = &m;
            }
        }
    }
    info.insert(QStringLiteral("has"), false);
    if (!sprite) {
        return info;
    }
    auto number = [&](const char* key, double fallback) -> double {
        const vfx::Value* v = sprite->find(key);
        if (const auto* i = v ? std::get_if<std::int64_t>(v) : nullptr) {
            return static_cast<double>(*i);
        }
        if (const auto* d = v ? std::get_if<double>(v) : nullptr) {
            return *d;
        }
        return fallback;
    };
    // The shape's own settings (how many points, how thick...), which the
    // Shape control shows under the shapes.
    for (const char* key : {"shapeCount", "shapeThickness", "shapeSharpness", "shapeRound"}) {
        info.insert(QString::fromLatin1(key), number(key, 0));
    }
    info.insert(QStringLiteral("columns"), number("columns", 1));
    info.insert(QStringLiteral("rows"), number("rows", 1));
    info.insert(QStringLiteral("frames"), number("frames", 0));
    info.insert(QStringLiteral("fps"), number("fps", 12));
    const vfx::Value* animate = sprite->find("animate");
    info.insert(QStringLiteral("animate"),
                animate && std::holds_alternative<std::string>(*animate)
                    ? text(std::get<std::string>(*animate))
                    : QStringLiteral("life"));
    const vfx::Value* randomStart = sprite->find("randomStart");
    info.insert(QStringLiteral("randomStart"),
                randomStart && std::holds_alternative<bool>(*randomStart) && std::get<bool>(*randomStart));

    const vfx::Value* texture = sprite->find("texture");
    const auto* ref = texture ? std::get_if<vfx::AssetRef>(texture) : nullptr;
    if (!ref || !ref->id.valid()) {
        return info;
    }
    const vfx::Asset* asset = vfx::findAsset(session_.effect(), ref->id);
    if (!asset) {
        return info;
    }
    info.insert(QStringLiteral("has"), true);
    const auto file = session_.projectFolder() / vfx::pathFromUtf8(asset->path);
    info.insert(QStringLiteral("source"), QUrl::fromLocalFile(QString::fromStdU16String(file.u16string())));
    QString name = text(asset->path);
    name = name.mid(name.lastIndexOf(QLatin1Char('/')) + 1);
    info.insert(QStringLiteral("name"), name);
    if (const vfx::editor::Image* image = session_.images().find(ref->id)) {
        info.insert(QStringLiteral("width"), image->width);
        info.insert(QStringLiteral("height"), image->height);
        info.insert(QStringLiteral("problem"), QString());
    } else {
        info.insert(QStringLiteral("width"), 0);
        info.insert(QStringLiteral("height"), 0);
        info.insert(QStringLiteral("problem"),
                    QStringLiteral("This picture can't be found next to the effect. Choose it again."));
    }
    return info;
}

bool AppController::usePictureImage(const QImage& source, const QString& name) {
    const vfx::Id layer = selectedLayerId();
    if (!layer.valid() || !spriteModuleId().valid()) {
        say(QStringLiteral("Pick a layer first."), true);
        return false;
    }
    if (source.isNull()) {
        say(QStringLiteral("That picture could not be read."), true);
        return false;
    }
    if (source.width() > 8192 || source.height() > 8192) {
        say(QStringLiteral("That picture is too large: at most 8192 pixels wide and high."), true);
        return false;
    }
    const QImage image = source.convertToFormat(QImage::Format_RGBA8888);
    vfx::editor::Image pixels;
    pixels.width = image.width();
    pixels.height = image.height();
    pixels.rgba.resize(static_cast<std::size_t>(pixels.width) * static_cast<std::size_t>(pixels.height) * 4u);
    for (int y = 0; y < image.height(); ++y) {
        std::memcpy(&pixels.rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(pixels.width) * 4u],
                    image.constScanLine(y), static_cast<std::size_t>(pixels.width) * 4u);
    }
    const std::string png = vfx::editor::encodePng(pixels);

    // "fire_4x2.png", "smoke 8x8.png": the grid is in the name.
    const QRegularExpression grid(QStringLiteral("(\\d{1,2})\\s*[xX×]\\s*(\\d{1,2})"));
    const QRegularExpressionMatch match = grid.match(name);
    const bool first = !pictureInfo().value(QStringLiteral("has")).toBool();
    session_.beginEdit("Use Picture");
    bool ok = report(session_.useImage(layer, utf8(name), png));
    bool whitened = false;
    if (ok) {
        const vfx::Id sprite = spriteModuleId();
        auto put = [&](const char* key, vfx::Value value) {
            (void)session_.set(vfx::Path::property(layer, sprite, key), std::move(value));
        };
        // The colour tints the picture. The first time a layer gets one, it
        // starts white, so the picture shows as it was painted.
        if (first) {
            if (const vfx::Layer* l = vfx::findLayer(session_.effect(), layer)) {
                for (const vfx::Module& m : l->modules) {
                    const vfx::Value* c = m.type == "initial" ? m.find("color") : nullptr;
                    if (const auto* color = c ? std::get_if<vfx::Color>(c) : nullptr) {
                        if (color->r != 1.0 || color->g != 1.0 || color->b != 1.0) {
                            whitened = session_.set(vfx::Path::property(layer, m.id, "color"),
                                                    vfx::Value(vfx::Color{1.0, 1.0, 1.0, color->a}))
                                           .ok();
                        }
                    }
                }
            }
        }
        if (match.hasMatch()) {
            const int columns = std::clamp(match.captured(1).toInt(), 1, 64);
            const int rows = std::clamp(match.captured(2).toInt(), 1, 64);
            put("columns", vfx::Value(static_cast<std::int64_t>(columns)));
            put("rows", vfx::Value(static_cast<std::int64_t>(rows)));
            put("frames", vfx::Value(std::int64_t{0}));
        }
    }
    session_.endEdit();
    if (ok) {
        changed(Structure);
        QString line = match.hasMatch()
                           ? QStringLiteral("Drawing %1 as a %2 by %3 sprite sheet.")
                                 .arg(name, match.captured(1), match.captured(2))
                           : QStringLiteral("Drawing %1. If it is a sprite sheet, set how many pictures across and down.")
                                 .arg(name);
        if (whitened) {
            line += QStringLiteral(" Colour set to white so it shows as painted.");
        }
        say(line);
    }
    return ok;
}

bool AppController::usePicture(const QUrl& file) { return usePicturePath(file.toLocalFile()); }

bool AppController::usePicturePath(const QString& path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        say(QStringLiteral("That picture could not be read: %1").arg(reader.errorString()), true);
        return false;
    }
    return usePictureImage(image, QFileInfo(path).fileName());
}

bool AppController::useSamplePicture() {
    const QImage image(QStringLiteral(":/samples/toon-flame-4x2.png"));
    if (!usePictureImage(image, QStringLiteral("toon-flame-4x2.png"))) {
        return false;
    }
    // A sheet that loops reads as a living flame.
    setPictureChoice(QStringLiteral("animate"), QStringLiteral("loop"));
    setPictureFlag(QStringLiteral("randomStart"), true);
    changed(Structure);  // so the panel shows the loop
    return true;
}

void AppController::clearPicture() {
    if (report(session_.clearImage(selectedLayerId()))) {
        changed(Structure);
        say(QStringLiteral("Back to the shape."));
    }
}

void AppController::setPictureNumber(const QString& key, double value) {
    const vfx::Id sprite = spriteModuleId();
    const vfx::ModuleTypeDesc* desc = vfx::Registry::builtin().findModule("sprite");
    const vfx::PropertyDesc* p = desc ? desc->find(utf8(key)) : nullptr;
    if (!sprite.valid() || !p) {
        return;
    }
    value = clampTo(*p, value);
    vfx::Value v = p->kind == vfx::ValueKind::Int
                       ? vfx::Value(static_cast<std::int64_t>(std::lround(value)))
                       : vfx::Value(value);
    if (report(session_.set(vfx::Path::property(selectedLayerId(), sprite, utf8(key)), std::move(v)))) {
        changed(Values);
    }
}

void AppController::setPictureChoice(const QString& key, const QString& value) {
    const vfx::Id sprite = spriteModuleId();
    if (sprite.valid() &&
        report(session_.set(vfx::Path::property(selectedLayerId(), sprite, utf8(key)), vfx::Value(utf8(value))))) {
        changed(Values);
    }
}

void AppController::setPictureFlag(const QString& key, bool value) {
    const vfx::Id sprite = spriteModuleId();
    if (sprite.valid() &&
        report(session_.set(vfx::Path::property(selectedLayerId(), sprite, utf8(key)), vfx::Value(value)))) {
        changed(Values);
    }
}

// -------------------------------------------------------------- library

QVariantList AppController::presets() const {
    QVariantList list;
    for (const vfx::editor::PresetInfo& info : vfx::editor::presets()) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), text(info.id));
        row.insert(QStringLiteral("name"), text(info.name));
        row.insert(QStringLiteral("category"), text(info.category));
        row.insert(QStringLiteral("description"), text(info.description));
        list.push_back(row);
    }
    return list;
}

QStringList AppController::presetCategories() const {
    QStringList list;
    for (const std::string& category : vfx::editor::presetCategories()) {
        list.push_back(text(category));
    }
    return list;
}

void AppController::setLibraryOpen(bool open) {
    if (open != libraryOpen_) {
        libraryOpen_ = open;
        emit libraryOpenChanged();
    }
}

bool AppController::openPreset(const QString& id) {
    const std::string key = utf8(id);
    if (!report(session_.openPreset(key))) {
        return false;
    }
    selected_ = 0;
    loadBackdrop();
    changed(Structure);
    emit playbackChanged();
    if (const vfx::editor::PresetInfo* info = vfx::editor::findPreset(key)) {
        // A little room around the effect, so it does not touch the edges.
        emit viewSuggested(static_cast<double>(info->viewX), static_cast<double>(info->viewY),
                           static_cast<double>(info->viewHeight) * 1.25);
        say(QStringLiteral("Opened %1 from the library. Every layer and control in it can be changed.")
                .arg(text(info->name)));
    }
    return true;
}

// ---------------------------------------------------------------- groups

void AppController::groupDone(const vfx::Status& status, const QString& said) {
    if (!report(status)) {
        return;
    }
    changed(Structure);
    emit playbackChanged();
    if (!said.isEmpty()) {
        say(said);
    }
}

void AppController::moveGroup(const QString& group, double dx, double dy, double dz) {
    session_.endEdit();
    groupDone(session_.moveGroup(utf8(group), dx, dy, dz), {});
}

void AppController::delayGroup(const QString& group, double seconds) {
    session_.endEdit();
    const double before = session_.effect().duration;
    vfx::Status done = session_.delayGroup(utf8(group), seconds);
    const double after = session_.effect().duration;
    groupDone(done, after > before + 1e-9
                        ? QStringLiteral("The effect is now %1 s long, so all of %2 fits.").arg(after, 0, 'f', 2).arg(group)
                        : QString());
}

void AppController::sizeGroup(const QString& group, double factor) {
    session_.endEdit();
    groupDone(session_.sizeGroup(utf8(group), factor), {});
}

void AppController::setGroupEnabled(const QString& group, bool enabled) {
    groupDone(session_.setGroupEnabled(utf8(group), enabled), {});
}

void AppController::setGroupLocked(const QString& group, bool locked) {
    groupDone(session_.setGroupLocked(utf8(group), locked),
              locked ? QStringLiteral("Locked: the handles leave it alone.") : QStringLiteral("Unlocked."));
}

void AppController::removeGroup(const QString& group) {
    const int before = static_cast<int>(session_.effect().layers.size());
    vfx::Status done = session_.removeGroup(utf8(group));
    if (done) {
        const int removed = before - static_cast<int>(session_.effect().layers.size());
        selected_ = std::max(0, std::min(selected_, before - removed - 1));
    }
    groupDone(done, QStringLiteral("Removed %1. Undo brings it back.").arg(group));
}

void AppController::ungroup(const QString& group) {
    groupDone(session_.ungroup(utf8(group)), QStringLiteral("%1 is no longer a group; its layers stay.").arg(group));
}

bool AppController::addPreset(const QString& id) {
    const int before = static_cast<int>(session_.effect().layers.size());
    int added = 0;
    if (!report(session_.addPreset(utf8(id), &added))) {
        return false;
    }
    selected_ = before;  // the first of the new layers
    // The new group is selected, so its handles are ready to move it.
    if (before < static_cast<int>(session_.effect().layers.size())) {
        const vfx::Layer& first = session_.effect().layers[static_cast<std::size_t>(before)];
        selection_ = {};
        if (!first.group.empty()) {
            selection_.groups.push_back(first.group);
        } else {
            selection_.layers.push_back(first.id);
        }
        selectedNothing_ = false;
        selectionFor_ = first.id;
    }
    changed(Structure);
    const vfx::editor::PresetInfo* info = vfx::editor::findPreset(utf8(id));
    emit playbackChanged();  // it may have made the effect longer
    say(QStringLiteral("Added %1 (%2 layers) as a group: move, delay or size it under Layers. Undo takes it out again.")
            .arg(info ? text(info->name) : id)
            .arg(added));
    return true;
}

// ------------------------------------------------------------- backdrop

QString AppController::backdropName() const {
    return backdrop_.source.isEmpty() ? QString() : QFileInfo(backdrop_.source.toLocalFile()).fileName();
}

QString AppController::backdropKey() const {
    return QStringLiteral("backdrops/") + text(vfx::formatId('e', session_.effect().id));
}

void AppController::loadBackdrop() {
    QSettings settings;
    const QString key = backdropKey();
    Backdrop loaded;
    const QString path = settings.value(key + QStringLiteral("/path")).toString();
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        loaded.source = QUrl::fromLocalFile(path);
        loaded.x = settings.value(key + QStringLiteral("/x"), 0.0).toDouble();
        loaded.y = settings.value(key + QStringLiteral("/y"), 1.5).toDouble();
        loaded.height = settings.value(key + QStringLiteral("/height"), 3.0).toDouble();
        loaded.opacity = settings.value(key + QStringLiteral("/opacity"), 1.0).toDouble();
    }
    backdrop_ = loaded;
    emit backdropChanged();
    loadStage();
}

// ---------------------------------------------------------------- model
//
// Like the backdrop, the model is remembered for each effect on this
// computer, not saved in the effect: it is for seeing the effect on, and in
// the game it is already there.

QString AppController::modelName() const {
    if (!model_) {
        return QString();
    }
    return QString::fromUtf8(model_->name.c_str());
}

QVariantMap AppController::modelPlaceMap() const {
    QVariantMap out;
    out.insert(QStringLiteral("x"), static_cast<double>(modelPlace_.x));
    out.insert(QStringLiteral("y"), static_cast<double>(modelPlace_.y));
    out.insert(QStringLiteral("z"), static_cast<double>(modelPlace_.z));
    out.insert(QStringLiteral("turn"), static_cast<double>(modelPlace_.turn));
    out.insert(QStringLiteral("height"), static_cast<double>(modelPlace_.height));
    return out;
}

bool AppController::showModelFile(const QString& path, bool quiet) {
    if (path == modelPath_ && model_ && !path.isEmpty()) {
        return true;  // already showing it
    }
    auto loaded = vfx::editor::loadModel(toPath(path));
    if (!loaded) {
        if (!quiet) {
            say(text(loaded.error().message), true);
        }
        return false;
    }
    model_ = std::make_shared<const vfx::editor::Model>(std::move(loaded.value()));
    modelPath_ = path;
    return true;
}

void AppController::loadStage() {
    QSettings settings;
    const QString key = QStringLiteral("models/") + text(vfx::formatId('e', session_.effect().id));
    const QString kind = settings.value(key + QStringLiteral("/kind")).toString();
    bool shown = false;
    if (kind == QStringLiteral("dummy")) {
        if (!model_ || !modelPath_.isEmpty()) {
            model_ = std::make_shared<const vfx::editor::Model>(vfx::editor::practiceDummy());
        }
        modelPath_.clear();
        shown = true;
    } else if (kind == QStringLiteral("file")) {
        const QString path = settings.value(key + QStringLiteral("/path")).toString();
        shown = QFileInfo::exists(path) && showModelFile(path, true);
    }
    if (!shown) {
        model_.reset();
        modelPath_.clear();
    } else {
        modelPlace_.x = settings.value(key + QStringLiteral("/x"), 0.0).toFloat();
        modelPlace_.y = settings.value(key + QStringLiteral("/y"), 0.0).toFloat();
        modelPlace_.z = settings.value(key + QStringLiteral("/z"), 0.0).toFloat();
        modelPlace_.turn = settings.value(key + QStringLiteral("/turn"), 0.0).toFloat();
        modelPlace_.height = settings.value(key + QStringLiteral("/height"), 3.0).toFloat();
        hideBehindModel_ = settings.value(key + QStringLiteral("/hide"), true).toBool();
    }
    emit modelChanged();
}

void AppController::storeStage() {
    QSettings settings;
    const QString key = QStringLiteral("models/") + text(vfx::formatId('e', session_.effect().id));
    if (!model_) {
        settings.remove(key);
    } else {
        settings.setValue(key + QStringLiteral("/kind"), modelPath_.isEmpty() ? QStringLiteral("dummy") : QStringLiteral("file"));
        settings.setValue(key + QStringLiteral("/path"), modelPath_);
        settings.setValue(key + QStringLiteral("/x"), static_cast<double>(modelPlace_.x));
        settings.setValue(key + QStringLiteral("/y"), static_cast<double>(modelPlace_.y));
        settings.setValue(key + QStringLiteral("/z"), static_cast<double>(modelPlace_.z));
        settings.setValue(key + QStringLiteral("/turn"), static_cast<double>(modelPlace_.turn));
        settings.setValue(key + QStringLiteral("/height"), static_cast<double>(modelPlace_.height));
        settings.setValue(key + QStringLiteral("/hide"), hideBehindModel_);
    }
    emit modelChanged();
}

bool AppController::useModel(const QUrl& file) {
    const QString path = file.toLocalFile();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        say(QStringLiteral("That model could not be found on this computer."), true);
        return false;
    }
    const bool had = model_ != nullptr;
    if (!showModelFile(path, false)) {
        return false;
    }
    if (!had) {
        modelPlace_ = vfx::editor::ModelPlace{};
        // In a 2D effect the particles are a flat sheet through the middle
        // of the model, so hiding what is behind it would hide half the
        // effect: there it starts off.
        hideBehindModel_ = session_.effect().space == "3d";
    }
    storeStage();
    say(QStringLiteral("Showing %1 (%2 triangles). Drag the camera to look round it; place it under Backdrop.")
            .arg(modelName())
            .arg(modelTriangles()));
    return true;
}

void AppController::useSampleModel() {
    const bool had = model_ != nullptr;
    model_ = std::make_shared<const vfx::editor::Model>(vfx::editor::practiceDummy());
    modelPath_.clear();
    if (!had) {
        modelPlace_ = vfx::editor::ModelPlace{};
        hideBehindModel_ = session_.effect().space == "3d";
    }
    storeStage();
    say(QStringLiteral("Showing the practice dummy. Place it under Backdrop."));
}

void AppController::clearModel() {
    model_.reset();
    modelPath_.clear();
    storeStage();
}

void AppController::setModelPlace(const QString& key, double value) {
    if (!model_ || !std::isfinite(value)) {
        return;
    }
    const auto v = static_cast<float>(value);
    if (key == QStringLiteral("x")) {
        modelPlace_.x = std::clamp(v, -1000.0f, 1000.0f);
    } else if (key == QStringLiteral("y")) {
        modelPlace_.y = std::clamp(v, -1000.0f, 1000.0f);
    } else if (key == QStringLiteral("z")) {
        modelPlace_.z = std::clamp(v, -1000.0f, 1000.0f);
    } else if (key == QStringLiteral("turn")) {
        modelPlace_.turn = std::fmod(v, 360.0f);
    } else if (key == QStringLiteral("height")) {
        modelPlace_.height = std::clamp(v, 0.01f, 1000.0f);
    } else {
        return;
    }
    storeStage();
}

void AppController::setHideBehindModel(bool hide) {
    if (hide == hideBehindModel_) {
        return;
    }
    hideBehindModel_ = hide;
    storeStage();
}

void AppController::storeBackdrop() {
    QSettings settings;
    const QString key = backdropKey();
    if (backdrop_.source.isEmpty()) {
        settings.remove(key);
    } else {
        settings.setValue(key + QStringLiteral("/path"), backdrop_.source.toLocalFile());
        settings.setValue(key + QStringLiteral("/x"), backdrop_.x);
        settings.setValue(key + QStringLiteral("/y"), backdrop_.y);
        settings.setValue(key + QStringLiteral("/height"), backdrop_.height);
        settings.setValue(key + QStringLiteral("/opacity"), backdrop_.opacity);
    }
    emit backdropChanged();
}

// ------------------------------------------------------------- target

bool AppController::hasTarget() const {
    return vfx::findAsset(session_.effect(), session_.effect().target.id) != nullptr;
}

QUrl AppController::targetSource() const {
    const vfx::Asset* asset = vfx::findAsset(session_.effect(), session_.effect().target.id);
    if (!asset) {
        return {};
    }
    const auto file = session_.projectFolder() / vfx::pathFromUtf8(asset->path);
    return QUrl::fromLocalFile(QString::fromStdU16String(file.u16string()));
}

QString AppController::targetName() const {
    const vfx::Asset* asset = vfx::findAsset(session_.effect(), session_.effect().target.id);
    return asset ? QFileInfo(text(asset->path)).fileName() : QString();
}

double AppController::targetHeight() const {
    const QUrl source = targetSource();
    const QString key = source.toLocalFile();
    if (key != targetAspectFor_) {
        targetAspectFor_ = key;
        targetAspect_ = 1.0;
        QImageReader reader(key);
        const QSize size = reader.size();
        if (size.width() > 0 && size.height() > 0) {
            targetAspect_ = static_cast<double>(size.height()) / size.width();
        }
    }
    return session_.effect().targetWidth * targetAspect_;
}

int AppController::fittedLayers() const {
    int n = 0;
    for (const vfx::Layer& l : session_.effect().layers) {
        for (const vfx::Module& m : l.modules) {
            if (m.type == "shape") {
                const vfx::Value* fit = m.find("fitTarget");
                n += fit && std::holds_alternative<bool>(*fit) && std::get<bool>(*fit) ? 1 : 0;
            }
        }
    }
    return n;
}

bool AppController::setTargetFile(const QUrl& image) {
    const QString path = image.isLocalFile() ? image.toLocalFile() : image.toString();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        say(QStringLiteral("That picture could not be found on this computer."), true);
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        say(QStringLiteral("That picture could not be read."), true);
        return false;
    }
    QByteArray bytes = file.readAll();
    QString name = QFileInfo(path).fileName();
    // Anything other than a PNG is turned into one, keeping its see-through parts.
    if (!bytes.startsWith("\x89PNG")) {
        QImage picture;
        if (!picture.loadFromData(bytes)) {
            say(QStringLiteral("That file is not a picture this version can read. Try a PNG with a see-through background."), true);
            return false;
        }
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        picture.convertToFormat(QImage::Format_RGBA8888).save(&buffer, "PNG");
        bytes = png;
        name = QFileInfo(path).completeBaseName() + QStringLiteral(".png");
    }
    session_.endEdit();
    if (!report(session_.setTarget(utf8(name), std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()))))) {
        return false;
    }
    changed(Structure);
    const int fitted = fittedLayers();
    say(fitted > 0 ? QStringLiteral("The target is %1; %2 layers follow its outline.").arg(name).arg(fitted)
                   : QStringLiteral("The target is %1. Select layers and click Fit to its outline, or open a "
                                    "Symbol Outlines effect from the Library.").arg(name));
    return true;
}

void AppController::clearTarget() {
    if (report(session_.clearTarget())) {
        changed(Structure);
        say(QStringLiteral("The target is gone; the layers keep the outline they had. Undo brings it back."));
    }
}

void AppController::placeTarget(double width, double x, double y) {
    session_.endEdit();
    if (report(session_.placeTarget(width, vfx::Vec3{x, y, session_.effect().targetPosition.z}))) {
        changed(Structure);
    }
}

void AppController::fitSelectionToTarget() {
    const vfx::editor::ResolvedSelection r = session_.resolve(selection_);
    if (r.targets.empty()) {
        say(QStringLiteral("Select the layers to fit to the target first."), true);
        return;
    }
    session_.endEdit();
    if (report(session_.fitToTarget(r.targets))) {
        changed(Structure);
        say(QStringLiteral("%1 layers now follow the target's outline. Try Travel, Padding and Thickness under Shape.")
                .arg(static_cast<int>(r.targets.size())));
    }
}

void AppController::setPreviewMode(const QString& mode) {
    if (mode != previewMode_ && (mode == QStringLiteral("composite") || mode == QStringLiteral("effect") ||
                                 mode == QStringLiteral("target"))) {
        previewMode_ = mode;
        emit previewChanged();
        emit redraw();
    }
}

void AppController::setPreviewBackground(const QString& background) {
    if (background != previewBackground_ &&
        (background == QStringLiteral("dark") || background == QStringLiteral("black") ||
         background == QStringLiteral("white") || background == QStringLiteral("checker"))) {
        previewBackground_ = background;
        emit previewChanged();
    }
}

void AppController::setBackdrop(const QUrl& image) {
    const QString path = image.toLocalFile();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        say(QStringLiteral("That picture could not be found on this computer."), true);
        return;
    }
    QImageReader reader(path);
    if (!reader.canRead()) {
        say(QStringLiteral("That file is not a picture this version can show. Try a PNG or a JPG."), true);
        return;
    }
    const double x = backdrop_.x, y = backdrop_.y, height = backdrop_.height, opacity = backdrop_.opacity;
    const bool had = hasBackdrop();
    backdrop_ = Backdrop{};
    backdrop_.source = QUrl::fromLocalFile(path);
    if (had) {  // swapping the picture keeps where the last one was
        backdrop_.x = x;
        backdrop_.y = y;
        backdrop_.height = height;
        backdrop_.opacity = opacity;
    }
    storeBackdrop();
    say(QStringLiteral("Showing %1 behind the effect. Hold Option (Alt) and drag to move it, "
                       "Option and scroll to resize it.")
            .arg(backdropName()));
}

void AppController::clearBackdrop() {
    backdrop_ = Backdrop{};
    storeBackdrop();
}

void AppController::setBackdropPlace(double x, double y, double height) {
    if (!hasBackdrop() || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(height)) {
        emit backdropChanged();
        return;
    }
    backdrop_.x = std::clamp(x, -1000.0, 1000.0);
    backdrop_.y = std::clamp(y, -1000.0, 1000.0);
    backdrop_.height = std::clamp(height, 0.05, 1000.0);
    storeBackdrop();
}

void AppController::moveBackdrop(double dx, double dy) {
    setBackdropPlace(backdrop_.x + dx, backdrop_.y + dy, backdrop_.height);
}

void AppController::scaleBackdrop(double factor) {
    if (std::isfinite(factor) && factor > 0.0) {
        setBackdropPlace(backdrop_.x, backdrop_.y, backdrop_.height * factor);
    }
}

void AppController::setBackdropOpacity(double opacity) {
    if (!hasBackdrop() || !std::isfinite(opacity)) {
        return;
    }
    backdrop_.opacity = std::clamp(opacity, 0.0, 1.0);
    storeBackdrop();
}

// --------------------------------------------------------------- export

void AppController::setExportOpen(bool open) {
    if (open != exportOpen_) {
        exportOpen_ = open;
        emit exportOpenChanged();
    }
}

QString AppController::lastUnityProject() const {
    return QSettings().value(QStringLiteral("export/unityProject")).toString();
}

QString AppController::lastUnityProjectName() const {
    const QString path = lastUnityProject();
    return path.isEmpty() ? QString() : QFileInfo(path).fileName();
}

int AppController::exportFrameCount() const {
    return vfx::editor::frameCount(session_.effect(), session_.effect().frameRate);
}

bool AppController::exportToUnity(const QUrl& folder) { return exportToUnityPath(folder.toLocalFile()); }

bool AppController::exportToUnityPath(const QString& folder) {
    if (folder.isEmpty()) {
        say(QStringLiteral("That is not a folder on this computer."), true);
        return false;
    }
    // Accept the project itself or any folder inside it (Assets, say).
    std::filesystem::path at = toPath(QFileInfo(folder).absoluteFilePath());
    std::filesystem::path project;
    for (std::filesystem::path p = at; !p.empty(); p = p.parent_path()) {
        if (vfx::editor::isUnityProject(p)) {
            project = p;
            break;
        }
        if (p == p.parent_path()) {
            break;
        }
    }
    if (project.empty()) {
        say(QStringLiteral("That folder is not inside a Unity project. Choose the project's folder: "
                           "the one that holds Assets and ProjectSettings."),
            true);
        return false;
    }
    std::filesystem::path written;
    if (!report(vfx::editor::exportToUnityProject(session_.effect(), project, &written, session_.projectFolder()))) {
        return false;
    }
    const QString projectPath = text(vfx::pathToUtf8(project));
    QSettings().setValue(QStringLiteral("export/unityProject"), projectPath);
    emit exportsChanged();
    say(QStringLiteral("Exported to %1. In Unity, the prefab is in Assets/VFXForge/Effects: drag it into a scene.")
            .arg(QFileInfo(projectPath).fileName()));
    return true;
}

bool AppController::exportUnityPackage(const QUrl& file) {
    QString path = file.toLocalFile();
    if (path.isEmpty()) {
        say(QStringLiteral("That is not a place on this computer."), true);
        return false;
    }
    if (!path.endsWith(QStringLiteral(".unitypackage"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".unitypackage");
    }
    if (!report(vfx::editor::exportUnityPackage(session_.effect(), toPath(path), session_.projectFolder()))) {
        return false;
    }
    say(QStringLiteral("Saved %1. In Unity: Assets > Import Package > Custom Package.")
            .arg(QFileInfo(path).fileName()));
    return true;
}

bool AppController::exportFrames(const QUrl& folder, int size, bool transparent, bool sheet,
                                 const QVariantMap& view) {
    const QString path = folder.toLocalFile();
    if (path.isEmpty()) {
        say(QStringLiteral("That is not a folder on this computer."), true);
        return false;
    }
    vfx::editor::FrameOptions options;
    options.width = options.height = std::clamp(size, 16, 4096);
    options.framesPerSecond = session_.effect().frameRate;
    options.transparent = transparent;
    options.sheet = sheet;
    options.images = &session_.images();
    auto number = [&view](const char* key, float fallback) {
        const QVariant v = view.value(QString::fromLatin1(key));
        bool ok = false;
        const double d = v.toDouble(&ok);
        return ok && std::isfinite(d) ? static_cast<float>(d) : fallback;
    };
    vfx::editor::View& v = options.view;
    v.centerX = number("centerX", v.centerX);
    v.centerY = number("centerY", v.centerY);
    v.unitsHigh = number("unitsHigh", v.unitsHigh);
    v.targetX = number("targetX", v.targetX);
    v.targetY = number("targetY", v.targetY);
    v.targetZ = number("targetZ", v.targetZ);
    v.yaw = number("yaw", v.yaw);
    v.pitch = number("pitch", v.pitch);
    v.distance = number("distance", v.distance);
    v.fieldOfView = number("fieldOfView", v.fieldOfView);

    const std::string stem = vfx::editor::unityFileStem(session_.effect());
    auto written = vfx::editor::exportFrames(session_.effect(), options, toPath(path), stem);
    if (!written) {
        report(written.error());
        return false;
    }
    // The sound of the same pass, mixed down, beside the frames.
    bool withSound = false;
    if (vfx::editor::hasSound(session_.effect())) {
        const auto wav = toPath(path) / vfx::pathFromUtf8(stem + " sound.wav");
        withSound = vfx::writeFileAtomic(wav, vfx::editor::mixdownWav(session_.effect(), session_.sounds())).ok();
    }
    say(QStringLiteral("Wrote %1 frames%2%3 to %4.")
            .arg(written.value().frames)
            .arg(written.value().sheet.empty() ? QString() : QStringLiteral(", a sprite sheet"))
            .arg(withSound ? QStringLiteral(" and the sound") : QString())
            .arg(QFileInfo(path).fileName()));
    return true;
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
    audio_->update(session_.effect(), session_.sounds(), session_.clock().time(), session_.clock().playing(),
                   session_.clock().timeScale(), soundOn_);

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
        if (!session_.imageProblems().empty()) {
            warning = text(session_.imageProblems().front());
        } else if (!session_.soundProblems().empty()) {
            warning = text(session_.soundProblems().front());
        } else if (!capped.empty()) {
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
