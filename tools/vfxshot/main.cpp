// vfxshot: pictures of effects, with no window and no graphics card.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "vfx/FileIO.h"
#include "vfx/Program.h"
#include "vfx/Simulation.h"
#include "vfx/editor/Picture.h"
#include "vfx/editor/Presets.h"
#include "vfx/editor/ShapeLibrary.h"
#include "vfx/editor/Shapes.h"
#include "vfx/editor/SpriteMesh.h"

namespace {

using namespace vfx;
using namespace vfx::editor;

const char* kUsage =
    "Usage:\n"
    "  vfxshot list\n"
    "      Name every preset.\n"
    "  vfxshot preset <id> <out.png> [--time s] [--size WxH]\n"
    "      Draw one preset at one moment.\n"
    "  vfxshot sheet <out.png> [--category name] [--cell WxH]\n"
    "      Draw every preset (or one category) at four moments, one row each.\n"
    "  vfxshot shapes <out.png>\n"
    "      Draw every built-in particle shape, large and small.\n"
    "  vfxshot library <out.png> [--category name] [--cell N] [--columns N]\n"
    "      Draw every shape there is (or one category), formula shapes and\n"
    "      library shapes alike, each large with a row of small ones under it.\n"
    "  vfxshot edits <out.png> [--cell N]\n"
    "      Draw every shape that has settings of its own, one to a row: as\n"
    "      drawn, then at each end of its count, thickness, pointiness, roundness.\n"
    "  vfxshot file <effect.vfx> <out.png> [--time s] [--size WxH] [--view x,y,height]\n"
    "      Draw a saved effect at one moment.\n";

std::string option(std::vector<std::string>& args, const char* name, const char* fallback) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == name) {
            std::string value = args[i + 1];
            args.erase(args.begin() + static_cast<std::ptrdiff_t>(i),
                       args.begin() + static_cast<std::ptrdiff_t>(i) + 2);
            return value;
        }
    }
    return fallback;
}

bool parseSize(const std::string& text, int& width, int& height) {
    return std::sscanf(text.c_str(), "%dx%d", &width, &height) == 2 && width > 0 && height > 0 &&
           width <= 8192 && height <= 8192;
}

Picture draw(const Effect& effect, double time, const View& view) {
    Simulation simulation(compileEffect(effect));
    simulation.seek(static_cast<std::int64_t>(time / kSimulationStep + 0.5));
    RenderFrame frame;
    simulation.extract(frame);
    SpriteMesh mesh;
    buildSpriteMesh(frame, view, mesh);
    return drawPicture(mesh, static_cast<int>(view.width), static_cast<int>(view.height));
}

View viewFor(const PresetInfo& info, int width, int height) {
    View view;
    view.width = static_cast<float>(width);
    view.height = static_cast<float>(height);
    view.centerX = info.viewX;
    view.centerY = info.viewY;
    view.unitsHigh = info.viewHeight;
    return view;
}

Effect build(const PresetInfo& info) {
    IdGenerator ids(2026);
    const IdSource newId = [&ids]() { return ids.next(); };
    return makePreset(info.id, newId).value();
}

