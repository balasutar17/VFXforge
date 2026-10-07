// vfxcli: the VFX Forge core without a window.
//
// Everything here goes through the same core library the editor uses, so the
// tool doubles as a check that the core stands alone: no UI, no GPU, no Qt.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "vfx/Command.h"
#include "vfx/CommandStack.h"
#include "vfx/Document.h"
#include "vfx/FileIO.h"
#include "vfx/Program.h"
#include "vfx/Serialize.h"
#include "vfx/Simulation.h"
#include "vfx/Templates.h"

namespace {

using Args = std::vector<std::string>;

constexpr const char* kUsage =
    "vfxcli - VFX Forge command-line tool (milestone 1.2)\n"
    "\n"
    "Usage:\n"
    "  vfxcli new <file.vfx> [--name <name>] [--3d] [--empty] [--force]\n"
    "      Create an effect with one basic emitter layer.\n"
    "  vfxcli validate <file.vfx>... [--strict]\n"
    "      Check that files load. --strict also fails on any note.\n"
    "  vfxcli format <file.vfx> [--check] [--accept-repairs]\n"
    "      Rewrite a file in canonical form. --check only reports.\n"
    "  vfxcli info <file.vfx>\n"
    "      Show the layers and modules in an effect.\n"
    "  vfxcli list <file.vfx>\n"
    "      Show every property path and its value.\n"
    "  vfxcli get <file.vfx> <path>\n"
    "      Print one value, for example: effect/duration\n"
    "  vfxcli set <file.vfx> <path> <json value>\n"
    "      Change one value and save, for example: effect/duration 3.5\n"
    "  vfxcli schema\n"
    "      Print every module type and property as JSON.\n"
    "  vfxcli simulate <file.vfx> [--time <seconds>] [--every <seconds>]\n"
    "      Run the effect with no window and print particle counts and a\n"
    "      fingerprint of the result. The same file gives the same fingerprints\n"
    "      on every machine.\n"
    "  vfxcli benchmark <file.vfx> [--seconds <s>]\n"
    "  vfxcli benchmark --particles <count> [--3d] [--seconds <s>]\n"
    "      Measure how long each simulation step takes.\n";

void printError(const vfx::Error& error) {
    std::fflush(stdout);  // keep errors in order with normal output
    std::fprintf(stderr, "error: %s\n", error.message.c_str());
    if (!error.detail.empty()) {
        std::fprintf(stderr, "       (%s)\n", error.detail.c_str());
    }
}

bool takeFlag(Args& args, std::string_view flag) {
    for (auto it = args.begin(); it != args.end(); ++it) {
        if (*it == flag) {
            args.erase(it);
            return true;
        }
    }
    return false;
}

bool takeOption(Args& args, std::string_view name, std::string& value) {
    for (auto it = args.begin(); it != args.end(); ++it) {
        if (*it == name && it + 1 != args.end()) {
            value = *(it + 1);
            args.erase(it, it + 2);
            return true;
        }
    }
    return false;
}

void printNotes(const vfx::LoadedEffect& loaded) {
    for (const auto& d : loaded.diagnostics) {
        std::printf("  note: %s: %s\n", d.where.c_str(), d.message.c_str());
    }
}

// Loads a file that is about to be modified and saved. Refuses when saving
// would lose something the artist has not agreed to lose.
bool loadForEditing(const std::filesystem::path& path, bool acceptRepairs,
                    vfx::LoadedEffect& out) {
    auto loaded = vfx::loadEffect(path);
    if (!loaded) {
        printError(loaded.error());
        return false;
    }
    if (loaded.value().readOnly) {
        printNotes(loaded.value());
        std::fprintf(stderr, "error: this file is read-only here and was not changed.\n");
        return false;
    }
    if (loaded.value().repaired && !acceptRepairs) {
        printNotes(loaded.value());
        std::fprintf(stderr,
                     "error: some values in this file were invalid. Saving would replace them.\n"
                     "       Run 'vfxcli format <file> --accept-repairs' to accept the "
                     "corrections above.\n");
        return false;
    }
    out = std::move(loaded.value());
    return true;
}

int cmdNew(Args args) {
    std::string name = "Untitled";
    takeOption(args, "--name", name);
    const bool threeD = takeFlag(args, "--3d");
    const bool empty = takeFlag(args, "--empty");
    const bool force = takeFlag(args, "--force");
    if (args.size() != 1) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    const auto path = vfx::pathFromUtf8(args[0]);
    std::error_code ec;
    if (!force && std::filesystem::exists(path, ec)) {
        std::fprintf(stderr, "error: \"%s\" already exists. Use --force to replace it.\n",
                     args[0].c_str());
        return 1;
    }

    vfx::IdGenerator ids = vfx::IdGenerator::fromEntropy();
    const vfx::IdSource newId = [&ids]() { return ids.next(); };
    vfx::Document document(vfx::makeEmptyEffect(newId, "Untitled", threeD), ids);
    vfx::CommandStack stack(document);

    // Built through commands, exactly as the editor would do it.
    const vfx::IdSource docId = [&document]() { return document.newId(); };
    if (auto s = stack.push(std::make_unique<vfx::SetPropertyCommand>(
            vfx::Path::effect("name"), vfx::Value(name)));
        !s) {
        printError(s.error());
        return 1;
    }
    if (!empty) {
        if (auto s = stack.push(std::make_unique<vfx::AddLayerCommand>(
                vfx::makeBasicEmitter(docId, "Emitter")));
            !s) {
            printError(s.error());
            return 1;
        }
    }
    if (auto s = vfx::saveEffect(path, document.effect()); !s) {
        printError(s.error());
        return 1;
    }
    std::printf("Created %s\n", args[0].c_str());
    return 0;
}

int cmdValidate(Args args) {
    const bool strict = takeFlag(args, "--strict");
    if (args.empty()) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    int failures = 0;
    for (const auto& arg : args) {
        const auto path = vfx::pathFromUtf8(arg);
        auto bytes = vfx::readFile(path);
        if (!bytes) {
            std::printf("FAIL  %s\n", arg.c_str());
            printError(bytes.error());
            ++failures;
            continue;
        }
        auto loaded = vfx::readEffect(bytes.value());
        if (!loaded) {
            std::printf("FAIL  %s\n", arg.c_str());
            printError(loaded.error());
            ++failures;
            continue;
        }
        const auto& l = loaded.value();
        std::printf("OK    %s  (%zu layer%s, format version %d)\n", arg.c_str(),
                    l.effect.layers.size(), l.effect.layers.size() == 1 ? "" : "s",
                    l.fileVersion);
        printNotes(l);
        const bool canonical = !l.readOnly && vfx::writeEffect(l.effect) == bytes.value();
        if (!l.readOnly && !canonical) {
            std::printf("  note: not in canonical form; 'vfxcli format' would rewrite it\n");
        }
        if (strict && (!l.diagnostics.empty() || !canonical)) {
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}

int cmdFormat(Args args) {
    const bool check = takeFlag(args, "--check");
    const bool acceptRepairs = takeFlag(args, "--accept-repairs");
    if (args.size() != 1) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    const auto path = vfx::pathFromUtf8(args[0]);
    auto bytes = vfx::readFile(path);
    if (!bytes) {
        printError(bytes.error());
        return 1;
    }
    vfx::LoadedEffect loaded;
    if (!loadForEditing(path, acceptRepairs || check, loaded)) {
        return 1;
    }
    const std::string canonical = vfx::writeEffect(loaded.effect);
    if (canonical == bytes.value()) {
        std::printf("%s is already canonical\n", args[0].c_str());
        return 0;
    }
    if (check) {
        std::printf("%s is not canonical\n", args[0].c_str());
        return 1;
    }
    printNotes(loaded);
    if (auto s = vfx::writeFileAtomic(path, canonical); !s) {
        printError(s.error());
        return 1;
    }
    std::printf("Rewrote %s\n", args[0].c_str());
    return 0;
}

int cmdInfo(Args args) {
    if (args.size() != 1) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    auto loaded = vfx::loadEffect(vfx::pathFromUtf8(args[0]));
    if (!loaded) {
        printError(loaded.error());
        return 1;
    }
    const auto& e = loaded.value().effect;
    std::printf("%s  (%s, %g s, %s, seed %lld, %g fps)\n", e.name.c_str(), e.space.c_str(),
                e.duration, e.loop.c_str(), static_cast<long long>(e.seed), e.frameRate);
    std::printf("  id %s, %zu asset%s\n", vfx::formatId('e', e.id).c_str(), e.assets.size(),
                e.assets.size() == 1 ? "" : "s");
    for (const auto& l : e.layers) {
        std::printf("  layer \"%s\"  %s  start %g s, lasts %g s%s\n", l.name.c_str(),
                    vfx::formatId('l', l.id).c_str(), l.start, l.duration,
                    l.enabled ? "" : "  [disabled]");
        for (const auto& m : l.modules) {
            std::printf("    %-14s %s%s\n", m.desc ? m.desc->label.c_str() : m.type.c_str(),
                        vfx::formatId('m', m.id).c_str(), m.desc ? "" : "  [unsupported]");
        }
    }
    printNotes(loaded.value());
    return 0;
}

int cmdList(Args args) {
    if (args.size() != 1) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    auto loaded = vfx::loadEffect(vfx::pathFromUtf8(args[0]));
    if (!loaded) {
        printError(loaded.error());
        return 1;
    }
    vfx::Document document(std::move(loaded.value().effect));
    const auto& registry = vfx::Registry::builtin();
    auto show = [&document](const vfx::Path& path) {
        if (auto value = document.get(path)) {
            std::printf("%s = %s\n", path.str().c_str(), vfx::valueToText(value.value()).c_str());
        }
    };
    for (const auto& p : registry.effectFields().properties) {
        show(vfx::Path::effect(p.key));
    }
    for (const auto& l : document.effect().layers) {
        for (const auto& p : registry.layerFields().properties) {
            show(vfx::Path::layerField(l.id, p.key));
        }
        for (const auto& m : l.modules) {
            if (!m.desc) {
                continue;
            }
            for (const auto& p : m.desc->properties) {
                show(vfx::Path::property(l.id, m.id, p.key));
            }
        }
    }
    return 0;
}

int cmdGet(Args args) {
    if (args.size() != 2) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    auto loaded = vfx::loadEffect(vfx::pathFromUtf8(args[0]));
    if (!loaded) {
        printError(loaded.error());
        return 1;
    }
    auto path = vfx::Path::parse(args[1]);
    if (!path) {
        printError(path.error());
        return 1;
    }
    vfx::Document document(std::move(loaded.value().effect));
    auto value = document.get(path.value());
    if (!value) {
        printError(value.error());
        return 1;
    }
    std::printf("%s\n", vfx::valueToText(value.value()).c_str());
    return 0;
}

int cmdSet(Args args) {
    if (args.size() != 3) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    const auto file = vfx::pathFromUtf8(args[0]);
    vfx::LoadedEffect loaded;
    if (!loadForEditing(file, false, loaded)) {
        return 1;
    }
    auto path = vfx::Path::parse(args[1]);
    if (!path) {
        printError(path.error());
        return 1;
    }
    vfx::Document document(std::move(loaded.effect));
    const vfx::PropertyDesc* desc = document.describe(path.value());
    if (!desc) {
        printError(vfx::makeError("There is no property at \"" + args[1] + "\"."));
        return 1;
    }
    auto value = vfx::valueFromText(*desc, args[2]);
    if (!value) {
        printError(value.error());
        return 1;
    }
    vfx::CommandStack stack(document);
    if (auto s = stack.push(
            std::make_unique<vfx::SetPropertyCommand>(path.value(), std::move(value.value())));
        !s) {
        printError(s.error());
        return 1;
    }
    if (!stack.isDirty()) {
        std::printf("%s already has that value\n", desc->label.c_str());
        return 0;
    }
    if (auto s = vfx::saveEffect(file, document.effect()); !s) {
        printError(s.error());
        return 1;
    }
    std::printf("%s set to %s\n", desc->label.c_str(), args[2].c_str());
    return 0;
}

bool parseNumber(const std::string& text, double& out) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size() || !(v == v) || v > 1e300 || v < -1e300) {
        return false;
    }
    out = v;
    return true;
}

// Takes "--name <number>". Returns false, after printing why, when the value
// is not a number in range.
bool takeNumber(Args& args, const char* name, double low, double high, double& value) {
    std::string text;
    if (!takeOption(args, name, text)) {
        return true;
    }
    double v = 0;
    if (!parseNumber(text, v) || v < low || v > high) {
        std::fprintf(stderr, "error: %s needs a number from %g to %g.\n", name, low, high);
        return false;
    }
    value = v;
    return true;
}

std::string layerName(const vfx::Effect& effect, vfx::Id id) {
    const vfx::Layer* layer = vfx::findLayer(effect, id);
    return layer ? layer->name : vfx::formatId('l', id);
}

void printLayerStats(const vfx::Effect& effect, const vfx::Simulation& sim) {
    const auto stats = sim.stats();
    for (std::size_t i = 0; i < stats.size(); ++i) {
        const auto& st = stats[i];
        const auto& program = *sim.program().emitters[i];
        std::printf("  %-20s %7u alive, room for %u", layerName(effect, st.layer).c_str(), st.alive,
                    st.capacity);
        if (st.dropped > 0) {
            std::printf(", %llu not created (layer full)",
                        static_cast<unsigned long long>(st.dropped));
        }
        std::printf("\n");
        if (program.capped()) {
            std::printf("    note: this layer asks for up to %llu particles at once; the limit "
                        "per layer is %u\n",
                        static_cast<unsigned long long>(program.wanted),
                        vfx::kMaxParticlesPerEmitter);
        }
    }
}

int cmdSimulate(Args args) {
    double until = -1.0, every = 0.0;
    if (!takeNumber(args, "--time", 0.0, 86400.0, until) ||
        !takeNumber(args, "--every", 0.001, 86400.0, every)) {
        return 2;
    }
    if (args.size() != 1) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    auto loaded = vfx::loadEffect(vfx::pathFromUtf8(args[0]));
    if (!loaded) {
        printError(loaded.error());
        return 1;
    }
    const vfx::Effect& effect = loaded.value().effect;
    if (until < 0.0) {
        until = effect.duration;
    }

    vfx::Simulation sim(vfx::compileEffect(effect));
    const double step = sim.program().step;
    const auto lastStep = static_cast<std::int64_t>(until / step + 1e-6);
    std::printf("%s: %s, %g s, %s, seed %lld, %zu layer%s simulated, step 1/%g s\n",
                effect.name.c_str(), effect.space.c_str(), effect.duration, effect.loop.c_str(),
                static_cast<long long>(effect.seed), sim.emitterCount(),
                sim.emitterCount() == 1 ? "" : "s", 1.0 / step);

    vfx::RenderFrame frame;
    auto report = [&]() {
        sim.extract(frame);
        std::printf("time %9.4f  step %7lld  particles %7u  state %016llx  frame %016llx\n",
                    sim.time(), static_cast<long long>(sim.step()), sim.aliveCount(),
                    static_cast<unsigned long long>(sim.stateHash()),
                    static_cast<unsigned long long>(vfx::hashFrame(frame)));
    };

    if (every > 0.0) {
        auto stride = static_cast<std::int64_t>(every / step + 0.5);
        if (stride < 1) {
            stride = 1;
        }
        for (std::int64_t s = 0; s < lastStep; s += stride) {
            sim.seek(s);
            report();
        }
    }
    sim.seek(lastStep);
    report();
    printLayerStats(effect, sim);
    return 0;
}

// A stand-in effect that keeps roughly the requested number of particles
// alive, using every Phase 1 feature that costs time per particle.
vfx::Effect makeBenchmarkEffect(double particles, bool threeD) {
    vfx::IdGenerator ids(20261003);
    const vfx::IdSource newId = [&ids]() { return ids.next(); };
    vfx::Effect effect = vfx::makeEmptyEffect(newId, "Benchmark", threeD);
    effect.duration = 2.0;
    effect.loop = "loop";

    vfx::Layer layer = vfx::makeBasicEmitter(newId, "Benchmark");
    for (auto& m : layer.modules) {
        if (m.type == "emission") {
            *m.find("rate") = vfx::Scalar::constant(particles / 2.0);
        } else if (m.type == "shape") {
            *m.find("shape") = std::string("sphere");
        } else if (m.type == "initial") {
            *m.find("lifetime") = vfx::Scalar::constant(2.0);
            *m.find("speed") = vfx::Scalar::random(1.0, 3.0);
            *m.find("spread") = 180.0;
            *m.find("rotation") = vfx::Scalar::random(0.0, 360.0);
        } else if (m.type == "motion") {
            *m.find("drag") = 0.5;
            *m.find("spin") = vfx::Scalar::random(-90.0, 90.0);
        } else if (m.type == "overLife") {
            *m.find("size") = vfx::Scalar::curve({{0.0, 1.0}, {1.0, 0.4}});
            *m.find("color") = vfx::Gradient{{{0.0, vfx::Color{1, 1, 1, 1}},
                                              {1.0, vfx::Color{1, 0.5, 0.1, 1}}}};
        }
    }
    effect.layers.push_back(std::move(layer));
    return effect;
}

int cmdBenchmark(Args args) {
    double seconds = 5.0, particles = 0.0;
    const bool threeD = takeFlag(args, "--3d");
    if (!takeNumber(args, "--seconds", 0.1, 600.0, seconds) ||
        !takeNumber(args, "--particles", 1.0, vfx::kMaxParticlesPerEmitter - 2000.0, particles)) {
        return 2;
    }

    vfx::Effect effect;
    if (particles > 0.0 && args.empty()) {
        effect = makeBenchmarkEffect(particles, threeD);
    } else if (args.size() == 1 && particles == 0.0) {
        auto loaded = vfx::loadEffect(vfx::pathFromUtf8(args[0]));
        if (!loaded) {
            printError(loaded.error());
            return 1;
        }
        effect = std::move(loaded.value().effect);
    } else {
        std::fputs(kUsage, stderr);
        return 2;
    }

    vfx::Simulation sim(vfx::compileEffect(effect));
    const double step = sim.program().step;

    // Let the effect fill up first, so the timing is of its steady state.
    double longest = 0.0;
    for (const auto& e : sim.program().emitters) {
        longest = e->maxLifetime > longest ? e->maxLifetime : longest;
    }
    if (longest > 60.0) {
        longest = 60.0;
    }
    sim.seek(static_cast<std::int64_t>((longest + 0.5) / step));

    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::duration d) {
        return std::chrono::duration<double, std::milli>(d).count();
    };
    const auto steps = static_cast<std::int64_t>(seconds / step);
    vfx::RenderFrame frame;
    double stepTotal = 0.0, stepWorst = 0.0, frameTotal = 0.0, frameWorst = 0.0;
    std::uint64_t aliveTotal = 0;
    std::uint32_t alivePeak = 0;
    for (std::int64_t i = 0; i < steps; ++i) {
        const auto a = Clock::now();
        sim.advance();
        const auto b = Clock::now();
        sim.extract(frame);
        const auto c = Clock::now();
        const double s = ms(b - a), f = ms(c - b);
        stepTotal += s;
        frameTotal += f;
        stepWorst = s > stepWorst ? s : stepWorst;
        frameWorst = f > frameWorst ? f : frameWorst;
        const std::uint32_t alive = sim.aliveCount();
        aliveTotal += alive;
        alivePeak = alive > alivePeak ? alive : alivePeak;
    }
    const double count = static_cast<double>(steps > 0 ? steps : 1);
    const double budget = 1000.0 / 60.0;
    const double used = (stepTotal + frameTotal) / count;

