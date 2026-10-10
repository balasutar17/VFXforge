// VFX Forge app: the Reference to VFX workspace.
//
// Takes a reference picture, GIF or short video; has it studied; has an
// editable effect built from what was found; and shows the two side by side.
// All the thinking is in the editor layer (Reference.h, Reconstruct.h,
// Compare.h, Refine.h). This class reads the files with Qt, runs the slow
// parts on a worker thread so the window never freezes, and puts results
// into the open effect through the session, each as one undo step.
#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QQuickPaintedItem>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "vfx/editor/Compare.h"
#include "vfx/editor/Reconstruct.h"
#include "vfx/editor/Reference.h"
#include "vfx/editor/Refine.h"

class AppController;
class QMediaPlayer;
class QVideoSink;
class QVideoFrame;
class QTimer;
class QThread;

class ReferenceController : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool open READ open WRITE setOpen NOTIFY openChanged)

    // ---- the reference itself
    Q_PROPERTY(bool hasReference READ hasReference NOTIFY referenceChanged)
    Q_PROPERTY(QString name READ name NOTIFY referenceChanged)
    Q_PROPERTY(QString info READ info NOTIFY referenceChanged)
    Q_PROPERTY(bool moving READ moving NOTIFY referenceChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY referenceChanged)
    Q_PROPERTY(double fullAspect READ fullAspect NOTIFY referenceChanged)
    Q_PROPERTY(double aspect READ aspect NOTIFY optionsChanged)
    Q_PROPERTY(QString formats READ formats CONSTANT)
    Q_PROPERTY(bool keptWithProject READ keptWithProject NOTIFY referenceChanged)

    // ---- work in progress
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString stage READ stage NOTIFY progressChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)
    Q_PROPERTY(QString problemDetail READ problemDetail NOTIFY problemChanged)

    // ---- how the reference is read
    Q_PROPERTY(QString backdrop READ backdrop WRITE setBackdrop NOTIFY optionsChanged)
    Q_PROPERTY(QColor keyColour READ keyColour WRITE setKeyColour NOTIFY optionsChanged)
    Q_PROPERTY(double cropLeft READ cropLeft NOTIFY optionsChanged)
    Q_PROPERTY(double cropTop READ cropTop NOTIFY optionsChanged)
    Q_PROPERTY(double cropRight READ cropRight NOTIFY optionsChanged)
    Q_PROPERTY(double cropBottom READ cropBottom NOTIFY optionsChanged)
    Q_PROPERTY(int firstFrame READ firstFrame WRITE setFirstFrame NOTIFY optionsChanged)
    Q_PROPERTY(int lastFrame READ lastFrame WRITE setLastFrame NOTIFY optionsChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY optionsChanged)
    Q_PROPERTY(double cutoff READ cutoff WRITE setCutoff NOTIFY optionsChanged)
    Q_PROPERTY(QString detail READ detail WRITE setDetail NOTIFY optionsChanged)
    Q_PROPERTY(double sampling READ sampling WRITE setSampling NOTIFY optionsChanged)
    Q_PROPERTY(double sourceRate READ sourceRate NOTIFY referenceChanged)

    // ---- what was found
    Q_PROPERTY(bool analysed READ analysed NOTIFY analysisChanged)
    Q_PROPERTY(QVariantList findings READ findings NOTIFY analysisChanged)
    Q_PROPERTY(QStringList uncertain READ uncertain NOTIFY analysisChanged)
    Q_PROPERTY(QStringList methods READ methods NOTIFY analysisChanged)
    Q_PROPERTY(QVariantList palette READ palette NOTIFY analysisChanged)

    // ---- how to rebuild it
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY buildOptionsChanged)
    Q_PROPERTY(QString variation READ variation WRITE setVariation NOTIFY buildOptionsChanged)
    Q_PROPERTY(QString target READ target WRITE setTarget NOTIFY buildOptionsChanged)
    Q_PROPERTY(QString pace READ pace WRITE setPace NOTIFY buildOptionsChanged)
    Q_PROPERTY(bool cutouts READ cutouts WRITE setCutouts NOTIFY buildOptionsChanged)
    Q_PROPERTY(QString quality READ quality WRITE setQuality NOTIFY buildOptionsChanged)
    Q_PROPERTY(QVariantMap priorities READ priorities NOTIFY buildOptionsChanged)

    // ---- the result
    Q_PROPERTY(bool built READ built NOTIFY resultChanged)
    Q_PROPERTY(QVariantMap layerNotes READ layerNotes NOTIFY resultChanged)
    Q_PROPERTY(QStringList notes READ notes NOTIFY resultChanged)
    Q_PROPERTY(QVariantMap similarity READ similarity NOTIFY resultChanged)
    Q_PROPERTY(QStringList differences READ differences NOTIFY resultChanged)
    Q_PROPERTY(QStringList adjustments READ adjustments NOTIFY resultChanged)
    Q_PROPERTY(QString costs READ costs NOTIFY resultChanged)
    Q_PROPERTY(QString said READ said NOTIFY resultChanged)
    Q_PROPERTY(QVariantList versions READ versions NOTIFY resultChanged)
    Q_PROPERTY(QVariantList refinements READ refinements CONSTANT)

    // ---- comparing
    Q_PROPERTY(double time READ time WRITE setTime NOTIFY timeChanged)
    Q_PROPERTY(double length READ length NOTIFY timelineChanged)
    Q_PROPERTY(int frame READ frame NOTIFY timeChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(QVariantList referenceMarkers READ referenceMarkers NOTIFY timelineChanged)
    Q_PROPERTY(QVariantList effectMarkers READ effectMarkers NOTIFY timelineChanged)
    Q_PROPERTY(double overlayOpacity READ overlayOpacity WRITE setOverlayOpacity NOTIFY picturesChanged)

public:
    explicit ReferenceController(AppController* app, QObject* parent = nullptr);
    ~ReferenceController() override;

    bool open() const { return open_; }
    void setOpen(bool open);

    bool hasReference() const { return static_cast<bool>(reference_); }
    QString name() const { return name_; }
    QString info() const;
    bool moving() const { return reference_ && reference_->moving(); }
    int frameCount() const { return reference_ ? static_cast<int>(reference_->frames.size()) : 0; }
    double fullAspect() const;
    double aspect() const;
    QString formats() const;
    bool keptWithProject() const { return kept_; }

    bool busy() const { return busy_; }
    double progress() const { return progress_; }
    QString stage() const { return stage_; }
    QString problem() const { return problem_; }
    QString problemDetail() const { return problemDetail_; }

    QString backdrop() const;
    void setBackdrop(const QString& backdrop);
    QColor keyColour() const;
    void setKeyColour(const QColor& colour);
    double cropLeft() const { return options_.cropLeft; }
    double cropTop() const { return options_.cropTop; }
    double cropRight() const { return options_.cropRight; }
    double cropBottom() const { return options_.cropBottom; }
    int firstFrame() const;
    void setFirstFrame(int frame);
    int lastFrame() const;
    void setLastFrame(int frame);
    double speed() const { return options_.speed; }
    void setSpeed(double speed);
    double cutoff() const { return options_.cutoff; }
    void setCutoff(double cutoff);
    QString detail() const;
    void setDetail(const QString& detail);
    double sampling() const { return sampling_; }
    void setSampling(double framesPerSecond);
    double sourceRate() const { return reference_ ? reference_->framesPerSecond : 0.0; }

    bool analysed() const { return static_cast<bool>(analysis_); }
    QVariantList findings() const { return findings_; }
    QStringList uncertain() const { return uncertain_; }
    QStringList methods() const { return methods_; }
    QVariantList palette() const { return palette_; }

    QString mode() const;
    void setMode(const QString& mode);
    QString variation() const;
    void setVariation(const QString& variation);
    QString target() const;
    void setTarget(const QString& target);
    QString pace() const;
    void setPace(const QString& pace);
    bool cutouts() const { return how_.cutouts; }
    void setCutouts(bool cutouts);
    QString quality() const { return quality_; }
    void setQuality(const QString& quality);
    QVariantMap priorities() const;

    bool built() const { return built_; }
    QVariantMap layerNotes() const { return layerNotes_; }
    QStringList notes() const { return notes_; }
    QVariantMap similarity() const { return similarity_; }
    QStringList differences() const { return differences_; }
    QStringList adjustments() const { return adjustments_; }
    QString costs() const { return costs_; }
    QString said() const { return said_; }
    QVariantList versions() const;
    QVariantList refinements() const;

    double time() const { return time_; }
    void setTime(double seconds);
    double length() const;
    int frame() const;
    bool playing() const { return playing_; }
    QVariantList referenceMarkers() const;
    QVariantList effectMarkers() const;
    double overlayOpacity() const { return overlayOpacity_; }
    void setOverlayOpacity(double opacity);

    // ---- the reference
    Q_INVOKABLE bool load(const QUrl& file);
    Q_INVOKABLE bool loadPath(const QString& path);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void cancel();

    // ---- reading it
    Q_INVOKABLE void setCrop(double left, double top, double right, double bottom);
    Q_INVOKABLE void resetCrop();
    // Takes the background colour from a point of the whole picture (0 to 1).
    Q_INVOKABLE void pickBackdropAt(double x, double y);
    Q_INVOKABLE void setPriority(const QString& key, double value);
    Q_INVOKABLE void analyse();

    // ---- rebuilding
    Q_INVOKABLE void build();
    Q_INVOKABLE void fitAgain(bool outlineOnly);
    Q_INVOKABLE void refine(const QString& key);
    Q_INVOKABLE void measure();
    Q_INVOKABLE void restoreVersion(int index);

    // ---- comparing
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void stepFrames(int frames);
    // Called once per displayed frame, like AppController::tick.
    Q_INVOKABLE void tick(double seconds);

    // One of the comparison pictures: "full" (the whole frame, uncropped),
    // "reference", "made", "overlay" or "difference".
    QImage picture(const QString& kind);

    // For the self-test.
    bool waitUntilIdle(int milliseconds);
    QString report() const;

signals:
    void openChanged();
    void referenceChanged();
    void busyChanged();
    void progressChanged();
    void problemChanged();
    void optionsChanged();
    void analysisChanged();
    void buildOptionsChanged();
    void resultChanged();
    void timeChanged();
    void timelineChanged();
    void playingChanged();
    void picturesChanged();

private:
    struct Version {
        QString label;
        vfx::Effect effect;
        std::vector<vfx::editor::PictureUse> pictures;
        vfx::editor::Placement placement;
        float overall = 0;
    };
    struct JobResult;

    void setProblem(const QString& message, const QString& detail = {});
    void setBusy(bool busy, const QString& stage = {});
    void adoptReference(std::shared_ptr<const vfx::editor::Reference> reference, const QString& name,
                        const QString& sourcePath);
    bool loadStill(const QString& path);
    bool loadAnimation(const QString& path);
    void loadVideo(const QString& path);
    void finishVideo(bool ok, const QString& why = {});
    void onVideoFrame(const QVideoFrame& frame);
    void optionsEdited();
    void forgetAnalysis();
    void forgetResult();
    void startJob(int kind, const QString& label, bool outlineOnly = false);
    void finishJob(std::shared_ptr<JobResult> result);
    void applyBuilt(JobResult& result);
    void applyEffect(const vfx::Effect& effect, const std::vector<vfx::editor::PictureUse>* pictures,
                     const QString& stepName);
    void showSimilarity(const vfx::editor::Similarity& similarity);
    void pushVersion(const QString& label, float overall);
    void frameMainView();
    void rebuildFindings();
    void invalidatePictures();
    QString notesJson() const;
    void restoreFromProject();
    void applyNotes(const QString& json);
    vfx::editor::Priorities weights() const { return priorities_; }
    int fitRounds() const;
    void pictureSize(int& width, int& height) const;
    double effectTimeNow() const;
    bool steady() const;

    AppController* app_ = nullptr;
    bool open_ = false;

    std::shared_ptr<const vfx::editor::Reference> reference_;
    QString name_;
    QString sourcePath_;
    bool kept_ = false;        // the original file has been copied into the project
    bool keepPending_ = false; // it should be, at the next build
    std::uint64_t seenGeneration_ = 0;

    vfx::editor::ReferenceOptions options_;
    double sampling_ = 30.0;
    vfx::editor::ReconstructOptions how_;
    vfx::editor::Priorities priorities_;
    QString quality_ = QStringLiteral("normal");

    std::shared_ptr<const vfx::editor::ReferenceAnalysis> analysis_;
    QVariantList findings_;
    QStringList uncertain_, methods_;
    QVariantList palette_;
    bool analysisStale_ = false;

    bool built_ = false;
    vfx::editor::Placement placement_;
    QVariantMap layerNotes_;
    QStringList notes_, differences_, adjustments_;
    QVariantMap similarity_;
    QString costs_, said_;
    std::vector<Version> versions_;
    int currentVersion_ = -1;

    bool busy_ = false;
    double progress_ = 0.0;
    QString stage_;
    QString problem_, problemDetail_;
    QThread* worker_ = nullptr;
    std::shared_ptr<std::atomic<bool>> stop_;
    int jobSerial_ = 0;
    QElapsedTimer progressClock_;
    int queuedJob_ = -1;

    double time_ = 0.0;
    bool playing_ = false;
    double overlayOpacity_ = 0.5;
    int pictureSerial_ = 0;
    QHash<QString, QImage> pictureCache_;
    int cacheSerial_ = -1;

    // Reading a video: frames arrive as it plays.
    QMediaPlayer* player_ = nullptr;
    QVideoSink* sink_ = nullptr;
    QTimer* videoWatch_ = nullptr;
    std::shared_ptr<vfx::editor::Reference> videoFrames_;
    std::vector<double> videoTimes_;
    double nextSample_ = 0.0;
    QString videoPath_;
    qint64 lastVideoFrameAt_ = 0;
    QElapsedTimer videoClock_;
};

// One of the comparison pictures, with zoom and pan shared between views.
class ReferenceView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(ReferenceController* source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY changed)
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY changed)
    Q_PROPERTY(double panX READ panX WRITE setPanX NOTIFY changed)
    Q_PROPERTY(double panY READ panY WRITE setPanY NOTIFY changed)

public:
    explicit ReferenceView(QQuickItem* parent = nullptr);

    ReferenceController* source() const { return source_; }
    void setSource(ReferenceController* source);
    QString kind() const { return kind_; }
    void setKind(const QString& kind);
    double zoom() const { return zoom_; }
    void setZoom(double zoom);
    double panX() const { return panX_; }
    void setPanX(double pan);
    double panY() const { return panY_; }
    void setPanY(double pan);

    void paint(QPainter* painter) override;

    // Where the picture sits in the item, for laying things over it.
    Q_INVOKABLE QRectF pictureRect() const;

signals:
    void changed();

private:
    ReferenceController* source_ = nullptr;
    QString kind_ = QStringLiteral("reference");
    double zoom_ = 1.0, panX_ = 0.0, panY_ = 0.0;
};