int save(const std::string& path, const Picture& picture) {
    const Status saved = writePng(pathFromUtf8(path), picture);
    if (!saved) {
        std::fprintf(stderr, "%s\n", saved.error().message.c_str());
        return 1;
    }
    std::printf("wrote %s (%dx%d)\n", path.c_str(), picture.width, picture.height);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    const std::string command = args[0];
    args.erase(args.begin());

    if (command == "list") {
        for (const PresetInfo& info : presets()) {
            std::printf("%-16s %-16s %s\n", info.id.c_str(), info.category.c_str(), info.name.c_str());
        }
        return 0;
    }

    if (command == "preset" && args.size() >= 2) {
        int width = 640, height = 480;
        const double time = std::atof(option(args, "--time", "-1").c_str());
        if (!parseSize(option(args, "--size", "640x480"), width, height)) {
            std::fputs(kUsage, stderr);
            return 2;
        }
        const PresetInfo* info = findPreset(args[0]);
        if (!info) {
            std::fprintf(stderr, "There is no preset called %s. Try: vfxshot list\n", args[0].c_str());
            return 1;
        }
        return save(args[1], draw(build(*info), time < 0 ? info->previewTime : time,
                                  viewFor(*info, width, height)));
    }

    if (command == "sheet" && !args.empty()) {
        int cellW = 240, cellH = 180;
        const std::string category = option(args, "--category", "");
        if (!parseSize(option(args, "--cell", "240x180"), cellW, cellH)) {
            std::fputs(kUsage, stderr);
            return 2;
        }
        std::vector<const PresetInfo*> rows;
        for (const PresetInfo& info : presets()) {
            if (category.empty() || info.category == category) {
                rows.push_back(&info);
            }
        }
        if (rows.empty()) {
            std::fprintf(stderr, "No presets in that category.\n");
            return 1;
        }
        const int columns = 4, gap = 4;
        Picture sheet;
        sheet.width = columns * (cellW + gap) + gap;
        sheet.height = static_cast<int>(rows.size()) * (cellH + gap) + gap;
        sheet.rgba.assign(static_cast<std::size_t>(sheet.width) * static_cast<std::size_t>(sheet.height) * 4u, 60);
        for (std::size_t r = 0; r < rows.size(); ++r) {
            const PresetInfo& info = *rows[r];
            const Effect effect = build(info);
            // The preview moment, a little before it, and two later ones.
            const double moments[4] = {info.previewTime * 0.4, info.previewTime,
                                       info.previewTime + 0.35 * (effect.duration - info.previewTime),
                                       effect.duration + info.previewTime};
            for (int c = 0; c < columns; ++c) {
                paste(sheet, draw(effect, moments[c], viewFor(info, cellW, cellH)),
                      gap + c * (cellW + gap), gap + static_cast<int>(r) * (cellH + gap));
            }
            std::printf("row %2zu  %-16s times %.2f %.2f %.2f %.2f\n", r + 1, info.id.c_str(),
                        moments[0], moments[1], moments[2], moments[3]);
        }
        return save(args[0], sheet);
    }

    if (command == "shapes" && !args.empty()) {
        // Each shape twice: large on a dark ground, and small, where the
        // edge softening matters most.
        const int cell = 128, gap = 4;
        Picture sheet;
        sheet.width = kSpriteShapeCount * (cell + gap) + gap;
        sheet.height = 2 * (cell + gap) + gap;
        sheet.rgba.assign(static_cast<std::size_t>(sheet.width) * static_cast<std::size_t>(sheet.height) * 4u, 60);
        for (int i = 0; i < kSpriteShapeCount; ++i) {
            for (int row = 0; row < 2; ++row) {
                RenderFrame frame;
                frame.flat = true;
                RenderBatch batch;
                batch.shape = static_cast<SpriteShape>(i);
                batch.first = 0;
                if (row == 0) {
                    SpriteInstance p;
                    p.size = 1.7f;
                    p.r = 1.0f;
                    p.g = 0.55f;
                    p.b = 0.12f;
                    frame.instances.push_back(p);
                } else {
                    for (int n = 0; n < 9; ++n) {
                        SpriteInstance p;
                        p.x = -0.6f + 0.6f * static_cast<float>(n % 3);
                        p.y = 0.6f - 0.6f * static_cast<float>(n / 3);
                        p.size = 0.08f + 0.05f * static_cast<float>(n);
                        p.rotation = 0.35f * static_cast<float>(n);
                        p.r = 0.5f;
                        p.g = 0.8f;
                        p.b = 1.0f;
                        frame.instances.push_back(p);
                    }
                }
                batch.count = static_cast<std::uint32_t>(frame.instances.size());
                frame.batches.push_back(batch);
                View view;
                view.width = view.height = static_cast<float>(cell);
                view.centerX = view.centerY = 0;
                view.unitsHigh = 2.0f;
                SpriteMesh mesh;
                buildSpriteMesh(frame, view, mesh);
                paste(sheet, drawPicture(mesh, cell, cell), gap + i * (cell + gap), gap + row * (cell + gap));
            }
            std::printf("column %2d  %s\n", i + 1, shapeName(static_cast<SpriteShape>(i)));
        }
        return save(args[0], sheet);
    }

    if (command == "library" && !args.empty()) {
        const std::string category = option(args, "--category", "");
        const int cell = std::atoi(option(args, "--cell", "128").c_str());
        const int columns = std::atoi(option(args, "--columns", "10").c_str());
        if (cell < 16 || cell > 1024 || columns < 1 || columns > 64) {
            std::fputs(kUsage, stderr);
            return 2;
        }
        std::vector<int> wanted;
        for (int i = 0; i < shapeCount(); ++i) {
            if (category.empty() || category == shapeInfo(i).category) {
                wanted.push_back(i);
            }
        }
        if (wanted.empty()) {
            std::fprintf(stderr, "No shapes in a category called \"%s\".\n", category.c_str());
            return 1;
        }
        const int gap = 4, strip = cell / 3;
        const int rows = (static_cast<int>(wanted.size()) + columns - 1) / columns;
        Picture sheet;
        sheet.width = columns * (cell + gap) + gap;
        sheet.height = rows * (cell + strip + gap) + gap;
        sheet.rgba.assign(static_cast<std::size_t>(sheet.width) * static_cast<std::size_t>(sheet.height) * 4u, 60);
        for (std::size_t n = 0; n < wanted.size(); ++n) {
            const int left = gap + static_cast<int>(n % static_cast<std::size_t>(columns)) * (cell + gap);
            const int top = gap + static_cast<int>(n / static_cast<std::size_t>(columns)) * (cell + strip + gap);
            // Large, in a warm colour; then three small ones in a cool one,
            // where the softening of edges matters most.
            for (int part = 0; part < 2; ++part) {
                RenderFrame frame;
                frame.flat = true;
                RenderBatch batch;
                batch.shape = static_cast<SpriteShape>(wanted[n]);
                View view;
                view.width = static_cast<float>(cell);
                view.height = static_cast<float>(part == 0 ? cell : strip);
                view.centerX = view.centerY = 0;
                view.unitsHigh = part == 0 ? 2.0f : 2.0f * static_cast<float>(strip) / static_cast<float>(cell);
                if (part == 0) {
                    SpriteInstance p;
                    p.size = 1.8f;
                    p.r = 1.0f;
                    p.g = 0.5f;
                    p.b = 0.1f;
                    frame.instances.push_back(p);
                } else {
                    for (int k = 0; k < 3; ++k) {
                        SpriteInstance p;
                        p.x = -0.62f + 0.62f * static_cast<float>(k);
                        p.size = 0.22f + 0.14f * static_cast<float>(k);
                        p.r = 0.45f;
                        p.g = 0.78f;
                        p.b = 1.0f;
                        frame.instances.push_back(p);
                    }
                }
                batch.count = static_cast<std::uint32_t>(frame.instances.size());
                frame.batches.push_back(batch);
                SpriteMesh mesh;
                buildSpriteMesh(frame, view, mesh);
                paste(sheet, drawPicture(mesh, cell, part == 0 ? cell : strip), left, top + (part == 0 ? 0 : cell));
            }
            std::printf("%3zu  %-12s %-18s %s\n", n + 1, shapeInfo(wanted[n]).name, shapeInfo(wanted[n]).label,
                        shapeInfo(wanted[n]).category);
        }
        return save(args[0], sheet);
    }

    if (command == "edits" && !args.empty()) {
        // Every shape that can be changed, one to a row: as drawn, then each
        // of its settings at one end and the other.
        const int cell = std::atoi(option(args, "--cell", "96").c_str());
        if (cell < 16 || cell > 512) {
            std::fputs(kUsage, stderr);
            return 2;
        }
        std::vector<int> wanted;
        for (int i = 0; i < shapeCount(); ++i) {
            if (shapeEditInfo(i).any()) {
                wanted.push_back(i);
            }
        }
        const int gap = 4, columns = 9;
        Picture sheet;
        sheet.width = columns * (cell + gap) + gap;
        sheet.height = static_cast<int>(wanted.size()) * (cell + gap) + gap;
        sheet.rgba.assign(static_cast<std::size_t>(sheet.width) * static_cast<std::size_t>(sheet.height) * 4u, 60);
        for (std::size_t n = 0; n < wanted.size(); ++n) {
            const ShapeEditInfo& info = shapeEditInfo(wanted[n]);
            for (int column = 0; column < columns; ++column) {
                ShapeEdit edit;
                bool has = column == 0;
                const float end = column % 2 == 1 ? -1.0f : 1.0f;
                if (column == 1 || column == 2) {
                    has = info.count();
                    edit.count = column == 1 ? info.countLowest : info.countHighest;
                } else if (column == 3 || column == 4) {
                    has = info.thickness;
                    edit.thickness = end;
                } else if (column == 5 || column == 6) {
                    has = info.sharpness;
                    edit.sharpness = end;
                } else if (column == 7 || column == 8) {
                    has = info.round && (column == 8 || info.sharperToo);
                    edit.round = end;
                }
                if (!has) {
                    continue;
                }
                RenderFrame frame;
                frame.flat = true;
                RenderBatch batch;
                batch.shape = static_cast<SpriteShape>(wanted[n]);
                batch.shapeEdit = edit;
                SpriteInstance p;
                p.size = 1.8f;
                p.r = column == 0 ? 1.0f : 0.45f;
                p.g = column == 0 ? 0.5f : 0.78f;
                p.b = column == 0 ? 0.1f : 1.0f;
                frame.instances.push_back(p);
                batch.count = 1;
                frame.batches.push_back(batch);
                View view;
                view.width = view.height = static_cast<float>(cell);
                view.centerX = view.centerY = 0;
                view.unitsHigh = 2.0f;
                SpriteMesh mesh;
                buildSpriteMesh(frame, view, mesh);
                paste(sheet, drawPicture(mesh, cell, cell), gap + column * (cell + gap),
                      gap + static_cast<int>(n) * (cell + gap));
            }
            std::printf("%-12s %s%s%s%s\n", shapeInfo(wanted[n]).name,
                        info.count() ? (std::string(info.countLabel) + " ").c_str() : "",
                        info.thickness ? "Thickness " : "", info.sharpness ? "Pointiness " : "",
                        info.round ? "Roundness" : "");
        }
        return save(args[0], sheet);
    }

    if (command == "file" && args.size() >= 2) {
        int width = 640, height = 480;
        const double time = std::atof(option(args, "--time", "0.5").c_str());
        View view;
        float x = 0, y = 1.5f, units = 7;
        const std::string placed = option(args, "--view", "0,1.5,7");
        if (!parseSize(option(args, "--size", "640x480"), width, height) ||
            std::sscanf(placed.c_str(), "%f,%f,%f", &x, &y, &units) != 3) {
            std::fputs(kUsage, stderr);
            return 2;
        }
        auto loaded = loadEffect(pathFromUtf8(args[0]));
        if (!loaded) {
            std::fprintf(stderr, "%s\n", loaded.error().message.c_str());
            return 1;
        }
        view.width = static_cast<float>(width);
        view.height = static_cast<float>(height);
        view.centerX = x;
        view.centerY = y;
        view.unitsHigh = units;
        return save(args[1], draw(loaded.value().effect, time, view));
    }

    std::fputs(kUsage, stderr);
    return 2;
}
