// VFX Forge app: the bridge between the window and an editing session.
//
// The window (QML) only ever talks to this object. This object only ever
// talks to vfx::editor::Session. No rule about effects lives here: it
// translates between the window's types and the session's, and says when
// something changed.
#pragma once

#include <memory>

#include <QColor>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include "AudioPlayer.h"
#include "vfx/editor/Model.h"
#include "vfx/editor/Session.h"

class AppController : public QObject {
    Q_OBJECT

    // The open effect.
    Q_PROPERTY(QString effectName READ effectName NOTIFY documentChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY documentChanged)
    Q_PROPERTY(QString windowTitle READ windowTitle NOTIFY documentChanged)
    Q_PROPERTY(bool hasFile READ hasFile NOTIFY documentChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY documentChanged)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY documentChanged)
    Q_PROPERTY(bool threeD READ threeD NOTIFY documentChanged)
    Q_PROPERTY(bool loop READ loop NOTIFY documentChanged)
    // The layers that jump back when the loop starts again, as a sentence;
    // empty when nothing does. smoothLoop() mends them, as one undo step.
    Q_PROPERTY(QString loopJump READ loopJump NOTIFY documentChanged)
    // The viewport's handles: where they stand (the middle of the selected
    // layer, or of its whole group) and what they change.
    Q_PROPERTY(QVariantMap gizmo READ gizmo NOTIFY gizmoChanged)
    // move, turn or size: which handles the viewport shows.
    Q_PROPERTY(QString gizmoMode READ gizmoMode WRITE setGizmoMode NOTIFY gizmoChanged)
    // center, active or individual: what Turn and Size happen about.
    Q_PROPERTY(QString pivotMode READ pivotMode WRITE setPivotMode NOTIFY gizmoChanged)
    // world, or local (the active layer's own axes: up is the way it sends particles).
    Q_PROPERTY(QString gizmoSpace READ gizmoSpace WRITE setGizmoSpace NOTIFY gizmoChanged)
    // The Layers list as shown: group headings (nested) and layers, with
    // what is selected. Folded groups hide what is inside them.
    Q_PROPERTY(QVariantList layerRows READ layerRows NOTIFY layersChanged)
    // The one group selected on its own, or empty.
    Q_PROPERTY(QString selectedGroup READ selectedGroup NOTIFY layersChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY layersChanged)
    Q_PROPERTY(double duration READ duration NOTIFY documentChanged)
    Q_PROPERTY(double frameRate READ frameRate NOTIFY documentChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY documentChanged)
    Q_PROPERTY(double seed READ seed NOTIFY documentChanged)

    // Undo.
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY documentChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY documentChanged)
    Q_PROPERTY(QString undoName READ undoName NOTIFY documentChanged)
    Q_PROPERTY(QString redoName READ redoName NOTIFY documentChanged)

    // Layers and the Simple controls of the selected one.
    Q_PROPERTY(QVariantList layers READ layers NOTIFY layersChanged)
    Q_PROPERTY(int selectedLayer READ selectedLayer WRITE selectLayer NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedLayerName READ selectedLayerName NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList controls READ controls NOTIFY controlsChanged)
    // Simple shows a handful of controls; Advanced shows every setting of
    // the layer, grouped, with curve and gradient editors.
    Q_PROPERTY(bool advanced READ advanced WRITE setAdvanced NOTIFY controlsChanged)
    Q_PROPERTY(QVariantList curveShapes READ curveShapes CONSTANT)
    // One-click changes to the whole effect built on the animation
    // principles: on twos, squash and stretch, anticipation, overshoot, curl.
    Q_PROPERTY(QVariantList motionChanges READ motionChanges CONSTANT)

    // Playback.
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(double timeScale READ timeScale WRITE setTimeScale NOTIFY playbackChanged)
    Q_PROPERTY(double time READ time NOTIFY frameChanged)
    Q_PROPERTY(int frame READ frame NOTIFY frameChanged)
    Q_PROPERTY(int particleCount READ particleCount NOTIFY frameChanged)
    Q_PROPERTY(int framesPerSecond READ framesPerSecond NOTIFY frameChanged)

    // One line for the status bar, and a standing warning if there is one.
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY messageChanged)
    Q_PROPERTY(QString warning READ warning NOTIFY frameChanged)
    Q_PROPERTY(QString version READ version CONSTANT)

    // The effect library.
    Q_PROPERTY(QVariantList presets READ presets CONSTANT)
    Q_PROPERTY(QStringList presetCategories READ presetCategories CONSTANT)
    Q_PROPERTY(bool libraryOpen READ libraryOpen WRITE setLibraryOpen NOTIFY libraryOpenChanged)

    // A picture shown behind the effect, to work against. It is not part of
    // the effect and is remembered on this computer only.
    // The target: the picture (a slot symbol, a logo) the effect is made for.
    // Saved with the effect. Layers set to Fit to target follow its outline.
    Q_PROPERTY(bool hasTarget READ hasTarget NOTIFY documentChanged)
    Q_PROPERTY(QUrl targetSource READ targetSource NOTIFY documentChanged)
    Q_PROPERTY(QString targetName READ targetName NOTIFY documentChanged)
    Q_PROPERTY(double targetWidth READ targetWidth NOTIFY documentChanged)
    Q_PROPERTY(double targetHeight READ targetHeight NOTIFY documentChanged)
    Q_PROPERTY(double targetX READ targetX NOTIFY documentChanged)
    Q_PROPERTY(double targetY READ targetY NOTIFY documentChanged)
    Q_PROPERTY(int fittedLayers READ fittedLayers NOTIFY documentChanged)
    // What the viewport shows: composite (target and effect), effect, target.
    Q_PROPERTY(QString previewMode READ previewMode WRITE setPreviewMode NOTIFY previewChanged)
    // Behind it all: dark (the editor's), black, white, checker.
    Q_PROPERTY(QString previewBackground READ previewBackground WRITE setPreviewBackground NOTIFY previewChanged)
    Q_PROPERTY(QUrl backdropSource READ backdropSource NOTIFY backdropChanged)
    Q_PROPERTY(QString backdropName READ backdropName NOTIFY backdropChanged)
    Q_PROPERTY(bool hasBackdrop READ hasBackdrop NOTIFY backdropChanged)
    Q_PROPERTY(double backdropX READ backdropX NOTIFY backdropChanged)
    Q_PROPERTY(double backdropY READ backdropY NOTIFY backdropChanged)
    Q_PROPERTY(double backdropHeight READ backdropHeight NOTIFY backdropChanged)
    Q_PROPERTY(double backdropOpacity READ backdropOpacity NOTIFY backdropChanged)
    // A 3D model to make the effect on (see vfx/editor/Model.h).
    Q_PROPERTY(bool hasModel READ hasModel NOTIFY modelChanged)
    Q_PROPERTY(QString modelName READ modelName NOTIFY modelChanged)
    Q_PROPERTY(int modelTriangles READ modelTriangles NOTIFY modelChanged)
    Q_PROPERTY(QVariantMap modelPlace READ modelPlaceMap NOTIFY modelChanged)
    Q_PROPERTY(bool hideBehindModel READ hideBehindModel WRITE setHideBehindModel NOTIFY modelChanged)

    // Exports.
    Q_PROPERTY(bool exportOpen READ exportOpen WRITE setExportOpen NOTIFY exportOpenChanged)
    Q_PROPERTY(QString lastUnityProject READ lastUnityProject NOTIFY exportsChanged)
    Q_PROPERTY(QString lastUnityProjectName READ lastUnityProjectName NOTIFY exportsChanged)
    Q_PROPERTY(int exportFrameCount READ exportFrameCount NOTIFY documentChanged)

    // Sound.
    Q_PROPERTY(bool soundOn READ soundOn WRITE setSoundOn NOTIFY soundOnChanged)
    Q_PROPERTY(bool soundAvailable READ soundAvailable CONSTANT)
    Q_PROPERTY(QVariantList librarySounds READ librarySounds CONSTANT)
    Q_PROPERTY(bool importingSound READ importingSound NOTIFY importingSoundChanged)

