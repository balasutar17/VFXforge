// VFX Forge app: the Shader tab.
//
// Pick a shader family and a preset, change its controls, watch it animate, save it, and send
// it to Unity. The shader maths, the keyframes, saving and the Unity export all live in the
// editor layer (ShaderLib.h, ShaderPreview.h, ShaderExport.h); this class holds the open
// shader, its undo history and the timeline clock, and hands the preview picture to Qt.
#pragma once

#include <memory>
#include <vector>

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "vfx/editor/Model.h"
#include "vfx/editor/ShaderExport.h"
#include "vfx/editor/ShaderLib.h"
#include "vfx/editor/ShaderPreview.h"

class QTimer;

// What one preview picture is made from, copied so a worker thread can draw it.
struct ShaderFrame {
    vfx::editor::shader::Settings settings;
    vfx::editor::shader::Settings other;  // the pinned shader, when comparing
    bool compare = false;
    vfx::editor::shader::PreviewOptions options;
    std::shared_ptr<const vfx::editor::Model> model;
};

class ShaderStudio : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool open READ isOpen WRITE setOpen NOTIFY openChanged)
    // Change counters: QML bindings read them to refresh.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(int structureRevision READ structureRevision NOTIFY structureChanged)
    Q_PROPERTY(int animationRevision READ animationRevision NOTIFY animationChanged)
    Q_PROPERTY(int libraryRevision READ libraryRevision NOTIFY libraryChanged)
    Q_PROPERTY(int previewRevision READ previewRevision NOTIFY previewChanged)

    Q_PROPERTY(QString familyId READ familyId NOTIFY structureChanged)
    Q_PROPERTY(QString familyName READ familyName NOTIFY structureChanged)
    Q_PROPERTY(QString familySummary READ familySummary NOTIFY structureChanged)
    Q_PROPERTY(QStringList familyLimits READ familyLimits NOTIFY structureChanged)
    Q_PROPERTY(QString presetId READ presetId NOTIFY structureChanged)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    Q_PROPERTY(QString shape READ shape WRITE setShape NOTIFY previewChanged)
    Q_PROPERTY(QString blend READ blend WRITE setBlend NOTIFY changed)
    Q_PROPERTY(int quality READ quality WRITE setQuality NOTIFY changed)
    Q_PROPERTY(int background READ background WRITE setBackground NOTIFY previewChanged)
    Q_PROPERTY(bool bloom READ bloom WRITE setBloom NOTIFY previewChanged)
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY previewChanged)
    Q_PROPERTY(double yaw READ yaw WRITE setYaw NOTIFY previewChanged)
    Q_PROPERTY(double pitch READ pitch WRITE setPitch NOTIFY previewChanged)
    Q_PROPERTY(bool compareOn READ compareOn WRITE setCompareOn NOTIFY previewChanged)
    Q_PROPERTY(bool hasPinned READ hasPinned NOTIFY previewChanged)
    Q_PROPERTY(bool hasModel READ hasModel NOTIFY previewChanged)
    Q_PROPERTY(double renderMs READ renderMs NOTIFY renderMsChanged)

    // Layer shaders: which layer the controls show (-1 shows the whole shader's own settings).
    Q_PROPERTY(bool isLayers READ isLayers NOTIFY structureChanged)
    Q_PROPERTY(int selectedLayer READ selectedLayer WRITE setSelectedLayer NOTIFY structureChanged)

    // The timeline.
    Q_PROPERTY(double playhead READ playhead WRITE setPlayhead NOTIFY playheadChanged)
    Q_PROPERTY(double timelineTime READ timelineTime NOTIFY playheadChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double duration READ duration WRITE setDuration NOTIFY animationChanged)
    Q_PROPERTY(double playbackSpeed READ playbackSpeed WRITE setPlaybackSpeed NOTIFY animationChanged)
    Q_PROPERTY(QString loopMode READ loopMode WRITE setLoopMode NOTIFY animationChanged)

    // The file.
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileChanged)
    Q_PROPERTY(bool hasFile READ hasFile NOTIFY fileChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool statusIsError READ statusIsError NOTIFY statusChanged)
    Q_PROPERTY(QStringList report READ report NOTIFY reportChanged)
    Q_PROPERTY(QString reportTitle READ reportTitle NOTIFY reportChanged)
    Q_PROPERTY(QString lastUnityProject READ lastUnityProject NOTIFY statusChanged)

