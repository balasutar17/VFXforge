#include "ReferenceController.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QPainter>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>

#include "AppController.h"
#include "vfx/FileIO.h"
#include "vfx/Id.h"
#include "vfx/editor/Picture.h"
#include "vfx/editor/Session.h"

using namespace vfx::editor;

namespace {

enum JobKind { Analyse = 0, Build = 1, Fit = 2, Measure = 3 };

constexpr int kStillSide = 1536;  // a picture is kept at most this large
constexpr int kClipSide = 320;    // a clip's frames at most this large
constexpr int kClipFrames = 240;  // and at most this many of them
constexpr double kClipSeconds = 12.0;
constexpr int kShownSide = 384;   // the comparison pictures
constexpr qint64 kKeepBytes = 96ll * 1024 * 1024;

QString text(const std::string& s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }
std::string utf8(const QString& s) { return s.toUtf8().toStdString(); }

Image toImage(const QImage& source, int maxSide, int forceWidth = 0, int forceHeight = 0) {
    QImage image = source;
    if (forceWidth > 0 && forceHeight > 0) {
        if (image.width() != forceWidth || image.height() != forceHeight) {
            image = image.scaled(forceWidth, forceHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
    } else if (std::max(image.width(), image.height()) > maxSide) {
        image = image.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    image = image.convertToFormat(QImage::Format_RGBA8888);
    Image out;
    out.width = image.width();
    out.height = image.height();
    out.rgba.resize(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4u);
    for (int y = 0; y < image.height(); ++y) {
        std::memcpy(&out.rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) * 4u],
                    image.constScanLine(y), static_cast<std::size_t>(out.width) * 4u);
    }
    return out;
}

QImage toQImage(int width, int height, const std::vector<std::uint8_t>& rgba) {
    if (width <= 0 || height <= 0 || rgba.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u) {
        return {};
    }
    return QImage(rgba.data(), width, height, width * 4, QImage::Format_RGBA8888).copy();
}

bool isVideoName(const QString& path) {
    static const QStringList kVideo = {QStringLiteral("mp4"), QStringLiteral("webm"), QStringLiteral("mov"),
                                       QStringLiteral("m4v"), QStringLiteral("mkv"), QStringLiteral("avi")};
    return kVideo.contains(QFileInfo(path).suffix().toLower());
}

const char* paceName(Pace pace) {
    switch (pace) {
        case Pace::Auto: return "auto";
        case Pace::Burst: return "burst";
        case Pace::Steady: return "steady";
    }
    return "auto";
}

QString percentText(float value) { return QString::number(static_cast<int>(std::lround(value * 100.0f))) + QLatin1Char('%'); }

}  // namespace

struct ReferenceController::JobResult {
    int kind = Analyse;
    int serial = 0;
    bool cancelled = false;
    QString error, detail;
    std::shared_ptr<const ReferenceAnalysis> analysis;
    Reconstruction built;
    vfx::Effect effect;
    FitResult fit;
    bool fitted = false;
    Similarity similarity;
    bool measured = false;
    bool outlineOnly = false;
    QString label;
};

// ------------------------------------------------------------------ set-up

ReferenceController::ReferenceController(AppController* app, QObject* parent) : QObject(parent), app_(app) {
    seenGeneration_ = app_->session().generation();
    progressClock_.start();
    videoClock_.start();
}

ReferenceController::~ReferenceController() {
    if (stop_) {
        stop_->store(true);
    }
    if (worker_) {
        worker_->wait();
        delete worker_;
        worker_ = nullptr;
    }
}

void ReferenceController::setOpen(bool open) {
    if (open_ == open) {
        return;
    }
    open_ = open;
    if (!open_ && playing_) {
        playing_ = false;
        emit playingChanged();
    }
    emit openChanged();
    if (open_) {
        invalidatePictures();
        // A project that came with a reference shows it again.
        if (!reference_ && !sourcePath_.isEmpty() && kept_) {
            const QString path = sourcePath_;
            const bool wasBuilt = built_;
            const Placement placement = placement_;
            loadPath(path);
            kept_ = true;
            keepPending_ = false;
            built_ = wasBuilt;
            placement_ = placement;
            emit referenceChanged();
            emit resultChanged();
        }
    }
}

QString ReferenceController::formats() const {
    QStringList still;
    for (const QByteArray& format : QImageReader::supportedImageFormats()) {
        const QString f = QString::fromLatin1(format).toUpper();
        if (f == QStringLiteral("PNG") || f == QStringLiteral("JPG") || f == QStringLiteral("JPEG") ||
            f == QStringLiteral("WEBP") || f == QStringLiteral("GIF") || f == QStringLiteral("BMP")) {
            if (!still.contains(f)) {
                still.push_back(f);
            }
        }
    }
    return QStringLiteral("Pictures: %1. Clips: GIF%2, and MP4, WebM or MOV video.")
        .arg(still.join(QStringLiteral(", ")), still.contains(QStringLiteral("WEBP")) ? QStringLiteral(", animated WebP") : QString());
}

QString ReferenceController::info() const {
    if (!reference_ || reference_->frames.empty()) {
        return {};
    }
    const Image& first = reference_->frames.front();
    QString out = QStringLiteral("%1 × %2 pixels").arg(first.width).arg(first.height);
    if (reference_->moving()) {
        out += QStringLiteral(" · %1 frames · %2 s · %3 frames a second")
                   .arg(reference_->frames.size())
                   .arg(reference_->seconds(), 0, 'f', 2)
                   .arg(reference_->framesPerSecond, 0, 'f', 1);
    } else {
        out += QStringLiteral(" · a still picture");
    }
    return out;
}

double ReferenceController::fullAspect() const {
    if (!reference_ || reference_->frames.empty()) {
        return 1.0;
    }
    const Image& first = reference_->frames.front();
    return first.height > 0 ? static_cast<double>(first.width) / first.height : 1.0;
}

double ReferenceController::aspect() const {
    const double w = std::max(0.01, std::fabs(static_cast<double>(options_.cropRight) - options_.cropLeft));
    const double h = std::max(0.01, std::fabs(static_cast<double>(options_.cropBottom) - options_.cropTop));
    return fullAspect() * w / h;
}

void ReferenceController::setProblem(const QString& message, const QString& detail) {
    if (problem_ == message && problemDetail_ == detail) {
        return;
    }
    problem_ = message;
    problemDetail_ = detail;
    emit problemChanged();
}

void ReferenceController::setBusy(bool busy, const QString& stage) {
    if (!stage.isNull()) {
        stage_ = stage;
    }
    if (busy) {
        progress_ = 0.0;
    }
    const bool was = busy_;
    busy_ = busy;
    emit progressChanged();
    if (was != busy_) {
        emit busyChanged();
    }
}

void ReferenceController::invalidatePictures() {
    ++pictureSerial_;
    emit picturesChanged();
}

// ------------------------------------------------------------- loading

bool ReferenceController::load(const QUrl& file) { return loadPath(file.toLocalFile()); }

bool ReferenceController::loadPath(const QString& path) {
    setProblem({});
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        setProblem(QStringLiteral("That is not a file on this computer."));
        return false;
    }
    cancel();
    if (isVideoName(path)) {
        loadVideo(path);
        return true;
    }
    QImageReader probe(path);
    probe.setDecideFormatFromContent(true);
    if (!probe.canRead()) {
        setProblem(QStringLiteral("That file is not a kind of picture or clip this can read."),
                   formats() + QStringLiteral(" (") + probe.errorString() + QStringLiteral(")"));
        return false;
    }
    if (probe.supportsAnimation() && probe.imageCount() != 1) {
        return loadAnimation(path);
    }
    return loadStill(path);
}

