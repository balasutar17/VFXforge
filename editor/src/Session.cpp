#include "vfx/editor/Session.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "vfx/Command.h"
#include "vfx/FileIO.h"
#include "vfx/Metadata.h"
#include "vfx/Path.h"
#include "vfx/editor/Presets.h"

namespace vfx::editor {

namespace {

constexpr double kPi = 3.14159265358979323846;

// If the display stalls (the window was hidden, the machine slept), jump to
// the right moment instead of replaying every step in between one by one.
constexpr std::int64_t kMaxStepsPerTick = 30;

Value* property(Layer& layer, const char* type, const char* key) {
    for (auto& m : layer.modules) {
        if (m.type == type) {
            return m.find(key);
        }
    }
    return nullptr;
}

void put(Layer& layer, const char* type, const char* key, Value value) {
    if (Value* slot = property(layer, type, key)) {
        *slot = std::move(value);
    }
}

}  // namespace

Effect makeStarterEffect(const IdSource& newId, bool threeD) {
    Effect effect = makeEmptyEffect(newId, "Untitled", threeD);
    effect.duration = 3.0;
    effect.loop = "loop";
    effect.seed = 1;

    Layer layer = makeBasicEmitter(newId, "Sparks");
    layer.duration = 3.0;
    put(layer, "emission", "rate", Scalar::constant(60.0));
    put(layer, "initial", "lifetime", Scalar::random(0.8, 1.4));
    put(layer, "initial", "speed", Scalar::random(4.5, 7.0));
    put(layer, "initial", "spread", 22.0);
    put(layer, "initial", "size", Scalar::random(0.12, 0.26));
    put(layer, "initial", "color", Color{1.0, 0.45, 0.08, 1.0});
    put(layer, "motion", "gravity", Vec3{0.0, -7.0, 0.0});
    put(layer, "sprite", "blend", std::string("additive"));
    put(layer, "sprite", "glow", 1.6);
    effect.layers.push_back(std::move(layer));
    return effect;
}

// Everything that belongs to one open effect. Replaced as a whole when
// another effect is opened, so nothing can be left pointing at the old one.
struct Session::State {
    Document document;
    CommandStack commands;
    LiveProgram live;
    Simulation simulation;
    PlaybackClock clock;
    RenderFrame frame;