public:
    explicit ShaderStudio(QObject* parent = nullptr);
    ~ShaderStudio() override;

    bool isOpen() const { return open_; }
    void setOpen(bool open);
    int revision() const { return revision_; }
    int structureRevision() const { return structureRevision_; }
    int animationRevision() const { return animationRevision_; }
    int libraryRevision() const { return libraryRevision_; }
    int previewRevision() const { return previewRevision_; }

    QString familyId() const;
    QString familyName() const;
    QString familySummary() const;
    QStringList familyLimits() const;
    QString presetId() const { return QString::fromStdString(s_.preset); }
    QString name() const { return QString::fromStdString(s_.name); }
    void setName(const QString& name);
    QString shape() const { return QString::fromStdString(s_.shape); }
    void setShape(const QString& shape);
    QString blend() const;
    void setBlend(const QString& blend);
    int quality() const { return s_.quality; }
    void setQuality(int quality);
    int background() const { return background_; }
    void setBackground(int background);
    bool bloom() const { return bloom_; }
    void setBloom(bool on);
    double zoom() const { return zoom_; }
    void setZoom(double zoom);
    double yaw() const { return yaw_; }
    void setYaw(double yaw);
    double pitch() const { return pitch_; }
    void setPitch(double pitch);
    bool compareOn() const { return compareOn_; }
    void setCompareOn(bool on);
    bool hasPinned() const { return pinned_ != nullptr; }
    bool hasModel() const { return model_ != nullptr; }
    double renderMs() const { return renderMs_; }
    void reportRenderMs(double ms);

    double playhead() const { return playhead_; }
    void setPlayhead(double seconds);
    double timelineTime() const;
    bool playing() const { return playing_; }
    double duration() const { return s_.animation.duration; }
    void setDuration(double seconds);
    double playbackSpeed() const { return s_.animation.speed; }
    void setPlaybackSpeed(double speed);
    QString loopMode() const;
    void setLoopMode(const QString& mode);

    bool dirty() const { return dirty_; }
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    QString fileName() const;
    bool hasFile() const { return !file_.isEmpty(); }
    QString status() const { return status_; }
    bool statusIsError() const { return statusError_; }
    QStringList report() const { return report_; }
    QString reportTitle() const { return reportTitle_; }
    QString lastUnityProject() const;

    // For the preview items.
    ShaderFrame frame() const;

    // ---- the library
    Q_INVOKABLE QStringList tags() const;
    Q_INVOKABLE QVariantList library(const QString& search, const QString& tag) const;
    Q_INVOKABLE QVariantList userPresets() const;
    Q_INVOKABLE bool isFavorite(const QString& key) const;
    Q_INVOKABLE void toggleFavorite(const QString& key);
    Q_INVOKABLE void loadPreset(const QString& family, const QString& preset);
    Q_INVOKABLE void loadUserPreset(const QString& name);
    Q_INVOKABLE void saveUserPreset(const QString& name);
    Q_INVOKABLE void deleteUserPreset(const QString& name);

    // ---- layers (the "layers" family): the stack, top layer first
    bool isLayers() const;
    int selectedLayer() const { return selectedLayer_; }
    void setSelectedLayer(int slot);
    Q_INVOKABLE QVariantList layers() const;
    Q_INVOKABLE QVariantList elements() const;
    Q_INVOKABLE bool addLayer(int element);
    Q_INVOKABLE void removeLayer(int slot);
    Q_INVOKABLE void duplicateLayer(int slot);
    Q_INVOKABLE void moveLayer(int slot, int delta);
    Q_INVOKABLE void toggleLayer(int slot);
    Q_INVOKABLE void renameLayer(int slot, const QString& name);
    Q_INVOKABLE void newLayerShader();

    // ---- the controls
    Q_INVOKABLE QVariantList controls(const QString& search, bool advanced) const;
    Q_INVOKABLE double number(int index) const;
    Q_INVOKABLE QVariantMap colorInfo(int index) const;
    Q_INVOKABLE QColor colorShown(int index) const;
    Q_INVOKABLE bool isDefault(int index) const;
    Q_INVOKABLE bool isAnimated(int index) const;
    Q_INVOKABLE bool hasKeyHere(int index) const;
    Q_INVOKABLE void checkpoint();
    Q_INVOKABLE void setNumber(int index, double value);
    Q_INVOKABLE void setChoice(int index, int choice);
    Q_INVOKABLE void setColorHsv(int index, double hue, double saturation, double value, double boost);
    Q_INVOKABLE void resetControl(int index);
    Q_INVOKABLE void resetAll();
    Q_INVOKABLE void colorVariant(double degrees);

    // ---- the timeline
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void toggleKey(int index);
    Q_INVOKABLE void removeTrack(int index);
    Q_INVOKABLE QVariantList tracks() const;
    Q_INVOKABLE void setKeyEasing(int index, int key, const QString& easing);
    Q_INVOKABLE void removeKey(int index, int key);
    Q_INVOKABLE void moveKey(int index, int key, double time);
    Q_INVOKABLE QStringList animationModes() const;
    Q_INVOKABLE QString animationModeTip(const QString& mode) const;
    Q_INVOKABLE void applyAnimationMode(const QString& mode);
    Q_INVOKABLE void triggerHit();
    Q_INVOKABLE bool hasHit() const;

    // ---- the preview
    Q_INVOKABLE QStringList shapes() const;
    Q_INVOKABLE QStringList qualityNames() const;
    Q_INVOKABLE QString qualityNote(int quality) const;
    Q_INVOKABLE void resetCamera();
    Q_INVOKABLE void pinForCompare();
    Q_INVOKABLE bool loadPreviewModel(const QUrl& file);
    Q_INVOKABLE void clearPreviewModel();

    // ---- the file and Unity
    Q_INVOKABLE void newShader();
    Q_INVOKABLE void duplicate();
    Q_INVOKABLE bool openFile(const QUrl& file);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl& file);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void validate();
    Q_INVOKABLE bool exportToUnity(const QUrl& folder);
    Q_INVOKABLE void clearReport();