bool ReferenceController::loadStill(const QString& path) {
    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        setProblem(QStringLiteral("That picture could not be read."), reader.errorString());
        return false;
    }
    auto reference = std::make_shared<Reference>();
    reference->frames.push_back(toImage(image, kStillSide));
    adoptReference(reference, QFileInfo(path).fileName(), path);
    return true;
}

bool ReferenceController::loadAnimation(const QString& path) {
    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    std::vector<QImage> frames;
    std::vector<int> delays;
    double total = 0.0;
    while (reader.canRead()) {
        const QImage image = reader.read();
        if (image.isNull()) {
            break;
        }
        const int delay = std::clamp(reader.nextImageDelay(), 10, 2000);
        frames.push_back(image);
        delays.push_back(delay);
        total += delay / 1000.0;
        if (frames.size() >= 2000 || total >= kClipSeconds) {
            break;
        }
    }
    if (frames.empty()) {
        setProblem(QStringLiteral("That clip could not be read."), reader.errorString());
        return false;
    }
    if (frames.size() == 1) {
        auto reference = std::make_shared<Reference>();
        reference->frames.push_back(toImage(frames.front(), kStillSide));
        adoptReference(reference, QFileInfo(path).fileName(), path);
        return true;
    }
    // Frames of a GIF can each last a different time. They are laid onto an
    // even beat: the clip's own average rate, held to what can be studied.
    double rate = static_cast<double>(frames.size()) / std::max(0.05, total);
    rate = std::clamp(rate, 4.0, 50.0);
    if (total * rate > kClipFrames) {
        rate = kClipFrames / total;
    }
    const int count = std::clamp(static_cast<int>(std::lround(total * rate)), 2, kClipFrames);
    auto reference = std::make_shared<Reference>();
    reference->framesPerSecond = rate;
    const Image first = toImage(frames.front(), kClipSide);
    std::size_t at = 0;
    double shownUntil = delays[0] / 1000.0;
    for (int k = 0; k < count; ++k) {
        const double moment = (k + 0.5) / rate;
        while (at + 1 < frames.size() && moment >= shownUntil) {
            ++at;
            shownUntil += delays[at] / 1000.0;
        }
        reference->frames.push_back(at == 0 ? first : toImage(frames[at], kClipSide, first.width, first.height));
    }
    adoptReference(reference, QFileInfo(path).fileName(), path);
    return true;
}

void ReferenceController::loadVideo(const QString& path) {
    finishVideo(false);  // drop any reading already under way, quietly
    videoPath_ = path;
    videoFrames_ = std::make_shared<Reference>();
    videoTimes_.clear();
    nextSample_ = 0.0;
    lastVideoFrameAt_ = videoClock_.elapsed();
    setBusy(true, QStringLiteral("Reading the video"));

    player_ = new QMediaPlayer(this);
    sink_ = new QVideoSink(this);
    player_->setVideoSink(sink_);
    connect(sink_, &QVideoSink::videoFrameChanged, this, &ReferenceController::onVideoFrame);
    connect(player_, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& why) {
        finishVideo(videoFrames_ && videoFrames_->frames.size() >= 2, why);
    });
    connect(player_, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia) {
            finishVideo(true);
        } else if (status == QMediaPlayer::InvalidMedia) {
            finishVideo(false, QStringLiteral("The file is not a video this computer can play."));
        }
    });
    videoWatch_ = new QTimer(this);
    videoWatch_->setInterval(500);
    connect(videoWatch_, &QTimer::timeout, this, [this]() {
        // No picture for a long while: it has stalled, or cannot be decoded.
        if (videoClock_.elapsed() - lastVideoFrameAt_ > 8000) {
            finishVideo(videoFrames_ && videoFrames_->frames.size() >= 2,
                        QStringLiteral("No pictures came out of the video."));
        }
    });
    videoWatch_->start();
    player_->setSource(QUrl::fromLocalFile(path));
    player_->play();
}

void ReferenceController::onVideoFrame(const QVideoFrame& frame) {
    if (!videoFrames_ || !player_ || !frame.isValid()) {
        return;
    }
    lastVideoFrameAt_ = videoClock_.elapsed();
    const double moment = frame.startTime() >= 0 ? static_cast<double>(frame.startTime()) / 1e6
                                                : static_cast<double>(player_->position()) / 1000.0;
    if (moment + 1e-6 < nextSample_) {
        return;
    }
    const QImage image = frame.toImage();
    if (image.isNull()) {
        return;
    }
    if (videoFrames_->frames.empty()) {
        videoFrames_->frames.push_back(toImage(image, kClipSide));
    } else {
        const Image& first = videoFrames_->frames.front();
        videoFrames_->frames.push_back(toImage(image, kClipSide, first.width, first.height));
    }
    videoTimes_.push_back(moment);
    nextSample_ = moment + 1.0 / std::max(1.0, sampling_) - 0.002;

    const double length = player_->duration() > 0 ? std::min(kClipSeconds, static_cast<double>(player_->duration()) / 1000.0)
                                                  : kClipSeconds;
    progress_ = std::clamp(moment / std::max(0.1, length), 0.0, 1.0);
    emit progressChanged();
    if (moment >= kClipSeconds || static_cast<int>(videoFrames_->frames.size()) >= kClipFrames) {
        finishVideo(true);
    }
}

void ReferenceController::finishVideo(bool ok, const QString& why) {
    if (!player_ && !videoFrames_) {
        return;
    }
    // Take everything down first: stopping the player can send more signals.
    QMediaPlayer* player = player_;
    QVideoSink* sink = sink_;
    QTimer* watch = videoWatch_;
    player_ = nullptr;
    sink_ = nullptr;
    videoWatch_ = nullptr;
    std::shared_ptr<Reference> frames = std::move(videoFrames_);
    videoFrames_.reset();
    if (watch) {
        watch->stop();
        watch->deleteLater();
    }
    if (sink) {
        sink->disconnect(this);
        sink->deleteLater();
    }
    if (player) {
        player->disconnect(this);
        player->stop();
        player->deleteLater();
    }
    if (videoPath_.isEmpty()) {
        return;
    }
    const QString path = videoPath_;
    videoPath_.clear();
    setBusy(false);
    if (!ok || !frames || frames->frames.size() < 2 || videoTimes_.size() < 2) {
        if (ok || !why.isEmpty()) {
            setProblem(QStringLiteral("That video could not be read on this computer."),
                       (why.isEmpty() ? QString() : why + QLatin1Char(' ')) +
                           QStringLiteral("Try an MP4 (H.264) or a GIF of the same clip."));
        }
        return;
    }
    const double span = videoTimes_.back() - videoTimes_.front();
    frames->framesPerSecond = span > 1e-3 ? static_cast<double>(videoTimes_.size() - 1) / span : sampling_;
    frames->framesPerSecond = std::clamp(frames->framesPerSecond, 1.0, 120.0);
    adoptReference(frames, QFileInfo(path).fileName(), path);
}

void ReferenceController::adoptReference(std::shared_ptr<const Reference> reference, const QString& name,
                                         const QString& sourcePath) {
    if (const vfx::Status ok = validateReference(*reference); !ok) {
        setProblem(text(ok.error().message), text(ok.error().detail));
        return;
    }
    const bool sameFile = sourcePath == sourcePath_ && kept_;
    reference_ = std::move(reference);
    name_ = name;
    sourcePath_ = sourcePath;
    if (!sameFile) {
        kept_ = false;
        keepPending_ = true;
        options_ = ReferenceOptions{};
        forgetResult();
    }
    // Trim settings that no longer fit the frames there are fall back to "all of it".
    if (options_.lastFrame >= frameCount() || options_.firstFrame >= frameCount() - 1) {
        options_.firstFrame = 0;
        options_.lastFrame = -1;
    }
    time_ = 0.0;
    forgetAnalysis();
    emit referenceChanged();
    emit optionsChanged();
    emit timelineChanged();
    emit timeChanged();
    invalidatePictures();
    analyse();
}

