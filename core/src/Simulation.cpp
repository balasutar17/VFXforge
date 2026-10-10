#include "vfx/Simulation.h"

#include <cmath>
#include <cstring>
#include <utility>

#include "vfx/DetMath.h"

// Determinism rules for this file (see DetMath.h):
//   - no system maths functions except sqrt, floor, fabs and copysign
//   - every random number is a hash of what it is for, never a running generator
//   - nothing depends on wall-clock time, thread timing or memory addresses

namespace vfx {

namespace {

// One array per field, all the same length, so the hot loops touch only the
// data they need and the compiler can process several particles at once.
enum Field : std::size_t {
    kX, kY, kZ,
    kVX, kVY, kVZ,
    kAge, kLife,
    kSize, kRot, kSpin,
    kR, kG, kB, kA,
    kPick,  // a random number fixed at birth, for picking a sprite-sheet picture
    kFieldCount
};

// Each property of a particle draws from its own random stream. Add new
// entries at the end: renumbering would change every existing effect.
enum Channel : std::uint32_t {
    kChLifetime, kChSpeed, kChSize, kChRotation, kChSpin,
    kChShapeU, kChShapeV, kChShapeW,
    kChSpreadU, kChSpreadV,
    kChSizeScale, kChOpacityScale,
    kChRate,
    kChFrame
};

constexpr float kDegreesToRadiansF = static_cast<float>(det::kDegreesToRadians);

inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

inline std::uint32_t bits(float v) {
    std::uint32_t u;
    std::memcpy(&u, &v, sizeof u);
    return u;
}

inline std::uint64_t bits(double v) {
    std::uint64_t u;
    std::memcpy(&u, &v, sizeof u);
    return u;
}

struct Rgba {
    float r, g, b, a;
};

Rgba evalGradient(const std::vector<GradientKeyF>& keys, float x) {
    const GradientKeyF& first = keys.front();
    if (!(x > first.t)) {
        return Rgba{first.r, first.g, first.b, first.a};
    }
    const std::size_t last = keys.size() - 1;
    if (!(x < keys[last].t)) {
        return Rgba{keys[last].r, keys[last].g, keys[last].b, keys[last].a};
    }
    std::size_t i = 0;
    while (i + 1 < last && x >= keys[i + 1].t) {
        ++i;
    }
    const GradientKeyF& k0 = keys[i];
    const GradientKeyF& k1 = keys[i + 1];
    const float f = (x - k0.t) / (k1.t - k0.t);
    return Rgba{k0.r + (k1.r - k0.r) * f, k0.g + (k1.g - k0.g) * f, k0.b + (k1.b - k0.b) * f,
                k0.a + (k1.a - k0.a) * f};
}

}  // namespace

// ---------------------------------------------------------------- Emitter

struct Simulation::Emitter {
    std::shared_ptr<const EmitterProgram> p;

    std::uint32_t count = 0;
    std::vector<float> f[kFieldCount];
    std::vector<std::uint32_t> survivors;  // scratch space for closing up after deaths

    // Emission bookkeeping. All of it restarts at the beginning of each pass
    // of the effect, so what a pass emits depends only on which pass it is.
    std::int64_t pass = -1;
    double carried = 0.0;        // fraction of a particle not yet emitted
    std::uint64_t spawned = 0;   // particles asked for in this pass
    float rateRandom = 0.0f;     // this pass's pick when the rate is a random range

    std::int64_t firstPass = 0;  // passes before this one are known to be over
    std::int64_t step = 0;
    std::uint64_t dropped = 0;

    explicit Emitter(std::shared_ptr<const EmitterProgram> program) : p(std::move(program)) {
        for (auto& field : f) {
            field.assign(p->capacity, 0.0f);
        }
        survivors.assign(p->capacity, 0u);
    }

    void restart(std::int64_t fromPass, std::int64_t atStep) {
        count = 0;
        pass = -1;
        carried = 0.0;
        spawned = 0;
        rateRandom = 0.0f;
        firstPass = fromPass;
        step = atStep;
        dropped = 0;
    }