public:
    explicit AppController(QObject* parent = nullptr);

    vfx::editor::Session& session() { return session_; }
    const vfx::editor::Session& session() const { return session_; }

    QString effectName() const;
    QString fileName() const;
    QString windowTitle() const;
    bool hasFile() const { return session_.hasFile(); }
    bool dirty() const { return session_.dirty(); }
    bool readOnly() const { return session_.readOnly(); }
    bool threeD() const;
    bool loop() const;
    double duration() const;
    double frameRate() const;
    int frameCount() const;
    double seed() const;

    bool canUndo() const;
    bool canRedo() const;
    QString undoName() const;
    QString redoName() const;

    QVariantList layers() const { return layers_; }
    int selectedLayer() const { return selected_; }
    QString selectedLayerName() const;
    QVariantList controls() const { return controls_; }
    bool advanced() const { return advanced_; }
    void setAdvanced(bool advanced);
    QVariantList curveShapes() const;
    QVariantList motionChanges() const;

    bool playing() const;
    double timeScale() const;
    double time() const { return time_; }
    int frame() const { return frame_; }
    int particleCount() const { return particles_; }
    int framesPerSecond() const { return fps_; }

    QString message() const { return message_; }
    bool messageIsError() const { return messageIsError_; }
    QString warning() const { return warning_; }
    QString version() const;

    QVariantList presets() const;
    QStringList presetCategories() const;
    bool libraryOpen() const { return libraryOpen_; }
    void setLibraryOpen(bool open);

    QUrl backdropSource() const { return backdrop_.source; }
    QString backdropName() const;
    bool hasBackdrop() const { return !backdrop_.source.isEmpty(); }
    double backdropX() const { return backdrop_.x; }
    double backdropY() const { return backdrop_.y; }
    double backdropHeight() const { return backdrop_.height; }
    double backdropOpacity() const { return backdrop_.opacity; }

    bool hasModel() const { return model_ != nullptr; }
    QString modelName() const;
    int modelTriangles() const { return model_ ? static_cast<int>(model_->triangleCount()) : 0; }
    QVariantMap modelPlaceMap() const;
    bool hideBehindModel() const { return hideBehindModel_; }
    void setHideBehindModel(bool hide);
    // For the viewport, which draws the model.
    std::shared_ptr<const vfx::editor::Model> model() const { return model_; }
    const vfx::editor::ModelPlace& modelPlace() const { return modelPlace_; }

    bool exportOpen() const { return exportOpen_; }
    void setExportOpen(bool open);
    QString lastUnityProject() const;
    QString lastUnityProjectName() const;
    int exportFrameCount() const;

    // ------------------------------------------------------------ files
    Q_INVOKABLE void newEffect(bool threeD);
    Q_INVOKABLE bool openFile(const QUrl& file);
    Q_INVOKABLE bool openPath(const QString& path);
    Q_INVOKABLE bool save();  // false when there is no file yet, or it failed
    Q_INVOKABLE bool saveAs(const QUrl& file);

    // ---------------------------------------------------------- library
    Q_INVOKABLE bool openPreset(const QString& id);  // replaces the open effect
    Q_INVOKABLE bool addPreset(const QString& id);   // adds its layers to the open effect, as a group
    // A group of layers (an effect added from the library), changed as one.
    Q_INVOKABLE void moveGroup(const QString& group, double dx, double dy, double dz);
    Q_INVOKABLE void delayGroup(const QString& group, double seconds);
    Q_INVOKABLE void sizeGroup(const QString& group, double factor);
    Q_INVOKABLE void setGroupEnabled(const QString& group, bool enabled);
    Q_INVOKABLE void setGroupLocked(const QString& group, bool locked);
    Q_INVOKABLE void removeGroup(const QString& group);
    Q_INVOKABLE void ungroup(const QString& group);
    // A drag of one of the viewport's handles ("move", "turn" or "size").
    // Each update is the whole change since the drag began; the drag
    // undoes as one step.
    // kind is move, turn or size. A drag's change, as a map: move [x, y, z]
    // (world units), turn (degrees) about axis (0, 1, 2 of the gizmo's axes),
    // factor (size of everything), scale [x, y, z] (stretch along the world
    // axes). Each drag is the whole change since it began; it undoes as one step.
    Q_INVOKABLE bool beginGizmo(const QString& kind);
    Q_INVOKABLE void dragGizmo(const QVariantMap& change);
    Q_INVOKABLE void endGizmo();
    Q_INVOKABLE void cancelGizmo();
    // Moves the pivot only (Option-drag of the middle); nothing else moves.
    Q_INVOKABLE void movePivot(double dx, double dy, double dz);
    Q_INVOKABLE void resetPivot();

    // Selecting. modifiers are Qt's: Cmd/Ctrl adds or takes away, Shift
    // selects everything between the active layer and this one.
    Q_INVOKABLE void clickLayer(int index, int modifiers);
    Q_INVOKABLE void clickGroup(const QString& path, int modifiers);
    Q_INVOKABLE void selectNone();
    Q_INVOKABLE void toggleFold(const QString& path);
    // A click in the viewport: the layer whose emitter is within a few
    // pixels of it (see gizmo().markers) is selected, otherwise nothing.
    Q_INVOKABLE void clickViewport(int layerIndex, int modifiers);
    // The selected layers become a group (Cmd+G); the selected group is
    // ungrouped one level (Cmd+Shift+G).
    Q_INVOKABLE void groupSelection();
    Q_INVOKABLE void ungroupSelection();
    QString gizmoMode() const { return gizmoMode_; }
    void setGizmoMode(const QString& mode);
    QString pivotMode() const { return pivotMode_; }
    void setPivotMode(const QString& mode);
    QString gizmoSpace() const { return gizmoSpace_; }
    void setGizmoSpace(const QString& space);
    QVariantList layerRows() const { return layerRows_; }
    QString selectedGroup() const;
    int selectionCount() const;
    // The same in one click: for the Group panel's buttons.
    Q_INVOKABLE void turnSelection(double degrees);
    QVariantMap gizmo() const;

    // ----------------------------------------------------------- target
    // Any picture Qt can read (PNG, JPG, WebP...); it is kept as a PNG.
    Q_INVOKABLE bool setTargetFile(const QUrl& image);
    Q_INVOKABLE void clearTarget();
    Q_INVOKABLE void placeTarget(double width, double x, double y);
    // Makes the selected layers follow the target's outline.
    Q_INVOKABLE void fitSelectionToTarget();
    bool hasTarget() const;
    QUrl targetSource() const;
    QString targetName() const;
    double targetWidth() const { return session_.effect().targetWidth; }
    double targetHeight() const;
    double targetX() const { return session_.effect().targetPosition.x; }
    double targetY() const { return session_.effect().targetPosition.y; }
    int fittedLayers() const;
    QString previewMode() const { return previewMode_; }
    void setPreviewMode(const QString& mode);
    QString previewBackground() const { return previewBackground_; }
    void setPreviewBackground(const QString& background);

    // --------------------------------------------------------- backdrop
    Q_INVOKABLE void setBackdrop(const QUrl& image);
    Q_INVOKABLE void clearBackdrop();
    Q_INVOKABLE void setBackdropPlace(double x, double y, double height);
    // The model: one of the artist's own (.obj, .glb, .gltf), or the practice dummy.
    Q_INVOKABLE bool useModel(const QUrl& file);
    Q_INVOKABLE void useSampleModel();
    Q_INVOKABLE void clearModel();
    // key: "x", "y", "z", "turn" or "height".
    Q_INVOKABLE void setModelPlace(const QString& key, double value);
    Q_INVOKABLE void moveBackdrop(double dx, double dy);
    Q_INVOKABLE void scaleBackdrop(double factor);
    Q_INVOKABLE void setBackdropOpacity(double opacity);

    // ----------------------------------------------------------- export
    // Into a Unity project: the folder may be the project itself or any
    // folder inside it.
    Q_INVOKABLE bool exportToUnity(const QUrl& folder);
    Q_INVOKABLE bool exportToUnityPath(const QString& folder);
    Q_INVOKABLE bool exportUnityPackage(const QUrl& file);
    Q_INVOKABLE bool exportToUnreal(const QUrl& folder, int size, const QVariantMap& view);
    // Numbered PNG frames, and a sprite sheet, showing what the view shows.
    Q_INVOKABLE bool exportFrames(const QUrl& folder, int size, bool transparent, bool sheet,
                                  const QVariantMap& view);

    // ------------------------------------------------------------ edits
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void beginEdit(const QString& name);
    Q_INVOKABLE void endEdit();

    Q_INVOKABLE void setEffectName(const QString& name);
    Q_INVOKABLE void setLoop(bool loop);
    Q_INVOKABLE void setDuration(double seconds);
    Q_INVOKABLE void setFrameRate(double framesPerSecond);
    Q_INVOKABLE void setSeed(double seed);
    Q_INVOKABLE void newVariation();
    // Makes one of motionChanges to every layer that is not locked, as one
    // undo step, and says what it did.
    Q_INVOKABLE void applyMotionChange(const QString& key);
    Q_INVOKABLE void smoothLoop() { applyMotionChange(QStringLiteral("smooth-loop")); }
    QString loopJump() const;

    Q_INVOKABLE void selectLayer(int index);
    Q_INVOKABLE void addLayer();
    Q_INVOKABLE void removeLayer(int index);
    Q_INVOKABLE void renameLayer(int index, const QString& name);
    Q_INVOKABLE void setLayerEnabled(int index, bool enabled);
    // A locked layer is left alone by Refine and by rebuilding from a reference.
    Q_INVOKABLE void setLayerLocked(int index, bool locked);
    // Moves a layer up (-1, drawn earlier, further back) or down (+1) the list.
    Q_INVOKABLE void moveLayer(int index, int by);
    Q_INVOKABLE void duplicateLayer(int index);

    Q_INVOKABLE void setControlNumber(int index, double value);
    Q_INVOKABLE void setControlRange(int index, double from, double to);
    Q_INVOKABLE void setControlRandom(int index, bool random);
    Q_INVOKABLE void setControlColor(int index, const QColor& color);
    Q_INVOKABLE void setControlDirection(int index, double heading, double tilt);
    Q_INVOKABLE void setControlChoice(int index, const QString& option);
    // Picks a particle shape, as drawn: its own settings (points,
    // thickness...) go back to none in the same undo step.
    Q_INVOKABLE void chooseShape(int index, const QString& option);
    Q_INVOKABLE void setControlFlag(int index, bool on);
    Q_INVOKABLE void setControlVector(int index, double x, double y, double z);
    // A value that is one number ("fixed"), a random range ("random") or a
    // curve ("curve"): changes which, keeping the look as close as it can.
    Q_INVOKABLE void setControlMode(int index, const QString& mode);
    // Curves. The editor in the window holds a list of points, each
    // {t, v, smooth}; these take such a list and give back the list as it
    // was accepted (in order, within the limits), so the two never differ.
    Q_INVOKABLE QVariantList setControlCurve(int index, const QVariantList& keys);
    Q_INVOKABLE QVariantList applyCurveShape(int index, const QString& shape);
    // The curve between its points, bends included, as the simulation plays
    // it: a list of {t, v} joined by straight lines.
    Q_INVOKABLE QVariantList curveLine(int index, const QVariantList& keys) const;
    // Colour gradients. Stops are {t, color, bright, alpha}: a screen
    // colour, how many times brighter than that it is (1 or more: fire is
    // white-hot at 2 or 3), and its opacity.
    Q_INVOKABLE QVariantList setControlGradient(int index, const QVariantList& stops);
    // The gradient drawn as that many colours side by side (alpha is each
    // one's opacity), and the stop that would sit at one moment.
    Q_INVOKABLE QVariantList gradientStrip(const QVariantList& stops, int count) const;
    Q_INVOKABLE QVariantMap gradientStopAt(const QVariantList& stops, double t) const;
    // Bursts: a list of {time, count}.
    Q_INVOKABLE QVariantList setControlBursts(int index, const QVariantList& items);

    // ------------------------------------------------------------- sound
    bool soundOn() const { return soundOn_; }
    void setSoundOn(bool on);
    bool soundAvailable() const;
    QVariantList librarySounds() const;
    bool importingSound() const { return importingSound_; }

    // WAV and AIFF are read here; MP3, OGG, FLAC and others are converted to
    // WAV first by the system's decoder, which takes a moment.
    Q_INVOKABLE bool useSoundFile(const QUrl& file);
    Q_INVOKABLE bool useSoundPath(const QString& path);
    Q_INVOKABLE bool useLibrarySound(const QString& id);
    Q_INVOKABLE void clearSound();
    Q_INVOKABLE void previewLibrarySound(const QString& id);
    Q_INVOKABLE void previewLayerSound();
    Q_INVOKABLE void stopPreview();
    // Sound module settings of the selected layer.
    Q_INVOKABLE void setSoundNumber(const QString& key, double value);
    Q_INVOKABLE void setSoundChoice(const QString& key, const QString& value);
    Q_INVOKABLE void setSoundFlag(const QString& key, bool value);

    // ----------------------------------------------- the artist's pictures
    // Any picture Qt can read (PNG, JPEG, WebP, TIFF...) becomes a PNG copy
    // next to the effect, drawn by the selected layer instead of its Shape.
    // A name like "fire_4x2.png" sets the sprite-sheet grid to 4 by 2.
    Q_INVOKABLE bool usePicture(const QUrl& file);
    Q_INVOKABLE bool usePicturePath(const QString& path);
    Q_INVOKABLE bool useSamplePicture();  // the toon flame sheet that ships with the app
    Q_INVOKABLE void clearPicture();
    // Sprite-sheet settings of the selected layer's picture: columns, rows,
    // frames, fps (numbers), animate (life, loop, random), randomStart.
    Q_INVOKABLE void setPictureNumber(const QString& key, double value);
    Q_INVOKABLE void setPictureChoice(const QString& key, const QString& value);
    Q_INVOKABLE void setPictureFlag(const QString& key, bool value);

    // --------------------------------------------------------- playback
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void restart();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void stepFrames(int frames);
    void setTimeScale(double scale);

    // Called once per displayed frame with the real time since the last one.
    Q_INVOKABLE void tick(double seconds);

    // For the Reference to VFX workspace, which changes the effect as a
    // whole: refresh everything shown, say something, frame the viewport.
    void effectChangedOutside(const QString& message, bool error = false);
    void suggestView(double x, double y, double unitsHigh) { emit viewSuggested(x, y, unitsHigh); }

