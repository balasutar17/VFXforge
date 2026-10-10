#include "vfx/editor/Session.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <system_error>
#include <utility>

#include "vfx/Command.h"
#include "vfx/FileIO.h"
#include "vfx/Metadata.h"
#include "vfx/Path.h"
#include "vfx/editor/Archive.h"
#include "vfx/editor/Presets.h"
#include "vfx/editor/SoundLibrary.h"

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

Session::~Session() {
    if (!scratch_.empty()) {
        std::error_code ignored;
        std::filesystem::remove_all(scratch_, ignored);
    }
}

void Session::adopt(Effect effect) {
    state_ = std::make_unique<State>(std::move(effect), IdGenerator::fromEntropy());
    ++generation_;
    images_.clear();
    images_.retryFailed();
    sounds_.clear();
    sounds_.retryFailed();
}

std::filesystem::path Session::projectFolder() const {
    if (!path_.empty()) {
        return path_.parent_path();
    }
    return scratch_;
}

void Session::syncImages() {
    imageProblems_ = images_.sync(effect(), projectFolder());
    soundProblems_ = sounds_.sync(effect(), projectFolder());
}

void Session::ensureScratch() {
    if (path_.empty() && scratch_.empty()) {
        std::error_code error;
        const auto base = std::filesystem::temp_directory_path(error);
        if (error) {
            return;
        }
        IdGenerator ids = IdGenerator::fromEntropy();
        char name[40];
        std::snprintf(name, sizeof name, "unsaved-%016llx",
                      static_cast<unsigned long long>(ids.next().value));
        scratch_ = base / "VFX Forge" / name;
    }
}

Status Session::attachSound(Id layerId, const std::string& relative) {
    const Layer* layer = findLayer(effect(), layerId);
    if (!layer) {
        return makeError("That layer no longer exists.");
    }
    Id asset;
    for (const Asset& a : effect().assets) {
        if (a.kind == "sound" && a.path == relative) {
            asset = a.id;
        }
    }
    std::vector<CommandPtr> steps;
    if (!asset.valid()) {
        Asset added;
        added.id = state_->document.newId();
        added.kind = "sound";
        added.path = relative;
        asset = added.id;
        steps.push_back(std::make_unique<AddAssetCommand>(std::move(added)));
    }
    const Module* existing = nullptr;
    for (const Module& m : layer->modules) {
        if (m.type == "sound") {
            existing = &m;
        }
    }
    Id previous;
    if (existing) {
        if (const auto* ref = std::get_if<AssetRef>(existing->find("sound"))) {
            previous = ref->id;
        }
        if (previous == asset && steps.empty()) {
            return {};
        }
        steps.push_back(std::make_unique<SetPropertyCommand>(
            Path::property(layerId, existing->id, "sound"), Value(AssetRef{asset})));
    } else {
        Module module = makeModule(*Registry::builtin().findModule("sound"), state_->document.newId());
        *module.find("sound") = AssetRef{asset};
        steps.push_back(std::make_unique<AddModuleCommand>(layerId, std::move(module)));
    }
    if (previous.valid() && previous != asset) {
        int users = 0;
        for (const Layer& l : effect().layers) {
            for (const Module& m : l.modules) {
                if (const auto* ref = m.type == "sound" ? std::get_if<AssetRef>(m.find("sound")) : nullptr) {
                    users += ref->id == previous ? 1 : 0;
                }
            }
        }
        if (users <= 1) {
            steps.push_back(std::make_unique<RemoveAssetCommand>(previous));
        }
    }
    Status pushed = state_->commands.push(std::make_unique<CompositeCommand>("Use Sound", std::move(steps)));
    syncImages();
    return pushed;
}

Status Session::useSound(Id layerId, std::string_view originalName, std::string_view wavBytes) {
    if (!findLayer(effect(), layerId)) {
        return makeError("That layer no longer exists.");
    }
    auto decoded = decodeSound(wavBytes);
    if (!decoded) {
        return decoded.error();
    }
    ensureScratch();
    const std::string relative = soundAssetPath(originalName, wavBytes);
    const std::filesystem::path target = projectFolder() / pathFromUtf8(relative);
    std::error_code error;
    if (!std::filesystem::exists(target, error)) {
        std::filesystem::create_directories(target.parent_path(), error);
        if (Status written = writeFileAtomic(target, wavBytes); !written) {
            return written;
        }
    }
    return attachSound(layerId, relative);
}

