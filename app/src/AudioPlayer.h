// VFX Forge app: playing an effect's sound in step with its picture.
//
// The editor layer's mixer says exactly what the effect sounds like at any
// moment. This keeps a short stretch of that sound queued on the computer's
// audio output, a little ahead of the playback clock. When the clock jumps
// (a seek, a restart, a change of speed) or the two drift apart, the queue is
// thrown away and filled again from the clock's new time, so the sound is
// never more than a few hundredths of a second off the picture.
#pragma once

#include <memory>
#include <vector>

#include <QAudioFormat>
#include <QObject>

class QAudioSink;
class QIODevice;

namespace vfx {
struct Effect;
}
namespace vfx::editor {
class SoundSet;
struct Sound;
}

class AudioPlayer : public QObject {
    Q_OBJECT
public:
    explicit AudioPlayer(QObject* parent = nullptr);
    ~AudioPlayer() override;

    // False when the computer has no sound output.
    bool available() const { return available_; }

    // Call once per displayed frame. time is the clock's time counting every
    // pass; speed is the playback speed.
    void update(const vfx::Effect& effect, const vfx::editor::SoundSet& sounds, double time, bool playing,
                double speed, bool enabled);

    // Plays one sound on its own, from the start (the library's preview).
    void preview(std::shared_ptr<const vfx::editor::Sound> sound, float volume = 1.0f);
    void stopPreview();

private:
    void start();
    void stop();
    void write(const float* stereo, int frames, QIODevice* to);

    bool available_ = false;
    QAudioFormat format_;
    int rate_ = 48000;

    QAudioSink* sink_ = nullptr;
    QIODevice* io_ = nullptr;
    bool running_ = false;
    double writeHead_ = 0.0;  // playback time of the next sample to queue
    double speed_ = 1.0;
    std::vector<float> chunk_;
    std::vector<char> bytes_;

    QAudioSink* previewSink_ = nullptr;
    QIODevice* previewIo_ = nullptr;
    std::shared_ptr<const vfx::editor::Sound> previewSound_;
    double previewAt_ = 0.0;  // seconds into the preview sound
    float previewVolume_ = 1.0f;
};