signals:
    void documentChanged();
    void layersChanged();
    void selectionChanged();
    void gizmoChanged();
    void controlsChanged();
    // Either side of the list of controls being rebuilt: first so that the
    // panel can note where it is scrolled to, then whether to go back there.
    void controlsAboutToChange();
    void controlsPlace(bool keep);
    void playbackChanged();
    void frameChanged();
    void messageChanged();
    // The picture needs drawing again.
    void redraw();
    void libraryOpenChanged();
    void backdropChanged();
    void previewChanged();
    void modelChanged();
    void exportOpenChanged();
    void exportsChanged();
    void soundOnChanged();
    void importingSoundChanged();
    // The viewport should show this part of the world: the point in the
    // middle and how many units fit top to bottom. Zero height means "reset".
    void viewSuggested(double x, double y, double unitsHigh);

private:
    enum Refresh { Values = 0, Structure = 1 };
    void groupDone(const vfx::Status& status, const QString& said);
    vfx::editor::LayerTransform gizmoTransform_;
    vfx::Vec3 gizmoPivot_{};
    QString gizmoKind_;
    QString gizmoMode_ = QStringLiteral("move");
    QString previewMode_ = QStringLiteral("composite");
    QString previewBackground_ = QStringLiteral("dark");
    mutable double targetAspect_ = 1.0;  // height over width of the target's picture
    mutable QString targetAspectFor_;
    QString pivotMode_ = QStringLiteral("center");
    QString gizmoSpace_ = QStringLiteral("world");
    vfx::editor::Selection selection_;
    bool selectedNothing_ = false;     // the artist clicked away: no handles
    vfx::Id selectionFor_;             // the active layer the selection was made for
    vfx::Vec3 pivotShift_{};           // moved by hand, until the selection changes
    QSet<QString> folded_;
    QVariantList layerRows_;
    void syncSelection();
    void selectionEdited();
    vfx::Vec3 currentPivot(const std::vector<vfx::Id>& targets) const;
    std::vector<vfx::Vec3> gizmoAxes() const;

    // Call after anything that may have changed the document.
    void changed(Refresh what);
    void rebuildLayers();
    void rebuildControls();
    void refreshControls();
    void say(const QString& text, bool error = false);
    bool report(const vfx::Status& status);
    vfx::Id selectedLayerId() const;
    bool control(int index, vfx::editor::ControlView& out) const;
    // The settings shown now: the Simple controls, or every setting.
    std::vector<vfx::editor::ControlView> views() const;
    QVariantMap pictureInfo() const;
    QVariantList shapeEdits(int number) const;
    QVariantMap soundInfo() const;
    vfx::Id soundModuleId() const;
    bool useSoundBytes(const std::string& wav, const QString& name);
    bool usePictureImage(const QImage& image, const QString& name);
    vfx::Id spriteModuleId() const;
    void applyControl(int index, const vfx::Value& value);
    void loadBackdrop();
    void storeBackdrop();
    QString backdropKey() const;

    struct Backdrop {
        QUrl source;
        double x = 0.0, y = 1.5, height = 3.0, opacity = 1.0;
    };
    Backdrop backdrop_;
    void loadStage();
    void storeStage();
    bool showModelFile(const QString& path, bool quiet);
    std::shared_ptr<const vfx::editor::Model> model_;
    QString modelPath_;  // empty for the practice dummy
    vfx::editor::ModelPlace modelPlace_;
    bool hideBehindModel_ = true;
    bool libraryOpen_ = false;
    bool exportOpen_ = false;

    vfx::editor::Session session_;
    QVariantList layers_;
    QVariantList controls_;
    int selected_ = 0;
    bool advanced_ = false;
    vfx::Id shownLayer_;
    bool shownAdvanced_ = false;

    double time_ = 0.0;
    int frame_ = 0;
    int particles_ = 0;
    int fps_ = 0;
    double fpsWindow_ = 0.0;
    int fpsFrames_ = 0;
    bool wasPlaying_ = false;

    AudioPlayer* audio_ = nullptr;
    bool soundOn_ = true;
    bool importingSound_ = false;

    QString message_;
    bool messageIsError_ = false;
    QString warning_;
};