Status Session::useLibrarySound(Id layerId, std::string_view soundId) {
    if (!findLibrarySound(soundId)) {
        return makeError("There is no library sound called \"" + std::string(soundId) + "\".");
    }
    ensureScratch();
    // The file is made on the next sync, from the library itself.
    return attachSound(layerId, librarySoundPath(soundId));
}

Status Session::clearSound(Id layerId) {
    const Layer* layer = findLayer(effect(), layerId);
    if (!layer) {
        return makeError("That layer no longer exists.");
    }
    const Module* sound = nullptr;
    for (const Module& m : layer->modules) {
        if (m.type == "sound") {
            sound = &m;
        }
    }
    if (!sound) {
        return {};
    }
    std::vector<CommandPtr> steps;
    steps.push_back(std::make_unique<RemoveModuleCommand>(layerId, sound->id));
    if (const auto* ref = std::get_if<AssetRef>(sound->find("sound")); ref && ref->id.valid()) {
        int users = 0;
        for (const Layer& l : effect().layers) {
            for (const Module& m : l.modules) {
                if (const auto* r = m.type == "sound" ? std::get_if<AssetRef>(m.find("sound")) : nullptr) {
                    users += r->id == ref->id ? 1 : 0;
                }
            }
        }
        if (users <= 1) {
            steps.push_back(std::make_unique<RemoveAssetCommand>(ref->id));
        }
    }
    Status pushed = state_->commands.push(std::make_unique<CompositeCommand>("Remove Sound", std::move(steps)));
    syncImages();
    return pushed;
}

namespace {

const Module* spriteOf(const Layer& layer) {
    for (const auto& m : layer.modules) {
        if (m.type == "sprite") {
            return &m;
        }
    }
    return nullptr;
}

}  // namespace

Status Session::useImage(Id layerId, std::string_view originalName, std::string_view pngBytes) {
    const Layer* layer = findLayer(effect(), layerId);
    if (!layer) {
        return makeError("That layer no longer exists.");
    }
    const Module* sprite = spriteOf(*layer);
    if (!sprite) {
        return makeError("This layer is not drawn, so it can't show a picture.");
    }
    auto decoded = decodePng(pngBytes);
    if (!decoded) {
        return decoded.error();
    }

    // Unsaved effects keep their pictures in a scratch folder until saved.
    if (path_.empty() && scratch_.empty()) {
        std::error_code error;
        const auto base = std::filesystem::temp_directory_path(error);
        if (error) {
            return makeError("There is nowhere to keep the picture.", error.message());
        }
        IdGenerator ids = IdGenerator::fromEntropy();
        char name[40];
        std::snprintf(name, sizeof name, "unsaved-%016llx",
                      static_cast<unsigned long long>(ids.next().value));
        scratch_ = base / "VFX Forge" / name;
    }
    const std::string relative = imageAssetPath(originalName, pngBytes);
    const std::filesystem::path target = projectFolder() / pathFromUtf8(relative);
    std::error_code error;
    if (!std::filesystem::exists(target, error)) {
        std::filesystem::create_directories(target.parent_path(), error);
        if (Status written = writeFileAtomic(target, pngBytes); !written) {
            return written;
        }
    }

    // The same picture already in the effect is used again, not added twice.
    Id asset;
    for (const Asset& a : effect().assets) {
        if (a.kind == "texture" && a.path == relative) {
            asset = a.id;
        }
    }
    std::vector<CommandPtr> steps;
    if (!asset.valid()) {
        Asset added;
        added.id = state_->document.newId();
        added.kind = "texture";
        added.path = relative;
        asset = added.id;
        steps.push_back(std::make_unique<AddAssetCommand>(std::move(added)));
    }
    const Id previous = std::get_if<AssetRef>(sprite->find("texture"))
                            ? std::get<AssetRef>(*sprite->find("texture")).id
                            : Id{};
    if (steps.empty() && previous == asset) {
        return {};  // already showing this picture
    }
    steps.push_back(std::make_unique<SetPropertyCommand>(
        Path::property(layerId, sprite->id, "texture"), Value(AssetRef{asset})));
    if (previous.valid() && previous != asset) {
        // Dropped only if nothing else uses it; checked when the step runs.
        int users = 0;
        for (const Layer& l : effect().layers) {
            if (const Module* s = spriteOf(l)) {
                if (const auto* ref = std::get_if<AssetRef>(s->find("texture"))) {
                    users += ref->id == previous ? 1 : 0;
                }
            }
        }
        if (users <= 1) {
            steps.push_back(std::make_unique<RemoveAssetCommand>(previous));
        }
    }
    auto image = std::make_shared<const Image>(std::move(decoded.value()));
    Status pushed = state_->commands.push(
        std::make_unique<CompositeCommand>("Use Picture", std::move(steps)));
    if (pushed) {
        images_.put(asset, std::move(image), relative);
        syncImages();
    }
    return pushed;
}