    State(Effect effect, IdGenerator ids)
        : document(std::move(effect), ids),
          commands(document),
          live(document),
          simulation(live.current()),
          clock(document.effect().duration, document.effect().loop == "loop",
                document.effect().frameRate) {
        simulation.extract(frame);
    }
};

Session::Session() { newEffect(false); }

Session::~Session() = default;

void Session::adopt(Effect effect) {
    state_ = std::make_unique<State>(std::move(effect), IdGenerator::fromEntropy());
    ++generation_;
}

void Session::newEffect(bool threeD) {
    IdGenerator ids = IdGenerator::fromEntropy();
    const IdSource newId = [&ids]() { return ids.next(); };
    adopt(makeStarterEffect(newId, threeD));
    path_.clear();
    readOnly_ = false;
    notes_.clear();
    state_->clock.play();
}

Status Session::openPreset(std::string_view presetId) {
    IdGenerator ids = IdGenerator::fromEntropy();
    const IdSource newId = [&ids]() { return ids.next(); };
    auto made = makePreset(presetId, newId);
    if (!made) {
        return made.error();
    }
    adopt(std::move(made.value()));
    path_.clear();
    readOnly_ = false;
    notes_.clear();
    state_->clock.play();
    return {};
}

Status Session::open(const std::filesystem::path& path) {
    auto loaded = loadEffect(path);
    if (!loaded) {
        return loaded.error();
    }
    LoadedEffect& file = loaded.value();
    adopt(std::move(file.effect));
    path_ = path;
    readOnly_ = file.readOnly;
    notes_ = std::move(file.diagnostics);
    state_->clock.play();
    return {};
}

Status Session::save() {
    if (path_.empty()) {
        return makeError("This effect has not been saved yet. Choose where to save it.");
    }
    return saveAs(path_);
}

Status Session::saveAs(const std::filesystem::path& path) {
    if (readOnly_ && path == path_) {
        return makeError(
            "This file was made by a newer version of VFX Forge, so it cannot be saved over. "
            "Use Save As to keep a copy.");
    }
    if (Status saved = saveEffect(path, effect()); !saved) {
        return saved;
    }
    path_ = path;
    readOnly_ = false;
    state_->commands.markSaved();
    return {};
}

bool Session::dirty() const { return state_->commands.isDirty(); }

const Effect& Session::effect() const { return state_->document.effect(); }
Document& Session::document() { return state_->document; }
CommandStack& Session::commands() { return state_->commands; }
const CommandStack& Session::commands() const { return state_->commands; }

Status Session::set(const Path& path, Value value) {
    return state_->commands.push(std::make_unique<SetPropertyCommand>(path, std::move(value)));
}

Status Session::addEmitter(std::string name, Id* created) {
    Document& doc = state_->document;
    const IdSource newId = [&doc]() { return doc.newId(); };
    Layer layer = makeBasicEmitter(newId, std::move(name));
    layer.duration = effect().duration;
    const Id id = layer.id;
    Status pushed = state_->commands.push(std::make_unique<AddLayerCommand>(std::move(layer)));
    if (pushed && created) {
        *created = id;
    }
    return pushed;
}

Status Session::removeLayer(Id layer) {
    return state_->commands.push(std::make_unique<RemoveLayerCommand>(layer));
}

Status Session::addPreset(std::string_view presetId, int* added) {
    Document& doc = state_->document;
    const IdSource newId = [&doc]() { return doc.newId(); };
    auto made = makePreset(presetId, newId);
    if (!made) {
        return made.error();
    }
    const PresetInfo* info = findPreset(presetId);
    std::vector<CommandPtr> steps;
    for (Layer& layer : made.value().layers) {
        steps.push_back(std::make_unique<AddLayerCommand>(std::move(layer)));
    }
    const int count = static_cast<int>(steps.size());
    Status pushed = state_->commands.push(std::make_unique<CompositeCommand>(
        "Add " + (info ? info->name : std::string("Preset")), std::move(steps)));
    if (pushed && added) {
        *added = count;
    }
    return pushed;
}

Status Session::undo() { return state_->commands.undo(); }
Status Session::redo() { return state_->commands.redo(); }

void Session::beginEdit(std::string name) { state_->commands.beginTransaction(std::move(name)); }

void Session::endEdit() {
    if (state_->commands.inTransaction()) {
        state_->commands.endTransaction();
    }
}

std::vector<ControlView> Session::controls(Id layerId) const {
    std::vector<ControlView> out;
    const Layer* layer = findLayer(effect(), layerId);
    if (!layer) {
        return out;
    }
    out.reserve(layer->controls.size());
    for (const SimpleControl& control : layer->controls) {
        ControlView view;
        view.layer = layerId;
        view.control = control.id;
        view.label = control.label;
        if (!control.targets.empty()) {
            const ControlTarget& target = control.targets.front();
            const Module* module = findModule(*layer, target.module);
            if (module && module->known()) {
                const int index = module->desc->indexOf(target.property);
                if (index >= 0) {
                    const auto i = static_cast<std::size_t>(index);
                    view.desc = &module->desc->properties[i];
                    view.value = module->values[i];
                }
            }
        }
        out.push_back(std::move(view));
    }
    return out;
}

Status Session::setControl(Id layerId, Id controlId, const Value& value) {
    const Layer* layer = findLayer(effect(), layerId);
    if (!layer) {
        return makeError("That layer no longer exists.");
    }
    for (const SimpleControl& control : layer->controls) {
        if (control.id != controlId) {
            continue;
        }
        std::vector<CommandPtr> children;
        for (const ControlTarget& target : control.targets) {
            children.push_back(std::make_unique<SetPropertyCommand>(
                Path::property(layerId, target.module, target.property), value));
        }
        if (children.empty()) {
            return makeError("This control is not connected to anything.");
        }
        return state_->commands.push(
            std::make_unique<CompositeCommand>("Change " + control.label, std::move(children)));
    }
    return makeError("That control no longer exists.");
}

PlaybackClock& Session::clock() { return state_->clock; }
const PlaybackClock& Session::clock() const { return state_->clock; }

void Session::tick(double realSeconds) {
    State& s = *state_;
    const Effect& e = s.document.effect();
    s.clock.configure(e.duration, e.loop == "loop", e.frameRate);
    s.clock.advance(realSeconds);

    std::shared_ptr<const Program> program = s.live.current();
    if (program.get() != &s.simulation.program()) {
        s.simulation.setProgram(std::move(program));
    }

    const std::int64_t target = s.clock.simulationStep();
    const std::int64_t behind = target - s.simulation.step();
    if (behind > 0 && behind <= kMaxStepsPerTick) {
        for (std::int64_t i = 0; i < behind; ++i) {
            s.simulation.advance();
        }
    } else if (behind != 0) {
        s.simulation.seek(target);
    }
    s.simulation.extract(s.frame);
}

const RenderFrame& Session::frame() const { return state_->frame; }

std::uint32_t Session::particleCount() const { return state_->simulation.aliveCount(); }

std::vector<Id> Session::cappedLayers() const {
    std::vector<Id> out;
    for (const auto& emitter : state_->simulation.program().emitters) {
        if (emitter->capped()) {
            out.push_back(emitter->layer);
        }
    }
    return out;
}

// -------------------------------------------------------------- helpers

double linearToSrgb(double linear) {
    if (!(linear > 0.0)) {
        return 0.0;
    }
    if (linear <= 0.0031308) {
        return linear * 12.92;
    }
    return 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}

double srgbToLinear(double srgb) {
    if (!(srgb > 0.0)) {
        return 0.0;
    }
    if (srgb <= 0.04045) {
        return srgb / 12.92;
    }
    return std::pow((srgb + 0.055) / 1.055, 2.4);
}

Heading headingFromDirection(const Vec3& d) {
    Heading out;
    const double flat = std::sqrt(d.x * d.x + d.y * d.y);
    const double length = std::sqrt(flat * flat + d.z * d.z);
    if (!(length > 1e-12)) {
        return out;  // no direction at all: call it "up"
    }
    if (flat > 1e-12) {
        out.heading = std::atan2(d.y, d.x) * 180.0 / kPi;
        if (out.heading < 0.0) {
            out.heading += 360.0;
        }
    }
    out.tilt = std::atan2(d.z, flat) * 180.0 / kPi;
    return out;
}

Vec3 directionFromHeading(const Heading& h) {
    const double heading = h.heading * kPi / 180.0;
    const double tilt = std::clamp(h.tilt, -90.0, 90.0) * kPi / 180.0;
    auto tidy = [](double v) { return std::abs(v) < 1e-12 ? 0.0 : v; };
    return Vec3{tidy(std::cos(heading) * std::cos(tilt)), tidy(std::sin(heading) * std::cos(tilt)),
                tidy(std::sin(tilt))};
}

}  // namespace vfx::editor
