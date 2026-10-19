// vfxexport: the app's exports, from a terminal.
//
//   vfxexport unity <effect.vfx | preset-id>... --project <Unity project folder>
//   vfxexport unity <effect.vfx | preset-id>... --package <out.unitypackage>
//
// A file is read from disk; anything else is taken as a preset id
// (vfxshot list names them).
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "vfx/FileIO.h"
#include "vfx/Id.h"
#include "vfx/Serialize.h"
#include "vfx/Program.h"
#include "vfx/Simulation.h"
#include "vfx/editor/Archive.h"
#include "vfx/editor/Picture.h"
#include "vfx/editor/SpriteMesh.h"
#include "vfx/editor/Presets.h"
#include "vfx/editor/UnrealExport.h"
#include "vfx/editor/UnityExport.h"

using namespace vfx;
using namespace vfx::editor;

namespace {

int usage() {
    std::cerr << "Usage:\n"
                 "  vfxexport unity <effect.vfx | preset-id>... --project <Unity project folder>\n"
                 "    [--model dummy|<file.obj|glb|gltf> [--model-at x z height turn]]\n"
                 "  vfxexport unity <effect.vfx | preset-id>... --package <out.unitypackage>\n"
                 "  vfxexport unreal <effect.vfx | preset-id>... --out <folder> [--size n]\n"
                 "  vfxexport plan <effect.vfx | preset-id>... --out <folder> [--size n] [--times a,b,c]\n"
                 "      For checking an export in an engine: writes plan.json (how to frame each\n"
                 "      effect and when to look) and VFX Forge's own picture of each moment.\n"
                 "      A file is framed with --view x,y,height (2D) given before it.\n";
    return 2;
}

struct Loaded {
    Effect effect;
    std::filesystem::path folder;  // where its pictures are kept
    float viewX = 0, viewY = 0, viewHeight = 6;
};

Result<Loaded> load(const std::string& what) {
    const std::filesystem::path path = pathFromUtf8(what);
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
        auto loaded = loadEffect(path);
        if (!loaded) {
            return loaded.error();
        }
        return Loaded{std::move(loaded.value().effect), path.parent_path()};
    }
    IdGenerator ids(4242);
    const IdSource newId = [&ids]() { return ids.next(); };
    auto preset = makePreset(what, newId);
    if (!preset) {
        return preset.error();
    }
    Loaded out{std::move(preset.value()), {}};
    if (const PresetInfo* info = findPreset(what)) {
        out.viewX = info->viewX;
        out.viewY = info->viewY;
        out.viewHeight = info->viewHeight;
    }
    return out;
}

}  // namespace

std::string number(double v) {
    char text[32];
    std::snprintf(text, sizeof text, "%.6g", v);
    return text;
}

// plan.json and VFX Forge's pictures, for comparing with an engine.
int plan(const std::vector<std::string>& args) {
    std::string out;
    int size = 400;
    std::vector<double> times{0.25, 0.6, 1.0};
    float vx = 0, vy = 0, vh = 0;
    std::string json = "{\n  \"size\": ";
    std::vector<std::string> rows;
    std::vector<std::pair<std::string, Loaded>> effects;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--out" && i + 1 < args.size()) {
            out = args[++i];
        } else if (args[i] == "--size" && i + 1 < args.size()) {
            size = std::atoi(args[++i].c_str());
        } else if (args[i] == "--times" && i + 1 < args.size()) {
            times.clear();
            std::string list = args[++i];
            for (std::size_t at = 0; at < list.size();) {
                const std::size_t comma = list.find(',', at);
                times.push_back(std::atof(list.substr(at, comma - at).c_str()));
                at = comma == std::string::npos ? list.size() : comma + 1;
            }
        } else if (args[i] == "--view" && i + 1 < args.size()) {
            std::sscanf(args[++i].c_str(), "%f,%f,%f", &vx, &vy, &vh);
        } else {
            auto loaded = load(args[i]);
            if (!loaded) {
                std::cerr << args[i] << ": " << loaded.error().message << "\n";
                return 1;
            }
            if (vh > 0) {
                loaded.value().viewX = vx;
                loaded.value().viewY = vy;
                loaded.value().viewHeight = vh;
                vh = 0;
            }
            effects.emplace_back(args[i], std::move(loaded.value()));
        }
    }
    if (out.empty() || effects.empty()) {
        return usage();
    }
    std::filesystem::create_directories(pathFromUtf8(out));
    json += std::to_string(size) + ",\n  \"effects\": [\n";
    for (std::size_t k = 0; k < effects.size(); ++k) {
        const Loaded& e = effects[k].second;
        const std::string stem = unityFileStem(e.effect);
        const bool flat = e.effect.space != "3d";
        View view;
        view.width = view.height = static_cast<float>(size);
        view.centerX = e.viewX;
        view.centerY = e.viewY;
        view.unitsHigh = e.viewHeight;
        view.targetX = e.viewX;
        view.targetY = e.viewY;
        view.distance = e.viewHeight * 1.45f;
        view.yaw = 30;
        view.pitch = 15;
        json += std::string(k ? ",\n" : "") + "    {\"name\": \"" + stem + "\", \"flat\": " + (flat ? "true" : "false") +
                ", \"x\": " + number(view.centerX) + ", \"y\": " + number(view.centerY) + ", \"height\": " +
                number(view.unitsHigh) + ", \"yaw\": 30, \"pitch\": 15, \"distance\": " + number(view.distance) +
                ", \"fov\": " + number(view.fieldOfView) + ", \"times\": [";
        for (std::size_t t = 0; t < times.size(); ++t) {
            const double at = times[t];
            json += (t ? ", " : "") + number(at);
            Simulation simulation(compileEffect(e.effect));
            simulation.seek(static_cast<std::int64_t>(at / kSimulationStep + 0.5));
            RenderFrame frame;
            simulation.extract(frame);
            SpriteMesh mesh;
            buildSpriteMesh(frame, view, mesh);
            const Picture picture = drawPicture(mesh, size, size);
            char name[64];
            std::snprintf(name, sizeof name, "_%.2f_vfxforge.png", at);
            if (Status ok = writePng(pathFromUtf8(out) / pathFromUtf8(stem + name), picture); !ok) {
                std::cerr << ok.error().message << "\n";
                return 1;
            }
        }
        json += "]}";
    }
    json += "\n  ]\n}\n";
    if (Status ok = writeFileAtomic(pathFromUtf8(out) / "plan.json", json); !ok) {
        std::cerr << ok.error().message << "\n";
        return 1;
    }
    std::cout << "wrote " << out << "/plan.json and " << effects.size() * times.size() << " pictures\n";
    return 0;
}