Status Session::clearImage(Id layerId) {
    const Layer* layer = findLayer(effect(), layerId);
    if (!layer) {
        return makeError("That layer no longer exists.");
    }
    const Module* sprite = spriteOf(*layer);
    const auto* ref = sprite ? std::get_if<AssetRef>(sprite->find("texture")) : nullptr;
    if (!ref || !ref->id.valid()) {
        return {};
    }
    const Id previous = ref->id;
    std::vector<CommandPtr> steps;
    steps.push_back(std::make_unique<SetPropertyCommand>(
        Path::property(layerId, sprite->id, "texture"), Value(AssetRef{})));
    int users = 0;
    for (const Layer& l : effect().layers) {
        if (const Module* s = spriteOf(l)) {
            if (const auto* r = std::get_if<AssetRef>(s->find("texture"))) {
                users += r->id == previous ? 1 : 0;
            }
        }
    }
    if (users <= 1) {
        steps.push_back(std::make_unique<RemoveAssetCommand>(previous));
    }
    Status pushed = state_->commands.push(
        std::make_unique<CompositeCommand>("Use Shape", std::move(steps)));
    syncImages();
    return pushed;
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
    // Pictures travel with the effect: copy any the new folder lacks.
    const std::filesystem::path from = projectFolder();
    const std::filesystem::path to = path.parent_path();
    if (from != to) {
        for (const Asset& asset : effect().assets) {
            const auto source = from / pathFromUtf8(asset.path);
            const auto target = to / pathFromUtf8(asset.path);
            std::error_code error;
            if (std::filesystem::exists(target, error)) {
                continue;
            }
            if (from.empty() || !std::filesystem::exists(source, error)) {
                // A library sound is made again rather than copied.
                if (const std::string id = librarySoundId(asset.path); !id.empty()) {
                    if (auto made = makeLibrarySound(id)) {
                        std::filesystem::create_directories(target.parent_path(), error);
                        (void)writeFileAtomic(target, encodeWav(made.value()));
                    }
                }
                continue;
            }
            auto bytes = readFile(source);
            if (!bytes) {
                return makeError("A picture could not be copied next to the effect.",
                                 bytes.error().message);
            }
            std::filesystem::create_directories(target.parent_path(), error);
            if (Status copied = writeFileAtomic(target, bytes.value()); !copied) {
                return makeError("A picture could not be copied next to the effect.",
                                 copied.error().message);
            }
        }
    }
    if (Status saved = saveEffect(path, effect()); !saved) {
        return saved;
    }
    path_ = path;
    images_.retryFailed();
    sounds_.retryFailed();
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
    // The preset's sounds and pictures come too; one this effect already has
    // (the same file) is used again rather than added twice.
    std::map<std::uint64_t, Id> same;
    for (const Asset& asset : made.value().assets) {
        Id existing;
        for (const Asset& a : effect().assets) {
            if (a.path == asset.path && a.kind == asset.kind) {
                existing = a.id;
            }
        }
        if (existing.valid()) {
            same[asset.id.value] = existing;
        } else {
            steps.push_back(std::make_unique<AddAssetCommand>(asset));
        }
    }
    int count = 0;
    for (Layer& layer : made.value().layers) {
        for (Module& m : layer.modules) {
            for (Value& v : m.values) {
                if (auto* ref = std::get_if<AssetRef>(&v)) {
                    if (const auto it = same.find(ref->id.value); it != same.end()) {
                        ref->id = it->second;
                    }
                }
            }
        }
        steps.push_back(std::make_unique<AddLayerCommand>(std::move(layer)));
        ++count;
    }
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
    syncImages();
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

// ------------------------------------------------ changing the whole effect

namespace vfx::editor {

namespace {

// Lower-case letters, digits and dashes from a file name, and its extension.
std::string referencePath(std::string_view originalName, std::string_view bytes) {
    std::string stem, extension;
    const std::size_t dot = originalName.find_last_of('.');
    const std::string_view base = dot == std::string_view::npos ? originalName : originalName.substr(0, dot);
    if (dot != std::string_view::npos) {
        for (char c : originalName.substr(dot)) {
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.') {
                extension.push_back(c);
            } else if (c >= 'A' && c <= 'Z') {
                extension.push_back(static_cast<char>(c - 'A' + 'a'));
            }
        }
    }
    for (char c : base) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            stem.push_back(c);
        } else if (c >= 'A' && c <= 'Z') {
            stem.push_back(static_cast<char>(c - 'A' + 'a'));
        } else if (!stem.empty() && stem.back() != '-') {
            stem.push_back('-');
        }
    }
    while (!stem.empty() && stem.back() == '-') {
        stem.pop_back();
    }
    if (stem.empty()) {
        stem = "reference";
    }
    if (stem.size() > 40) {
        stem.resize(40);
    }
    char mark[16];
    std::snprintf(mark, sizeof mark, "-%08x", static_cast<unsigned>(crc32(bytes)));
    return std::string("reference/") + stem + mark + (extension.size() > 1 && extension.size() <= 6 ? extension : std::string());
}

