#include "vfx/Program.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "vfx/DetMath.h"
#include "vfx/Metadata.h"

namespace vfx {

float ScalarF::maxValue() const {
    switch (mode) {
        case Mode::Constant:
            return a;
        case Mode::Random:
            return a > b ? a : b;
        case Mode::Curve: {
            float best = keys.empty() ? 0.0f : keys.front().v;
            for (const auto& k : keys) {
                best = k.v > best ? k.v : best;
            }
            return best;
        }
    }
    return a;
}

float evalCurve(const std::vector<CurveKeyF>& keys, float x) {
    if (keys.empty()) {
        return 0.0f;
    }
    if (!(x > keys.front().t)) {
        return keys.front().v;
    }
    const std::size_t last = keys.size() - 1;
    if (!(x < keys[last].t)) {
        return keys[last].v;
    }
    std::size_t i = 0;
    while (i + 1 < last && x >= keys[i + 1].t) {
        ++i;
    }
    const CurveKeyF& k0 = keys[i];
    const CurveKeyF& k1 = keys[i + 1];
    const float f = (x - k0.t) / (k1.t - k0.t);
    return k0.v + (k1.v - k0.v) * f;
}

float sample(const ScalarF& s, float random01, float curveX) {
    switch (s.mode) {
        case ScalarF::Mode::Constant:
            return s.a;
        case ScalarF::Mode::Random:
            return s.a + (s.b - s.a) * random01;
        case ScalarF::Mode::Curve:
            return evalCurve(s.keys, curveX);
    }
    return s.a;
}

namespace {

// The first module of a type is the one that counts. A hand-edited file can
// hold two; the loader keeps both and says so.
const Module* firstOfType(const Layer& layer, const char* type) {
    for (const auto& m : layer.modules) {
        if (m.desc && m.type == type) {
            return &m;
        }
    }
    return nullptr;
}

// Reads a property from the module, or the metadata default when the layer
// has no such module.
class Reader {
public:
    Reader(const Module* module, const char* type)
        : module_(module), desc_(Registry::builtin().findModule(type)) {}

    bool present() const { return module_ != nullptr; }

