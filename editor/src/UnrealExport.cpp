#include "vfx/editor/UnrealExport.h"

#include <algorithm>
#include <cmath>
#include <system_error>

#include <nlohmann/json.hpp>

#include "vfx/FileIO.h"
#include "vfx/Program.h"
#include "vfx/Simulation.h"
#include "vfx/editor/Frames.h"
#include "vfx/editor/Picture.h"
#include "vfx/editor/UnityExport.h"

namespace vfx::editor {

namespace {

constexpr double kPi = 3.14159265358979323846;

}  // namespace

Result<UnrealWritten> exportToUnreal(const Effect& effect, const UnrealOptions& options,
                                     const std::filesystem::path& folder) {
    UnrealWritten out;
    const std::string stem = unityFileStem(effect);
    out.folder = folder / "VFXForge for Unreal";
    out.effect = out.folder / "Effects" / pathFromUtf8(stem);
    std::error_code ec;
    std::filesystem::create_directories(out.effect, ec);
    if (ec) {
        return makeError("The folder for Unreal could not be made.", ec.message());
    }
    for (const UnrealFile& f : unrealHelperSources()) {
        if (Status ok = writeFileAtomic(out.folder / pathFromUtf8(f.path), f.text); !ok) {
            return ok.error();
        }
    }

    // Every frame of one pass, at the effect's own frame rate, packed in a
    // square-ish grid that fits one picture.
    const double fps = effect.frameRate > 0.0 ? effect.frameRate : 30.0;
    out.frames = frameCount(effect, fps);
    View view = options.view;

    const bool loops = effect.loop == "loop";
    const double begin = loops ? effect.duration : 0.0;  // as Frames does: the second time round
    const bool flat = effect.space != "3d";
    Simulation simulation(compileEffect(effect));
    RenderFrame frame;
    SpriteMesh mesh;
    float spanY = view.unitsHigh;  // how much of the world a frame shows, top to bottom
    float spanX = view.unitsHigh;  // and across

    // A flat effect is framed to what it covers over its whole pass, so the
    // frames spend their pixels on the effect rather than on empty space.
    if (flat) {
        View wide = view;
        wide.unitsHigh = view.unitsHigh * 2.0f;  // room for what reaches past the window
        constexpr int kLook = 192;
        wide.width = wide.height = static_cast<float>(kLook);
        int left = kLook, right = -1, top = kLook, bottom = -1;
        for (int k = 0; k < out.frames; k += std::max(1, out.frames / 40)) {
            const double t = begin + static_cast<double>(k) / fps;
            simulation.seek(static_cast<std::int64_t>(std::llround(t / kSimulationStep)) + 1);
            simulation.extract(frame);
            buildSpriteMesh(frame, wide, mesh, options.images);
            const Picture look = drawPictureClear(mesh, kLook, kLook, options.images);
            for (int y = 0; y < kLook; ++y) {
                for (int x = 0; x < kLook; ++x) {
                    if (look.pixel(x, y)[3] > 4) {
                        left = std::min(left, x);
                        right = std::max(right, x);
                        top = std::min(top, y);
                        bottom = std::max(bottom, y);
                    }
                }
            }
        }
        if (right >= left && bottom >= top) {
            const float perPixel = wide.unitsHigh / static_cast<float>(kLook);
            const float midX = (static_cast<float>(left + right + 1) * 0.5f - kLook * 0.5f) * perPixel;
            const float midY = (kLook * 0.5f - static_cast<float>(top + bottom + 1) * 0.5f) * perPixel;
            view.centerX = wide.centerX + midX;
            view.centerY = wide.centerY + midY;
            // A little margin for soft edges.
            spanX = std::max(static_cast<float>(right - left + 1) * perPixel * 1.08f, 0.05f);
            spanY = std::max(static_cast<float>(bottom - top + 1) * perPixel * 1.08f, 0.05f);
        }
    }

    // Frames as wide as the effect is: the longer side gets frameSize pixels.
    const int longSide = std::clamp(options.frameSize, 16, kUnrealSheetSide);
    out.frameWidth = spanX >= spanY ? longSide : std::max(8, static_cast<int>(std::lround(longSide * spanX / spanY)));
    out.frameHeight = spanY >= spanX ? longSide : std::max(8, static_cast<int>(std::lround(longSide * spanY / spanX)));
    // Laid out in a grid that is about square, made smaller if it would not fit.
    out.columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(out.frames) * out.frameHeight /
                                                                     out.frameWidth))));
    out.columns = std::min(out.columns, out.frames);
    out.rows = (out.frames + out.columns - 1) / out.columns;
    const double fit = std::min({1.0, static_cast<double>(kUnrealSheetSide) / (out.columns * out.frameWidth),
                                 static_cast<double>(kUnrealSheetSide) / (out.rows * out.frameHeight)});
    out.frameWidth = std::max(8, static_cast<int>(std::floor(out.frameWidth * fit)));
    out.frameHeight = std::max(8, static_cast<int>(std::floor(out.frameHeight * fit)));
    out.frameSize = std::max(out.frameWidth, out.frameHeight);
    view.width = static_cast<float>(out.frameWidth);
    view.height = static_cast<float>(out.frameHeight);
    view.unitsHigh = spanY;

    Picture sheet;
    sheet.width = out.columns * out.frameWidth;
    sheet.height = out.rows * out.frameHeight;
    sheet.rgba.assign(static_cast<std::size_t>(sheet.width) * static_cast<std::size_t>(sheet.height) * 4u, 0);
    for (int k = 0; k < out.frames; ++k) {
        const double t = begin + static_cast<double>(k) / fps;
        simulation.seek(static_cast<std::int64_t>(std::llround(t / kSimulationStep)) + 1);
        simulation.extract(frame);
        buildSpriteMesh(frame, view, mesh, options.images);
        const Picture picture = drawPictureClear(mesh, out.frameWidth, out.frameHeight, options.images);
        paste(sheet, picture, (k % out.columns) * out.frameWidth, (k / out.columns) * out.frameHeight);
    }
    const std::string sheetName = stem + "_flipbook.png";
    if (Status ok = writePng(out.effect / pathFromUtf8(sheetName), sheet); !ok) {
        return ok.error();
    }

    // How much of the world a frame shows, and where VFX Forge's centre is:
    // for a 3D effect, at the distance of the point the camera looks at.
    const double unitsHigh =
        flat ? spanY : 2.0 * view.distance * std::tan(view.fieldOfView * 0.5 * kPi / 180.0);
    const double centerX = flat ? view.centerX : view.targetX;
    const double centerY = flat ? view.centerY : view.targetY;

    nlohmann::ordered_json d;
    d["format"] = "vfxforge.unreal";
    d["formatVersion"] = 1;
    d["name"] = effect.name;
    d["stem"] = stem;
    d["flipbook"] = sheetName;
    d["frames"] = out.frames;
    d["columns"] = out.columns;
    d["rows"] = out.rows;
    d["frameWidth"] = out.frameWidth;
    d["frameHeight"] = out.frameHeight;
    d["framesPerSecond"] = fps;
    d["duration"] = effect.duration;
    d["loop"] = loops;
    d["space"] = flat ? "2d" : "3d";
    d["unitsHigh"] = unitsHigh;
    d["centerX"] = centerX;
    d["centerY"] = centerY;
    if (Status ok = writeFileAtomic(out.effect / pathFromUtf8(stem + ".json"), d.dump(2) + "\n"); !ok) {
        return ok.error();
    }
    return out;
}

}  // namespace vfx::editor