    void seek(std::int64_t target);
    void advance();
    void emit(std::int64_t inPass, double a, double b, double base, double t0);
    void spawn(std::int64_t inPass, double offset, float progress);
};

void Simulation::Emitter::seek(std::int64_t target) {
    if (target == step) {
        return;
    }
    const EmitterProgram& e = *p;

    // Where could a from-scratch run begin and still be exact at the target?
    // Every particle alive at the target was born within one lifetime of it,
    // and each pass emits the same thing whatever came before. So in a
    // looping effect it is enough to begin at the pass that was running one
    // lifetime earlier. The margin covers rounding in a particle's age.
    std::int64_t fromPass = 0;
    std::int64_t fromStep = 0;
    if (e.loop && e.emits) {
        const double time = static_cast<double>(target) * e.step;
        const double margin = e.maxLifetime * 0.02 + 4.0 * e.step;
        const double earliest = time - e.maxLifetime - margin;
        if (earliest > e.effectDuration) {
            fromPass = static_cast<std::int64_t>(std::floor(earliest / e.effectDuration));
            const double passStart = static_cast<double>(fromPass) * e.effectDuration;
            fromStep = static_cast<std::int64_t>(std::floor(passStart / e.step)) - 1;
            if (fromStep < 0) {
                fromStep = 0;
            }
        }
    } else if (!e.emits) {
        fromStep = target;  // nothing is ever alive, so there is nothing to replay
    }

    // Carry on from where we are when that is the shorter road.
    if (target < step || step < fromStep) {
        restart(fromPass, fromStep);
    }
    while (step < target) {
        advance();
    }
}

void Simulation::Emitter::advance() {
    const EmitterProgram& e = *p;

    // Times come from the step number, never from adding steps up, so they
    // cannot drift however long the effect has been running.
    const double t0 = static_cast<double>(step) * e.step;
    const double t1 = static_cast<double>(step + 1) * e.step;

    // 1. Birth. A step may straddle the end of one pass and the start of the
    // next, so walk every pass the step touches.
    if (e.emits) {
        const double length = e.effectDuration;
        if (e.loop) {
            auto k = static_cast<std::int64_t>(std::floor(t0 / length));
            for (;;) {
                const double base = static_cast<double>(k) * length;
                const double end = base + length;
                const double a = (t0 > base ? t0 : base) - base;
                const double b = (t1 < end ? t1 : end) - base;
                if (b > a && k >= firstPass) {
                    emit(k, a, b, base, t0);
                }
                if (end >= t1) {
                    break;
                }
                ++k;
            }
        } else if (t0 < length) {
            emit(0, t0, t1 < length ? t1 : length, 0.0, t0);
        }
    }

    // 2. Motion. A particle born part-way through this step has a negative
    // age going in, and moves only for the part of the step it was alive.
    const float dt = static_cast<float>(e.step);
    const std::uint32_t n = count;
    float* x = f[kX].data();
    float* y = f[kY].data();
    float* z = f[kZ].data();
    float* vx = f[kVX].data();
    float* vy = f[kVY].data();
    float* vz = f[kVZ].data();
    float* age = f[kAge].data();
    float* life = f[kLife].data();
    float* rot = f[kRot].data();
    float* spin = f[kSpin].data();
    const float gx = e.gravityX, gy = e.gravityY, gz = e.gravityZ;
    const float drag = e.drag;

    if (e.spin.isCurve()) {
        for (std::uint32_t i = 0; i < n; ++i) {
            const float a1 = age[i] + dt;
            const float h = a1 < dt ? (a1 > 0.0f ? a1 : 0.0f) : dt;
            rot[i] += evalCurve(e.spin.keys, clamp01(a1 / life[i])) * h;
        }
    } else {
        for (std::uint32_t i = 0; i < n; ++i) {
            const float a1 = age[i] + dt;
            const float h = a1 < dt ? (a1 > 0.0f ? a1 : 0.0f) : dt;
            rot[i] += spin[i] * h;
        }
    }
    for (std::uint32_t i = 0; i < n; ++i) {
        const float a1 = age[i] + dt;
        const float h = a1 < dt ? (a1 > 0.0f ? a1 : 0.0f) : dt;
        // Gravity, then drag, then move. Drag divides rather than using an
        // exponential: it is stable at any strength and needs no maths library.
        const float damp = 1.0f / (1.0f + drag * h);
        const float nvx = (vx[i] + gx * h) * damp;
        const float nvy = (vy[i] + gy * h) * damp;
        const float nvz = (vz[i] + gz * h) * damp;
        vx[i] = nvx;
        vy[i] = nvy;
        vz[i] = nvz;
        x[i] += nvx * h;
        y[i] += nvy * h;
        z[i] += nvz * h;
        age[i] = a1;
    }

    // 3. Death. Survivors close up in order, so the oldest particle is always
    // first and the draw order never flickers.
    // First list who survives, then close up one field at a time: walking
    // sixteen arrays at once is far slower than walking each in turn.
    std::uint32_t* keep = survivors.data();
    std::uint32_t kept = 0;
    for (std::uint32_t i = 0; i < n; ++i) {
        if (age[i] < life[i]) {
            keep[kept++] = i;
        }
    }
    if (kept != n) {
        std::uint32_t first = 0;
        while (first < kept && keep[first] == first) {
            ++first;
        }
        for (auto& field : f) {
            float* data = field.data();
            for (std::uint32_t k = first; k < kept; ++k) {
                data[k] = data[keep[k]];
            }
        }
    }
    count = kept;
    ++step;
}

// Emits for the part [a, b) of one pass, measured in seconds from the pass's
// start. base is the pass's start in absolute time and t0 the step's start.
void Simulation::Emitter::emit(std::int64_t inPass, double a, double b, double base, double t0) {
    const EmitterProgram& e = *p;
    if (inPass != pass) {
        pass = inPass;
        carried = 0.0;
        spawned = 0;
        rateRandom = det::unitFloat(det::channel(
            det::particleKey(e.key, static_cast<std::uint64_t>(inPass), ~0ull), kChRate));
    }

    // Clip to the layer's own start and duration.
    const double ta = a - e.start > 0.0 ? a - e.start : 0.0;
    const double tb = b - e.start < e.duration ? b - e.start : e.duration;
    if (!(tb > ta)) {
        return;
    }
    const double layerStart = base + e.start;

    // Steady emission. Whole particles are released as the running total
    // crosses each whole number, spread evenly through the interval.
    const auto middle = static_cast<float>(((ta + tb) * 0.5) / e.duration);
    const double rate = static_cast<double>(sample(e.rate, rateRandom, middle));
    if (rate > 0.0) {
        carried += rate * (tb - ta);
        if (carried >= 1.0) {
            double whole = std::floor(carried);
            carried -= whole;
            if (whole > 1e15) {
                whole = 1e15;  // only reachable with data that skipped validation
            }
            const auto n = static_cast<std::uint64_t>(whole);
            // No point walking through particles there is no room for. They
            // still take their numbers, so the ones that fit are unaffected.
            const std::uint64_t room = e.capacity - count;
            const std::uint64_t fit = n < room ? n : room;
            const double inverse = 1.0 / static_cast<double>(n);
            for (std::uint64_t i = 0; i < fit; ++i) {
                const double at = ta + (static_cast<double>(i) + 0.5) * inverse * (tb - ta);
                spawn(inPass, (layerStart + at) - t0, static_cast<float>(at / e.duration));
            }
            spawned += n - fit;
            dropped += n - fit;
        }
    }

    // Bursts whose moment falls inside this interval.
    for (const BurstF& burst : e.bursts) {
        if (burst.time >= ta && burst.time < tb) {
            const double offset = (layerStart + burst.time) - t0;
            const auto progress = static_cast<float>(burst.time / e.duration);
            for (std::uint32_t i = 0; i < burst.count; ++i) {
                spawn(inPass, offset, progress);
            }
        }
    }
}

// offset is how far into the current step the particle is born, in seconds.
// progress is how far through the layer's duration that is, from 0 to 1.
void Simulation::Emitter::spawn(std::int64_t inPass, double offset, float progress) {
    const EmitterProgram& e = *p;

    // The index is taken even when the layer is full, so a dropped particle
    // never changes how the others look.
    const std::uint64_t index = spawned++;
    if (count >= e.capacity) {
        ++dropped;
        return;
    }
    const std::uint64_t key = det::particleKey(e.key, static_cast<std::uint64_t>(inPass), index);
    auto random = [key](Channel c) { return det::unitFloat(det::channel(key, c)); };

    const std::uint32_t i = count++;

    float lifetime = sample(e.lifetime, random(kChLifetime), progress);
    if (!(lifetime > 1e-4f)) {
        lifetime = 1e-4f;
    }
    const float speed = sample(e.speed, random(kChSpeed), progress);
    float size = sample(e.size, random(kChSize), progress);
    float alpha = e.color[3];
    if (e.hasOverLife) {
        if (!e.sizeOverLife.isCurve()) {
            size *= sample(e.sizeOverLife, random(kChSizeScale), 0.0f);
        }
        if (!e.opacityOverLife.isCurve()) {
            alpha *= sample(e.opacityOverLife, random(kChOpacityScale), 0.0f);
        }
    }

    // ---- where it appears, and which way it is sent before spread
    const det::Vec3f axis{e.dirX, e.dirY, e.dirZ};
    det::Vec3f pos{0.0f, 0.0f, 0.0f};
    det::Vec3f dir = axis;

    ShapeType shape = e.shape;
    if (e.flat) {
        // In 2D a ball is a disc and a box is a rectangle.
        if (shape == ShapeType::Sphere) shape = ShapeType::Circle;
        if (shape == ShapeType::Box) shape = ShapeType::Rectangle;
    }
    const float u = random(kChShapeU);
    const float v = random(kChShapeV);
    const float w = random(kChShapeW);

    switch (shape) {
        case ShapeType::Point:
            break;
        case ShapeType::Circle: {
            double s = 0.0, c = 1.0;
            det::sinCos(det::kTwoPi * static_cast<double>(u), s, c);
            const float r = e.fromEdge ? e.radius : e.radius * std::sqrt(v);
            pos = det::Vec3f{r * static_cast<float>(c), r * static_cast<float>(s), 0.0f};
            break;
        }
        case ShapeType::Rectangle: {
            const float sx = e.sizeX, sy = e.sizeY;
            if (!e.fromEdge) {
                pos = det::Vec3f{(u - 0.5f) * sx, (v - 0.5f) * sy, 0.0f};
            } else {
                // Walk the outline: bottom, right, top, left.
                float d = u * (2.0f * (sx + sy));
                if (d < sx) {
                    pos = det::Vec3f{d - 0.5f * sx, -0.5f * sy, 0.0f};
                } else if ((d -= sx) < sy) {
                    pos = det::Vec3f{0.5f * sx, d - 0.5f * sy, 0.0f};
                } else if ((d -= sy) < sx) {
                    pos = det::Vec3f{0.5f * sx - d, 0.5f * sy, 0.0f};
                } else {
                    d -= sx;
                    pos = det::Vec3f{-0.5f * sx, 0.5f * sy - (d < sy ? d : sy), 0.0f};
                }
            }
            break;
        }
        case ShapeType::Sphere: {
            // An even spread over the ball: even in height, even in angle,
            // and the cube root keeps the centre from being crowded.
            const float height = 1.0f - 2.0f * u;
            const float ring = std::sqrt(1.0f - height * height > 0.0f ? 1.0f - height * height : 0.0f);
            double s = 0.0, c = 1.0;
            det::sinCos(det::kTwoPi * static_cast<double>(v), s, c);
            const float r = e.fromEdge ? e.radius : e.radius * det::cbrt01(w);
            pos = det::Vec3f{r * ring * static_cast<float>(c), r * ring * static_cast<float>(s),
                             r * height};
            break;
        }
        case ShapeType::Box: {
            const float sx = e.sizeX, sy = e.sizeY, sz = e.sizeZ;
            const float xy = sx * sy, yz = sy * sz, xz = sx * sz;
            const float total = 2.0f * (xy + yz + xz);
            if (!e.fromEdge || !(total > 0.0f)) {
                pos = det::Vec3f{(u - 0.5f) * sx, (v - 0.5f) * sy, (w - 0.5f) * sz};
            } else {
                // Pick a face in proportion to its area, then a point on it.
                float t = w * total;
                const float a = u - 0.5f, b = v - 0.5f;
                if (t < xy) {
                    pos = det::Vec3f{a * sx, b * sy, 0.5f * sz};
                } else if ((t -= xy) < xy) {
                    pos = det::Vec3f{a * sx, b * sy, -0.5f * sz};
                } else if ((t -= xy) < yz) {
                    pos = det::Vec3f{0.5f * sx, a * sy, b * sz};
                } else if ((t -= yz) < yz) {
                    pos = det::Vec3f{-0.5f * sx, a * sy, b * sz};
                } else if ((t -= yz) < xz) {
                    pos = det::Vec3f{a * sx, 0.5f * sy, b * sz};
                } else {
                    pos = det::Vec3f{a * sx, -0.5f * sy, b * sz};
                }
            }
            break;
        }
        case ShapeType::Cone: {
            // Particles leave a disc across the direction and fan outward:
            // straight ahead at the centre, tilted by the cone angle at the rim.
            if (e.flat) {
                const float side = e.fromEdge ? (u < 0.5f ? -1.0f : 1.0f) : 2.0f * u - 1.0f;
                const det::Vec3f across{-axis.y, axis.x, 0.0f};
                pos = det::Vec3f{e.radius * side * across.x, e.radius * side * across.y, 0.0f};
                double s = 0.0, c = 1.0;
                det::sinCos(static_cast<double>(e.coneAngle * side), s, c);
                const auto sf = static_cast<float>(s), cf = static_cast<float>(c);
                dir = det::Vec3f{cf * axis.x + sf * across.x, cf * axis.y + sf * across.y, 0.0f};
            } else {
                det::Vec3f t, b;
                det::basis(axis, t, b);
                double s = 0.0, c = 1.0;
                det::sinCos(det::kTwoPi * static_cast<double>(u), s, c);
                const auto sf = static_cast<float>(s), cf = static_cast<float>(c);
                const det::Vec3f outward{cf * t.x + sf * b.x, cf * t.y + sf * b.y,
                                         cf * t.z + sf * b.z};
                const float fraction = e.fromEdge ? 1.0f : std::sqrt(v);
                const float r = e.radius * fraction;
                pos = det::Vec3f{r * outward.x, r * outward.y, r * outward.z};
                double ts = 0.0, tc = 1.0;
                det::sinCos(static_cast<double>(e.coneAngle * fraction), ts, tc);
                const auto tsf = static_cast<float>(ts), tcf = static_cast<float>(tc);
                dir = det::Vec3f{tcf * axis.x + tsf * outward.x, tcf * axis.y + tsf * outward.y,
                                 tcf * axis.z + tsf * outward.z};
            }
            break;
        }
    }

    // ---- spread: a random direction within the given angle of dir
    if (e.spread > 0.0f) {
        const float su = random(kChSpreadU);
        if (e.flat) {
            double s = 0.0, c = 1.0;
            det::sinCos(static_cast<double>(e.spread * (2.0f * su - 1.0f)), s, c);
            const auto sf = static_cast<float>(s), cf = static_cast<float>(c);
            dir = det::Vec3f{cf * dir.x - sf * dir.y, sf * dir.x + cf * dir.y, 0.0f};
        } else {
            // Even over the cap of the sphere, so no direction is favoured.
            const float cosTilt = 1.0f - su * (1.0f - e.cosSpread);
            const float sinTilt =
                std::sqrt(1.0f - cosTilt * cosTilt > 0.0f ? 1.0f - cosTilt * cosTilt : 0.0f);
            double s = 0.0, c = 1.0;
            det::sinCos(det::kTwoPi * static_cast<double>(random(kChSpreadV)), s, c);
            const auto sf = static_cast<float>(s), cf = static_cast<float>(c);
            det::Vec3f t, b;
            det::basis(dir, t, b);
            dir = det::normalizeOr(
                det::Vec3f{sinTilt * (cf * t.x + sf * b.x) + cosTilt * dir.x,
                           sinTilt * (cf * t.y + sf * b.y) + cosTilt * dir.y,
                           sinTilt * (cf * t.z + sf * b.z) + cosTilt * dir.z},
                dir);
        }
    }

    double clipped = offset;
    if (!(clipped > 0.0)) {
        clipped = 0.0;
    } else if (clipped > e.step) {
        clipped = e.step;
    }

    f[kX][i] = pos.x;
    f[kY][i] = pos.y;
    f[kZ][i] = e.flat ? 0.0f : pos.z;
    f[kVX][i] = dir.x * speed;
    f[kVY][i] = dir.y * speed;
    f[kVZ][i] = e.flat ? 0.0f : dir.z * speed;
    f[kAge][i] = -static_cast<float>(clipped);
    f[kLife][i] = lifetime;
    f[kSize][i] = size;
    f[kRot][i] = sample(e.rotation, random(kChRotation), progress);
    f[kSpin][i] = e.spin.isCurve() ? 0.0f : sample(e.spin, random(kChSpin), 0.0f);
    f[kR][i] = e.color[0];
    f[kG][i] = e.color[1];
    f[kB][i] = e.color[2];
    f[kA][i] = alpha;
    f[kPick][i] = random(kChFrame);
}

// ------------------------------------------------------------- Simulation

Simulation::Simulation(std::shared_ptr<const Program> program) : program_(std::move(program)) {
    if (!program_) {
        program_ = std::make_shared<Program>();
    }
    emitters_.reserve(program_->emitters.size());
    for (const auto& e : program_->emitters) {
        emitters_.emplace_back(e);
    }
}

Simulation::~Simulation() = default;
Simulation::Simulation(Simulation&&) noexcept = default;
Simulation& Simulation::operator=(Simulation&&) noexcept = default;

void Simulation::setProgram(std::shared_ptr<const Program> program) {
    if (!program) {
        program = std::make_shared<Program>();
    }
    std::vector<Emitter> next;
    next.reserve(program->emitters.size());
    for (const auto& e : program->emitters) {
        bool reused = false;
        for (auto& old : emitters_) {
            if (old.p == e) {
                next.push_back(std::move(old));
                old.p.reset();  // each state is handed over at most once
                reused = true;
                break;
            }
        }
        if (!reused) {
            next.emplace_back(e);
            next.back().seek(step_);
        }
    }
    emitters_ = std::move(next);
    program_ = std::move(program);
}

void Simulation::advance() {
    for (auto& e : emitters_) {
        e.advance();
    }
    ++step_;
}

void Simulation::seek(std::int64_t step) {
    if (step < 0) {
        step = 0;
    }
    for (auto& e : emitters_) {
        e.seek(step);
    }
    step_ = step;
}

std::uint32_t Simulation::aliveCount() const {
    std::uint32_t total = 0;
    for (const auto& e : emitters_) {
        total += e.count;
    }
    return total;
}

std::vector<EmitterStats> Simulation::stats() const {
    std::vector<EmitterStats> out;
    out.reserve(emitters_.size());
    for (const auto& e : emitters_) {
        out.push_back(EmitterStats{e.p->layer, e.count, e.p->capacity, e.dropped});
    }
    return out;
}

std::size_t Simulation::emitterCount() const { return emitters_.size(); }

ParticleView Simulation::particles(std::size_t emitter) const {
    ParticleView view;
    if (emitter >= emitters_.size()) {
        return view;
    }
    const Emitter& e = emitters_[emitter];
    view.count = e.count;
    view.x = e.f[kX].data();
    view.y = e.f[kY].data();
    view.z = e.f[kZ].data();
    view.vx = e.f[kVX].data();
    view.vy = e.f[kVY].data();
    view.vz = e.f[kVZ].data();
    view.age = e.f[kAge].data();
    view.lifetime = e.f[kLife].data();
    view.size = e.f[kSize].data();
    view.rotation = e.f[kRot].data();
    return view;
}

void Simulation::extract(RenderFrame& frame) const {
    frame.step = step_;
    frame.time = time();
    frame.flat = program_->flat;
    frame.batches.clear();

    std::size_t total = 0;
    for (const auto& e : emitters_) {
        if (e.p->drawn) {
            total += e.count;
        }
    }
    frame.instances.resize(total);

    std::uint32_t at = 0;
    for (const auto& em : emitters_) {
        const EmitterProgram& e = *em.p;
        if (!e.drawn) {
            continue;
        }
        RenderBatch batch;
        batch.layer = e.layer;
        batch.texture = e.texture;
        batch.blend = e.blend;
        batch.facing = e.facing;
        batch.glow = e.glow;
        batch.shape = e.spriteShape;
        batch.alongMotion = e.alongMotion;
        batch.stretch = e.stretch;
        batch.columns = e.columns;
        batch.rows = e.rows;
        batch.first = at;
        batch.count = em.count;
        frame.batches.push_back(batch);

        const bool sizeCurve = e.hasOverLife && e.sizeOverLife.isCurve();
        const bool opacityCurve = e.hasOverLife && e.opacityOverLife.isCurve();
        const bool tint = !e.colorOverLife.empty();
        const float* x = em.f[kX].data();
        const float* y = em.f[kY].data();
        const float* z = em.f[kZ].data();
        const float* vx = em.f[kVX].data();
        const float* vy = em.f[kVY].data();
        const float* vz = em.f[kVZ].data();
        const float* age = em.f[kAge].data();
        const float* life = em.f[kLife].data();
        const float* size = em.f[kSize].data();
        const float* rot = em.f[kRot].data();
        const float* r = em.f[kR].data();
        const float* g = em.f[kG].data();
        const float* b = em.f[kB].data();
        const float* a = em.f[kA].data();
        const float* pick = em.f[kPick].data();
        const int frames = e.frames;

        for (std::uint32_t i = 0; i < em.count; ++i) {
            SpriteInstance& out = frame.instances[at + i];
            const float lived = clamp01(age[i] / life[i]);
            out.x = x[i];
            out.y = y[i];
            out.z = z[i];
            out.vx = vx[i];
            out.vy = vy[i];
            out.vz = vz[i];
            out.size = sizeCurve ? size[i] * evalCurve(e.sizeOverLife.keys, lived) : size[i];
            out.rotation = rot[i] * kDegreesToRadiansF;
            out.r = r[i];
            out.g = g[i];
            out.b = b[i];
            out.a = opacityCurve ? a[i] * evalCurve(e.opacityOverLife.keys, lived) : a[i];
            if (tint) {
                const Rgba c = evalGradient(e.colorOverLife, lived);
                out.r *= c.r;
                out.g *= c.g;
                out.b *= c.b;
                out.a *= c.a;
            }
            int cell = 0;
            if (frames > 1) {
                const float n = static_cast<float>(frames);
                switch (e.animate) {
                    case Animate::Life:
                        cell = static_cast<int>(lived * n);
                        break;
                    case Animate::Random:
                        cell = static_cast<int>(pick[i] * n);
                        break;
                    case Animate::Loop: {
                        double played = static_cast<double>(age[i]) * e.fps;
                        if (e.randomStart) {
                            played += static_cast<double>(pick[i]) * frames;
                        }
                        cell = static_cast<int>(
                            static_cast<std::int64_t>(std::floor(played)) % frames);
                        break;
                    }
                }
                cell = cell < 0 ? 0 : (cell >= frames ? frames - 1 : cell);
            }
            out.frame = static_cast<float>(cell);
        }
        at += em.count;
    }
}

std::uint64_t Simulation::stateHash() const {
    std::uint64_t h = det::mix64(0x56465846ull ^ static_cast<std::uint64_t>(step_));
    for (const auto& e : emitters_) {
        h = det::mix64(h ^ e.p->layer.value);
        h = det::mix64(h ^ e.count);
        // The pass bookkeeping is part of the state: it decides what is
        // emitted next. Dropped counts are a statistic and are left out.
        h = det::mix64(h ^ static_cast<std::uint64_t>(e.pass));
        h = det::mix64(h ^ e.spawned);
        h = det::mix64(h ^ bits(e.carried));
        for (const auto& field : e.f) {
            const float* data = field.data();
            for (std::uint32_t i = 0; i < e.count; ++i) {
                h = det::mix64(h ^ bits(data[i]));
            }
        }
    }
    return h;
}

std::uint64_t hashFrame(const RenderFrame& frame) {
    std::uint64_t h = det::mix64(0x4652414Dull ^ static_cast<std::uint64_t>(frame.step));
    h = det::mix64(h ^ (frame.flat ? 1u : 0u));
    for (const auto& b : frame.batches) {
        h = det::mix64(h ^ b.layer.value);
        h = det::mix64(h ^ b.texture.value);
        h = det::mix64(h ^ static_cast<std::uint64_t>(b.blend));
        h = det::mix64(h ^ static_cast<std::uint64_t>(b.facing));
        h = det::mix64(h ^ bits(b.glow));
        h = det::mix64(h ^ static_cast<std::uint64_t>(b.shape));
        h = det::mix64(h ^ (b.alongMotion ? 1u : 0u));
        h = det::mix64(h ^ bits(b.stretch));
        h = det::mix64(h ^ static_cast<std::uint64_t>(b.columns * 4096 + b.rows));
        h = det::mix64(h ^ b.first);
        h = det::mix64(h ^ b.count);
    }
    for (const auto& s : frame.instances) {
        for (float v : {s.x, s.y, s.z, s.size, s.rotation, s.r, s.g, s.b, s.a, s.vx, s.vy, s.vz,
                        s.frame}) {
            h = det::mix64(h ^ bits(v));
        }
    }
    return h;
}

// --------------------------------------------------------------- Snapshot

Simulation::Snapshot::Snapshot() = default;
Simulation::Snapshot::~Snapshot() = default;
Simulation::Snapshot::Snapshot(const Snapshot&) = default;
Simulation::Snapshot& Simulation::Snapshot::operator=(const Snapshot&) = default;
Simulation::Snapshot::Snapshot(Snapshot&&) noexcept = default;
Simulation::Snapshot& Simulation::Snapshot::operator=(Snapshot&&) noexcept = default;

Simulation::Snapshot Simulation::snapshot() const {
    Snapshot s;
    s.program_ = program_;
    s.emitters_ = std::make_shared<const std::vector<Emitter>>(emitters_);
    s.step_ = step_;
    return s;
}

void Simulation::restore(const Snapshot& snapshot) {
    if (!snapshot.emitters_) {
        return;
    }
    program_ = snapshot.program_;
    emitters_ = *snapshot.emitters_;
    step_ = snapshot.step_;
}

}  // namespace vfx
