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

// Squash and stretch as two multipliers on a particle's size: `along` its
// own upright (or the way it is moving, for a particle aligned to its
// movement) and `across` that. Above 0 it is longer and thinner: 0.5 is
// half as long again. Below 0 it is shorter and wider by the same rule the
// other way round. The two always multiply to 1, so the area never changes.
inline void stretchFactors(float amount, float& along, float& across) {
    if (amount >= 0.0f) {
        along = 1.0f + amount;
        across = 1.0f / along;
    } else {
        across = 1.0f - amount;
        along = 1.0f / across;
    }
}

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
// How a sprite sheet steps through its pictures.
enum class Animate : std::uint8_t { Life, Loop, Random };

// The built-in particle pictures, in the order the "shape" property lists them.
enum class SpriteShape : std::uint8_t {
    Soft, Disc, Ring, Bubble, Sparkle, Star, Smoke, Square, Diamond, Heart, Streak, Flame,
    Puff, Burst, Crescent, Orb, Glint, Blaze,
    Candy, Shard, Drop, Splat, Shockwave, Twinkle, Flare, Rays, Swirl, Bean, Bolt,
    Starflash, Sliver, Shardring,
    Vortex, Sigil, Beam
};
// How many of them there are. The "shape" property lists more after these:
// the shape library, which the editor layer draws from pictures instead of
// formulas. A shape's number is its place in that list either way, so a
// SpriteShape may hold a number from kSpriteShapeCount up.
inline constexpr int kSpriteShapeCount = 35;
inline constexpr int kMaxSpriteShapes = 250;

// Changes to a shape's own design, for the shapes that can be changed (see
// the editor's ShapeLibrary.h): how many points, sides, rays or teeth it
// has, and how thick, how pointed and how round it is. Everything at zero
// is the shape as it was drawn. The three amounts run from -1 to 1 and are
// kept in whole steps of 1/kShapeEditSteps, so that a setting has one exact
// value everywhere it is drawn.
inline constexpr int kShapeEditSteps = 63;
inline constexpr int kShapeEditMaxCount = 31;

struct ShapeEdit {
    int count = 0;        // 0: the shape's own
    float thickness = 0;  // below 0 thinner, above 0 thicker
    float sharpness = 0;  // below 0 blunter, above 0 more pointed
    float round = 0;      // below 0 sharper corners, above 0 rounder

    bool any() const { return count != 0 || thickness != 0.0f || sharpness != 0.0f || round != 0.0f; }
    friend bool operator==(const ShapeEdit& a, const ShapeEdit& b) {
        return a.count == b.count && a.thickness == b.thickness && a.sharpness == b.sharpness && a.round == b.round;
    }
};

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
    float offsetX = 0, offsetY = 0, offsetZ = 0;  // where the shape sits
    bool offset = false;                          // any of them is not zero

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
    // A field of swirls that carries particles along, on top of their own
    // movement. 0 speed means none.
    float turbulence = 0;         // units per second, on average
    float turbulencePerUnit = 1;  // one over the size of a swirl
    float turbulenceChange = 0;   // new patterns per second
    // How fast a particle travels its own path, as a multiplier on its own
    // movement (never on the swirls that carry it): 1 leaves it alone, 0
    // stops it, below 0 sends it backwards. A curve reads across the
    // particle's life; a fixed number or a random range is the particle's
    // own for life.
    ScalarF speedOverLife;
    bool eases = false;  // speedOverLife is anything other than a fixed 1
    // Turning about the layer's centre, so paths become arcs and spirals.
    // Radians per second, counter-clockwise; 0 means none. The sine and
    // cosine are of one whole step's turn.
    float orbit = 0;
    float orbitSin = 0, orbitCos = 1;

    // How it changes over its life. A fixed number or random range is a
    // per-particle multiplier chosen at birth; a curve is read every frame.
    bool hasOverLife = false;
    ScalarF sizeOverLife;
    ScalarF opacityOverLife;
    std::vector<GradientKeyF> colorOverLife;  // empty means no tint
    // The share of its life, at the end, over which a particle is eaten
    // away in a ragged pattern. 0 means it is not.
    float dissolve = 0;
    float dissolveEdge = 0;  // how brightly the eaten edge glows, 0 to 1
    float dissolveSoft = 0.15f;  // how soft the eaten edge is, 0 (torn) to 1 (smoke)
    // Squash and stretch: above 0 the particle is drawn longer and thinner,
    // below 0 shorter and wider, its area kept (see stretchFactors). A curve
    // reads across the particle's life; a fixed number or a random range is
    // the particle's own for life.
    ScalarF squashStretch;
    bool squashStretches = false;  // squashStretch is anything other than a fixed 0

    // How it is drawn. A layer with no Sprite module is simulated but not drawn.
    bool drawn = false;
    Id texture;
    BlendMode blend = BlendMode::Alpha;
    Facing facing = Facing::Camera;
    float glow = 1;
    SpriteShape spriteShape = SpriteShape::Soft;
    ShapeEdit shapeEdit;       // changes to the shape's own design
    bool alongMotion = false;  // point each particle the way it is moving
    float stretch = 0;         // seconds of travel drawn as a streak
    float tilt = 0;            // degrees leaned back, as if lying on the ground

    // A sprite sheet: the texture cut into columns x rows pictures, of which
    // the first `frames` are used. One picture when there is no texture.
    int columns = 1, rows = 1, frames = 1;
    Animate animate = Animate::Life;
    float fps = 12;
    bool randomStart = false;

    // A ribbon behind each particle along the path it took. 0 means none.
    float trail = 0;       // seconds
    float trailWidth = 0;  // a share of the particle's size

    // Held frames: the layer is shown this many times a second, each
    // drawing held until the next, as hand-drawn animation is ("on twos" is
    // 12). 0 means every step is shown.
    float hold = 0;

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

// Which drawing of a held layer the particles at a step belong to. A held
// layer (hold > 0) is shown `hold` times a second, each drawing held until
// the next: the particles shown at a step are those of the first step with
// the same number as it. Drawings are counted from the start of each pass
// of the effect, so every pass of a loop is drawn at the same moments, and
// a burst at the very start of a pass is in that pass's first drawing.
std::int64_t heldDrawing(const EmitterProgram& e, std::int64_t step);

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