    std::printf("%s: %lld steps (%g s of the effect), %zu layer%s\n", effect.name.c_str(),
                static_cast<long long>(steps), seconds, sim.emitterCount(),
                sim.emitterCount() == 1 ? "" : "s");
    std::printf("  particles alive   average %.0f, peak %u\n",
                static_cast<double>(aliveTotal) / count, alivePeak);
    std::printf("  simulate a step   average %.4f ms, worst %.4f ms\n", stepTotal / count, stepWorst);
    std::printf("  build frame data  average %.4f ms, worst %.4f ms\n", frameTotal / count,
                frameWorst);
    std::printf("  together          %.4f ms, which is %.1f%% of a 60 fps frame (%.2f ms)\n", used,
                100.0 * used / budget, budget);
    printLayerStats(effect, sim);
    // The fingerprint proves that a faster build still computes the same thing.
    std::printf("  final state       %016llx\n", static_cast<unsigned long long>(sim.stateHash()));
    return 0;
}

int run(Args args) {
    if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
        std::fputs(kUsage, stdout);
        return args.empty() ? 2 : 0;
    }
    const std::string command = args[0];
    args.erase(args.begin());
    if (command == "new") return cmdNew(std::move(args));
    if (command == "validate") return cmdValidate(std::move(args));
    if (command == "format") return cmdFormat(std::move(args));
    if (command == "info") return cmdInfo(std::move(args));
    if (command == "list") return cmdList(std::move(args));
    if (command == "get") return cmdGet(std::move(args));
    if (command == "set") return cmdSet(std::move(args));
    if (command == "simulate") return cmdSimulate(std::move(args));
    if (command == "benchmark") return cmdBenchmark(std::move(args));
    if (command == "schema") {
        std::fputs(vfx::schemaText().c_str(), stdout);
        return 0;
    }
    std::fprintf(stderr, "error: unknown command \"%s\"\n\n", command.c_str());
    std::fputs(kUsage, stderr);
    return 2;
}

}  // namespace

#if defined(_WIN32) && defined(_MSC_VER)
// Windows hands narrow arguments over in the local code page, which cannot
// hold every file name. The wide entry point gives the real names.
int wmain(int argc, wchar_t** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        args.push_back(vfx::pathToUtf8(std::filesystem::path(argv[i])));
    }
    return run(std::move(args));
}
#else
int main(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    return run(std::move(args));
}
#endif
