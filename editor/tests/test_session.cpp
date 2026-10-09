// An editing session, driven the way the window drives it, with no window.
#include <catch2/catch_amalgamated.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "vfx/FileIO.h"
#include "vfx/editor/Session.h"
#include "vfx/editor/SpriteMesh.h"

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

namespace {

// A 4 x 2 sprite sheet of 8-pixel cells, each one solid colour.
std::string testSheet() {
    Image sheet;
    sheet.width = 32;
    sheet.height = 16;
    sheet.rgba.resize(32 * 16 * 4);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 32; ++x) {
            const int cell = (y / 8) * 4 + x / 8;
            std::uint8_t* p = &sheet.rgba[static_cast<std::size_t>(y * 32 + x) * 4];
            p[0] = static_cast<std::uint8_t>(cell * 30);
            p[1] = static_cast<std::uint8_t>(255 - cell * 30);
            p[2] = 128;
            p[3] = 255;
        }
    }
    return encodePng(sheet);
}

Id firstLayer(const Session& s) { return s.effect().layers.at(0).id; }

Id textureOf(const Session& s) {
    for (const auto& m : s.effect().layers.at(0).modules) {
        if (m.type == "sprite") {
            return std::get<AssetRef>(*m.find("texture")).id;
        }
    }
    return {};
}

}  // namespace

TEST_CASE("a layer can draw the artist's own picture, with undo", "[session][image]") {
    Session s;
    const std::string png = testSheet();
    REQUIRE(s.useImage(firstLayer(s), "/Users/me/My Fire.png", png).ok());
    REQUIRE(s.effect().assets.size() == 1);
    const Asset asset = s.effect().assets[0];
    CHECK(asset.path.rfind("images/my-fire-", 0) == 0);
    CHECK(textureOf(s) == asset.id);
    CHECK(s.images().has(asset.id));
    CHECK(std::filesystem::exists(s.projectFolder() / asset.path));

    // Drawn as the picture: the mesh says so, in one run of that picture.
    play(s, 0.5);
    SpriteMesh mesh;
    View view;
    buildSpriteMesh(s.frame(), view, mesh, &s.images());
    REQUIRE(mesh.drawn > 0);
    REQUIRE(mesh.runs.size() == 1);
    CHECK(mesh.runs[0].texture == asset.id);
    CHECK(mesh.runs[0].indexCount == mesh.indices.size());
    CHECK(mesh.vertices[0].shape == kPictureShape);

    // Without the pictures it falls back to the Shape.
    buildSpriteMesh(s.frame(), view, mesh);
    CHECK(mesh.vertices[0].shape >= 0.0f);
    CHECK_FALSE(mesh.runs[0].texture.valid());

    // The same picture again is not added twice.
    REQUIRE(s.useImage(firstLayer(s), "My Fire.png", png).ok());
    CHECK(s.effect().assets.size() == 1);

    REQUIRE(s.undo().ok());
    CHECK_FALSE(s.commands().canUndo());
    CHECK(s.effect().assets.empty());
    CHECK_FALSE(textureOf(s).valid());
    play(s, 0.1);
    CHECK_FALSE(s.images().has(asset.id));
    REQUIRE(s.redo().ok());
    play(s, 0.1);
    CHECK(textureOf(s) == asset.id);
    CHECK(s.images().has(asset.id));
    CHECK(s.imageProblems().empty());

    // Back to the shape drops the picture from the effect.
    REQUIRE(s.clearImage(firstLayer(s)).ok());
    CHECK(s.effect().assets.empty());
    CHECK_FALSE(textureOf(s).valid());
}

TEST_CASE("pictures are saved next to the effect and found again", "[session][image]") {
    const auto folder = scratchFile("pictures");
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    std::filesystem::path relative;
    {
        Session s;
        REQUIRE(s.useImage(firstLayer(s), "sheet.png", testSheet()).ok());
        relative = pathFromUtf8(s.effect().assets[0].path);
        const auto scratch = s.projectFolder();
        REQUIRE(s.saveAs(folder / "Fire.vfx").ok());
        CHECK(std::filesystem::exists(folder / relative));
        CHECK(s.projectFolder() == folder);
        CHECK_FALSE(scratch.empty());
    }
    Session again;
    REQUIRE(again.open(folder / "Fire.vfx").ok());
    play(again, 0.2);
    CHECK(again.imageProblems().empty());
    CHECK(again.images().size() == 1);

    // A picture that has gone missing is reported, and the layer still draws.
    std::filesystem::remove(folder / relative);
    Session third;
    REQUIRE(third.open(folder / "Fire.vfx").ok());
    play(third, 0.5);
    REQUIRE(third.imageProblems().size() == 1);
    CHECK(third.imageProblems()[0].find("can't be found") != std::string::npos);
    SpriteMesh mesh;
    buildSpriteMesh(third.frame(), View{}, mesh, &third.images());
    CHECK(mesh.drawn > 0);
    std::filesystem::remove_all(folder);
}

TEST_CASE("a damaged picture is refused and nothing changes", "[session][image]") {
    Session s;
    CHECK_FALSE(s.useImage(firstLayer(s), "x.png", "not a png").ok());
    CHECK(s.effect().assets.empty());
    CHECK_FALSE(s.commands().canUndo());
}
