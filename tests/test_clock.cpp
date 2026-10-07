#include <catch2/catch_amalgamated.hpp>

#include <cmath>
#include <limits>

#include "vfx/Clock.h"

using namespace vfx;

TEST_CASE("The clock only moves while playing") {
    PlaybackClock clock(2.0, true, 30.0);
    CHECK_FALSE(clock.playing());
    clock.advance(0.5);
    CHECK(clock.time() == 0.0);

    clock.play();
    clock.advance(0.5);
    CHECK(clock.time() == 0.5);
    CHECK(clock.simulationStep() == 30);

    clock.pause();
    clock.advance(0.5);
    CHECK(clock.time() == 0.5);

    clock.play();
    clock.advance(0.25);
    CHECK(clock.time() == 0.75);
    clock.stop();
    CHECK(clock.time() == 0.0);
    CHECK_FALSE(clock.playing());
}

TEST_CASE("A one-shot effect plays to its end and stops there") {
    PlaybackClock clock(2.0, false, 30.0);
    clock.play();
    clock.advance(1.5);
    CHECK_FALSE(clock.finished());
    clock.advance(1.5);
    CHECK(clock.time() == 2.0);
    CHECK(clock.displayTime() == 2.0);
    CHECK(clock.finished());
    CHECK_FALSE(clock.playing());
    CHECK(clock.simulationStep() == 120);
    CHECK(clock.pass() == 0);

    clock.advance(1.0);  // nothing more happens
    CHECK(clock.time() == 2.0);

    clock.play();  // play again starts from the beginning
    CHECK(clock.time() == 0.0);
    CHECK(clock.playing());
}

TEST_CASE("A looping effect wraps the playhead while time keeps counting") {
    PlaybackClock clock(2.0, true, 30.0);
    clock.play();
    clock.advance(5.25);
    CHECK(clock.playing());
    CHECK_FALSE(clock.finished());
    CHECK(clock.time() == 5.25);
    CHECK(clock.displayTime() == Catch::Approx(1.25));
    CHECK(clock.pass() == 2);
    CHECK(clock.simulationStep() == 315);
    CHECK(clock.displayFrame() == 37);
}

TEST_CASE("Time scale speeds playback up and slows it down") {
    PlaybackClock clock(10.0, true, 30.0);
    clock.play();
    clock.setTimeScale(0.5);
    clock.advance(1.0);
    CHECK(clock.time() == 0.5);
    clock.setTimeScale(4.0);
    clock.advance(1.0);
    CHECK(clock.time() == 4.5);
    clock.setTimeScale(0.0);  // frozen but still "playing"
    clock.advance(1.0);
    CHECK(clock.time() == 4.5);

    clock.setTimeScale(-3.0);
    CHECK(clock.timeScale() == 0.0);
    clock.setTimeScale(1000.0);
    CHECK(clock.timeScale() == 16.0);
    clock.setTimeScale(std::nan(""));
    CHECK(clock.timeScale() == 16.0);
}

TEST_CASE("Scrubbing moves the playhead and stays on the timeline") {
    PlaybackClock clock(2.0, true, 30.0);
    clock.play();
    clock.advance(7.0);
    clock.seek(0.75);
    CHECK(clock.time() == 0.75);  // scrubbing always shows the first pass
    CHECK(clock.pass() == 0);
    CHECK(clock.playing());       // scrubbing does not stop playback
    clock.seek(-1.0);
    CHECK(clock.time() == 0.0);
    clock.seek(99.0);
    CHECK(clock.time() == 2.0);
    clock.seek(std::nan(""));
    CHECK(clock.time() == 2.0);
}

TEST_CASE("Frame stepping pauses and moves by whole frames") {
    PlaybackClock clock(2.0, false, 24.0);
    clock.play();
    clock.advance(0.51);  // between frame 12 and frame 13
    clock.stepFrames(1);
    CHECK_FALSE(clock.playing());
    CHECK(clock.displayFrame() == 13);
    CHECK(clock.time() == Catch::Approx(13.0 / 24.0));
    clock.stepFrames(1);
    CHECK(clock.displayFrame() == 14);
    clock.stepFrames(-2);
    CHECK(clock.displayFrame() == 12);
    clock.stepFrames(-100);
    CHECK(clock.displayFrame() == 0);
    clock.stepFrames(1000);
    CHECK(clock.time() == 2.0);

    // Every frame can be reached, and stepping never skips or sticks.
    clock.stop();
    for (int frame = 1; frame <= 48; ++frame) {
        clock.stepFrames(1);
        REQUIRE(clock.displayFrame() == frame);
    }
}

TEST_CASE("Changing the effect's settings keeps the clock sensible") {
    PlaybackClock clock(4.0, false, 30.0);
    clock.play();
    clock.advance(3.0);
    clock.configure(2.0, false, 30.0);  // the effect was shortened past the playhead
    CHECK(clock.time() == 2.0);
    CHECK(clock.finished());

    clock.configure(2.0, true, 60.0);
    CHECK(clock.loop());
    CHECK(clock.frameRate() == 60.0);
    CHECK_FALSE(clock.finished());
    clock.play();
    clock.advance(1.0);
    CHECK(clock.displayTime() == Catch::Approx(1.0));
}

TEST_CASE("The clock shrugs off nonsense") {
    const double inf = std::numeric_limits<double>::infinity();
    PlaybackClock clock(-5.0, true, 0.0);  // falls back to sane settings
    CHECK(clock.duration() == 2.0);
    CHECK(clock.frameRate() == 30.0);
    clock.play();
    clock.advance(std::nan(""));
    clock.advance(inf);
    clock.advance(-3.0);
    CHECK(clock.time() == 0.0);
    clock.configure(std::nan(""), true, inf);
    CHECK(clock.duration() == 2.0);
    CHECK(clock.frameRate() == 30.0);
}

TEST_CASE("The clock and the step count agree over a long run of uneven frames") {
    PlaybackClock clock(1.5, true, 30.0);
    clock.play();
    double total = 0.0;
    std::int64_t last = 0;
    for (int i = 0; i < 100000; ++i) {
        const double frame = 0.004 + 0.0001 * (i % 97);  // 4 to 14 ms
        clock.advance(frame);
        total += frame;
        const std::int64_t step = clock.simulationStep();
        REQUIRE(step >= last);  // never runs backwards
        last = step;
    }
    CHECK(clock.time() == Catch::Approx(total));
    CHECK(std::llabs(last - static_cast<std::int64_t>(total * 60.0)) <= 1);
}
