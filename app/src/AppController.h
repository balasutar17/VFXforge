// VFX Forge app: the bridge between the window and an editing session.
//
// The window (QML) only ever talks to this object. This object only ever
// talks to vfx::editor::Session. No rule about effects lives here: it
// translates between the window's types and the session's, and says when
// something changed.
#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

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

    Q_INVOKABLE void setControlNumber(int index, double value);
    Q_INVOKABLE void setControlRange(int index, double from, double to);
    Q_INVOKABLE void setControlRandom(int index, bool random);
    Q_INVOKABLE void setControlColor(int index, const QColor& color);
    Q_INVOKABLE void setControlDirection(int index, double heading, double tilt);
    Q_INVOKABLE void setControlChoice(int index, const QString& option);

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

    QString message_;
    bool messageIsError_ = false;
    QString warning_;
};
