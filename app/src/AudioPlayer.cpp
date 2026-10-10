#include "AudioPlayer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <QAudioDevice>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>

#include "vfx/Effect.h"
#include "vfx/editor/Audio.h"

namespace {

constexpr double kLead = 0.12;        // seconds of sound queued ahead of the clock
constexpr double kTolerance = 0.06;   // more drift than this and the queue is refilled
constexpr int kChunk = 480;           // frames mixed at a time (10 ms at 48 kHz)

}  // namespace

AudioPlayer::AudioPlayer(QObject* parent) : QObject(parent) {
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        return;
    }
    format_.setSampleRate(rate_);
    format_.setChannelCount(2);
    format_.setSampleFormat(QAudioFormat::Float);
    if (!device.isFormatSupported(format_)) {
        format_.setSampleFormat(QAudioFormat::Int16);
        if (!device.isFormatSupported(format_)) {
            format_ = device.preferredFormat();
            rate_ = format_.sampleRate() > 0 ? format_.sampleRate() : 48000;
            if (format_.channelCount() != 2 ||
                (format_.sampleFormat() != QAudioFormat::Float && format_.sampleFormat() != QAudioFormat::Int16)) {
                return;  // an output this player cannot feed
            }
        }
    }
    sink_ = new QAudioSink(device, format_, this);
    sink_->setBufferSize(static_cast<qsizetype>(format_.bytesForDuration(300000)));  // 0.3 s
    previewSink_ = new QAudioSink(device, format_, this);
    previewSink_->setBufferSize(static_cast<qsizetype>(format_.bytesForDuration(300000)));
    available_ = true;
    chunk_.resize(static_cast<std::size_t>(kChunk) * 2);
}

AudioPlayer::~AudioPlayer() {
    stop();
    stopPreview();
}

void AudioPlayer::start() {
    if (!sink_) {
        return;
    }
    io_ = sink_->start();
    running_ = io_ != nullptr;
}

void AudioPlayer::stop() {
    if (sink_ && running_) {
        sink_->stop();
    }
    io_ = nullptr;
    running_ = false;
}

void AudioPlayer::write(const float* stereo, int frames, QIODevice* to) {
    if (!to) {
        return;
    }
    if (format_.sampleFormat() == QAudioFormat::Float) {
        to->write(reinterpret_cast<const char*>(stereo), static_cast<qint64>(frames) * 2 * 4);
        return;
    }
    bytes_.resize(static_cast<std::size_t>(frames) * 2 * 2);
    auto* out = reinterpret_cast<std::int16_t*>(bytes_.data());
    for (int i = 0; i < frames * 2; ++i) {
        out[i] = static_cast<std::int16_t>(std::lround(std::clamp(stereo[i], -1.0f, 1.0f) * 32767.0f));
    }
    to->write(bytes_.data(), static_cast<qint64>(bytes_.size()));
}

void AudioPlayer::update(const vfx::Effect& effect, const vfx::editor::SoundSet& sounds, double time,
                         bool playing, double speed, bool enabled) {
    if (!available_) {
        return;
    }

    // The library preview, if one is playing.
    if (previewIo_ && previewSound_) {
        const auto& s = *previewSound_;
        const int bytesPerFrame = format_.bytesPerFrame();
        int room = bytesPerFrame > 0 ? static_cast<int>(previewSink_->bytesFree()) / bytesPerFrame : 0;
        const double step = static_cast<double>(s.sampleRate) / rate_;
        while (room >= kChunk && previewAt_ < static_cast<double>(s.frames())) {
            for (int i = 0; i < kChunk; ++i) {
                const double at = previewAt_ + step * i;
                float l = 0.0f, r = 0.0f;
                if (at < static_cast<double>(s.frames()) - 1.0) {
                    const auto i0 = static_cast<std::size_t>(at);
                    const auto f = static_cast<float>(at - static_cast<double>(i0));
                    const auto ch = static_cast<std::size_t>(s.channels);
                    l = s.samples[i0 * ch] + (s.samples[(i0 + 1) * ch] - s.samples[i0 * ch]) * f;
                    r = s.channels == 2 ? s.samples[i0 * ch + 1] + (s.samples[(i0 + 1) * ch + 1] - s.samples[i0 * ch + 1]) * f
                                        : l;
                }
                chunk_[static_cast<std::size_t>(2 * i)] = l * previewVolume_;
                chunk_[static_cast<std::size_t>(2 * i + 1)] = r * previewVolume_;
            }
            write(chunk_.data(), kChunk, previewIo_);
            previewAt_ += step * kChunk;
            room -= kChunk;
        }
    }

    const bool wanted = enabled && playing && speed > 0.0 && vfx::editor::hasSound(effect);
    if (!wanted) {
        stop();
        return;
    }
    // How far ahead of the clock the queue already reaches.
    const int bytesPerFrame = format_.bytesPerFrame();
    const double queued = running_ && bytesPerFrame > 0
                              ? static_cast<double>(sink_->bufferSize() - sink_->bytesFree()) / bytesPerFrame / rate_
                              : 0.0;
    const double heard = writeHead_ - queued * speed_;  // what is coming out of the speakers now
    if (!running_ || speed != speed_ || std::abs(heard - time) > kTolerance) {
        stop();
        start();
        writeHead_ = time;
        speed_ = speed;
    }
    if (!running_) {
        return;
    }
    int room = bytesPerFrame > 0 ? static_cast<int>(sink_->bytesFree()) / bytesPerFrame : 0;
    while (room >= kChunk && writeHead_ - time < kLead * speed) {
        vfx::editor::mixSound(effect, sounds, writeHead_, speed, kChunk, rate_, chunk_.data());
        write(chunk_.data(), kChunk, io_);
        writeHead_ += kChunk * speed / rate_;
        room -= kChunk;
    }
}

void AudioPlayer::preview(std::shared_ptr<const vfx::editor::Sound> sound, float volume) {
    if (!available_ || !sound || sound->frames() == 0) {
        return;
    }
    stopPreview();
    previewSound_ = std::move(sound);
    previewAt_ = 0.0;
    previewVolume_ = volume;
    previewIo_ = previewSink_->start();
}

void AudioPlayer::stopPreview() {
    if (previewSink_ && previewIo_) {
        previewSink_->stop();
    }
    previewIo_ = nullptr;
    previewSound_.reset();
}
