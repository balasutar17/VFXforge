// An editing session, driven the way the window drives it, with no window.
#include <catch2/catch_amalgamated.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "vfx/FileIO.h"
#include "vfx/editor/Session.h"

using namespace vfx;
using namespace vfx::editor;

namespace {

std::filesystem::path scratchFile(const char* name) {
    return std::filesystem::temp_directory_path() / (std::string("vfxforge_session_") + name);
}

void play(Session& s, double seconds) {
    const int frames = static_cast<int>(std::lround(seconds * 60.0));
    for (int i = 0; i < frames; ++i) {
        s.tick(1.0 / 60.0);
    }
}

const ControlView* find(const std::vector<ControlView>& controls, const char* label) {
    for (const auto& c : controls) {
        if (c.label == label) {
            return &c;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("a new session opens playing, with something to look at", "[session]") {
    Session s;
    REQUIRE(s.effect().layers.size() == 1);
    CHECK(s.effect().space == "2d");
    CHECK_FALSE(s.dirty());
    CHECK_FALSE(s.hasFile());
    CHECK(s.clock().playing());

    play(s, 1.0);
    CHECK(s.particleCount() > 20);
    CHECK(s.frame().instances.size() == s.particleCount());
    REQUIRE(s.frame().batches.size() == 1);
    CHECK(s.frame().batches[0].blend == BlendMode::Additive);
    CHECK(s.cappedLayers().empty());
}

TEST_CASE("a new 3D session is three-dimensional", "[session]") {
    Session s;
    const auto before = s.generation();
    s.newEffect(true);
    CHECK(s.effect().space == "3d");
    CHECK(s.generation() == before + 1);
    play(s, 0.5);
    CHECK_FALSE(s.frame().flat);
    CHECK(s.particleCount() > 0);
}

TEST_CASE("the starter effect offers the ten Simple controls", "[session]") {
    Session s;
    const Id layer = s.effect().layers[0].id;
    const auto controls = s.controls(layer);
    REQUIRE(controls.size() == 10);
    for (const char* label : {"Size", "Speed", "Amount", "Lifetime", "Color", "Glow", "Spread",
                              "Direction", "Shape", "Blend"}) {
        const ControlView* c = find(controls, label);
        REQUIRE(c != nullptr);
        REQUIRE(c->desc != nullptr);
        CHECK(holdsKind(c->value, c->desc->kind));
    }
    CHECK(std::get<Scalar>(find(controls, "Amount")->value) == Scalar::constant(60.0));
    CHECK(std::get<double>(find(controls, "Glow")->value) == 1.6);
    CHECK(s.controls(Id{12345}).empty());
}

TEST_CASE("changing a control changes the picture and can be undone", "[session]") {
    Session s;
    const Id layer = s.effect().layers[0].id;
    const ControlView amount = *find(s.controls(layer), "Amount");

    play(s, 2.0);
    const auto before = s.particleCount();

    REQUIRE(s.setControl(layer, amount.control, Scalar::constant(180.0)));
    CHECK(s.dirty());
    CHECK(s.commands().undoName() == "Change Amount");
    s.tick(0.0);
    const auto more = s.particleCount();
    CHECK(more > before * 2);  // three times the rate, caught up at once

    REQUIRE(s.undo());
    s.tick(0.0);
    CHECK(s.particleCount() == before);
    CHECK_FALSE(s.dirty());

    REQUIRE(s.redo());
    s.tick(0.0);
    CHECK(s.particleCount() == more);
}

TEST_CASE("a value the property cannot hold is refused and nothing changes", "[session]") {
    Session s;
    const Id layer = s.effect().layers[0].id;
    const ControlView amount = *find(s.controls(layer), "Amount");
    CHECK_FALSE(s.setControl(layer, amount.control, Scalar::constant(-5.0)));
    CHECK_FALSE(s.setControl(layer, amount.control, Value(std::string("lots"))));
    CHECK_FALSE(s.setControl(layer, Id{999}, Scalar::constant(5.0)));
    CHECK_FALSE(s.setControl(Id{999}, amount.control, Scalar::constant(5.0)));
    CHECK_FALSE(s.dirty());
    CHECK(std::get<Scalar>(find(s.controls(layer), "Amount")->value) == Scalar::constant(60.0));
}

TEST_CASE("a slider drag is one undo step", "[session]") {
    Session s;
    const Id layer = s.effect().layers[0].id;
    const ControlView glow = *find(s.controls(layer), "Glow");

    s.beginEdit("Change Glow");
    for (int i = 1; i <= 50; ++i) {
        REQUIRE(s.setControl(layer, glow.control, Value(1.6 + i * 0.1)));
        s.tick(1.0 / 60.0);
    }
    s.endEdit();
    s.endEdit();  // an extra end is harmless

    CHECK(s.commands().undoCount() == 1);
    CHECK(std::get<double>(find(s.controls(layer), "Glow")->value) == Catch::Approx(6.6));
    REQUIRE(s.undo());
    CHECK(std::get<double>(find(s.controls(layer), "Glow")->value) == 1.6);
    CHECK_FALSE(s.commands().canUndo());
}

TEST_CASE("editing while playing never changes what a moment looks like", "[session]") {
    // Two sessions open the same file and end with the same settings at the
    // same moment. One got there by editing mid-flight. They must show
    // exactly the same particles.
    const auto file = scratchFile("midflight.vfx");
    {
        Session s;
        REQUIRE(s.saveAs(file));
    }
    Session a, b;
    REQUIRE(a.open(file));
    REQUIRE(b.open(file));
    const Id layer = a.effect().layers[0].id;
    const ControlView speed = *find(a.controls(layer), "Speed");

    REQUIRE(a.setControl(layer, speed.control, Scalar::constant(8.0)));
    play(a, 1.5);

    play(b, 0.7);
    CHECK(hashFrame(b.frame()) != hashFrame(a.frame()));
    REQUIRE(b.setControl(layer, speed.control, Scalar::constant(8.0)));
    play(b, 0.8);

    CHECK(a.frame().step == b.frame().step);
    CHECK(a.particleCount() == b.particleCount());
    CHECK(hashFrame(a.frame()) == hashFrame(b.frame()));

    std::filesystem::remove(file);
}

TEST_CASE("scrubbing shows the same picture as playing to that moment", "[session]") {
    Session s;
    play(s, 1.25);
    const auto played = hashFrame(s.frame());
    const auto step = s.frame().step;

    s.clock().pause();
    s.clock().seek(2.5);
    s.tick(0.0);
    CHECK(s.frame().step != step);
    s.clock().seek(1.25);
    s.tick(0.0);
    CHECK(s.frame().step == step);
    CHECK(hashFrame(s.frame()) == played);

    // Paused: time passing changes nothing.
    s.tick(5.0);
    CHECK(hashFrame(s.frame()) == played);
}

TEST_CASE("a long stall jumps to the right moment instead of freezing", "[session]") {
    const auto file = scratchFile("stall.vfx");
    {
        Session s;
        REQUIRE(s.set(Path::effect("loop"), Value(std::string("once"))));
        REQUIRE(s.saveAs(file));
    }
    Session a, b;
    REQUIRE(a.open(file));
    REQUIRE(b.open(file));
    a.tick(2.0);  // one enormous frame
    play(b, 2.0);
    CHECK(a.frame().step == b.frame().step);
    CHECK(hashFrame(a.frame()) == hashFrame(b.frame()));

    // And the same when it happens in the middle of a looping effect.
    REQUIRE(a.set(Path::effect("loop"), Value(std::string("loop"))));
    REQUIRE(b.set(Path::effect("loop"), Value(std::string("loop"))));
    a.clock().play();
    b.clock().play();
    a.tick(7.5);
    play(b, 7.5);
    CHECK(a.frame().step == b.frame().step);
    CHECK(hashFrame(a.frame()) == hashFrame(b.frame()));

    std::filesystem::remove(file);
}

TEST_CASE("a one-shot effect stops at its end and plays again from the start", "[session]") {
    Session s;
    REQUIRE(s.set(Path::effect("loop"), Value(std::string("once"))));
    play(s, 4.0);
    CHECK(s.clock().finished());
    CHECK_FALSE(s.clock().playing());
    CHECK(s.clock().displayTime() == s.effect().duration);
    s.clock().play();
    s.tick(1.0 / 60.0);
    CHECK(s.clock().displayTime() < 0.1);
}

TEST_CASE("layers can be added and removed, with undo", "[session]") {
    Session s;
    Id added;
    REQUIRE(s.addEmitter("Smoke", &added));
    REQUIRE(s.effect().layers.size() == 2);
    CHECK(s.effect().layers[1].id == added);
    CHECK(s.effect().layers[1].name == "Smoke");
    CHECK(s.effect().layers[1].duration == s.effect().duration);
    CHECK(s.controls(added).size() == 10);

    play(s, 1.0);
    CHECK(s.frame().batches.size() == 2);

    REQUIRE(s.removeLayer(s.effect().layers[0].id));
    s.tick(0.0);
    CHECK(s.frame().batches.size() == 1);
    REQUIRE(s.undo());
    REQUIRE(s.undo());
    CHECK(s.effect().layers.size() == 1);
    CHECK_FALSE(s.dirty());
    CHECK_FALSE(s.removeLayer(Id{4242}));
}

TEST_CASE("save, change, save again, and open in a second session", "[session]") {
    const auto file = scratchFile("roundtrip ✨.vfx");
    std::filesystem::remove(file);

    Session s;
    CHECK_FALSE(s.save());  // nowhere to save to yet
    REQUIRE(s.set(Path::effect("name"), Value(std::string("Campfire"))));
    REQUIRE(s.saveAs(file));
    CHECK(s.hasFile());
    CHECK_FALSE(s.dirty());

    REQUIRE(s.set(Path::effect("duration"), Value(4.5)));
    CHECK(s.dirty());
    REQUIRE(s.save());
    CHECK_FALSE(s.dirty());
    REQUIRE(s.undo());
    CHECK(s.dirty());  // now differs from what is on disk

    Session other;
    const auto generation = other.generation();
    REQUIRE(other.open(file));
    CHECK(other.generation() == generation + 1);
    CHECK(other.effect().name == "Campfire");
    CHECK(other.effect().duration == 4.5);
    CHECK(other.filePath() == file);
    CHECK_FALSE(other.dirty());
    CHECK_FALSE(other.readOnly());
    CHECK(other.loadNotes().empty());
    CHECK(other.clock().duration() == 4.5);
    play(other, 0.5);
    CHECK(other.particleCount() > 0);

    std::filesystem::remove(file);
}

TEST_CASE("a file that will not open leaves the session as it was", "[session]") {
    const auto file = scratchFile("broken.vfx");
    REQUIRE(writeFileAtomic(file, "this is not an effect"));

    Session s;
    REQUIRE(s.set(Path::effect("name"), Value(std::string("Keep me"))));
    const auto generation = s.generation();
    const Status opened = s.open(file);
    REQUIRE_FALSE(opened);
    CHECK_FALSE(opened.error().message.empty());
    CHECK(s.effect().name == "Keep me");
    CHECK(s.generation() == generation);
    CHECK(s.dirty());
    CHECK_FALSE(s.open(scratchFile("does_not_exist.vfx")));

    std::filesystem::remove(file);
}

TEST_CASE("a file from a newer version opens read-only and is not saved over", "[session]") {
    const auto file = scratchFile("newer.vfx");
    const auto copy = scratchFile("newer_copy.vfx");
    {
        Session s;
        std::string text = writeEffect(s.effect());
        const std::string from = "\"formatVersion\": 1";
        const auto at = text.find(from);
        REQUIRE(at != std::string::npos);
        text.replace(at, from.size(), "\"formatVersion\": 99");
        REQUIRE(writeFileAtomic(file, text));
    }
    Session s;
    REQUIRE(s.open(file));
    CHECK(s.readOnly());
    CHECK_FALSE(s.loadNotes().empty());
    CHECK_FALSE(s.save());
    REQUIRE(s.saveAs(copy));
    CHECK_FALSE(s.readOnly());
    CHECK(s.filePath() == copy);

    std::filesystem::remove(file);
    std::filesystem::remove(copy);
}

TEST_CASE("the sample file opens and plays", "[session]") {
    Session s;
    REQUIRE(s.open(pathFromUtf8(VFX_SAMPLES_DIR "/coin_burst.vfx")));
    CHECK(s.effect().name == "Coin Burst");
    s.tick(0.25);
    CHECK(s.particleCount() == 24);
}

TEST_CASE("colour conversion round-trips and matches known values", "[session][color]") {
    CHECK(linearToSrgb(0.0) == 0.0);
    CHECK(linearToSrgb(1.0) == Catch::Approx(1.0));
    CHECK(linearToSrgb(0.5) == Catch::Approx(0.735357).epsilon(1e-5));
    CHECK(srgbToLinear(0.5) == Catch::Approx(0.214041).epsilon(1e-5));
    CHECK(linearToSrgb(-1.0) == 0.0);
    CHECK(linearToSrgb(std::nan("")) == 0.0);
    for (double v : {0.001, 0.003, 0.02, 0.2, 0.7, 1.0, 3.5}) {
        CHECK(srgbToLinear(linearToSrgb(v)) == Catch::Approx(v).epsilon(1e-12));
    }
}

TEST_CASE("directions and headings convert both ways", "[session][heading]") {
    CHECK(headingFromDirection(Vec3{0, 1, 0}).heading == Catch::Approx(90.0));
    CHECK(headingFromDirection(Vec3{1, 0, 0}).heading == Catch::Approx(0.0));
    CHECK(headingFromDirection(Vec3{0, -1, 0}).heading == Catch::Approx(270.0));
    CHECK(headingFromDirection(Vec3{-3, 0, 0}).heading == Catch::Approx(180.0));
    CHECK(headingFromDirection(Vec3{0, 0, 2}).tilt == Catch::Approx(90.0));
    CHECK(headingFromDirection(Vec3{0, 0, 0}).heading == 90.0);

    const Vec3 up = directionFromHeading(Heading{90.0, 0.0});
    CHECK(up.x == 0.0);  // tidied to an exact zero, so files stay clean
    CHECK(up.y == Catch::Approx(1.0));
    CHECK(up.z == 0.0);

    for (double heading : {0.0, 33.0, 90.0, 123.4, 180.0, 271.0, 359.0}) {
        for (double tilt : {-60.0, 0.0, 15.0, 80.0}) {
            const Heading back = headingFromDirection(directionFromHeading(Heading{heading, tilt}));
            CHECK(back.heading == Catch::Approx(heading).margin(1e-9));
            CHECK(back.tilt == Catch::Approx(tilt).margin(1e-9));
        }
    }
}
