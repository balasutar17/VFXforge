// vfxref: Reference to VFX from a terminal.
//
//   vfxref analyze <reference> [options]
//   vfxref build <reference> <out.vfx> [options] [--picture compare.png]
//
// <reference> is a PNG file, or a folder of PNG frames (read in name order)
// together with --fps.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "vfx/FileIO.h"
#include "vfx/Id.h"
#include "vfx/Serialize.h"
#include "vfx/editor/Compare.h"
#include "vfx/editor/Picture.h"
#include "vfx/editor/Reconstruct.h"
#include "vfx/editor/Reference.h"

using namespace vfx;
using namespace vfx::editor;

namespace {

int usage() {
    std::cerr << "Usage:\n"
                 "  vfxref analyze <reference> [options]\n"
                 "      Print what the reference shows.\n"
                 "  vfxref build <reference> <out.vfx> [options]\n"
                 "      Rebuild it as an editable effect.\n"
                 "\n"
                 "<reference> is a PNG, or a folder of PNG frames with --fps.\n"
                 "Options:\n"
                 "  --fps n            frame rate of a folder of frames\n"
                 "  --crop l,t,r,b     the part to look at, each 0 to 1\n"
                 "  --backdrop b       auto, transparent, dark, light or #rrggbb\n"
                 "  --cutoff x         how faint still counts as effect, 0 to 1\n"
                 "  --detail n         analysis grid, 128 / 192 / 256\n"
                 "  --first n --last n frames of a clip to use\n"
                 "  --speed x          play the clip this many times faster\n"
                 "  --mode m           shape, motion, layered or balanced\n"
                 "  --variation v      closest, performance or enhanced\n"
                 "  --target t         mobile, desktop or vr\n"
                 "  --pace p           auto, burst or steady\n"
                 "  --cutouts          let layers draw pieces cut from the reference\n"
                 "  --no-fit           skip the adjusting pass\n"
                 "  --picture out.png  reference | effect | overlay | difference\n"
                 "  --strip out.png    for a clip: both, at eight moments\n";
    return 2;
}

Result<Reference> load(const std::string& path, double fps) {
    Reference reference;
    const std::filesystem::path p = pathFromUtf8(path);
    reference.name = pathToUtf8(p.filename());
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    if (std::filesystem::is_directory(p, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(p, ec)) {
            if (entry.path().extension() == ".png") {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        reference.framesPerSecond = fps > 0 ? fps : 30.0;
    } else {
        files.push_back(p);
    }
    for (const auto& file : files) {
        auto bytes = readFile(file);
        if (!bytes) {
            return bytes.error();
        }
        auto image = decodePng(bytes.value());
        if (!image) {
            return image.error();
        }
        reference.frames.push_back(std::move(image.value()));
    }
    if (Status ok = validateReference(reference); !ok) {
        return ok.error();
    }
    return reference;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() < 2) {
        return usage();
    }
    const std::string command = args[0];
    const std::string input = args[1];
    std::string output, picture, strip;
    ReferenceOptions options;
    ReconstructOptions how;
    double fps = 0;
    bool fit = true;
    std::size_t i = 2;
    if (command == "build") {
        if (args.size() < 3) {
            return usage();
        }
        output = args[2];
        i = 3;
    } else if (command != "analyze") {
        return usage();
    }
    for (; i < args.size(); ++i) {
        const std::string& a = args[i];
        const auto next = [&]() -> std::string {
            return i + 1 < args.size() ? args[++i] : std::string();
        };
        if (a == "--fps") {
            fps = std::atof(next().c_str());
        } else if (a == "--crop") {
            std::sscanf(next().c_str(), "%f,%f,%f,%f", &options.cropLeft, &options.cropTop, &options.cropRight,
                        &options.cropBottom);
        } else if (a == "--backdrop") {
            const std::string b = next();
            if (b == "transparent") options.backdrop = Backdrop::Transparent;
            else if (b == "dark") options.backdrop = Backdrop::Dark;
            else if (b == "light") options.backdrop = Backdrop::Light;
            else if (!b.empty() && b[0] == '#') {
                unsigned rgb = 0;
                std::sscanf(b.c_str() + 1, "%x", &rgb);
                options.backdrop = Backdrop::Colour;
                options.keyR = static_cast<float>((rgb >> 16) & 0xffu) / 255.0f;
                options.keyG = static_cast<float>((rgb >> 8) & 0xffu) / 255.0f;
                options.keyB = static_cast<float>(rgb & 0xffu) / 255.0f;
            }
        } else if (a == "--cutoff") {
            options.cutoff = static_cast<float>(std::atof(next().c_str()));
        } else if (a == "--detail") {
            options.detail = std::atoi(next().c_str());
        } else if (a == "--first") {
            options.firstFrame = std::atoi(next().c_str());
        } else if (a == "--last") {
            options.lastFrame = std::atoi(next().c_str());
        } else if (a == "--speed") {
            options.speed = std::atof(next().c_str());
        } else if (a == "--mode") {
            if (!parseMatchMode(next(), how.mode)) return usage();
        } else if (a == "--variation") {
            if (!parseVariation(next(), how.variation)) return usage();
        } else if (a == "--target") {
            if (!parseTarget(next(), how.target)) return usage();
        } else if (a == "--pace") {
            const std::string p = next();
            how.pace = p == "burst" ? Pace::Burst : (p == "steady" ? Pace::Steady : Pace::Auto);
        } else if (a == "--cutouts") {
            how.cutouts = true;
        } else if (a == "--no-fit") {
            fit = false;
        } else if (a == "--picture") {
            picture = next();
        } else if (a == "--strip") {
            strip = next();
        } else {
            return usage();
        }
    }

    auto loaded = load(input, fps);
    if (!loaded) {
        std::cerr << loaded.error().message << "\n" << loaded.error().detail << "\n";
        return 1;
    }
    const Reference& reference = loaded.value();
    auto analyzed = analyzeReference(reference, options);
    if (!analyzed) {
        std::cerr << analyzed.error().message << "\n" << analyzed.error().detail << "\n";
        return 1;
    }
    const ReferenceAnalysis& analysis = analyzed.value();
    std::printf("Reference: %s, %dx%d, %zu frame(s)\n", reference.name.c_str(), reference.frames[0].width,
                reference.frames[0].height, reference.frames.size());
    for (const Finding& f : analysis.findings) {
        std::printf("  [%s %3d%%] %-28s %s\n", f.basis == Basis::Observed ? "seen   " : "assumed",
                    static_cast<int>(f.confidence * 100.0f + 0.5f), f.label.c_str(), f.value.c_str());
        if (!f.note.empty()) {
            std::printf("                 note: %s\n", f.note.c_str());
        }
    }
    for (const std::string& u : analysis.uncertain) {
        std::printf("  uncertain: %s\n", u.c_str());
    }
    std::printf("  analysis took %.2f s\n", analysis.cost);
    if (command == "analyze") {
        return 0;
    }

    IdGenerator ids(12345);
    const IdSource newId = [&ids]() { return ids.next(); };
    std::vector<Cutout> cutouts;
    if (how.cutouts) {
        cutouts = makeCutouts(reference, options, analysis);
    }
    Reconstruction built = reconstruct(analysis, how, newId, &cutouts);
    const Placement placement = placementOf(built);
    const ImageSet pictures = picturesOf(built);
    // The pictures go next to the effect file, where it will look for them.
    for (const PictureUse& use : built.pictures) {
        const auto target = pathFromUtf8(output).parent_path() / pathFromUtf8(use.path);
        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
        (void)writeFileAtomic(target, use.png);
    }
    std::printf("Effect: %zu layers, about %d particles at its busiest (limit %d), %.2f s\n", built.effect.layers.size(),
                built.particlesAtBusiest, built.budget, built.effect.duration);
    for (const LayerNote& n : built.layers) {
        std::printf("  %-14s %-8s %s\n", n.name.c_str(), n.role.c_str(), n.technique.c_str());
        if (!n.note.empty()) {
            std::printf("                 %s\n", n.note.c_str());
        }
    }
    for (const std::string& n : built.notes) {
        std::printf("  note: %s\n", n.c_str());
    }
    const auto show = [](const char* title, const Similarity& s) {
        std::printf("%s: overall %.0f%% | outline %.0f colour %.0f brightness %.0f pieces %.0f detail %.0f",
                    title, s.overall * 100.0, s.silhouette * 100.0, s.colour * 100.0, s.brightness * 100.0,
                    s.density * 100.0, s.detail * 100.0);
        // A still picture has no motion or timing to measure; say so rather than print a number.
        const auto part = [](const char* name, float v) {
            if (v >= 0.0f) {
                std::printf(" %s %.0f", name, v * 100.0);
            } else {
                std::printf(" %s not measured", name);
            }
        };
        part("motion", s.motion);
        part("timing", s.timing);
        std::printf("\n");
        for (const std::string& d : s.differences) {
            std::printf("    - %s\n", d.c_str());
        }
    };
    if (fit) {
        const FitResult fitted = fitToReference(built.effect, reference, options, analysis, placement, {}, {}, &pictures);
        show("Before adjusting", fitted.before);
        show("After adjusting ", fitted.after);
        std::printf("  %d drawings, %.2f s\n", fitted.drawings, fitted.cost);
        for (const std::string& c : fitted.changes) {
            std::printf("    * %s\n", c.c_str());
        }
    } else {
        show("Similarity", compareToReference(reference, options, analysis, built.effect, placement, {}, {}, &pictures));
    }

    if (Status saved = saveEffect(pathFromUtf8(output), built.effect); !saved) {
        std::cerr << saved.error().message << "\n";
        return 1;
    }

    const Image shown = referencePicture(reference, options, analysis.stillFrame, 384);
    const int w = shown.width, h = shown.height;
    if (!picture.empty()) {
        const int frame = analysis.stillFrame;
        const Picture a = drawReference(reference, options, analysis.still, frame, w, h);
        const Picture b = drawLikeReference(built.effect, placement, analysis.still, w, h,
                                            effectTimeAt(reference, options, placement, frame), &pictures);
        Picture all;
        all.width = w * 4 + 12;
        all.height = h;
        all.rgba.assign(static_cast<std::size_t>(all.width) * static_cast<std::size_t>(all.height) * 4u, 60);
        paste(all, a, 0, 0);
        paste(all, b, w + 4, 0);
        paste(all, overlayPictures(a, b, 0.5f), 2 * (w + 4), 0);
        paste(all, differencePicture(a, b), 3 * (w + 4), 0);
        if (Status ok = writePng(pathFromUtf8(picture), all); !ok) {
            std::cerr << ok.error().message << "\n";
            return 1;
        }
    }
    if (!strip.empty() && reference.moving()) {
        int first = 0, last = 0;
        frameRange(reference, options, first, last);
        const int cells = 8;
        const int cw = w / 2, ch = h / 2;
        Picture all;
        all.width = (cw + 2) * cells;
        all.height = ch * 2 + 2;
        all.rgba.assign(static_cast<std::size_t>(all.width) * static_cast<std::size_t>(all.height) * 4u, 60);
        for (int k = 0; k < cells; ++k) {
            const int frame = first + (last - first) * k / (cells - 1);
            paste(all, drawReference(reference, options, analysis.still, frame, cw, ch), k * (cw + 2), 0);
            paste(all, drawLikeReference(built.effect, placement, analysis.still, cw, ch,
                                         comparedTime(analysis, built.effect,
                                                      effectTimeAt(reference, options, placement, frame)),
                                         &pictures),
                  k * (cw + 2), ch + 2);
        }
        if (Status ok = writePng(pathFromUtf8(strip), all); !ok) {
            std::cerr << ok.error().message << "\n";
            return 1;
        }
    }
    return 0;
}
