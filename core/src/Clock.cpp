#include "vfx/Clock.h"

#include <cmath>

namespace vfx {

namespace {

bool finite(double v) { return v == v && v < 1e300 && v > -1e300; }

double atLeast(double v, double low, double fallback) {
    return finite(v) && v >= low ? v : fallback;
}

}  // namespace

PlaybackClock::PlaybackClock(double duration, bool loop, double frameRate, double step)
    : duration_(atLeast(duration, 0.01, 2.0)),
      loop_(loop),
      frameRate_(atLeast(frameRate, 1.0, 30.0)),
      step_(atLeast(step, 1e-6, kSimulationStep)) {}

void PlaybackClock::configure(double duration, bool loop, double frameRate) {
    duration_ = atLeast(duration, 0.01, duration_);
    loop_ = loop;
    frameRate_ = atLeast(frameRate, 1.0, frameRate_);
    if (!loop_ && time_ > duration_) {
        time_ = duration_;
        playing_ = false;
    }
}

void PlaybackClock::play() {
    if (finished()) {
        time_ = 0.0;
    }
    playing_ = true;
}

void PlaybackClock::pause() { playing_ = false; }

void PlaybackClock::stop() {
    playing_ = false;
    time_ = 0.0;
}

void PlaybackClock::setTimeScale(double scale) {
    if (!finite(scale)) {
        return;
    }
    timeScale_ = scale < 0.0 ? 0.0 : (scale > 16.0 ? 16.0 : scale);
}

void PlaybackClock::advance(double realSeconds) {
    if (!playing_ || !finite(realSeconds) || realSeconds <= 0.0) {
        return;
    }
    time_ += realSeconds * timeScale_;
    if (!loop_ && time_ >= duration_) {
        time_ = duration_;
        playing_ = false;
    }
}

void PlaybackClock::seek(double displayTime) {
    if (!finite(displayTime)) {
        return;
    }
    // Scrubbing always shows the first pass, so the same spot on the
    // timeline always shows the same picture.
    time_ = displayTime < 0.0 ? 0.0 : (displayTime > duration_ ? duration_ : displayTime);
}

void PlaybackClock::stepFrames(int frames) {
    playing_ = false;
    const double frame = std::floor(displayTime() * frameRate_ + 1e-6) + static_cast<double>(frames);
    seek(frame / frameRate_);
}

double PlaybackClock::displayTime() const {
    if (!loop_) {
        return time_ < duration_ ? time_ : duration_;
    }
    return time_ - std::floor(time_ / duration_) * duration_;
}

std::int64_t PlaybackClock::displayFrame() const {
    return static_cast<std::int64_t>(std::floor(displayTime() * frameRate_ + 1e-6));
}

std::int64_t PlaybackClock::pass() const {
    return loop_ ? static_cast<std::int64_t>(std::floor(time_ / duration_)) : 0;
}

std::int64_t PlaybackClock::simulationStep() const {
    return static_cast<std::int64_t>(std::floor(time_ / step_ + 1e-6));
}

}  // namespace vfx