void ReferenceController::clear() {
    cancel();
    finishVideo(false);
    reference_.reset();
    name_.clear();
    sourcePath_.clear();
    kept_ = false;
    keepPending_ = false;
    options_ = ReferenceOptions{};
    forgetAnalysis();
    forgetResult();
    setProblem({});
    time_ = 0.0;
    if (playing_) {
        playing_ = false;
        emit playingChanged();
    }
    emit referenceChanged();
    emit optionsChanged();
    emit timelineChanged();
    emit timeChanged();
    invalidatePictures();
}

void ReferenceController::cancel() {
    queuedJob_ = -1;
    if (stop_) {
        stop_->store(true);
    }
    if (player_) {
        finishVideo(false);
    }
}

// ------------------------------------------------------------ reading it

QString ReferenceController::backdrop() const { return QString::fromLatin1(backdropName(options_.backdrop)); }

void ReferenceController::setBackdrop(const QString& backdrop) {
    Backdrop next = Backdrop::Auto;
    for (Backdrop b : {Backdrop::Auto, Backdrop::Transparent, Backdrop::Dark, Backdrop::Light, Backdrop::Colour}) {
        if (backdrop == QLatin1String(backdropName(b))) {
            next = b;
        }
    }
    if (next != options_.backdrop) {
        options_.backdrop = next;
        optionsEdited();
    }
}

QColor ReferenceController::keyColour() const {
    return QColor::fromRgbF(options_.keyR, options_.keyG, options_.keyB);
}

void ReferenceController::setKeyColour(const QColor& colour) {
    options_.keyR = static_cast<float>(colour.redF());
    options_.keyG = static_cast<float>(colour.greenF());
    options_.keyB = static_cast<float>(colour.blueF());
    options_.backdrop = Backdrop::Colour;
    optionsEdited();
}

int ReferenceController::firstFrame() const { return options_.firstFrame; }

void ReferenceController::setFirstFrame(int frame) {
    frame = std::clamp(frame, 0, std::max(0, lastFrame() - 1));
    if (frame != options_.firstFrame) {
        options_.firstFrame = frame;
        optionsEdited();
    }
}

int ReferenceController::lastFrame() const {
    const int count = frameCount();
    return options_.lastFrame < 0 ? std::max(0, count - 1) : std::clamp(options_.lastFrame, 0, std::max(0, count - 1));
}

void ReferenceController::setLastFrame(int frame) {
    const int count = frameCount();
    frame = std::clamp(frame, std::min(options_.firstFrame + 1, std::max(0, count - 1)), std::max(0, count - 1));
    const int stored = frame >= count - 1 ? -1 : frame;
    if (stored != options_.lastFrame) {
        options_.lastFrame = stored;
        optionsEdited();
    }
}

void ReferenceController::setSpeed(double speed) {
    speed = std::clamp(speed, 0.1, 8.0);
    if (std::fabs(speed - options_.speed) > 1e-6) {
        options_.speed = speed;
        optionsEdited();
    }
}

void ReferenceController::setCutoff(double cutoff) {
    const float next = cutoff < 0.0 ? -1.0f : static_cast<float>(std::clamp(cutoff, 0.0, 0.9));
    if (std::fabs(next - options_.cutoff) > 1e-6f) {
        options_.cutoff = next;
        optionsEdited();
    }
}

QString ReferenceController::detail() const {
    return options_.detail <= 128 ? QStringLiteral("quick")
                                  : (options_.detail >= 256 ? QStringLiteral("fine") : QStringLiteral("normal"));
}

void ReferenceController::setDetail(const QString& detail) {
    const int next = detail == QStringLiteral("quick") ? 128 : (detail == QStringLiteral("fine") ? 256 : 192);
    if (next != options_.detail) {
        options_.detail = next;
        optionsEdited();
    }
}

void ReferenceController::setSampling(double framesPerSecond) {
    framesPerSecond = std::clamp(framesPerSecond, 5.0, 60.0);
    if (std::fabs(framesPerSecond - sampling_) < 1e-6) {
        return;
    }
    sampling_ = framesPerSecond;
    emit optionsChanged();
    // The frames are taken while the video is read, so it is read again.
    if (reference_ && isVideoName(sourcePath_)) {
        const QString path = sourcePath_;
        const ReferenceOptions kept = options_;
        const bool wasKept = kept_;
        loadVideo(path);
        options_ = kept;
        kept_ = wasKept;
    }
}

void ReferenceController::setCrop(double left, double top, double right, double bottom) {
    const auto unit = [](double v) { return static_cast<float>(std::clamp(v, 0.0, 1.0)); };
    float l = unit(std::min(left, right)), r = unit(std::max(left, right));
    float t = unit(std::min(top, bottom)), b = unit(std::max(top, bottom));
    // Never thinner than a twentieth of the picture.
    if (r - l < 0.05f) {
        r = std::min(1.0f, l + 0.05f);
        l = std::max(0.0f, r - 0.05f);
    }
    if (b - t < 0.05f) {
        b = std::min(1.0f, t + 0.05f);
        t = std::max(0.0f, b - 0.05f);
    }
    if (l == options_.cropLeft && t == options_.cropTop && r == options_.cropRight && b == options_.cropBottom) {
        return;
    }
    options_.cropLeft = l;
    options_.cropTop = t;
    options_.cropRight = r;
    options_.cropBottom = b;
    optionsEdited();
}

void ReferenceController::resetCrop() { setCrop(0.0, 0.0, 1.0, 1.0); }

void ReferenceController::pickBackdropAt(double x, double y) {
    if (!reference_ || reference_->frames.empty()) {
        return;
    }
    const Image& image = reference_->frames[static_cast<std::size_t>(std::clamp(frame(), 0, frameCount() - 1))];
    const int px = std::clamp(static_cast<int>(x * image.width), 0, image.width - 1);
    const int py = std::clamp(static_cast<int>(y * image.height), 0, image.height - 1);
    // The average of a small patch, so one odd pixel does not decide it.
    double r = 0, g = 0, b = 0;
    int n = 0;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            const int sx = std::clamp(px + dx, 0, image.width - 1), sy = std::clamp(py + dy, 0, image.height - 1);
            const std::uint8_t* p = &image.rgba[(static_cast<std::size_t>(sy) * static_cast<std::size_t>(image.width) +
                                                 static_cast<std::size_t>(sx)) * 4u];
            r += p[0];
            g += p[1];
            b += p[2];
            ++n;
        }
    }
    setKeyColour(QColor::fromRgbF(static_cast<float>(r / n / 255.0), static_cast<float>(g / n / 255.0),
                                  static_cast<float>(b / n / 255.0)));
}

void ReferenceController::optionsEdited() {
    forgetAnalysis();
    forgetResult();
    emit optionsChanged();
    emit timelineChanged();
    time_ = std::clamp(time_, 0.0, std::max(0.0, length()));
    emit timeChanged();
    invalidatePictures();
    // Dragging a slider changes this many times a second; the reading
    // starts once it has been left alone for a moment.
    const int serial = ++jobSerial_;
    QTimer::singleShot(350, this, [this, serial]() {
        if (serial == jobSerial_ && reference_) {
            analyse();
        }
    });
}

void ReferenceController::forgetAnalysis() {
    analysis_.reset();
    findings_.clear();
    uncertain_.clear();
    methods_.clear();
    palette_.clear();
    emit analysisChanged();
    emit timelineChanged();
}

void ReferenceController::forgetResult() {
    built_ = false;
    layerNotes_.clear();
    notes_.clear();
    differences_.clear();
    adjustments_.clear();
    similarity_.clear();
    costs_.clear();
    said_.clear();
    versions_.clear();
    currentVersion_ = -1;
    emit resultChanged();
    emit timelineChanged();
}

// ----------------------------------------------------------- build options

