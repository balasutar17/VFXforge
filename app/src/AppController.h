// VFX Forge app: the bridge between the window and an editing session.
//
// The window (QML) only ever talks to this object. This object only ever
// talks to vfx::editor::Session. No rule about effects lives here: it
// translates between the window's types and the session's, and says when
// something changed.
#pragma once

#include <QColor>
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include "AudioPlayer.h"
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
    Q_PROPERTY(QUrl backdropSource READ backdropSource NOTIFY backdropChanged)
    Q_PROPERTY(QString backdropName READ backdropName NOTIFY backdropChanged)
    Q_PROPERTY(bool hasBackdrop READ hasBackdrop NOTIFY backdropChanged)
    Q_PROPERTY(double backdropX READ backdropX NOTIFY backdropChanged)
    Q_PROPERTY(double backdropY READ backdropY NOTIFY backdropChanged)
    Q_PROPERTY(double backdropHeight READ backdropHeight NOTIFY backdropChanged)
    Q_PROPERTY(double backdropOpacity READ backdropOpacity NOTIFY backdropChanged)

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
    Q_INVOKABLE bool addPreset(const QString& id);   // adds its layers to the open effect

    // --------------------------------------------------------- backdrop
    Q_INVOKABLE void setBackdrop(const QUrl& image);
    Q_INVOKABLE void clearBackdrop();
    Q_INVOKABLE void setBackdropPlace(double x, double y, double height);
    Q_INVOKABLE void moveBackdrop(double dx, double dy);
    Q_INVOKABLE void scaleBackdrop(double factor);
    Q_INVOKABLE void setBackdropOpacity(double opacity);

    // ----------------------------------------------------------- export
    // Into a Unity project: the folder may be the project itself or any
    // folder inside it.
    Q_INVOKABLE bool exportToUnity(const QUrl& folder);
    Q_INVOKABLE bool exportToUnityPath(const QString& folder);
    Q_INVOKABLE bool exportUnityPackage(const QUrl& file);
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
    void controlsChanged();
    void playbackChanged();
    void frameChanged();
    void messageChanged();
    // The picture needs drawing again.
    void redraw();
    void libraryOpenChanged();
    void backdropChanged();
    void exportOpenChanged();
    void exportsChanged();
    void soundOnChanged();
    void importingSoundChanged();
    // The viewport should show this part of the world: the point in the
    // middle and how many units fit top to bottom. Zero height means "reset".
    void viewSuggested(double x, double y, double unitsHigh);

private:
    enum Refresh { Values = 0, Structure = 1 };

    // Call after anything that may have changed the document.
    void changed(Refresh what);
    void rebuildLayers();
    void rebuildControls();
    void say(const QString& text, bool error = false);
    bool report(const vfx::Status& status);
    vfx::Id selectedLayerId() const;
    bool control(int index, vfx::editor::ControlView& out) const;
    QVariantMap pictureInfo() const;
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
    bool libraryOpen_ = false;
    bool exportOpen_ = false;

    vfx::editor::Session session_;
    QVariantList layers_;
    QVariantList controls_;
    int selected_ = 0;

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
