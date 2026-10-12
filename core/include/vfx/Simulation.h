// VFX Forge core: the CPU particle simulation.
//
// One rule holds everything together: the state at a given step depends only
// on the program and the step number. Never on how the simulation got there,
// how fast frames arrived, or which machine it runs on. Playing to a moment
// and scrubbing straight to it give the same particles, bit for bit.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "vfx/Id.h"
#include "vfx/Program.h"

namespace vfx {

// --------------------------------------------------------------- output

// One particle as the renderer needs it: plain numbers, nothing else.
struct SpriteInstance {
    float x = 0, y = 0, z = 0;
    float size = 0;
    float rotation = 0;  // radians
    float r = 1, g = 1, b = 1, a = 1;  // straight alpha, linear colour
    float vx = 0, vy = 0, vz = 0;      // units per second, for streaks
    float frame = 0;  // which sprite-sheet picture, a whole number from 0
    float age = 0;    // seconds since birth (limits how far back a trail reaches)
    // How far it has been eaten away, 0 (whole) to 1 (gone), and a number
    // of its own from 0 to 1 so that no two are eaten in the same pattern.
    // Both stay 0 in a layer that does not dissolve.
    float dissolve = 0;
    float seed = 0;
    // The animation principles. All three keep their defaults in a layer
    // that uses none of them.
    float squashStretch = 0;  // squash and stretch now (see stretchFactors); 0 is neither
    float ease = 1;     // how fast it is travelling its own path now, as a multiplier
    float life = 0;     // seconds it lives in all (a trail needs it to retrace an eased path)
};

// A run of instances drawn with the same settings: one per drawn layer.
struct RenderBatch {
    Id layer;
    Id texture;  // invalid means the built-in soft dot
    BlendMode blend = BlendMode::Alpha;
    Facing facing = Facing::Camera;
    float glow = 1;
    SpriteShape shape = SpriteShape::Soft;
    bool alongMotion = false;  // point each particle the way it is moving
    float stretch = 0;         // seconds of travel drawn as a streak
    // Degrees the sprite leans back from facing the viewer, as if it lay on
    // the ground and were seen from an angle. It still turns in its own
    // plane, so a spinning ring stays a level oval. Flat effects only.
    float tilt = 0;
    int columns = 1, rows = 1; // the texture's sprite-sheet grid
    bool dissolves = false;    // its particles are eaten away toward the end of their lives
    float dissolveEdge = 0;    // how brightly the eaten edge glows, 0 to 1
    float dissolveSoft = 0;    // how soft the eaten edge is, 0 (torn) to 1 (smoke)

    // Trails. The path a particle took is worked out backwards from where
    // it is, by undoing the simulation's own steps, so it needs no memory.
    float trail = 0;           // seconds of path drawn as a ribbon; 0 for none
    float trailWidth = 0;      // a share of the particle's size
    float gravityX = 0, gravityY = 0, gravityZ = 0;
    float drag = 0;
    float step = 0;            // the simulation step, seconds
    // The field of swirls that carried the particles (see det::turbulence),
    // so that a trail can follow the winding path and a streak can point
    // the way its particle is really going. 0 speed means none.
    float turbulence = 0;
    float turbulencePerUnit = 1;
    float turbulenceChange = 0;
    std::uint64_t turbulenceKey = 0;

    // The animation principles (see EmitterProgram for what each means).
    bool squashStretches = false;  // its particles carry a squash and stretch
    bool eases = false;      // its particles carry an ease and a life
    float orbit = 0;         // radians per second about (centerX, centerY)
    float orbitSin = 0, orbitCos = 1;  // of one whole step's turn
    float centerX = 0, centerY = 0, centerZ = 0;
    bool ground = false;  // 3D: its flat parts lie on the ground, and Orbit turns about the upright
    float hold = 0;          // drawings per second; 0 for every step
    ShapeEdit shapeEdit;     // changes to the shape's own design
    // The step its particles are shown at: the frame's own step, or for a
    // held layer the step its current drawing was taken at.
    std::int64_t shownStep = 0;
    // The layer's program, for anything else a renderer needs to retrace a
    // particle's path (the Speed over Life curve). May be null in a frame
    // put together by hand.
    std::shared_ptr<const EmitterProgram> program;

