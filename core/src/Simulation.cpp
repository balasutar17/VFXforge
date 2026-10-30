#include "vfx/Simulation.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

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
    kChFrame,
    kChOpacity
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

// A second random number for a particle, from the one it was given at birth
// for picking a sprite-sheet picture. Used for the settings that came later
// (squash and stretch, speed over life), so that adding them did not add
// anything to what is stored for each particle.
inline float secondRandom(float pick, std::uint64_t salt) {
    return det::unitFloat(det::mix64(static_cast<std::uint64_t>(bits(pick)) ^ salt));
}
constexpr std::uint64_t kSaltEase = 0x45415345ull;
constexpr std::uint64_t kSaltStretch = 0x5351554153ull;

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

    // A held layer is shown as it was when its current drawing was taken
    // (see heldDrawing), so that moment's particles are kept beside the
    // live ones. Unused, and empty, in a layer that is not held.
    std::uint32_t heldCount = 0;
    std::int64_t heldStep = 0;
    std::vector<float> held[kFieldCount];

    // Sub emitters (see EmitterProgram::subParent). A parent records what happens to its particles in
    // `events`; a sub emitter runs a private copy of its parent alongside itself and releases its own
    // particles where those events happened.
    struct Event {
        float x, y, z, vx, vy, vz, r, g, b, a;
        double offset;  // seconds into the step it happened
    };
    std::vector<Event> events;
    int recordMask = 0;  // 1 births, 2 deaths, 4 bounces
    std::vector<Emitter> shadow;  // empty, or one private copy of the parent (copies along with this emitter)
    const Event* origin = nullptr;  // while a sub emitter's particle is being made

    void note(int kind, std::uint32_t i, double offset) {
        events.push_back(Event{f[kX][i], f[kY][i], f[kZ][i], f[kVX][i], f[kVY][i], f[kVZ][i], f[kR][i], f[kG][i],
                               f[kB][i], f[kA][i], offset});
        (void)kind;
    }

    explicit Emitter(std::shared_ptr<const EmitterProgram> program) : p(std::move(program)) {
        if (p->subParent) {
            shadow.emplace_back(p->subParent);
            shadow[0].recordMask = p->subTrigger;
        }
        for (auto& field : f) {
            field.assign(p->capacity, 0.0f);
        }
        survivors.assign(p->capacity, 0u);
        if (p->hold > 0.0f) {
            for (auto& field : held) {
                field.assign(p->capacity, 0.0f);
            }
        }
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
        heldCount = 0;
        heldStep = atStep;
        events.clear();
        if (!shadow.empty()) {
            shadow[0].restart(fromPass, atStep);
        }
    }

    void emitFromEvents(double t0);
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
        // A held layer shows the particles of a moment up to one drawing
        // back, so that moment must be exact too.
        const double heldBack = e.hold > 0.0f ? 1.0 / static_cast<double>(e.hold) + e.step : 0.0;
        const double earliest = time - e.maxLifetime - margin - heldBack;
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
    if (!shadow.empty()) {
        shadow[0].events.clear();
        shadow[0].advance();
        emitFromEvents(t0);
    } else if (e.emits) {
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
    if (e.spinBySpeed != 0.0f) {
        const float range = e.speedMax - e.speedMin;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float a1 = age[i] + dt;
            const float h = a1 < dt ? (a1 > 0.0f ? a1 : 0.0f) : dt;
            const float sp = std::sqrt(vx[i] * vx[i] + vy[i] * vy[i] + vz[i] * vz[i]);
            rot[i] += e.spinBySpeed * clamp01((sp - e.speedMin) / range) * h;
        }
    }
    if (e.hasForce) {
        // A push on top of gravity, scaled across the particle's life.
        for (std::uint32_t i = 0; i < n; ++i) {
            const float a1 = age[i] + dt;
            const float h = a1 < dt ? (a1 > 0.0f ? a1 : 0.0f) : dt;
            const float m = e.forceRamp.isCurve() ? evalCurve(e.forceRamp.keys, clamp01(a1 / life[i]))
                                                  : e.forceRamp.a;
            vx[i] += e.forceX * m * h;
            vy[i] += e.forceY * m * h;
            vz[i] += e.forceZ * m * h;
        }
    }
    if (e.turbulence > 0.0f) {
        // Carried along by the swirls as they were, where the particle was,
        // at the start of the step. Done before the particle's own movement
        // so that the step can be undone exactly (a trail does that).
        const std::uint64_t field = det::mix64(e.key ^ 0x5457495354ull);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float a1 = age[i] + dt;
            const float h = a1 < dt ? (a1 > 0.0f ? a1 : 0.0f) : dt;
            const det::Vec3f w = det::turbulence(field, e.turbulencePerUnit, e.turbulenceChange, e.flat, x[i], y[i],
                                                 z[i], t0);
            x[i] += w.x * e.turbulence * h;
            y[i] += w.y * e.turbulence * h;
            z[i] += w.z * e.turbulence * h;
        }
    }
    if (!e.eases) {
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
    } else {
        // The same, with the distance covered multiplied by Speed over Life
        // as it stands at the end of the step. The particle's speed itself
        // is left alone, so gravity and drag act as they always do.
        const float* pick = f[kPick].data();
        const ScalarF& ease = e.speedOverLife;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float a1 = age[i] + dt;
            const float h = a1 < dt ? (a1 > 0.0f ? a1 : 0.0f) : dt;
            const float damp = 1.0f / (1.0f + drag * h);
            const float nvx = (vx[i] + gx * h) * damp;
            const float nvy = (vy[i] + gy * h) * damp;
            const float nvz = (vz[i] + gz * h) * damp;
            vx[i] = nvx;
            vy[i] = nvy;
            vz[i] = nvz;
            const float m = ease.isCurve() ? evalCurve(ease.keys, clamp01(a1 / life[i]))
                                           : sample(ease, secondRandom(pick[i], kSaltEase), 0.0f);
            const float go = m * h;
            x[i] += nvx * go;
            y[i] += nvy * go;
            z[i] += nvz * go;
            age[i] = a1;
        }
    }
    if (e.driftX != 0.0f || e.driftY != 0.0f || e.driftZ != 0.0f) {
        for (std::uint32_t i = 0; i < n; ++i) {
            const float a1 = age[i];
            const float h = a1 < dt ? (a1 > 0.0f ? a1 : 0.0f) : dt;
            x[i] += e.driftX * h;
            y[i] += e.driftY * h;
            z[i] += e.driftZ * h;
        }
    }
    if (e.speedLimit > 0.0f) {
        const float pull = e.speedLimitDampen * std::min(1.0f, dt * 12.0f);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float sp = std::sqrt(vx[i] * vx[i] + vy[i] * vy[i] + vz[i] * vz[i]);
            if (sp > e.speedLimit) {
                const float k = 1.0f - pull * (1.0f - e.speedLimit / sp);
                vx[i] *= k;
                vy[i] *= k;
                vz[i] *= k;
            }
        }
    }
    if (e.orbit != 0.0f) {
        // Turned about the layer's centre, position and heading together,
        // after the particle's own movement. A whole step's turn was worked
        // out once; a particle born part-way through the step turns less.
        const float cx = e.offsetX, cy = e.offsetY, cz = e.offsetZ;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float a1 = age[i];
            float sn = e.orbitSin, cs = e.orbitCos;
            if (a1 < dt) {
                double s = 0.0, c = 1.0;
                det::sinCos(static_cast<double>(e.orbit) * static_cast<double>(a1 > 0.0f ? a1 : 0.0f), s, c);
                sn = static_cast<float>(s);
                cs = static_cast<float>(c);
            }
            if (e.ground) {
                // About the upright: counter-clockwise seen from above.
                const float rx = x[i] - cx, rz = z[i] - cz;
                x[i] = cx + (rx * cs + rz * sn);
                z[i] = cz + (rz * cs - rx * sn);
                const float wx = vx[i], wz = vz[i];
                vx[i] = wx * cs + wz * sn;
                vz[i] = wz * cs - wx * sn;
                continue;
            }
            const float rx = x[i] - cx, ry = y[i] - cy;
            x[i] = cx + (rx * cs - ry * sn);
            y[i] = cy + (rx * sn + ry * cs);
            const float wx = vx[i], wy = vy[i];
            vx[i] = wx * cs - wy * sn;
            vy[i] = wx * sn + wy * cs;
        }
    }

    if (e.collide) {
        // The floor: a particle that has gone through it is put back above it and sent up again,
        // keeping `bounce` of its downward speed and losing `collDampen` of all its speed.
        for (std::uint32_t i = 0; i < n; ++i) {
            if (age[i] > 0.0f && y[i] < e.floorY && vy[i] < 0.0f) {
                y[i] = e.floorY + (e.floorY - y[i]) * e.bounce;
                const float keep = 1.0f - e.collDampen;
                vx[i] *= keep;
                vz[i] *= keep;
                vy[i] = -vy[i] * e.bounce * keep;
                if (recordMask & 4) {
                    note(4, i, static_cast<double>(dt));
                }
                age[i] += e.collLifeLoss * life[i];
                if (e.collMinSpeed > 0.0f &&
                    vx[i] * vx[i] + vy[i] * vy[i] + vz[i] * vz[i] < e.collMinSpeed * e.collMinSpeed) {
                    age[i] = life[i];  // too slow to bounce on: gone
                }
            }
        }
    }
    if (e.zone != 0) {
        const float hx = e.zoneSX * 0.5f, hy = e.zoneSY * 0.5f, hz = e.zoneSZ * 0.5f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const bool inside = std::fabs(x[i] - e.zoneX) <= hx && std::fabs(y[i] - e.zoneY) <= hy &&
                                std::fabs(z[i] - e.zoneZ) <= hz;
            if (age[i] > 0.0f && (e.zone == 1 ? inside : !inside)) {
                age[i] = life[i];
            }
        }
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
        } else if (recordMask & 2) {
            note(2, i, static_cast<double>(dt));
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

    // 4. A held layer keeps the first moment of each of its drawings.
    if (e.hold > 0.0f && heldDrawing(e, step) != heldDrawing(e, step - 1)) {
        for (std::size_t k = 0; count > 0 && k < kFieldCount; ++k) {
            std::memcpy(held[k].data(), f[k].data(), static_cast<std::size_t>(count) * sizeof(float));
        }
        heldCount = count;
        heldStep = step;
    }
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
    // Only drawn when it is not the plain 1 every older effect has, so that
    // nothing about those changes.
    if (!(e.opacity.mode == ScalarF::Mode::Constant && e.opacity.a == 1.0f)) {
        alpha *= clamp01(sample(e.opacity, random(kChOpacity), progress));
    }
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
    float pathAngle = 0.0f;  // degrees: which way the path runs where the particle is born

    switch (shape) {
        case ShapeType::Path: {
            // Where along the path: anywhere, near a running head, or behind
            // the head of a path being drawn on. Spread evenly rather than at
            // random: each particle a golden-ratio step on from the one
            // before, so a line of many short-lived particles has no gaps.
            const float even = static_cast<float>((index * 0x9E3779B97F4A7C15ull) >> 40) * (1.0f / 16777216.0f);
            float along = even;
            if (e.travel != 0.0f) {
                const float time = progress * static_cast<float>(e.duration);
                if (e.travelDraw) {
                    float reached = std::fabs(e.travel) * time;
                    reached = reached < 1.0f ? reached : 1.0f;
                    along = e.travel > 0.0f ? even * reached : 1.0f - even * reached;
                } else {
                    const int head = static_cast<int>(v * static_cast<float>(e.travelHeads));
                    float at = e.travel * time + static_cast<float>(head < e.travelHeads ? head : e.travelHeads - 1) /
                                                      static_cast<float>(e.travelHeads);
                    at -= std::floor(at);
                    along = e.travel > 0.0f ? at - e.travelSpan * even : at + e.travelSpan * even;
                    along -= std::floor(along);
                    if (!e.pathClosed) {
                        // An open line is run along from end to end, not round.
                        along = along < 0.0f ? 0.0f : (along > 1.0f ? 1.0f : along);
                    }
                }
            }
            const float target = along * e.pathTotal;
            // The segment it falls on.
            std::size_t lo = 1, hi = e.pathCum.size() - 1;
            while (lo < hi) {
                const std::size_t mid = (lo + hi) / 2;
                if (e.pathCum[mid] < target) {
                    lo = mid + 1;
                } else {
                    hi = mid;
                }
            }
            const std::size_t j = lo, k = lo - 1;
            const float segment = e.pathCum[j] - e.pathCum[k];
            const float t = segment > 0.0f ? (target - e.pathCum[k]) / segment : 0.0f;
            const float dx = e.pathX[j] - e.pathX[k], dy = e.pathY[j] - e.pathY[k];
            const float len = std::sqrt(dx * dx + dy * dy);
            const float tx = len > 0.0f ? dx / len : 1.0f, ty = len > 0.0f ? dy / len : 0.0f;
            const float nx = -ty * e.pathOutward, ny = tx * e.pathOutward;
            pos = det::Vec3f{e.pathX[k] + dx * t, e.pathY[k] + dy * t, 0.0f};
            if (!e.fromEdge && e.pathClosed) {
                // Inside the outline: points in its box, kept when inside.
                // A few tries, then the outline itself.
                for (std::uint64_t attempt = 0; attempt < 24; ++attempt) {
                    const std::uint64_t h = det::mix64(key ^ (0x50415448ull + attempt * 0x9E3779B97F4A7C15ull));
                    const float px = e.pathMinX + (e.pathMaxX - e.pathMinX) * det::unitFloat(h);
                    const float py = e.pathMinY + (e.pathMaxY - e.pathMinY) * det::unitFloat(det::mix64(h));
                    bool inside = false;
                    for (std::size_t a = 0, b = e.pathX.size() - 1; a < e.pathX.size(); b = a++) {
                        if ((e.pathY[a] > py) != (e.pathY[b] > py) &&
                            px < (e.pathX[b] - e.pathX[a]) * (py - e.pathY[a]) / (e.pathY[b] - e.pathY[a]) + e.pathX[a]) {
                            inside = !inside;
                        }
                    }
                    if (inside) {
                        pos = det::Vec3f{px, py, 0.0f};
                        break;
                    }
                }
            } else if (e.pathThickness > 0.0f) {
                const float across = (w - 0.5f) * e.pathThickness;
                pos = det::Vec3f{pos.x + nx * across, pos.y + ny * across, 0.0f};
            }
            if (e.aim == 1) {
                dir = det::Vec3f{nx, ny, 0.0f};
            } else if (e.aim == 2) {
                dir = e.travel < 0.0f ? det::Vec3f{-tx, -ty, 0.0f} : det::Vec3f{tx, ty, 0.0f};
            }
            pathAngle = static_cast<float>(det::atan2(static_cast<double>(ty), static_cast<double>(tx)) * 57.29577951308232);
            if (e.ground) {
                pos = det::Vec3f{pos.x, 0.0f, -pos.y};
                if (e.aim != 0) {
                    dir = det::Vec3f{dir.x, 0.0f, -dir.y};
                }
            }
            break;
        }
        case ShapeType::Point:
            break;
        case ShapeType::Mesh: {
            const float time = progress * static_cast<float>(e.duration);
            // A tiny sideways scatter, so a fixed cloud of points never shows as dots.
            auto scatter = [&](det::Vec3f at, det::Vec3f normal, float amount) {
                det::Vec3f t, b;
                det::basis(normal, t, b);
                const float a = (random(kChShapeW) - 0.5f) * amount, c = (det::unitFloat(det::mix64(key ^ 0x4d455348ull)) - 0.5f) * amount;
                return det::Vec3f{at.x + t.x * a + b.x * c, at.y + t.y * a + b.y * c, at.z + t.z * a + b.z * c};
            };
            // Even coverage: the points a layer picks follow a low-discrepancy sequence in the
            // order they are born, so any stretch of time has them spread along the whole edge
            // or surface instead of clumped (random picks showed as beads and blotches).
            double spreadPos = 0.5 + 0.6180339887498949 * static_cast<double>(index & 0xffffffffull) +
                               static_cast<double>(det::unitFloat(det::mix64(e.key ^ static_cast<std::uint64_t>(inPass * 977 + 13))));
            spreadPos -= std::floor(spreadPos);
            const float ud = static_cast<float>(spreadPos);
            if (e.meshEdges) {
                const std::size_t n = e.segCum.size();
                const float target = ud * e.segCum[n - 1];
                std::size_t lo = 0, hi = n - 1;
                while (lo < hi) {
                    const std::size_t mid = (lo + hi) / 2;
                    if (e.segCum[mid] < target) {
                        lo = mid + 1;
                    } else {
                        hi = mid;
                    }
                }
                const float start = lo > 0 ? e.segCum[lo - 1] : 0.0f;
                const float len = e.segCum[lo] - start;
                const float t = len > 0.0f ? (target - start) / len : 0.0f;
                pos = det::Vec3f{e.segAx[lo] + (e.segBx[lo] - e.segAx[lo]) * t, e.segAy[lo] + (e.segBy[lo] - e.segAy[lo]) * t,
                                 e.segAz[lo] + (e.segBz[lo] - e.segAz[lo]) * t};
                const det::Vec3f out = det::normalizeOr(det::Vec3f{pos.x - e.meshCx, pos.y - e.meshCy, pos.z - e.meshCz}, axis);
                pos = det::Vec3f{pos.x + out.x * e.meshOffset, pos.y + out.y * e.meshOffset, pos.z + out.z * e.meshOffset};
                if (e.aim == 1) {
                    dir = out;
                } else if (e.aim == 2) {
                    dir = det::normalizeOr(det::Vec3f{e.segBx[lo] - e.segAx[lo], e.segBy[lo] - e.segAy[lo], e.segBz[lo] - e.segAz[lo]}, axis);
                }
            } else {
                const std::size_t n = e.meshKey.size();
                std::size_t lo = 0, hi = n;
                if (e.scanMode != 0) {
                    const float range = e.meshKeyMax - e.meshKeyMin;
                    float phase = e.scanSpeed * time;
                    phase -= std::floor(phase);
                    const int band = static_cast<int>(v * static_cast<float>(e.scanBands)) < e.scanBands
                                         ? static_cast<int>(v * static_cast<float>(e.scanBands)) : e.scanBands - 1;
                    float at = phase + static_cast<float>(band) / static_cast<float>(e.scanBands);
                    at -= std::floor(at);
                    const float centre = e.meshKeyMin + at * range;
                    const float half = 0.5f * e.scanWidth;
                    lo = static_cast<std::size_t>(std::lower_bound(e.meshKey.begin(), e.meshKey.end(), centre - half) - e.meshKey.begin());
                    hi = static_cast<std::size_t>(std::upper_bound(e.meshKey.begin(), e.meshKey.end(), centre + half) - e.meshKey.begin());
                    if (hi <= lo) {
                        // Between two points: the nearest one, so a thin band never goes dark.
                        lo = lo < n ? lo : n - 1;
                        hi = lo + 1;
                    }
                }
                std::size_t pick = lo + static_cast<std::size_t>(ud * static_cast<float>(hi - lo));
                pick = pick < hi ? pick : hi - 1;
                const det::Vec3f normal{e.meshNx[pick], e.meshNy[pick], e.meshNz[pick]};
                pos = det::Vec3f{e.meshPx[pick] + normal.x * e.meshOffset, e.meshPy[pick] + normal.y * e.meshOffset,
                                 e.meshPz[pick] + normal.z * e.meshOffset};
                pos = scatter(pos, normal, e.meshSpacing);
                if (e.aim == 1) {
                    dir = normal;
                } else if (e.aim == 2) {
                    // Along the band: level with the ground for a height scan, round the origin for a ripple.
                    det::Vec3f g = e.scanMode == 2 ? det::Vec3f{pos.x - e.scanOx, pos.y - e.scanOy, pos.z - e.scanOz} : det::Vec3f{0.0f, 1.0f, 0.0f};
                    const float gn = g.x * normal.x + g.y * normal.y + g.z * normal.z;
                    g = det::Vec3f{g.x - normal.x * gn, g.y - normal.y * gn, g.z - normal.z * gn};
                    const det::Vec3f c{normal.y * g.z - normal.z * g.y, normal.z * g.x - normal.x * g.z, normal.x * g.y - normal.y * g.x};
                    if (c.x * c.x + c.y * c.y + c.z * c.z < 1e-4f) {
                        det::Vec3f t, b;  // a surface facing straight up has no level direction: any along it will do
                        det::basis(normal, t, b);
                        dir = t;
                    } else {
                        dir = det::normalizeOr(c, axis);
                    }
                }
            }
            break;
        }
        case ShapeType::Circle: {
            double s = 0.0, c = 1.0;
            det::sinCos(det::kTwoPi * static_cast<double>(u), s, c);
            const float r = e.fromEdge ? e.radius : e.radius * std::sqrt(v);
            pos = det::Vec3f{r * static_cast<float>(c), r * static_cast<float>(s), 0.0f};
            if (e.ground) {
                pos = det::Vec3f{pos.x, 0.0f, -pos.y};
            }
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
            if (e.ground) {
                pos = det::Vec3f{pos.x, 0.0f, -pos.y};
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

    if (e.offset) {
        // Only when set, so effects without one keep their exact bits.
        pos = det::Vec3f{pos.x + e.offsetX, pos.y + e.offsetY, pos.z + e.offsetZ};
    }
    if (origin) {
        pos = det::Vec3f{pos.x + origin->x, pos.y + origin->y, pos.z + origin->z};
        dir = det::Vec3f{dir.x, dir.y, dir.z};
    }
    f[kX][i] = pos.x;
    f[kY][i] = pos.y;
    f[kZ][i] = e.flat ? 0.0f : pos.z;
    const float carry = origin ? e.subInheritMotion : 0.0f;
    f[kVX][i] = dir.x * speed + (origin ? origin->vx * carry : 0.0f);
    f[kVY][i] = dir.y * speed + (origin ? origin->vy * carry : 0.0f);
    f[kVZ][i] = e.flat ? 0.0f : dir.z * speed + (origin ? origin->vz * carry : 0.0f);
    f[kAge][i] = -static_cast<float>(clipped);
    f[kLife][i] = lifetime;
    f[kSize][i] = size;
    f[kRot][i] = sample(e.rotation, random(kChRotation), progress) + (e.pathAlign ? pathAngle : 0.0f);
    f[kSpin][i] = e.spin.isCurve() ? 0.0f : sample(e.spin, random(kChSpin), 0.0f);
    const bool tinted = origin && e.subInheritColor;
    f[kR][i] = tinted ? e.color[0] * origin->r : e.color[0];
    f[kG][i] = tinted ? e.color[1] * origin->g : e.color[1];
    f[kB][i] = tinted ? e.color[2] * origin->b : e.color[2];
    f[kA][i] = tinted ? alpha * origin->a : alpha;
    f[kPick][i] = random(kChFrame);
    if (recordMask & 1) {
        note(1, i, clipped);
    }
}

// A sub emitter's births: wherever its parent's particles were born, died or bounced during the step
// that has just been run, `subCount` of its own, each event deciding by chance whether it counts.
void Simulation::Emitter::emitFromEvents(double t0) {
    const EmitterProgram& e = *p;
    const double length = e.effectDuration;
    for (const Event& event : shadow[0].events) {
        const double when = t0 + event.offset;
        std::int64_t inPass = 0;
        if (e.loop) {
            inPass = static_cast<std::int64_t>(std::floor(when / length));
            if (inPass < firstPass) {
                continue;
            }
        } else if (!(when < length)) {
            continue;
        }
        const double inside = when - static_cast<double>(inPass) * length;
        if (inside < e.start || inside >= e.start + e.duration) {
            continue;
        }
        if (inPass != pass) {
            pass = inPass;
            carried = 0.0;
            spawned = 0;
            rateRandom = 0.0f;
        }
        if (e.subChance < 1.0f) {
            const std::uint64_t key = det::particleKey(e.key, static_cast<std::uint64_t>(inPass), spawned);
            if (!(det::unitFloat(det::mix64(key ^ 0x4348414e4345ull)) < e.subChance)) {
                spawned += static_cast<std::uint64_t>(e.subCount);
                continue;
            }
        }
        origin = &event;
        const auto progress = static_cast<float>((inside - e.start) / e.duration);
        for (int k = 0; k < e.subCount; ++k) {
            spawn(inPass, event.offset, progress);
        }
        origin = nullptr;
    }
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

    // A held layer is drawn from the particles kept when its current
    // drawing was taken; every other layer from the live ones.
    std::size_t total = 0;
    for (const auto& e : emitters_) {
        if (e.p->drawn) {
            total += e.p->hold > 0.0f ? e.heldCount : e.count;
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
        batch.tilt = e.flat ? e.tilt : 0.0f;
        batch.columns = e.columns;
        batch.rows = e.rows;
        batch.dissolves = e.dissolve > 0.0f;
        batch.dissolveEdge = batch.dissolves ? e.dissolveEdge : 0.0f;
        batch.dissolveSoft = batch.dissolves ? e.dissolveSoft : 0.0f;
        batch.trail = e.trail;
        batch.trailWidth = e.trailWidth;
        batch.gravityX = e.gravityX + e.forceX * (e.forceRamp.isCurve() ? 1.0f : e.forceRamp.a);
        batch.gravityY = e.gravityY + e.forceY * (e.forceRamp.isCurve() ? 1.0f : e.forceRamp.a);
        batch.gravityZ = e.flat ? 0.0f : e.gravityZ + e.forceZ * (e.forceRamp.isCurve() ? 1.0f : e.forceRamp.a);
        batch.drag = e.drag;
        batch.step = static_cast<float>(e.step);
        batch.turbulence = e.turbulence;
        batch.turbulencePerUnit = e.turbulencePerUnit;
        batch.turbulenceChange = e.turbulenceChange;
        batch.turbulenceKey = e.turbulence > 0.0f ? det::mix64(e.key ^ 0x5457495354ull) : 0;
        const bool isHeld = e.hold > 0.0f;
        const auto& from = isHeld ? em.held : em.f;
        const std::uint32_t shown = isHeld ? em.heldCount : em.count;
        batch.squashStretches = e.hasOverLife && e.squashStretches;
        batch.eases = e.eases;
        batch.orbit = e.orbit;
        batch.orbitSin = e.orbitSin;
        batch.orbitCos = e.orbitCos;
        batch.centerX = e.offsetX;
        batch.centerY = e.offsetY;
        batch.centerZ = e.offsetZ;
        batch.ground = e.ground;
        batch.hold = e.hold;
        batch.shapeEdit = e.shapeEdit;
        batch.shownStep = isHeld ? em.heldStep : step_;
        batch.program = em.p;
        batch.first = at;
        batch.count = shown;
        frame.batches.push_back(std::move(batch));

        const bool sizeCurve = e.hasOverLife && e.sizeOverLife.isCurve();
        const bool opacityCurve = e.hasOverLife && e.opacityOverLife.isCurve();
        const bool tint = !e.colorOverLife.empty();
        const bool bySpeedTint = !e.colorBySpeed.empty();
        const bool bySpeedSize = e.hasSizeBySpeed;
        const float* x = from[kX].data();
        const float* y = from[kY].data();
        const float* z = from[kZ].data();
        const float* vx = from[kVX].data();
        const float* vy = from[kVY].data();
        const float* vz = from[kVZ].data();
        const float* age = from[kAge].data();
        const float* life = from[kLife].data();
        const float* size = from[kSize].data();
        const float* rot = from[kRot].data();
        const float* r = from[kR].data();
        const float* g = from[kG].data();
        const float* b = from[kB].data();
        const float* a = from[kA].data();
        const float* pick = from[kPick].data();
        const bool stretches = e.hasOverLife && e.squashStretches;
        const int frames = e.frames;

        for (std::uint32_t i = 0; i < shown; ++i) {
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
            if (bySpeedTint || bySpeedSize) {
                const float sp = std::sqrt(vx[i] * vx[i] + vy[i] * vy[i] + vz[i] * vz[i]);
                const float s01 = clamp01((sp - e.speedMin) / (e.speedMax - e.speedMin));
                if (bySpeedSize) {
                    out.size *= e.sizeBySpeed.isCurve() ? evalCurve(e.sizeBySpeed.keys, s01) : e.sizeBySpeed.a;
                }
                if (bySpeedTint) {
                    const Rgba c = evalGradient(e.colorBySpeed, s01);
                    out.r *= c.r;
                    out.g *= c.g;
                    out.b *= c.b;
                    out.a *= c.a;
                }
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
            out.age = age[i];
            if (e.dissolve > 0.0f) {
                // Whole until the last share of its life, then steadily away.
                out.dissolve = clamp01((lived - (1.0f - e.dissolve)) / e.dissolve);
                out.seed = pick[i];
            } else {
                out.dissolve = 0.0f;
                out.seed = 0.0f;
            }
            out.squashStretch =
                !stretches ? 0.0f
                : e.squashStretch.isCurve() ? evalCurve(e.squashStretch.keys, lived)
                                            : sample(e.squashStretch, secondRandom(pick[i], kSaltStretch), 0.0f);
            if (e.eases) {
                out.ease = e.speedOverLife.isCurve()
                               ? evalCurve(e.speedOverLife.keys, lived)
                               : sample(e.speedOverLife, secondRandom(pick[i], kSaltEase), 0.0f);
                out.life = life[i];
            } else {
                out.ease = 1.0f;
                out.life = 0.0f;
            }
        }
        at += shown;
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
        if (b.trail > 0.0f) {
            // Mixed in only when there is a trail, so frames without one keep
            // the fingerprints they always had.
            h = det::mix64(h ^ bits(b.trail));
            h = det::mix64(h ^ bits(b.trailWidth));
        }
        if (b.tilt > 0.0f) {
            // Likewise: only a tilted layer changes its fingerprint.
            h = det::mix64(h ^ 0x54494C54ull ^ bits(b.tilt));
        }
        if (b.turbulence > 0.0f) {
            h = det::mix64(h ^ 0x54555242ull ^ bits(b.turbulence));
            h = det::mix64(h ^ bits(b.turbulencePerUnit));
            h = det::mix64(h ^ bits(b.turbulenceChange));
        }
        if (b.orbit != 0.0f) {
            h = det::mix64(h ^ 0x4F524249ull ^ bits(b.orbit));
        }
        if (b.ground) {
            // Only a layer on the ground changes its fingerprint.
            h = det::mix64(h ^ 0x47524E44ull);
        }
        if (b.hold > 0.0f) {
            h = det::mix64(h ^ 0x484F4C44ull ^ bits(b.hold));
            h = det::mix64(h ^ static_cast<std::uint64_t>(b.shownStep));
        }
        h = det::mix64(h ^ b.first);
        h = det::mix64(h ^ b.count);
    }
    for (const auto& s : frame.instances) {
        for (float v : {s.x, s.y, s.z, s.size, s.rotation, s.r, s.g, s.b, s.a, s.vx, s.vy, s.vz,
                        s.frame}) {
            h = det::mix64(h ^ bits(v));
        }
    }
    // Mixed in only for layers that dissolve, so every other frame keeps
    // the fingerprint it always had.
    for (const auto& b : frame.batches) {
        if (!b.dissolves) {
            continue;
        }
        h = det::mix64(h ^ 0x44495353ull ^ b.first);
        h = det::mix64(h ^ bits(b.dissolveEdge));
        h = det::mix64(h ^ bits(b.dissolveSoft));
        for (std::uint32_t i = 0; i < b.count; ++i) {
            const SpriteInstance& s = frame.instances[b.first + i];
            h = det::mix64(h ^ bits(s.dissolve));
            h = det::mix64(h ^ bits(s.seed));
        }
    }
    // And likewise for layers that squash and stretch, or ease.
    for (const auto& b : frame.batches) {
        if (!b.squashStretches && !b.eases) {
            continue;
        }
        h = det::mix64(h ^ 0x5052494Eull ^ b.first);
        for (std::uint32_t i = 0; i < b.count; ++i) {
            const SpriteInstance& s = frame.instances[b.first + i];
            if (b.squashStretches) {
                h = det::mix64(h ^ bits(s.squashStretch));
            }
            if (b.eases) {
                h = det::mix64(h ^ bits(s.ease));
            }
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