bool usesAsset(const Layer& layer, Id asset) {
    for (const Module& m : layer.modules) {
        for (const Value& v : m.values) {
            if (const auto* ref = std::get_if<AssetRef>(&v); ref && ref->id == asset) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

Status Session::applyEffect(const Effect& next, std::string stepName, const std::vector<PictureUse>* pictures) {
    // The pictures first: a layer must never point at a file that is not there.
    if (pictures && !pictures->empty()) {
        ensureScratch();
        for (const PictureUse& use : *pictures) {
            const std::filesystem::path target = projectFolder() / pathFromUtf8(use.path);
            std::error_code ec;
            if (std::filesystem::exists(target, ec)) {
                continue;
            }
            std::filesystem::create_directories(target.parent_path(), ec);
            if (Status written = writeFileAtomic(target, use.png); !written) {
                return makeError("A picture for the rebuilt effect could not be saved.", written.error().message);
            }
        }
    }

    std::vector<CommandPtr> steps;
    for (const Layer& layer : effect().layers) {
        steps.push_back(std::make_unique<RemoveLayerCommand>(layer.id));
    }
    // An asset the effect already has (the same file) is used again.
    std::map<std::uint64_t, Id> same;
    std::vector<Asset> added;
    for (const Asset& asset : next.assets) {
        if (findAsset(effect(), asset.id)) {
            continue;
        }
        Id existing;
        for (const Asset& a : effect().assets) {
            if (a.path == asset.path && a.kind == asset.kind) {
                existing = a.id;
            }
        }
        if (existing.valid()) {
            same[asset.id.value] = existing;
        } else {
            added.push_back(asset);
        }
    }
    std::vector<Layer> layers = next.layers;
    for (Layer& layer : layers) {
        for (Module& m : layer.modules) {
            for (Value& v : m.values) {
                if (auto* ref = std::get_if<AssetRef>(&v)) {
                    if (const auto it = same.find(ref->id.value); it != same.end()) {
                        ref->id = it->second;
                    }
                }
            }
        }
    }
    // Cut-outs from an earlier rebuild that nothing draws any more are
    // dropped from the list. (Their files stay.)
    for (const Asset& asset : effect().assets) {
        if (asset.kind != "texture" || asset.path.rfind(std::string(kImagesFolder) + "/reference-", 0) != 0) {
            continue;
        }
        bool used = false;
        for (const Layer& layer : layers) {
            used = used || usesAsset(layer, asset.id);
        }
        if (!used) {
            steps.push_back(std::make_unique<RemoveAssetCommand>(asset.id));
        }
    }
    for (const Asset& asset : added) {
        steps.push_back(std::make_unique<AddAssetCommand>(asset));
    }
    for (Layer& layer : layers) {
        steps.push_back(std::make_unique<AddLayerCommand>(std::move(layer)));
    }
    if (next.duration != effect().duration) {
        steps.push_back(std::make_unique<SetPropertyCommand>(Path::effect("duration"), Value(next.duration)));
    }
    if (next.loop != effect().loop) {
        steps.push_back(std::make_unique<SetPropertyCommand>(Path::effect("loop"), Value(next.loop)));
    }
    if (next.frameRate != effect().frameRate) {
        steps.push_back(std::make_unique<SetPropertyCommand>(Path::effect("frameRate"), Value(next.frameRate)));
    }
    Status pushed = state_->commands.push(std::make_unique<CompositeCommand>(std::move(stepName), std::move(steps)));
    if (pushed) {
        images_.retryFailed();
    }
    return pushed;
}

Status Session::moveLayer(Id layer, int newIndex) {
    return state_->commands.push(std::make_unique<MoveLayerCommand>(layer, newIndex));
}

Status Session::duplicateLayer(Id layerId, Id* created) {
    const Layer* source = findLayer(effect(), layerId);
    if (!source) {
        return makeError("That layer is no longer there.");
    }
    Document& doc = state_->document;
    Layer copy = *source;
    copy.id = doc.newId();
    copy.name = source->name + " copy";
    copy.locked = false;
    std::map<std::uint64_t, Id> renamed;
    for (Module& m : copy.modules) {
        const Id fresh = doc.newId();
        renamed[m.id.value] = fresh;
        m.id = fresh;
    }
    for (SimpleControl& control : copy.controls) {
        control.id = doc.newId();
        for (ControlTarget& target : control.targets) {
            if (const auto it = renamed.find(target.module.value); it != renamed.end()) {
                target.module = it->second;
            }
        }
    }
    const Id id = copy.id;
    const int index = layerIndex(effect(), layerId);
    Status pushed = state_->commands.push(std::make_unique<AddLayerCommand>(std::move(copy), index + 1));
    if (pushed && created) {
        *created = id;
    }
    return pushed;
}

Status Session::keepReference(std::string_view originalName, std::string_view bytes, std::string_view notesJson) {
    if (bytes.empty()) {
        return makeError("The reference file is empty.");
    }
    ensureScratch();
    const std::string relative = referencePath(originalName, bytes);
    const std::filesystem::path target = projectFolder() / pathFromUtf8(relative);
    std::error_code ec;
    if (!std::filesystem::exists(target, ec)) {
        std::filesystem::create_directories(target.parent_path(), ec);
        if (Status written = writeFileAtomic(target, bytes); !written) {
            return makeError("The reference could not be copied next to the effect.", written.error().message);
        }
    }
    std::vector<CommandPtr> steps;
    for (const Asset& a : effect().assets) {
        if (a.kind == "reference") {
            steps.push_back(std::make_unique<RemoveAssetCommand>(a.id));
        }
    }
    Asset asset;
    asset.id = state_->document.newId();
    asset.kind = "reference";
    asset.path = relative;
    if (!notesJson.empty()) {
        asset.extra.emplace_back("notes", std::string(notesJson));
    }
    steps.push_back(std::make_unique<AddAssetCommand>(std::move(asset)));
    return state_->commands.push(std::make_unique<CompositeCommand>("Keep reference", std::move(steps)));
}

Status Session::setReferenceNotes(std::string_view notesJson) {
    for (const Asset& a : effect().assets) {
        if (a.kind != "reference") {
            continue;
        }
        Asset changed = a;
        changed.extra.clear();
        for (const auto& entry : a.extra) {
            if (entry.first != "notes") {
                changed.extra.push_back(entry);
            }
        }
        if (!notesJson.empty()) {
            changed.extra.emplace_back("notes", std::string(notesJson));
        }
        std::vector<CommandPtr> steps;
        steps.push_back(std::make_unique<RemoveAssetCommand>(a.id));
        steps.push_back(std::make_unique<AddAssetCommand>(std::move(changed)));
        return state_->commands.push(std::make_unique<CompositeCommand>("Reference settings", std::move(steps)));
    }
    return makeError("This effect has no reference kept with it.");
}

Session::KeptReference Session::keptReference() const {
    KeptReference kept;
    for (const Asset& a : effect().assets) {
        if (a.kind != "reference") {
            continue;
        }
        kept.found = true;
        kept.file = projectFolder() / pathFromUtf8(a.path);
        kept.name = pathToUtf8(kept.file.filename());
        for (const auto& entry : a.extra) {
            if (entry.first == "notes") {
                kept.notes = entry.second;
            }
        }
    }
    return kept;
}

}  // namespace vfx::editor