    template <class T>
    const T& get(const char* key) const {
        if (module_) {
            if (const Value* v = module_->find(key)) {
                if (const T* typed = std::get_if<T>(v)) {
                    return *typed;
                }
            }
        }
        // The registry's own default always has the right type.
        return std::get<T>(desc_->find(key)->defaultValue);
    }

private:
    const Module* module_;
    const ModuleTypeDesc* desc_;
};

float f(double v) { return static_cast<float>(v); }

ScalarF toScalar(const Scalar& s) {
    ScalarF out;
    switch (s.kind) {
        case Scalar::Kind::Constant:
            out.mode = ScalarF::Mode::Constant;
            out.a = f(s.a);
            break;
        case Scalar::Kind::Random:
            out.mode = ScalarF::Mode::Random;
            out.a = f(s.a);
            out.b = f(s.b);
            break;
        case Scalar::Kind::Curve:
            if (s.keys.empty()) {
                out.mode = ScalarF::Mode::Constant;  // cannot happen through commands
                break;
            }
            out.mode = ScalarF::Mode::Curve;
            out.keys.reserve(s.keys.size());
            for (const auto& k : s.keys) {
                out.keys.push_back(CurveKeyF{f(k.t), f(k.v)});
            }
            break;
    }
    return out;
}

ScalarF constantScalar(float value) {
    ScalarF s;
    s.a = value;
    return s;
}

ShapeType shapeFrom(const std::string& name) {
    if (name == "circle") return ShapeType::Circle;
    if (name == "rectangle") return ShapeType::Rectangle;
    if (name == "sphere") return ShapeType::Sphere;
    if (name == "box") return ShapeType::Box;
    if (name == "cone") return ShapeType::Cone;
    return ShapeType::Point;
}

std::uint64_t toCount(double value) {
    if (!(value > 0.0)) {
        return 0;
    }
    if (value > 1e15) {
        return 1000000000000000ull;
    }
    return static_cast<std::uint64_t>(std::ceil(value));
}

}  // namespace

std::shared_ptr<const EmitterProgram> compileLayer(const Effect& effect, const Layer& layer) {
    auto e = std::make_shared<EmitterProgram>();
    e->layer = layer.id;
    e->key = det::emitterKey(static_cast<std::uint64_t>(effect.seed), layer.id.value);
    e->effectDuration = effect.duration > 0.0 ? effect.duration : 0.01;
    e->loop = effect.loop == "loop";
    e->flat = effect.space != "3d";
    e->step = kSimulationStep;

    // A layer cannot emit past the end of the effect.
    e->start = layer.start > 0.0 ? layer.start : 0.0;
    e->duration = std::min(layer.duration, e->effectDuration - e->start);
    if (!(e->duration > 0.0)) {
        e->duration = 0.0;
    }

    // ------------------------------------------------------------- emission
    const Reader emission(firstOfType(layer, "emission"), "emission");
    if (emission.present()) {
        e->rate = toScalar(emission.get<Scalar>("rate"));
        for (const auto& b : emission.get<BurstList>("bursts").items) {
            if (b.count > 0 && b.time >= 0.0 && b.time < e->duration) {
                const auto count = static_cast<std::uint32_t>(
                    std::min<std::int64_t>(b.count, kMaxParticlesPerEmitter));
                e->bursts.push_back(BurstF{b.time, count});
            }
        }
        std::stable_sort(e->bursts.begin(), e->bursts.end(),
                         [](const BurstF& x, const BurstF& y) { return x.time < y.time; });
    }
    const double maxRate = std::max(0.0, static_cast<double>(e->rate.maxValue()));
    e->emits = emission.present() && e->duration > 0.0 && (maxRate > 0.0 || !e->bursts.empty());

    // ---------------------------------------------------------------- shape
    const Reader shape(firstOfType(layer, "shape"), "shape");
    if (shape.present()) {
        e->shape = shapeFrom(shape.get<std::string>("shape"));
        e->fromEdge = shape.get<std::string>("emitFrom") == "edge";
        e->radius = f(shape.get<double>("radius"));
        const Vec3& size = shape.get<Vec3>("size");
        e->sizeX = f(size.x);
        e->sizeY = f(size.y);
        e->sizeZ = f(size.z);
        e->coneAngle = f(shape.get<double>("angle") * det::kDegreesToRadians);
    }

    // -------------------------------------------------------- initial state
    // With no Initial State module a particle still needs a lifetime and a
    // size, so the module's defaults are used.
    const Reader initial(firstOfType(layer, "initial"), "initial");
    e->lifetime = toScalar(initial.get<Scalar>("lifetime"));
    e->speed = toScalar(initial.get<Scalar>("speed"));
    e->size = toScalar(initial.get<Scalar>("size"));
    e->rotation = toScalar(initial.get<Scalar>("rotation"));

    const Vec3& direction = initial.get<Vec3>("direction");
    const det::Vec3f dir = det::normalizeOr(
        det::Vec3f{f(direction.x), f(direction.y), e->flat ? 0.0f : f(direction.z)},
        det::Vec3f{0.0f, 1.0f, 0.0f});
    e->dirX = dir.x;
    e->dirY = dir.y;
    e->dirZ = dir.z;

    const double spreadRadians = initial.get<double>("spread") * det::kDegreesToRadians;
    double spreadSin = 0.0, spreadCos = 1.0;
    det::sinCos(spreadRadians, spreadSin, spreadCos);
    e->spread = f(spreadRadians);
    e->cosSpread = f(spreadCos);

    const Color& color = initial.get<Color>("color");
    e->color[0] = f(color.r);
    e->color[1] = f(color.g);
    e->color[2] = f(color.b);
    e->color[3] = f(color.a);

    // --------------------------------------------------------------- motion
    // With no Motion module nothing acts on the particle at all.
    const Reader motion(firstOfType(layer, "motion"), "motion");
    if (motion.present()) {
        const Vec3& gravity = motion.get<Vec3>("gravity");
        e->gravityX = f(gravity.x);
        e->gravityY = f(gravity.y);
        e->gravityZ = e->flat ? 0.0f : f(gravity.z);
        e->drag = f(std::max(0.0, motion.get<double>("drag")));
        e->spin = toScalar(motion.get<Scalar>("spin"));
    }

    // ------------------------------------------------------------ over life
    const Reader overLife(firstOfType(layer, "overLife"), "overLife");
    e->sizeOverLife = constantScalar(1.0f);
    e->opacityOverLife = constantScalar(1.0f);
    if (overLife.present()) {
        e->hasOverLife = true;
        e->sizeOverLife = toScalar(overLife.get<Scalar>("size"));
        e->opacityOverLife = toScalar(overLife.get<Scalar>("opacity"));
        bool tints = false;
        for (const auto& k : overLife.get<Gradient>("color").keys) {
            const GradientKeyF key{f(k.t), f(k.color.r), f(k.color.g), f(k.color.b), f(k.color.a)};
            tints = tints || key.r != 1.0f || key.g != 1.0f || key.b != 1.0f || key.a != 1.0f;
            e->colorOverLife.push_back(key);
        }
        if (!tints) {
            e->colorOverLife.clear();  // all white: nothing to do each frame
        }
    }

    // --------------------------------------------------------------- sprite
    const Reader sprite(firstOfType(layer, "sprite"), "sprite");
    if (sprite.present()) {
        e->drawn = true;
        e->texture = sprite.get<AssetRef>("texture").id;
        e->blend = sprite.get<std::string>("blend") == "additive" ? BlendMode::Additive
                                                                  : BlendMode::Alpha;
        e->facing = sprite.get<std::string>("facing") == "plane" ? Facing::Plane : Facing::Camera;
        e->glow = f(sprite.get<double>("glow"));

        const std::string picture = sprite.get<std::string>("shape");
        const auto& options = Registry::builtin().findModule("sprite")->find("shape")->options;
        for (std::size_t i = 0; i < options.size() && i < static_cast<std::size_t>(kSpriteShapeCount); ++i) {
            if (options[i] == picture) {
                e->spriteShape = static_cast<SpriteShape>(i);
            }
        }
        e->alongMotion = sprite.get<std::string>("align") == "movement";
        e->stretch = e->alongMotion ? f(sprite.get<double>("stretch")) : 0.0f;

        if (e->texture.valid()) {
            auto whole = [&](const char* key, std::int64_t lo, std::int64_t hi) {
                return static_cast<int>(std::clamp(sprite.get<std::int64_t>(key), lo, hi));
            };
            e->columns = whole("columns", 1, 64);
            e->rows = whole("rows", 1, 64);
            const int cells = e->columns * e->rows;
            const int frames = whole("frames", 0, 4096);
            e->frames = frames == 0 ? cells : std::min(frames, cells);
            const std::string& animate = sprite.get<std::string>("animate");
            e->animate = animate == "loop"     ? Animate::Loop
                         : animate == "random" ? Animate::Random
                                               : Animate::Life;
            const double fps = sprite.get<double>("fps");
            e->fps = std::isfinite(fps) ? std::clamp(f(fps), 0.1f, 240.0f) : 12.0f;
            e->randomStart = sprite.get<bool>("randomStart");
        }
    }

    // ------------------------------------------------------------- capacity
    // Two upper bounds on how many particles can be alive at once; the
    // smaller is the one that matters.
    e->maxLifetime = std::max(1e-4, static_cast<double>(e->lifetime.maxValue()));
    if (e->emits) {
        double burstTotal = 0.0;
        for (const auto& b : e->bursts) {
            burstTotal += b.count;
        }
        const double passes =
            e->loop ? std::ceil(e->maxLifetime / e->effectDuration) + 1.0 : 1.0;
        const double byPass = (std::ceil(maxRate * e->duration) + burstTotal) * passes;
        const double byRate = std::ceil(maxRate * e->maxLifetime) + burstTotal * passes +
                              std::ceil(maxRate * e->step) + 2.0;
        e->wanted = toCount(std::min(byPass, byRate) + 16.0);
        e->capacity = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(e->wanted, kMaxParticlesPerEmitter));
    }
    return e;
}

