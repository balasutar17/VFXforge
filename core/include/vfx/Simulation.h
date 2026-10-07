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
};

// A run of instances drawn with the same settings: one per drawn layer.
struct RenderBatch {
    Id layer;
    Id texture;  // invalid means the built-in soft dot
    BlendMode blend = BlendMode::Alpha;
    Facing facing = Facing::Camera;
    float glow = 1;
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
