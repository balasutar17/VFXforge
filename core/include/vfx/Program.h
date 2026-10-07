// VFX Forge core: the compiled program.
//
// The document is what the artist edits. The simulation never reads it.
// Compiling turns each enabled layer into an immutable EmitterProgram: plain
// numbers, defaults filled in, units converted, limits worked out. Because a
// program can never change, it can be handed to another thread, or later to
// a GPU backend, an exporter or an engine runtime, without locking.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "vfx/Document.h"
#include "vfx/Effect.h"
#include "vfx/Id.h"

namespace vfx {

// The simulation always advances in steps of this length, whatever the
// display's frame rate. A fixed step is what makes playback repeatable.
inline constexpr double kSimulationStep = 1.0 / 60.0;

// A safety limit per layer. A layer that asks for more is capped and says so.
inline constexpr std::uint32_t kMaxParticlesPerEmitter = 200000;

struct CurveKeyF {
    float t = 0, v = 0;
};

// A number that is fixed, random per particle, or follows a curve.
struct ScalarF {
    enum class Mode : std::uint8_t { Constant, Random, Curve };

    Mode mode = Mode::Constant;
    float a = 0;  // the constant, or the random minimum
    float b = 0;  // the random maximum
    std::vector<CurveKeyF> keys;

    bool isCurve() const { return mode == Mode::Curve; }
    float maxValue() const;
};

// Straight-line interpolation between keys, held flat beyond the ends.
float evalCurve(const std::vector<CurveKeyF>& keys, float x);

// random01 picks within a random range; curveX is where to read a curve.
float sample(const ScalarF& s, float random01, float curveX);

struct GradientKeyF {
    float t = 0, r = 1, g = 1, b = 1, a = 1;
};

struct BurstF {
    double time = 0;  // seconds from the layer's start
    std::uint32_t count = 0;
};

enum class ShapeType : std::uint8_t { Point, Circle, Rectangle, Sphere, Box, Cone };
enum class BlendMode : std::uint8_t { Alpha, Additive };
enum class Facing : std::uint8_t { Camera, Plane };

// The built-in particle pictures, in the order the "shape" property lists them.
enum class SpriteShape : std::uint8_t {
    Soft, Disc, Ring, Bubble, Sparkle, Star, Smoke, Square, Diamond, Heart, Streak, Flame,
    Puff, Burst, Crescent, Orb, Glint, Blaze
};
inline constexpr int kSpriteShapeCount = 18;

struct EmitterProgram {
    Id layer;
    std::uint64_t key = 0;  // effect seed mixed with the layer's ID

    // Effect-level settings, copied in so an emitter stands alone.
    double effectDuration = 2.0;
    bool loop = true;
    bool flat = true;  // 2D: depth is locked to zero
    double step = kSimulationStep;

    // When it emits, within one pass of the effect. Already clipped so it
    // never runs past the effect's end.
    double start = 0.0;
    double duration = 0.0;
    bool emits = false;
    ScalarF rate;  // particles per second; a curve reads across the layer's duration
    std::vector<BurstF> bursts;

    // Where particles appear.
    ShapeType shape = ShapeType::Point;
    bool fromEdge = false;
    float radius = 0;
    float sizeX = 0, sizeY = 0, sizeZ = 0;
    float coneAngle = 0;  // radians

    // What each particle starts with. A curve reads across the layer's duration.
    ScalarF lifetime, speed, size, rotation;
    float dirX = 0, dirY = 1, dirZ = 0;  // unit length
    float spread = 0;     // radians
    float cosSpread = 1;
    float color[4] = {1, 1, 1, 1};

    // How it moves.
    float gravityX = 0, gravityY = 0, gravityZ = 0;
    float drag = 0;
    ScalarF spin;  // degrees per second; a curve reads across the particle's life

    // How it changes over its life. A fixed number or random range is a
    // per-particle multiplier chosen at birth; a curve is read every frame.
    bool hasOverLife = false;
    ScalarF sizeOverLife;
    ScalarF opacityOverLife;
    std::vector<GradientKeyF> colorOverLife;  // empty means no tint

    // How it is drawn. A layer with no Sprite module is simulated but not drawn.
    bool drawn = false;
    Id texture;
    BlendMode blend = BlendMode::Alpha;
    Facing facing = Facing::Camera;
    float glow = 1;
    SpriteShape spriteShape = SpriteShape::Soft;
    bool alongMotion = false;  // point each particle the way it is moving
    float stretch = 0;         // seconds of travel drawn as a streak

    // Limits worked out at compile time, so stepping never allocates memory.
    double maxLifetime = 0;
    std::uint32_t capacity = 0;
    std::uint64_t wanted = 0;  // what it would need with no safety limit
    bool capped() const { return wanted > capacity; }
};

struct Program {
    std::uint64_t seed = 1;
    double duration = 2.0;
    bool loop = true;
    bool flat = true;
    double step = kSimulationStep;

    // Enabled layers only, in stack order.
    std::vector<std::shared_ptr<const EmitterProgram>> emitters;
};

std::shared_ptr<const EmitterProgram> compileLayer(const Effect& effect, const Layer& layer);
std::shared_ptr<const Program> compileEffect(const Effect& effect);

// Keeps a program in step with a document while it is being edited.
//
// It listens to the document's change records and recompiles only the layers
// an edit touched. Untouched layers keep the very same EmitterProgram object,
// which is how the simulation knows it need not restart them.
class LiveProgram {
public:
    explicit LiveProgram(Document& document);
    ~LiveProgram();

    LiveProgram(const LiveProgram&) = delete;
    LiveProgram& operator=(const LiveProgram&) = delete;

    // The up-to-date program. Cheap when nothing has changed.
    std::shared_ptr<const Program> current();

    // How many layers have been compiled so far, for tests and profiling.
    std::uint64_t layersCompiled() const { return layersCompiled_; }

private:
    void onChange(const Change& change);

    Document& document_;
    int token_ = 0;
    std::shared_ptr<const Program> program_;
    bool stale_ = true;
    bool allDirty_ = true;
    std::vector<Id> dirtyLayers_;
    std::uint64_t layersCompiled_ = 0;
};

}  // namespace vfx
