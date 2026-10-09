#include "vfx/editor/Frames.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "vfx/FileIO.h"
#include "vfx/Program.h"
#include "vfx/Simulation.h"
#include "vfx/editor/Picture.h"

namespace vfx::editor {

namespace {

constexpr int kMaxFrames = 600;
constexpr int kMaxSheetSide = 8192;

}  // namespace

int frameCount(const Effect& effect, double framesPerSecond) {
    if (!(framesPerSecond > 0.0) || !(effect.duration > 0.0)) {
        return 1;
    }
    const auto n = static_cast<int>(std::lround(effect.duration * framesPerSecond));
    return std::clamp(n, 1, kMaxFrames);
}

Result<FramesWritten> exportFrames(const Effect& effect, const FrameOptions& options,
                                   const std::filesystem::path& folder, const std::string& stem) {
    if (options.width < 8 || options.height < 8 || options.width > 4096 || options.height > 4096) {
        return makeError("Frames must be between 8 and 4096 pixels on each side.");
    }
    if (!(options.framesPerSecond >= 1.0 && options.framesPerSecond <= 240.0)) {
        return makeError("The frame rate must be between 1 and 240 frames per second.");
    }

    FramesWritten out;
    out.frames = frameCount(effect, options.framesPerSecond);
    out.folder = folder / pathFromUtf8(stem + " frames");
    std::error_code ec;
    std::filesystem::create_directories(out.folder, ec);
    if (ec) {
        return makeError("The folder for the frames could not be made.", ec.message());
    }

    View view = options.view;
    view.width = static_cast<float>(options.width);
    view.height = static_cast<float>(options.height);

    // A looping effect is recorded on its second time round, when particles
    // from the first pass are already in the air, so the last frame leads
    // straight back into the first.
    const bool loops = effect.loop == "loop";
    const double begin = loops ? effect.duration : 0.0;

    Simulation simulation(compileEffect(effect));
    RenderFrame frame;
    SpriteMesh mesh;

    out.columns = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(out.frames))));
    out.rows = (out.frames + out.columns - 1) / out.columns;
    const bool sheet = options.sheet && out.columns * options.width <= kMaxSheetSide &&
                       out.rows * options.height <= kMaxSheetSide;
    Picture all;
    if (sheet) {
        all.width = out.columns * options.width;
        all.height = out.rows * options.height;
        all.rgba.assign(static_cast<std::size_t>(all.width) * static_cast<std::size_t>(all.height) * 4u,
                        options.transparent ? 0 : 0);
        if (!options.transparent) {
            for (std::size_t i = 3; i < all.rgba.size(); i += 4) {
                all.rgba[i] = 255;
            }
        }
    }

    for (int k = 0; k < out.frames; ++k) {
        // One simulation step in, so a burst at the very start is already
        // on screen in the first frame.
        const double t = begin + static_cast<double>(k) / options.framesPerSecond;
        simulation.seek(static_cast<std::int64_t>(std::llround(t / kSimulationStep)) + 1);
        simulation.extract(frame);
        buildSpriteMesh(frame, view, mesh);
        const Picture picture = options.transparent
                                    ? drawPictureClear(mesh, options.width, options.height)
                                    : drawPicture(mesh, options.width, options.height, ScreenColor{0, 0, 0});
        char name[32];
        std::snprintf(name, sizeof name, "_%04d.png", k);
        if (Status ok = writePng(out.folder / pathFromUtf8(stem + name), picture); !ok) {
            return ok.error();
        }
        if (sheet) {
            paste(all, picture, (k % out.columns) * options.width, (k / out.columns) * options.height);
        }
    }

    if (sheet) {
        out.sheet = folder / pathFromUtf8(stem + " sheet.png");
        if (Status ok = writePng(out.sheet, all); !ok) {
            return ok.error();
        }
    }
    return out;
}

}  // namespace vfx::editor