std::shared_ptr<const Program> compileEffect(const Effect& effect) {
    auto program = std::make_shared<Program>();
    program->seed = static_cast<std::uint64_t>(effect.seed);
    program->duration = effect.duration > 0.0 ? effect.duration : 0.01;
    program->loop = effect.loop == "loop";
    program->flat = effect.space != "3d";
    program->step = kSimulationStep;
    for (const auto& layer : effect.layers) {
        if (layer.enabled) {
            program->emitters.push_back(compileLayer(effect, layer));
        }
    }
    return program;
}

// ------------------------------------------------------------ LiveProgram

LiveProgram::LiveProgram(Document& document) : document_(document) {
    token_ = document_.subscribe([this](const Change& change) { onChange(change); });
}

LiveProgram::~LiveProgram() { document_.unsubscribe(token_); }

void LiveProgram::onChange(const Change& change) {
    switch (change.kind) {
        case Change::Kind::Property:
            if (!change.layer.valid()) {
                // Only these effect fields reach the simulation. The name and
                // the frame rate do not.
                if (change.field == "seed" || change.field == "duration" ||
                    change.field == "loop" || change.field == "space") {
                    allDirty_ = true;
                    stale_ = true;
                }
            } else if (!change.module.valid() && change.field == "name") {
                // Renaming a layer changes nothing that is simulated.
            } else {
                dirtyLayers_.push_back(change.layer);
                stale_ = true;
            }
            break;
        case Change::Kind::LayerAdded:
        case Change::Kind::ModuleAdded:
        case Change::Kind::ModuleRemoved:
        case Change::Kind::ModuleMoved:
            dirtyLayers_.push_back(change.layer);
            stale_ = true;
            break;
        case Change::Kind::LayerRemoved:
        case Change::Kind::LayerMoved:
            stale_ = true;  // the list changes; the layers themselves do not
            break;
        case Change::Kind::AssetAdded:
        case Change::Kind::AssetRemoved:
            break;
    }
}

