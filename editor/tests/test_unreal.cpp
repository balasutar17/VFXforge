// Exporting an effect for Unreal: a flipbook, its description and the
// import script.
#include <catch2/catch_amalgamated.hpp>

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "vfx/FileIO.h"
#include "vfx/editor/Frames.h"
#include "vfx/editor/Image.h"
#include "vfx/editor/Presets.h"
#include "vfx/editor/UnrealExport.h"

using namespace vfx;
using namespace vfx::editor;

TEST_CASE("an effect for Unreal is a flipbook, its description and the script", "[unreal]") {
    IdGenerator ids(6);
    const IdSource newId = [&ids]() { return ids.next(); };
    const Effect effect = makePreset("cyan-lightning-column", newId).value();

    const auto base = std::filesystem::temp_directory_path() / "vfxforge_unreal_test";
    std::filesystem::remove_all(base);
    UnrealOptions options;
    options.frameSize = 64;
    options.view.centerX = 0;
    options.view.centerY = 0;
    options.view.unitsHigh = 6;
    const auto written = exportToUnreal(effect, options, base);
    REQUIRE(written.ok());
    const UnrealWritten& w = written.value();
    CHECK(w.folder == base / "VFXForge for Unreal");
    CHECK(std::filesystem::exists(w.folder / "VFXForge_Import.py"));
    CHECK(std::filesystem::exists(w.folder / "README.txt"));
    CHECK(w.frames == frameCount(effect, effect.frameRate));
    CHECK(w.columns * w.rows >= w.frames);
    CHECK(w.columns * (w.rows - 1) < w.frames);
    // A tall column gets tall frames: the longer side is the size asked for.
    CHECK(w.frameHeight == 64);
    CHECK(w.frameWidth < w.frameHeight);

    const std::string stem = effect.name;
    const auto json = readFile(w.effect / (stem + ".json"));
    REQUIRE(json.ok());
    const auto d = nlohmann::json::parse(json.value());
    CHECK(d["format"] == "vfxforge.unreal");
    CHECK(d["frames"] == w.frames);
    CHECK(d["frameWidth"] == w.frameWidth);
    CHECK(d["frameHeight"] == w.frameHeight);
    CHECK(d["loop"] == (effect.loop == "loop"));
    CHECK(d["unitsHigh"].get<double>() > 1.0);
    CHECK(d["unitsHigh"].get<double>() < 12.0);

    const auto png = readFile(w.effect / (stem + "_flipbook.png"));
    REQUIRE(png.ok());
    const auto image = decodePng(png.value());
    REQUIRE(image.ok());
    CHECK(image.value().width == w.columns * w.frameWidth);
    CHECK(image.value().height == w.rows * w.frameHeight);
    // Something is drawn, and the corners of the sheet are see-through.
    bool any = false;
    for (std::size_t i = 3; i < image.value().rgba.size(); i += 4) {
        any = any || image.value().rgba[i] > 200;
    }
    CHECK(any);
    CHECK(image.value().rgba[3] == 0);

    const auto script = readFile(w.folder / "VFXForge_Import.py");
    REQUIRE(script.ok());
    CHECK(script.value().find("PaperFlipbook") != std::string::npos);
    CHECK(script.value().find("frameWidth") != std::string::npos);
    std::filesystem::remove_all(base);
}