    std::uint32_t first = 0;
    std::uint32_t count = 0;
};

// Everything needed to draw one moment of an effect. The renderer reads this
// and knows nothing about documents, layers or modules. Reuse one frame
// object across calls and no memory is allocated after the first few.
struct RenderFrame {
    std::int64_t step = 0;
    double time = 0;
    bool flat = true;
    std::vector<SpriteInstance> instances;  // oldest particle first within a batch
    std::vector<RenderBatch> batches;       // in layer stack order
};

std::uint64_t hashFrame(const RenderFrame& frame);

struct EmitterStats {
    Id layer;
    std::uint32_t alive = 0;
    std::uint32_t capacity = 0;
    std::uint64_t dropped = 0;  // particles not created because the layer was full
};

// Read-only view of one layer's live particles, oldest first. Valid until
// the simulation is next changed. For tests and debug views.
struct ParticleView {
    std::uint32_t count = 0;
    const float* x = nullptr;
    const float* y = nullptr;
    const float* z = nullptr;
    const float* vx = nullptr;
    const float* vy = nullptr;
    const float* vz = nullptr;
    const float* age = nullptr;       // seconds since birth
    const float* lifetime = nullptr;  // seconds
    const float* size = nullptr;      // before any over-lifetime curve
    const float* rotation = nullptr;  // degrees
};

// ----------------------------------------------------------- simulation

class Simulation {
public:
    explicit Simulation(std::shared_ptr<const Program> program);
    ~Simulation();

    Simulation(Simulation&&) noexcept;
    Simulation& operator=(Simulation&&) noexcept;

    const Program& program() const { return *program_; }

    // Swaps in an edited program without losing the current moment. Layers
    // whose EmitterProgram object is unchanged keep their particles. Changed
    // layers restart and catch up to the current step, so what is on screen
    // is always exactly what the new settings produce at this time.
    void setProgram(std::shared_ptr<const Program> program);

    // How many steps have been simulated. The state is the effect at time
    // step() * program().step seconds.
    std::int64_t step() const { return step_; }
    double time() const { return static_cast<double>(step_) * program_->step; }

    void advance();                 // one step forward
    void seek(std::int64_t step);   // any step, forward or back
    void reset() { seek(0); }

    std::uint32_t aliveCount() const;
    std::vector<EmitterStats> stats() const;
    std::size_t emitterCount() const;
    ParticleView particles(std::size_t emitter) const;

    // Fills a frame for the renderer. Size, colour and opacity over lifetime
    // are worked out here, only when a frame is actually wanted.
    void extract(RenderFrame& frame) const;

    // A fingerprint of the complete state. Equal fingerprints mean equal
    // particles, to the last bit.
    std::uint64_t stateHash() const;

    // A copy of the complete state, for a checkpoint cache.
    class Snapshot;
    Snapshot snapshot() const;
    void restore(const Snapshot& snapshot);

private:
    struct Emitter;

    std::shared_ptr<const Program> program_;
    std::vector<Emitter> emitters_;
    std::int64_t step_ = 0;
};

class Simulation::Snapshot {
public:
    Snapshot();
    ~Snapshot();
    Snapshot(const Snapshot&);
    Snapshot& operator=(const Snapshot&);
    Snapshot(Snapshot&&) noexcept;
    Snapshot& operator=(Snapshot&&) noexcept;

    std::int64_t step() const { return step_; }

private:
    friend class Simulation;
    std::shared_ptr<const Program> program_;
    std::shared_ptr<const std::vector<Simulation::Emitter>> emitters_;
    std::int64_t step_ = 0;
};

}  // namespace vfx