QString ReferenceController::mode() const { return QString::fromLatin1(matchModeName(how_.mode)); }
void ReferenceController::setMode(const QString& mode) {
    MatchMode next = how_.mode;
    if (parseMatchMode(utf8(mode), next) && next != how_.mode) {
        how_.mode = next;
        emit buildOptionsChanged();
    }
}

QString ReferenceController::variation() const { return QString::fromLatin1(variationName(how_.variation)); }
void ReferenceController::setVariation(const QString& variation) {
    Variation next = how_.variation;
    if (parseVariation(utf8(variation), next) && next != how_.variation) {
        how_.variation = next;
        emit buildOptionsChanged();
    }
}

QString ReferenceController::target() const { return QString::fromLatin1(targetName(how_.target)); }
void ReferenceController::setTarget(const QString& target) {
    Target next = how_.target;
    if (parseTarget(utf8(target), next) && next != how_.target) {
        how_.target = next;
        emit buildOptionsChanged();
    }
}

QString ReferenceController::pace() const { return QString::fromLatin1(paceName(how_.pace)); }
void ReferenceController::setPace(const QString& pace) {
    const Pace next = pace == QStringLiteral("burst") ? Pace::Burst
                                                      : (pace == QStringLiteral("steady") ? Pace::Steady : Pace::Auto);
    if (next != how_.pace) {
        how_.pace = next;
        emit buildOptionsChanged();
    }
}

void ReferenceController::setCutouts(bool cutouts) {
    if (cutouts != how_.cutouts) {
        how_.cutouts = cutouts;
        emit buildOptionsChanged();
    }
}

void ReferenceController::setQuality(const QString& quality) {
    const QString next = quality == QStringLiteral("quick") || quality == QStringLiteral("thorough") ? quality
                                                                                                     : QStringLiteral("normal");
    if (next != quality_) {
        quality_ = next;
        emit buildOptionsChanged();
    }
}

int ReferenceController::fitRounds() const {
    return quality_ == QStringLiteral("quick") ? 1 : (quality_ == QStringLiteral("thorough") ? 5 : 3);
}

QVariantMap ReferenceController::priorities() const {
    QVariantMap map;
    map.insert(QStringLiteral("silhouette"), priorities_.silhouette);
    map.insert(QStringLiteral("colour"), priorities_.colour);
    map.insert(QStringLiteral("brightness"), priorities_.brightness);
    map.insert(QStringLiteral("density"), priorities_.density);
    map.insert(QStringLiteral("motion"), priorities_.motion);
    map.insert(QStringLiteral("timing"), priorities_.timing);
    map.insert(QStringLiteral("detail"), priorities_.detail);
    return map;
}

void ReferenceController::setPriority(const QString& key, double value) {
    const float v = static_cast<float>(std::clamp(value, 0.0, 2.0));
    float* slot = nullptr;
    if (key == QStringLiteral("silhouette")) slot = &priorities_.silhouette;
    else if (key == QStringLiteral("colour")) slot = &priorities_.colour;
    else if (key == QStringLiteral("brightness")) slot = &priorities_.brightness;
    else if (key == QStringLiteral("density")) slot = &priorities_.density;
    else if (key == QStringLiteral("motion")) slot = &priorities_.motion;
    else if (key == QStringLiteral("timing")) slot = &priorities_.timing;
    else if (key == QStringLiteral("detail")) slot = &priorities_.detail;
    if (slot && std::fabs(*slot - v) > 1e-6f) {
        *slot = v;
        emit buildOptionsChanged();
    }
}

QVariantList ReferenceController::refinements() const {
    QVariantList list;
    for (const RefineInfo& info : vfx::editor::refinements()) {
        QVariantMap row;
        row.insert(QStringLiteral("key"), QString::fromLatin1(info.key));
        row.insert(QStringLiteral("label"), QString::fromUtf8(info.label));
        row.insert(QStringLiteral("help"), QString::fromUtf8(info.help));
        row.insert(QStringLiteral("needsReference"), info.needsReference);
        row.insert(QStringLiteral("needsClip"), info.needsClip);
        list.push_back(row);
    }
    return list;
}

// ------------------------------------------------------------- the work

void ReferenceController::analyse() {
    if (!reference_) {
        return;
    }
    startJob(Analyse, QStringLiteral("Studying the reference"));
}

void ReferenceController::build() {
    if (!reference_) {
        setProblem(QStringLiteral("Choose a reference first."));
        return;
    }
    startJob(Build, QStringLiteral("Building the effect"));
}

void ReferenceController::fitAgain(bool outlineOnly) {
    if (!reference_ || !analysis_ || !built_) {
        return;
    }
    startJob(Fit, outlineOnly ? QStringLiteral("Matching the outline") : QStringLiteral("Matching sizes and brightness"),
             outlineOnly);
}

void ReferenceController::measure() {
    if (!reference_ || !analysis_ || !built_) {
        return;
    }
    startJob(Measure, QStringLiteral("Comparing"));
}

void ReferenceController::startJob(int kind, const QString& label, bool outlineOnly) {
    if (busy_ && worker_) {
        // One thing at a time: stop what is running and do this next.
        queuedJob_ = kind;
        if (stop_) {
            stop_->store(true);
        }
        return;
    }
    if (player_) {
        return;  // a video is still being read
    }
    setProblem({});
    const int serial = ++jobSerial_;
    stop_ = std::make_shared<std::atomic<bool>>(false);
    setBusy(true, label);

    // Everything the worker needs, copied or shared read-only.
    const std::shared_ptr<const Reference> reference = reference_;
    const ReferenceOptions options = options_;
    const ReconstructOptions how = how_;
    const Priorities priorities = priorities_;
    const std::shared_ptr<const ReferenceAnalysis> known = analysis_;
    const vfx::Effect effect = app_->session().effect();
    const ImageSet images = app_->session().images();
    const Placement placement = placement_;
    const int rounds = fitRounds();
    const std::shared_ptr<std::atomic<bool>> stop = stop_;
    const QPointer<ReferenceController> self(this);

    auto progress = [self, stop, serial](float done, const char* stage) {
        if (stop->load()) {
            return false;
        }
        const QString words = QString::fromUtf8(stage ? stage : "");
        QMetaObject::invokeMethod(qApp, [self, serial, done, words]() {
            if (self && serial == self->jobSerial_ && self->busy_) {
                // Not more than about twenty times a second.
                if (self->progressClock_.elapsed() < 45 && done < 0.999f) {
                    return;
                }
                self->progressClock_.restart();
                self->progress_ = done;
                if (!words.isEmpty()) {
                    self->stage_ = words;
                }
                emit self->progressChanged();
            }
        }, Qt::QueuedConnection);
        return true;
    };

    worker_ = QThread::create([=]() {
        auto result = std::make_shared<JobResult>();
        result->kind = kind;
        result->serial = serial;
        result->outlineOnly = outlineOnly;
        result->label = label;
        const auto part = [&](float from, float to) {
            return Progress([=](float done, const char* stage) { return progress(from + (to - from) * done, stage); });
        };
        std::shared_ptr<const ReferenceAnalysis> analysis = known;
        if (kind == Analyse || !analysis) {
            auto analysed = analyzeReference(*reference, options, part(0.0f, kind == Analyse ? 1.0f : 0.4f));
            if (!analysed) {
                result->cancelled = stop->load();
                result->error = text(analysed.error().message);
                result->detail = text(analysed.error().detail);
            } else {
                analysis = std::make_shared<const ReferenceAnalysis>(std::move(analysed.value()));
                result->analysis = analysis;
            }
        }
        if (result->error.isEmpty() && analysis && kind == Build) {
            progress(0.42f, "Choosing layers");
            std::vector<Cutout> cutouts;
            if (how.cutouts) {
                cutouts = makeCutouts(*reference, options, *analysis);
            }
            vfx::IdGenerator ids = vfx::IdGenerator::fromEntropy();
            const vfx::IdSource newId = [&ids]() { return ids.next(); };
            result->built = reconstruct(*analysis, how, newId, &cutouts);
            const ImageSet pictures = picturesOf(result->built);
            FitOptions fit;
            fit.priorities = priorities;
            fit.rounds = rounds;
            result->fit = fitToReference(result->built.effect, *reference, options, *analysis, placementOf(result->built),
                                         fit, part(0.45f, 0.98f), &pictures);
            result->fitted = true;
            result->similarity = result->fit.after;
            result->measured = true;
            result->cancelled = stop->load();
        } else if (result->error.isEmpty() && analysis && kind == Fit) {
            result->effect = effect;
            FitOptions fit;
            fit.priorities = priorities;
            fit.rounds = rounds;
            fit.onlyOutline = outlineOnly;
            result->fit = fitToReference(result->effect, *reference, options, *analysis, placement, fit,
                                         part(0.02f, 0.98f), &images);
            result->fitted = true;
            result->similarity = result->fit.after;
            result->measured = true;
            result->cancelled = stop->load();
        } else if (result->error.isEmpty() && analysis && kind == Measure) {
            result->similarity = compareToReference(*reference, options, *analysis, effect, placement, priorities,
                                                    part(0.02f, 0.98f), &images);
            result->measured = true;
            result->cancelled = stop->load();
        }
        QMetaObject::invokeMethod(qApp, [self, result]() {
            if (self) {
                self->finishJob(result);
            }
        }, Qt::QueuedConnection);
    });
    worker_->start();
}