std::shared_ptr<const Program> LiveProgram::current() {
    if (!stale_ && program_) {
        return program_;
    }
    const Effect& effect = document_.effect();
    auto next = std::make_shared<Program>();
    next->seed = static_cast<std::uint64_t>(effect.seed);
    next->duration = effect.duration > 0.0 ? effect.duration : 0.01;
    next->loop = effect.loop == "loop";
    next->flat = effect.space != "3d";
    next->step = kSimulationStep;

    for (const auto& layer : effect.layers) {
        if (!layer.enabled) {
            continue;
        }
        std::shared_ptr<const EmitterProgram> reused;
        const bool dirty = allDirty_ || std::find(dirtyLayers_.begin(), dirtyLayers_.end(),
                                                  layer.id) != dirtyLayers_.end();
        if (!dirty && program_) {
            for (const auto& old : program_->emitters) {
                if (old->layer == layer.id) {
                    reused = old;
                    break;
                }
            }
        }
        if (reused) {
            next->emitters.push_back(std::move(reused));
        } else {
            next->emitters.push_back(compileLayer(effect, layer));
            ++layersCompiled_;
        }
    }

    program_ = std::move(next);
    stale_ = false;
    allDirty_ = false;
    dirtyLayers_.clear();
    return program_;
}

}  // namespace vfx
