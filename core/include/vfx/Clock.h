// VFX Forge core: the playback clock behind the timeline.
//
// The clock owns time: play, pause, stop, loop, scrub, frame step and time
// scale. It does not own a timer. Whoever drives it (the editor's frame
// loop, a test, the command-line tool) tells it how much real time passed.
// That keeps it free of threads and makes it exact to test.
#pragma once

#include <cstdint>

#include "vfx/Program.h"

namespace vfx {

class PlaybackClock {
public:
    PlaybackClock(double duration, bool loop, double frameRate, double step = kSimulationStep);

    // Applies edited effect settings. The current time is kept where it can be.
    void configure(double duration, bool loop, double frameRate);

    void play();   // from the start again if a one-shot effect has finished
    void pause();
    void stop();   // pause and return to the start
    bool playing() const { return playing_; }

    // True when a one-shot effect has reached its end.
    bool finished() const { return !loop_ && time_ >= duration_; }

    // 1 is real time, 0.5 is half speed. Kept between 0 and 16.
    void setTimeScale(double scale);
    double timeScale() const { return timeScale_; }

    // Moves time on while playing. Has no effect when paused.
    void advance(double realSeconds);

    // Scrubbing: jump to a moment on the timeline, in seconds.
    void seek(double displayTime);

    // Frame stepping: pause, snap to a whole frame, then move by this many.
    void stepFrames(int frames);

    // Time since playback began. In a looping effect this keeps growing, one
    // pass after another, because particles live on across the wrap.
    double time() const { return time_; }

    // Where the playhead sits on the timeline, from 0 to the duration.
    double displayTime() const;
    std::int64_t displayFrame() const;
    std::int64_t pass() const;  // which time round a looping effect is on

    // The simulation step that shows this moment.
    std::int64_t simulationStep() const;

    double duration() const { return duration_; }
    bool loop() const { return loop_; }
    double frameRate() const { return frameRate_; }

private:
    double duration_;
    bool loop_;
    double frameRate_;
    double step_;
    double time_ = 0.0;
    double timeScale_ = 1.0;
    bool playing_ = false;
};

}  // namespace vfx