void ReferenceController::finishJob(std::shared_ptr<JobResult> result) {
    if (worker_) {
        worker_->wait();
        delete worker_;
        worker_ = nullptr;
    }
    const bool current = result->serial == jobSerial_;
    setBusy(false);
    const int next = queuedJob_;
    queuedJob_ = -1;

    if (current && !result->cancelled && !result->error.isEmpty()) {
        setProblem(result->error, result->detail);
    } else if (current && !(result->cancelled && result->kind == Analyse)) {
        if (result->analysis) {
            analysis_ = result->analysis;
            rebuildFindings();
            emit analysisChanged();
            emit timelineChanged();
            invalidatePictures();
        }
        switch (result->kind) {
            case Analyse:
                // An effect that came with the project is measured against it.
                if (built_ && analysis_) {
                    QTimer::singleShot(0, this, [this]() { measure(); });
                }
                break;
            case Build:
                if (result->cancelled) {
                    said_ = QStringLiteral("Stopped. The effect was left as it was.");
                    emit resultChanged();
                } else {
                    applyBuilt(*result);
                }
                break;
            case Fit:
                if (result->fit.changes.empty()) {
                    showSimilarity(result->similarity);
                    said_ = result->cancelled ? QStringLiteral("Stopped. Nothing was changed.")
                                              : QStringLiteral("Nothing was changed: no adjustment measured closer.");
                    emit resultChanged();
                } else {
                    adjustments_.clear();
                    for (const std::string& change : result->fit.changes) {
                        adjustments_.push_back(text(change));
                    }
                    applyEffect(result->effect, nullptr,
                                result->outlineOnly ? QStringLiteral("Match the outline") : QStringLiteral("Match the reference"));
                    showSimilarity(result->similarity);
                    said_ = QStringLiteral("Adjusted %1 setting(s) to measure closer.").arg(result->fit.changes.size());
                    pushVersion(result->outlineOnly ? QStringLiteral("Outline matched") : QStringLiteral("Matched again"),
                                result->similarity.overall);
                    emit resultChanged();
                }
                break;
            case Measure:
                showSimilarity(result->similarity);
                emit resultChanged();
                break;
            default:
                break;
        }
    }
    if (next >= 0 && reference_) {
        startJob(next, next == Analyse ? QStringLiteral("Studying the reference") : QStringLiteral("Working"));
    }
}

void ReferenceController::rebuildFindings() {
    findings_.clear();
    uncertain_.clear();
    methods_.clear();
    palette_.clear();
    if (!analysis_) {
        return;
    }
    for (const Finding& f : analysis_->findings) {
        QVariantMap row;
        row.insert(QStringLiteral("key"), text(f.key));
        row.insert(QStringLiteral("label"), text(f.label));
        row.insert(QStringLiteral("value"), text(f.value));
        row.insert(QStringLiteral("confidence"), f.confidence);
        row.insert(QStringLiteral("seen"), f.basis == Basis::Observed);
        row.insert(QStringLiteral("note"), text(f.note));
        findings_.push_back(row);
    }
    for (const std::string& line : analysis_->uncertain) {
        uncertain_.push_back(text(line));
    }
    for (const std::string& line : analysis_->methods) {
        methods_.push_back(text(line));
    }
    for (const Swatch& swatch : analysis_->still.palette) {
        QVariantMap row;
        row.insert(QStringLiteral("colour"), text(hexColour(swatch)));
        row.insert(QStringLiteral("name"), text(colourName(swatch)));
        row.insert(QStringLiteral("share"), swatch.share);
        palette_.push_back(row);
    }
}

void ReferenceController::applyBuilt(JobResult& result) {
    vfx::editor::Session& session = app_->session();
    vfx::Effect next = result.built.effect;
    // Locked layers are the artist's own: they stay, on top, as they were.
    int kept = 0;
    for (const vfx::Layer& layer : session.effect().layers) {
        if (layer.locked) {
            next.layers.push_back(layer);
            ++kept;
        }
    }
    placement_ = placementOf(result.built);
    bool tooLarge = false;

    session.beginEdit("Rebuild from reference");
    if (keepPending_ && !sourcePath_.isEmpty()) {
        // The original file goes with the project, byte for byte.
        QFile file(sourcePath_);
        if (file.size() > kKeepBytes) {
            tooLarge = true;
        } else if (file.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = file.readAll();
            const vfx::Status keptOk = session.keepReference(utf8(name_), std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())),
                                                             utf8(notesJson()));
            kept_ = keptOk.ok();
        }
        keepPending_ = false;
    } else if (kept_) {
        (void)session.setReferenceNotes(utf8(notesJson()));
    }
    const vfx::Status applied = session.applyEffect(next, "Rebuild from reference", &result.built.pictures);
    session.endEdit();
    if (!applied) {
        setProblem(text(applied.error().message), text(applied.error().detail));
        app_->effectChangedOutside({});
        return;
    }

    built_ = true;
    layerNotes_.clear();
    for (const LayerNote& note : result.built.layers) {
        QVariantMap row;
        row.insert(QStringLiteral("technique"), text(note.technique));
        row.insert(QStringLiteral("note"), text(note.note));
        row.insert(QStringLiteral("lookSeen"), note.look == Basis::Observed);
        row.insert(QStringLiteral("motionSeen"), note.motion == Basis::Observed);
        row.insert(QStringLiteral("added"), note.from.empty());
        layerNotes_.insert(text(vfx::formatId('l', note.layer)), row);
    }
    notes_.clear();
    for (const std::string& line : result.built.notes) {
        notes_.push_back(text(line));
    }
    if (tooLarge) {
        notes_.push_back(QStringLiteral("The reference file is too large to copy into the project (over 96 MB), so it was not kept with it."));
    }
    adjustments_.clear();
    for (const std::string& change : result.fit.changes) {
        adjustments_.push_back(text(change));
    }
    showSimilarity(result.similarity);
    costs_ = QStringLiteral("Studying the reference took %1 s. Matching took %2 s (%3 drawings). The effect has %4 layer(s) and "
                            "about %5 particle(s) at its busiest; the limit for this device is %6. Playing it costs what any "
                            "effect of that size costs; exporting draws each frame once.")
                 .arg(analysis_ ? analysis_->cost : 0.0, 0, 'f', 2)
                 .arg(result.fit.cost, 0, 'f', 2)
                 .arg(result.fit.drawings)
                 .arg(result.built.effect.layers.size())
                 .arg(result.built.particlesAtBusiest)
                 .arg(result.built.budget);
    said_ = QStringLiteral("Built %1 layer(s) from the reference.").arg(result.built.effect.layers.size());
    if (kept > 0) {
        said_ += QStringLiteral(" %1 locked layer(s) were kept as they were.").arg(kept);
    }
    const QString label = QStringLiteral("%1 · %2")
                              .arg(mode() == QStringLiteral("shape") ? QStringLiteral("Shape and colour")
                                                                     : mode().left(1).toUpper() + mode().mid(1),
                                   variation().left(1).toUpper() + variation().mid(1));
    if (currentVersion_ + 1 < static_cast<int>(versions_.size())) {
        versions_.erase(versions_.begin() + currentVersion_ + 1, versions_.end());
    }
    Version version;
    version.label = label;
    version.effect = session.effect();
    version.pictures = result.built.pictures;
    version.placement = placement_;
    version.overall = result.similarity.overall;
    versions_.push_back(std::move(version));
    if (versions_.size() > 12) {
        versions_.erase(versions_.begin());
    }
    currentVersion_ = static_cast<int>(versions_.size()) - 1;

    // Show the moment the reference was described at.
    time_ = moving() ? std::clamp(placement_.peakTime + placement_.referenceStart, 0.0, length()) : placement_.peakTime;
    emit timelineChanged();
    emit timeChanged();
    emit referenceChanged();
    emit resultChanged();
    invalidatePictures();
    frameMainView();
    app_->effectChangedOutside(said_);
}