// The effects as flipbooks for Unreal, framed as their presets frame them.
int unrealExport(const std::vector<std::string>& args) {
    std::string out;
    int size = 512;
    std::vector<std::string> inputs;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--out" && i + 1 < args.size()) {
            out = args[++i];
        } else if (args[i] == "--size" && i + 1 < args.size()) {
            size = std::atoi(args[++i].c_str());
        } else {
            inputs.push_back(args[i]);
        }
    }
    if (out.empty() || inputs.empty()) {
        return usage();
    }
    for (const std::string& input : inputs) {
        auto loaded = load(input);
        if (!loaded) {
            std::cerr << input << ": " << loaded.error().message << "\n";
            return 1;
        }
        const Loaded& e = loaded.value();
        UnrealOptions options;
        options.frameSize = size;
        options.view.centerX = options.view.targetX = e.viewX;
        options.view.centerY = options.view.targetY = e.viewY;
        options.view.unitsHigh = e.viewHeight;
        options.view.distance = e.viewHeight * 1.45f;
        options.view.yaw = 0;
        options.view.pitch = 15;
        auto written = exportToUnreal(e.effect, options, pathFromUtf8(out));
        if (!written) {
            std::cerr << input << ": " << written.error().message << "\n";
            return 1;
        }
        std::cout << "wrote " << pathToUtf8(written.value().effect) << " (" << written.value().frames << " frames of "
                  << written.value().frameSize << " px)\n";
    }
    return 0;
}

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (!args.empty() && args[0] == "plan") {
        return plan(args);
    }
    if (!args.empty() && args[0] == "unreal") {
        return unrealExport(args);
    }
    if (args.size() < 3 || args[0] != "unity") {
        return usage();
    }
    std::string project, package, modelArg;
    float modelX = 0, modelZ = 0, modelHeight = 3, modelTurn = 0;
    std::vector<std::string> inputs;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--model" && i + 1 < args.size()) {
            modelArg = args[++i];  // "dummy", or an .obj / .glb / .gltf file
        } else if (args[i] == "--model-at" && i + 4 < args.size()) {
            modelX = std::strtof(args[++i].c_str(), nullptr);
            modelZ = std::strtof(args[++i].c_str(), nullptr);
            modelHeight = std::strtof(args[++i].c_str(), nullptr);
            modelTurn = std::strtof(args[++i].c_str(), nullptr);
        } else if (args[i] == "--project" && i + 1 < args.size()) {
            project = args[++i];
        } else if (args[i] == "--package" && i + 1 < args.size()) {
            package = args[++i];
        } else {
            inputs.push_back(args[i]);
        }
    }
    if (inputs.empty() || (project.empty() == package.empty())) {
        return usage();
    }
    Model model;
    UnityModel carried;
    if (!modelArg.empty()) {
        if (modelArg == "dummy") {
            model = practiceDummy();
        } else {
            auto read = loadModel(pathFromUtf8(modelArg));
            if (!read) {
                std::cerr << modelArg << ": " << read.error().message << "\n";
                return 1;
            }
            model = std::move(read.value());
        }
        carried.model = &model;
        carried.place.x = modelX;
        carried.place.z = modelZ;
        carried.place.height = modelHeight;
        carried.place.turn = modelTurn;
    }
    const UnityModel* withModel = modelArg.empty() ? nullptr : &carried;
    std::vector<TarEntry> all;
    for (const std::string& input : inputs) {
        auto loaded = load(input);
        if (!loaded) {
            std::cerr << input << ": " << loaded.error().message << "\n";
            return 1;
        }
        if (!project.empty()) {
            std::filesystem::path written;
            if (Status ok = exportToUnityProject(loaded.value().effect, pathFromUtf8(project), &written,
                                                 loaded.value().folder, withModel);
                !ok) {
                std::cerr << input << ": " << ok.error().message << "\n" << ok.error().detail << "\n";
                return 1;
            }
            std::cout << "wrote " << pathToUtf8(written) << "\n";
        } else {
            for (TarEntry& e : unityExportFiles(loaded.value().effect, loaded.value().folder, withModel)) {
                bool seen = false;
                for (const TarEntry& have : all) {
                    seen = seen || have.path == e.path;
                }
                if (!seen) {
                    all.push_back(std::move(e));
                }
            }
        }
    }
    if (!package.empty()) {
        if (Status ok = writeFileAtomic(pathFromUtf8(package), makeUnityPackage(all)); !ok) {
            std::cerr << ok.error().message << "\n";
            return 1;
        }
        std::cout << "wrote " << package << " (" << all.size() << " files)\n";
    }
    return 0;
}