signals:
    void openChanged();
    void changed();
    void structureChanged();
    void animationChanged();
    void libraryChanged();
    void previewChanged();
    void playheadChanged();
    void playingChanged();
    void historyChanged();
    void fileChanged();
    void statusChanged();
    void reportChanged();
    void renderMsChanged();

private:
    void touch(bool structure = false, bool animation = false);
    void adopt(const vfx::editor::shader::Settings& s, bool structure, bool clean);
    void say(const QString& message, bool error = false);
    void remember(const QString& key);
    void tick();
    void pushUndo();
    const vfx::editor::shader::Family* family() const;
    void setReport(const QString& title, const QStringList& lines);

    void normaliseSelection();

    bool open_ = false;
    int selectedLayer_ = -1;
    vfx::editor::shader::Settings s_;
    std::vector<vfx::editor::shader::Settings> undo_, redo_;
    bool checkpointed_ = false;
    std::unique_ptr<vfx::editor::shader::Settings> pinned_;
    std::shared_ptr<const vfx::editor::Model> model_;
    int revision_ = 0, structureRevision_ = 0, animationRevision_ = 0, libraryRevision_ = 0, previewRevision_ = 0;
    int background_ = 0;
    bool bloom_ = true, compareOn_ = false;
    double zoom_ = 1.0, yaw_ = 0.55, pitch_ = 0.30;
    double playhead_ = 0.0;
    bool playing_ = true;
    double renderMs_ = 0;
    bool dirty_ = false;
    QString file_;
    QString status_;
    bool statusError_ = false;
    QStringList report_;
    QString reportTitle_;
    QTimer* timer_ = nullptr;
    QElapsedTimer clock_;
    QElapsedTimer hitClock_;
    bool hitActive_ = false;
    qint64 lastTick_ = 0;
};

// The preview picture. It draws on a worker thread, newest request first, so dragging a slider
// never waits for a picture.
class ShaderPreviewItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(ShaderStudio* studio READ studio WRITE setStudio NOTIFY studioChanged)
    Q_PROPERTY(int maxSize READ maxSize WRITE setMaxSize NOTIFY studioChanged)

public:
    explicit ShaderPreviewItem(QQuickItem* parent = nullptr);
    ShaderStudio* studio() const { return studio_; }
    void setStudio(ShaderStudio* studio);
    int maxSize() const { return maxSize_; }
    void setMaxSize(int size);
    void paint(QPainter* painter) override;

signals:
    void studioChanged();

protected:
    void geometryChange(const QRectF& next, const QRectF& previous) override;

private:
    void schedule();

    QPointer<ShaderStudio> studio_;
    QImage image_;
    bool busy_ = false, again_ = false;
    int maxSize_ = 512;
    int side_ = 384;  // the picture's size, lowered when drawing is slow
};

// A small picture of one built-in preset, for the library list.
class ShaderThumb : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QString family READ family WRITE setFamily NOTIFY changed)
    Q_PROPERTY(QString preset READ preset WRITE setPreset NOTIFY changed)

public:
    explicit ShaderThumb(QQuickItem* parent = nullptr);
    QString family() const { return family_; }
    void setFamily(const QString& family);
    QString preset() const { return preset_; }
    void setPreset(const QString& preset);
    void paint(QPainter* painter) override;

signals:
    void changed();

private:
    void load();
    QString family_, preset_;
    QImage image_;
};