void ReferenceController::applyEffect(const vfx::Effect& effect, const std::vector<PictureUse>* pictures,
                                      const QString& stepName) {
    const vfx::Status applied = app_->session().applyEffect(effect, utf8(stepName), pictures);
    if (!applied) {
        setProblem(text(applied.error().message), text(applied.error().detail));
        return;
    }
    emit timelineChanged();
    invalidatePictures();
    app_->effectChangedOutside({});
}

void ReferenceController::showSimilarity(const Similarity& s) {
    similarity_.clear();
    similarity_.insert(QStringLiteral("overall"), s.overall);
    similarity_.insert(QStringLiteral("silhouette"), s.silhouette);
    similarity_.insert(QStringLiteral("colour"), s.colour);
    similarity_.insert(QStringLiteral("brightness"), s.brightness);
    similarity_.insert(QStringLiteral("density"), s.density);
    similarity_.insert(QStringLiteral("motion"), s.motion);
    similarity_.insert(QStringLiteral("timing"), s.timing);
    similarity_.insert(QStringLiteral("detail"), s.detail);
    differences_.clear();
    for (const std::string& line : s.differences) {
        differences_.push_back(text(line));
    }
}

void ReferenceController::pushVersion(const QString& label, float overall) {
    if (currentVersion_ + 1 < static_cast<int>(versions_.size())) {
        versions_.erase(versions_.begin() + currentVersion_ + 1, versions_.end());
    }
    Version version;
    version.label = label;
    version.effect = app_->session().effect();
    if (currentVersion_ >= 0 && currentVersion_ < static_cast<int>(versions_.size())) {
        version.pictures = versions_[static_cast<std::size_t>(currentVersion_)].pictures;
    }
    version.placement = placement_;
    version.overall = overall;
    versions_.push_back(std::move(version));
    if (versions_.size() > 12) {
        versions_.erase(versions_.begin());
    }
    currentVersion_ = static_cast<int>(versions_.size()) - 1;
}

QVariantList ReferenceController::versions() const {
    QVariantList list;
    for (std::size_t i = 0; i < versions_.size(); ++i) {
        const Version& v = versions_[i];
        QVariantMap row;
        row.insert(QStringLiteral("label"), v.label);
        row.insert(QStringLiteral("overall"), v.overall);
        row.insert(QStringLiteral("layers"), static_cast<int>(v.effect.layers.size()));
        row.insert(QStringLiteral("current"), static_cast<int>(i) == currentVersion_);
        list.push_back(row);
    }
    return list;
}

void ReferenceController::restoreVersion(int index) {
    if (index < 0 || index >= static_cast<int>(versions_.size()) || busy_) {
        return;
    }
    const Version& version = versions_[static_cast<std::size_t>(index)];
    placement_ = version.placement;
    currentVersion_ = index;
    applyEffect(version.effect, &version.pictures, QStringLiteral("Back to: ") + version.label);
    said_ = QStringLiteral("Back to “%1”. Undo returns to where you were.").arg(version.label);
    emit resultChanged();
    measure();
}

void ReferenceController::refine(const QString& key) {
    if (busy_) {
        return;
    }
    const RefineInfo* info = findRefinement(utf8(key));
    if (!info) {
        return;
    }
    vfx::editor::Session& session = app_->session();
    vfx::Document& document = session.document();
    const vfx::IdSource newId = [&document]() { return document.newId(); };
    std::string said;
    auto changed = vfx::editor::refine(session.effect(), info->id, analysis_.get(), newId, &said);
    if (!changed) {
        setProblem(text(changed.error().message), text(changed.error().detail));
        return;
    }
    setProblem({});
    applyEffect(changed.value(), nullptr, QString::fromUtf8(info->label));
    said_ = text(said);
    pushVersion(QString::fromUtf8(info->label), similarity_.value(QStringLiteral("overall")).toFloat());
    emit resultChanged();
    if (reference_ && analysis_ && built_) {
        measure();
    }
}

void ReferenceController::frameMainView() {
    // The main window's viewport shows the effect framed as the reference frames it.
    app_->suggestView(placement_.view.centerX, placement_.view.centerY, placement_.view.unitsHigh);
}

// ----------------------------------------------- keeping it with the project

