// Exporting animation frames and sprite sheets.
#include <catch2/catch_amalgamated.hpp>

#include <filesystem>

#include "vfx/editor/Frames.h"
#include "vfx/editor/Presets.h"

using namespace vfx;
using namespace vfx::editor;

TEST_CASE("one pass of an effect, as numbered frames and a sheet", "[frames]") {
    IdGenerator ids(4);
    const IdSource newId = [&ids]() { return ids.next(); };
    Effect effect = makePreset("toon-hit", newId).value();  // 0.9 s
    CHECK(frameCount(effect, 30) == 27);
    CHECK(frameCount(effect, 10) == 9);
    effect.duration = 1000;
    CHECK(frameCount(effect, 60) == 600);  // a safety limit
    effect.duration = 0.9;

    const auto base = std::filesystem::temp_directory_path() / "vfxforge_frames_test";
    std::filesystem::remove_all(base);

    FrameOptions options;
    options.width = 64;
    options.height = 48;
    options.framesPerSecond = 10;
    options.view.centerX = 0;
    options.view.centerY = 0;
    options.view.unitsHigh = 5.2f;
    const auto written = exportFrames(effect, options, base, "Toon Hit");
    REQUIRE(written.ok());
    const FramesWritten& w = written.value();
    CHECK(w.frames == 9);
    CHECK(w.columns == 3);
    CHECK(w.rows == 3);
    CHECK(w.folder == base / "Toon Hit frames");
    CHECK(std::filesystem::exists(w.folder / "Toon Hit_0000.png"));
    CHECK(std::filesystem::exists(w.folder / "Toon Hit_0008.png"));
    CHECK_FALSE(std::filesystem::exists(w.folder / "Toon Hit_0009.png"));
    CHECK(w.sheet == base / "Toon Hit sheet.png");
    CHECK(std::filesystem::file_size(w.sheet) > 100u);

    options.sheet = false;
    options.transparent = false;
    const auto again = exportFrames(effect, options, base, "Plain");
    REQUIRE(again.ok());
    CHECK(again.value().sheet.empty());

    options.width = 2;
    CHECK_FALSE(exportFrames(effect, options, base, "Bad").ok());
    std::filesystem::remove_all(base);
}