QString ReferenceController::notesJson() const {
    QJsonObject o;
    o.insert(QStringLiteral("version"), 1);
    o.insert(QStringLiteral("name"), name_);
    o.insert(QStringLiteral("backdrop"), backdrop());
    o.insert(QStringLiteral("key"), QJsonArray{options_.keyR, options_.keyG, options_.keyB});
    o.insert(QStringLiteral("crop"), QJsonArray{options_.cropLeft, options_.cropTop, options_.cropRight, options_.cropBottom});
    o.insert(QStringLiteral("first"), options_.firstFrame);
    o.insert(QStringLiteral("last"), options_.lastFrame);
    o.insert(QStringLiteral("speed"), options_.speed);
    o.insert(QStringLiteral("cutoff"), options_.cutoff);
    o.insert(QStringLiteral("detail"), detail());
    o.insert(QStringLiteral("sampling"), sampling_);
    o.insert(QStringLiteral("mode"), mode());
    o.insert(QStringLiteral("variation"), variation());
    o.insert(QStringLiteral("target"), target());
    o.insert(QStringLiteral("pace"), pace());
    o.insert(QStringLiteral("cutouts"), how_.cutouts);
    QJsonObject place;
    place.insert(QStringLiteral("x"), placement_.view.centerX);
    place.insert(QStringLiteral("y"), placement_.view.centerY);
    place.insert(QStringLiteral("units"), placement_.view.unitsHigh);
    place.insert(QStringLiteral("peak"), placement_.peakTime);
    place.insert(QStringLiteral("start"), placement_.referenceStart);
    o.insert(QStringLiteral("placement"), place);
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void ReferenceController::applyNotes(const QString& json) {
    const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
    if (o.isEmpty()) {
        return;
    }
    options_ = ReferenceOptions{};
    const QString backdropWord = o.value(QStringLiteral("backdrop")).toString(QStringLiteral("auto"));
    for (Backdrop b : {Backdrop::Auto, Backdrop::Transparent, Backdrop::Dark, Backdrop::Light, Backdrop::Colour}) {
        if (backdropWord == QLatin1String(backdropName(b))) {
            options_.backdrop = b;
        }
    }
    const QJsonArray key = o.value(QStringLiteral("key")).toArray();
    if (key.size() == 3) {
        options_.keyR = static_cast<float>(key[0].toDouble());
        options_.keyG = static_cast<float>(key[1].toDouble());
        options_.keyB = static_cast<float>(key[2].toDouble());
    }
    const QJsonArray crop = o.value(QStringLiteral("crop")).toArray();
    if (crop.size() == 4) {
        options_.cropLeft = static_cast<float>(std::clamp(crop[0].toDouble(), 0.0, 1.0));
        options_.cropTop = static_cast<float>(std::clamp(crop[1].toDouble(), 0.0, 1.0));
        options_.cropRight = static_cast<float>(std::clamp(crop[2].toDouble(1.0), 0.0, 1.0));
        options_.cropBottom = static_cast<float>(std::clamp(crop[3].toDouble(1.0), 0.0, 1.0));
    }
    options_.firstFrame = std::max(0, o.value(QStringLiteral("first")).toInt(0));
    options_.lastFrame = o.value(QStringLiteral("last")).toInt(-1);
    options_.speed = std::clamp(o.value(QStringLiteral("speed")).toDouble(1.0), 0.1, 8.0);
    options_.cutoff = static_cast<float>(o.value(QStringLiteral("cutoff")).toDouble(-1.0));
    const QString detail = o.value(QStringLiteral("detail")).toString();
    options_.detail = detail == QStringLiteral("quick") ? 128 : (detail == QStringLiteral("fine") ? 256 : 192);
    sampling_ = std::clamp(o.value(QStringLiteral("sampling")).toDouble(30.0), 5.0, 60.0);
    setMode(o.value(QStringLiteral("mode")).toString(QStringLiteral("balanced")));
    setVariation(o.value(QStringLiteral("variation")).toString(QStringLiteral("closest")));
    setTarget(o.value(QStringLiteral("target")).toString(QStringLiteral("desktop")));
    setPace(o.value(QStringLiteral("pace")).toString(QStringLiteral("auto")));
    how_.cutouts = o.value(QStringLiteral("cutouts")).toBool(false);
    const QJsonObject place = o.value(QStringLiteral("placement")).toObject();
    if (!place.isEmpty()) {
        placement_ = Placement{};
        placement_.view.centerX = static_cast<float>(place.value(QStringLiteral("x")).toDouble());
        placement_.view.centerY = static_cast<float>(place.value(QStringLiteral("y")).toDouble());
        placement_.view.unitsHigh = static_cast<float>(place.value(QStringLiteral("units")).toDouble(kReferenceUnits));
        placement_.peakTime = place.value(QStringLiteral("peak")).toDouble();
        placement_.referenceStart = place.value(QStringLiteral("start")).toDouble();
        built_ = true;
    }
}

void ReferenceController::restoreFromProject() {
    // A different effect is open now: nothing from the last one applies.
    cancel();
    finishVideo(false);
    reference_.reset();
    name_.clear();
    sourcePath_.clear();
    kept_ = false;
    keepPending_ = false;
    options_ = ReferenceOptions{};
    how_ = ReconstructOptions{};
    analysis_.reset();
    findings_.clear();
    uncertain_.clear();
    methods_.clear();
    palette_.clear();
    forgetResult();
    setProblem({});
    time_ = 0.0;

    const vfx::editor::Session::KeptReference kept = app_->session().keptReference();
    if (kept.found) {
        sourcePath_ = text(vfx::pathToUtf8(kept.file));
        name_ = text(kept.name);
        kept_ = true;
        ++jobSerial_;  // nothing scheduled by the calls below should start a reading yet
        applyNotes(text(kept.notes));
        ++jobSerial_;
    }
    emit referenceChanged();
    emit optionsChanged();
    emit analysisChanged();
    emit buildOptionsChanged();
    emit resultChanged();
    emit timelineChanged();
    emit timeChanged();
    invalidatePictures();
    // If the workspace is already showing, bring the reference back now.
    if (open_ && kept.found) {
        open_ = false;
        setOpen(true);
    }
}

// --------------------------------------------------------------- comparing

bool ReferenceController::steady() const { return analysis_ && analysis_->time.available && analysis_->time.continuous; }

double ReferenceController::length() const {
    if (reference_ && reference_->moving()) {
        int first = 0, last = 0;
        frameRange(*reference_, options_, first, last);
        const double rate = reference_->framesPerSecond * (options_.speed > 0.01 ? options_.speed : 1.0);
        return static_cast<double>(last - first + 1) / std::max(0.01, rate);
    }
    return std::max(0.1, app_->session().effect().duration);
}

int ReferenceController::frame() const {
    if (!reference_) {
        return 0;
    }
    int first = 0, last = 0;
    frameRange(*reference_, options_, first, last);
    if (!reference_->moving()) {
        return first;
    }
    const double rate = reference_->framesPerSecond * (options_.speed > 0.01 ? options_.speed : 1.0);
    return std::clamp(first + static_cast<int>(std::floor(time_ * rate + 1e-6)), first, last);
}

double ReferenceController::effectTimeNow() const {
    if (reference_ && reference_->moving()) {
        return time_ - placement_.referenceStart;
    }
    return time_;
}

void ReferenceController::setTime(double seconds) {
    seconds = std::clamp(seconds, 0.0, std::max(0.0, length() - 1e-4));
    if (std::fabs(seconds - time_) < 1e-9) {
        return;
    }
    time_ = seconds;
    emit timeChanged();
    invalidatePictures();
}

void ReferenceController::togglePlay() {
    playing_ = !playing_;
    emit playingChanged();
}

void ReferenceController::stepFrames(int frames) {
    if (playing_) {
        playing_ = false;
        emit playingChanged();
    }
    const double rate = reference_ && reference_->moving()
                            ? reference_->framesPerSecond * (options_.speed > 0.01 ? options_.speed : 1.0)
                            : std::max(1.0, app_->session().effect().frameRate);
    setTime(time_ + frames / rate);
}

void ReferenceController::setOverlayOpacity(double opacity) {
    opacity = std::clamp(opacity, 0.0, 1.0);
    if (std::fabs(opacity - overlayOpacity_) > 1e-6) {
        overlayOpacity_ = opacity;
        invalidatePictures();
    }
}

void ReferenceController::tick(double seconds) {
    // A new or opened effect brings its own reference, or none.
    if (app_->session().generation() != seenGeneration_) {
        seenGeneration_ = app_->session().generation();
        restoreFromProject();
    }
    if (!open_ || !playing_ || !reference_) {
        return;
    }
    if (!std::isfinite(seconds) || seconds < 0.0) {
        seconds = 0.0;
    }
    const double span = std::max(0.05, length());
    time_ = std::fmod(time_ + std::min(seconds, 0.25), span);
    emit timeChanged();
    invalidatePictures();
}

QVariantList ReferenceController::referenceMarkers() const {
    QVariantList list;
    if (analysis_ && analysis_->time.available) {
        for (const TimeMarker& marker : analysis_->time.markers) {
            QVariantMap row;
            row.insert(QStringLiteral("time"), marker.time);
            row.insert(QStringLiteral("label"), text(marker.label));
            row.insert(QStringLiteral("key"), text(marker.key));
            list.push_back(row);
        }
    }
    return list;
}

QVariantList ReferenceController::effectMarkers() const {
    QVariantList list;
    if (!built_) {
        return list;
    }
    const double offset = moving() ? placement_.referenceStart : 0.0;
    const double span = length();
    for (const vfx::Layer& layer : app_->session().effect().layers) {
        if (!layer.enabled) {
            continue;
        }
        const auto add = [&](double at, const QString& label) {
            const double shown = at + offset;
            if (shown >= -1e-6 && shown <= span + 1e-6) {
                QVariantMap row;
                row.insert(QStringLiteral("time"), shown);
                row.insert(QStringLiteral("label"), label);
                list.push_back(row);
            }
        };
        bool burst = false;
        for (const vfx::Module& m : layer.modules) {
            if (m.type != "emission") {
                continue;
            }
            if (const vfx::Value* v = m.find("bursts")) {
                if (const auto* bursts = std::get_if<vfx::BurstList>(v)) {
                    for (const vfx::Burst& b : bursts->items) {
                        add(layer.start + b.time, text(layer.name) + QStringLiteral(": burst"));
                        burst = true;
                    }
                }
            }
        }
        if (!burst) {
            add(layer.start, text(layer.name) + QStringLiteral(": starts"));
        }
    }
    return list;
}

void ReferenceController::pictureSize(int& width, int& height) const {
    const double a = aspect();
    if (a >= 1.0) {
        width = kShownSide;
        height = std::max(8, static_cast<int>(std::lround(kShownSide / a)));
    } else {
        height = kShownSide;
        width = std::max(8, static_cast<int>(std::lround(kShownSide * a)));
    }
}

QImage ReferenceController::picture(const QString& kind) {
    if (!reference_ || reference_->frames.empty()) {
        return {};
    }
    if (cacheSerial_ != pictureSerial_) {
        pictureCache_.clear();
        cacheSerial_ = pictureSerial_;
    }
    const auto found = pictureCache_.constFind(kind);
    if (found != pictureCache_.constEnd()) {
        return *found;
    }
    QImage out;
    int width = 0, height = 0;
    pictureSize(width, height);
    const int at = frame();
    if (kind == QStringLiteral("full")) {
        const Image whole = referencePicture(*reference_, ReferenceOptions{}, at, 640);
        out = toQImage(whole.width, whole.height, whole.rgba);
    } else if (kind == QStringLiteral("reference")) {
        if (analysis_) {
            const Picture p = drawReference(*reference_, options_, analysis_->still, at, width, height);
            out = toQImage(p.width, p.height, p.rgba);
        } else {
            const Image cropped = referencePicture(*reference_, options_, at, kShownSide);
            out = toQImage(cropped.width, cropped.height, cropped.rgba);
        }
    } else if (kind == QStringLiteral("made")) {
        if (built_ && analysis_) {
            const vfx::Effect& effect = app_->session().effect();
            // A burst is shown once through, as the clip shows it; one that
            // keeps going or comes round again is wrapped onto its length.
            const double t = comparedTime(*analysis_, effect, effectTimeNow());
            const Picture p = drawLikeReference(effect, placement_, analysis_->still, width, height, t,
                                                &app_->session().images());
            out = toQImage(p.width, p.height, p.rgba);
        }
    } else if (kind == QStringLiteral("overlay") || kind == QStringLiteral("difference")) {
        const QImage a = picture(QStringLiteral("reference"));
        const QImage b = picture(QStringLiteral("made"));
        if (!a.isNull() && !b.isNull() && a.size() == b.size()) {
            Picture pa, pb;
            pa.width = pb.width = a.width();
            pa.height = pb.height = a.height();
            const std::size_t bytes = static_cast<std::size_t>(a.width()) * static_cast<std::size_t>(a.height()) * 4u;
            pa.rgba.resize(bytes);
            pb.rgba.resize(bytes);
            for (int y = 0; y < a.height(); ++y) {
                std::memcpy(&pa.rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(a.width()) * 4u], a.constScanLine(y),
                            static_cast<std::size_t>(a.width()) * 4u);
                std::memcpy(&pb.rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(a.width()) * 4u], b.constScanLine(y),
                            static_cast<std::size_t>(a.width()) * 4u);
            }
            const Picture p = kind == QStringLiteral("overlay") ? overlayPictures(pa, pb, static_cast<float>(overlayOpacity_))
                                                                : differencePicture(pa, pb);
            out = toQImage(p.width, p.height, p.rgba);
        } else {
            out = a;
        }
    }
    pictureCache_.insert(kind, out);
    return out;
}

// ---------------------------------------------------------- the self-test

bool ReferenceController::waitUntilIdle(int milliseconds) {
    QElapsedTimer clock;
    clock.start();
    // A reading may have been scheduled a moment ago and not begun yet.
    while (clock.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (!busy_ && queuedJob_ < 0 && clock.elapsed() > 500) {
            return true;
        }
        QThread::msleep(5);
    }
    return !busy_;
}

QString ReferenceController::report() const {
    QString out = QStringLiteral("%1 (%2)").arg(name_, info());
    out += QStringLiteral("; findings: %1").arg(findings_.size());
    if (built_) {
        out += QStringLiteral("; built: %1 layers, measured %2 alike")
                   .arg(app_->session().effect().layers.size())
                   .arg(percentText(similarity_.value(QStringLiteral("overall")).toFloat()));
    }
    if (!problem_.isEmpty()) {
        out += QStringLiteral("; problem: ") + problem_ + QLatin1Char(' ') + problemDetail_;
    }
    return out;
}

// ------------------------------------------------------- the picture item

ReferenceView::ReferenceView(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    setOpaquePainting(false);
}

void ReferenceView::setSource(ReferenceController* source) {
    if (source_ == source) {
        return;
    }
    if (source_) {
        source_->disconnect(this);
    }
    source_ = source;
    if (source_) {
        connect(source_, &ReferenceController::picturesChanged, this, [this]() { update(); });
    }
    emit changed();
    update();
}

void ReferenceView::setKind(const QString& kind) {
    if (kind_ != kind) {
        kind_ = kind;
        emit changed();
        update();
    }
}

void ReferenceView::setZoom(double zoom) {
    zoom = std::clamp(zoom, 1.0, 16.0);
    if (zoom != zoom_) {
        zoom_ = zoom;
        emit changed();
        update();
    }
}

void ReferenceView::setPanX(double pan) {
    if (pan != panX_) {
        panX_ = pan;
        emit changed();
        update();
    }
}

void ReferenceView::setPanY(double pan) {
    if (pan != panY_) {
        panY_ = pan;
        emit changed();
        update();
    }
}

QRectF ReferenceView::pictureRect() const {
    const double a = source_ ? (kind_ == QStringLiteral("full") ? source_->fullAspect() : source_->aspect()) : 1.0;
    double w = width(), h = height();
    if (w <= 0 || h <= 0) {
        return {};
    }
    if (w / h > a) {
        w = h * a;
    } else {
        h = w / a;
    }
    w *= zoom_;
    h *= zoom_;
    return QRectF((width() - w) * 0.5 + panX_, (height() - h) * 0.5 + panY_, w, h);
}

void ReferenceView::paint(QPainter* painter) {
    if (!source_) {
        return;
    }
    const QImage image = source_->picture(kind_);
    if (image.isNull()) {
        return;
    }
    const QRectF target = pictureRect();
    painter->setClipRect(boundingRect());
    // A see-through reference sits on a checkerboard, so it can be told from black.
    if (image.hasAlphaChannel() && kind_ != QStringLiteral("difference")) {
        const QRectF shown = target.intersected(boundingRect());
        const int cell = 10;
        for (int y = static_cast<int>(shown.top()); y < shown.bottom(); y += cell) {
            for (int x = static_cast<int>(shown.left()); x < shown.right(); x += cell) {
                const bool dark = ((x / cell) + (y / cell)) % 2 == 0;
                painter->fillRect(QRectF(x, y, cell, cell).intersected(shown), dark ? QColor(38, 40, 46) : QColor(52, 55, 62));
            }
        }
    }
    painter->setRenderHint(QPainter::SmoothPixmapTransform, zoom_ < 3.0);
    painter->drawImage(target, image);
}
