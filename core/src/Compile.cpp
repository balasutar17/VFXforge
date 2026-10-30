#include "vfx/Program.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "vfx/DetMath.h"
#include "vfx/MeshData.h"
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

std::int64_t heldDrawing(const EmitterProgram& e, std::int64_t step) {
    if (step <= 0) {
        return -1;  // before anything has moved: a drawing of its own
    }
    // The particles at a step are the outcome of the step before it, which
    // began at this time.
    const double began = static_cast<double>(step - 1) * e.step;
    std::int64_t pass = 0;
    double within = began;
    if (e.loop) {
        pass = static_cast<std::int64_t>(std::floor(began / e.effectDuration));
        within = began - static_cast<double>(pass) * e.effectDuration;
    }
    // The small allowance keeps a moment that falls exactly on a change
    // from landing on either side of it by rounding.
    const auto drawing = static_cast<std::int64_t>(std::floor(within * static_cast<double>(e.hold) + 1e-6));
    return pass * (std::int64_t{1} << 20) + drawing;
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
            {
                // Smooth points are turned into short straight pieces here,
                // so everything that plays a curve only ever meets corners.
                const std::vector<CurveKey> flat = flattenCurve(s.keys);
                out.keys.reserve(flat.size());
                for (const auto& k : flat) {
                    out.keys.push_back(CurveKeyF{f(k.t), f(k.v)});
                }
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
    if (name == "path") return ShapeType::Path;
    if (name == "mesh") return ShapeType::Mesh;
    return ShapeType::Point;
}

// The path's points with Padding applied, its lengths and its box.
void compilePath(EmitterProgram& e, const PointList& list, bool closed, double padding) {
    std::vector<Point2> pts;
    for (const Point2& p : list.points) {
        if (std::isfinite(p.x) && std::isfinite(p.y) &&
            (pts.empty() || std::fabs(p.x - pts.back().x) + std::fabs(p.y - pts.back().y) > 1e-9)) {
            pts.push_back(p);
        }
    }
    if (closed && pts.size() > 2 && std::fabs(pts.front().x - pts.back().x) + std::fabs(pts.front().y - pts.back().y) < 1e-9) {
        pts.pop_back();
    }
    if (pts.size() < 2) {
        return;
    }
    closed = closed && pts.size() >= 3;
    // Which side is outside: for a closed outline, away from its inside;
    // for an open line, its left.
    double area = 0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const Point2& a = pts[i];
        const Point2& b = pts[(i + 1) % pts.size()];
        area += a.x * b.y - b.x * a.y;
    }
    const double outward = closed && area > 0 ? -1.0 : 1.0;  // a counter-clockwise outline's left is inside
    e.pathOutward = static_cast<float>(outward);
    const std::size_t n = pts.size();
    auto normalOf = [&](std::size_t i, std::size_t j) {
        const double dx = pts[j].x - pts[i].x, dy = pts[j].y - pts[i].y;
        const double l = std::sqrt(dx * dx + dy * dy);
        return l > 0 ? Point2{-dy / l * outward, dx / l * outward} : Point2{0, 0};
    };
    std::vector<Point2> moved = pts;
    if (std::isfinite(padding) && padding != 0.0) {
        for (std::size_t i = 0; i < n; ++i) {
            const bool hasPrev = closed || i > 0, hasNext = closed || i + 1 < n;
            Point2 a = hasPrev ? normalOf((i + n - 1) % n, i) : Point2{0, 0};
            Point2 b = hasNext ? normalOf(i, (i + 1) % n) : Point2{0, 0};
            if (!hasPrev) a = b;
            if (!hasNext) b = a;
            Point2 m{a.x + b.x, a.y + b.y};
            const double ml = std::sqrt(m.x * m.x + m.y * m.y);
            if (ml < 1e-9) {
                m = a;
            } else {
                m = Point2{m.x / ml, m.y / ml};
            }
            // Mitred: as far out along the corner's middle as keeps the sides parallel, at most twice.
            const double c = m.x * a.x + m.y * a.y;
            const double reach = padding / std::max(0.5, c);
            moved[i] = Point2{pts[i].x + m.x * reach, pts[i].y + m.y * reach};
        }
    }
    if (closed) {
        moved.push_back(moved.front());
    }
    e.pathClosed = closed;
    float total = 0;
    e.pathMinX = e.pathMaxX = static_cast<float>(moved[0].x);
    e.pathMinY = e.pathMaxY = static_cast<float>(moved[0].y);
    for (std::size_t i = 0; i < moved.size(); ++i) {
        const float x = static_cast<float>(moved[i].x), y = static_cast<float>(moved[i].y);
        if (i > 0) {
            const float dx = x - e.pathX.back(), dy = y - e.pathY.back();
            total += std::sqrt(dx * dx + dy * dy);
        }
        e.pathX.push_back(x);
        e.pathY.push_back(y);
        e.pathCum.push_back(total);
        e.pathMinX = std::min(e.pathMinX, x);
        e.pathMaxX = std::max(e.pathMaxX, x);
        e.pathMinY = std::min(e.pathMinY, y);
        e.pathMaxY = std::max(e.pathMaxY, y);
    }
    e.pathTotal = total;
}

// The Mesh shape: edges as segments with their running length; a surface as a
// cloud of points spread evenly over the triangles by area (a few to a
// triangle in proportion to its size, never more crowded where the model has
// more small triangles), then sorted by height or by distance from the scan's
// origin so a band is a run of the list.
void compileMesh(EmitterProgram& e, const Reader& shape) {
    const bool edges = shape.get<std::string>("meshKind") != "surface";
    const PointList& data = shape.get<PointList>("meshData");
    e.meshEdges = edges;
    e.meshOffset = static_cast<float>(shape.get<double>("padding"));
    const std::string& scan = shape.get<std::string>("scanMode");
    e.scanMode = scan == "height" ? 1 : scan == "ripple" ? 2 : 0;
    e.scanSpeed = static_cast<float>(shape.get<double>("scanSpeed"));
    e.scanWidth = std::max(0.0005f, static_cast<float>(shape.get<double>("scanWidth")));
    e.scanBands = static_cast<int>(std::clamp<std::int64_t>(shape.get<std::int64_t>("scanBands"), 1, 32));
    const Vec3& origin = shape.get<Vec3>("scanOrigin");
    e.scanOx = static_cast<float>(origin.x);
    e.scanOy = static_cast<float>(origin.y);
    e.scanOz = static_cast<float>(origin.z);

    double sx = 0, sy = 0, sz = 0;
    std::size_t corners = 0;
    if (edges) {
        const std::vector<MeshSegment> segments = unpackSegments(data);
        float total = 0;
        for (const MeshSegment& s : segments) {
            const float dx = s.v[3] - s.v[0], dy = s.v[4] - s.v[1], dz = s.v[5] - s.v[2];
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (!(len > 1e-7f) || !std::isfinite(len)) {
                continue;
            }
            total += len;
            e.segAx.push_back(s.v[0]);
            e.segAy.push_back(s.v[1]);
            e.segAz.push_back(s.v[2]);
            e.segBx.push_back(s.v[3]);
            e.segBy.push_back(s.v[4]);
            e.segBz.push_back(s.v[5]);
            e.segCum.push_back(total);
            sx += s.v[0] + s.v[3];
            sy += s.v[1] + s.v[4];
            sz += s.v[2] + s.v[5];
            corners += 2;
        }
        if (e.segCum.empty()) {
            e.shape = ShapeType::Point;  // nothing to emit along
        }
    } else {
        const std::vector<MeshTriangle> triangles = unpackTriangles(data);
        struct Tri {
            float v[9];
            float nx, ny, nz, area;
        };
        std::vector<Tri> good;
        double totalArea = 0;
        for (const MeshTriangle& t : triangles) {
            const float ux = t.v[3] - t.v[0], uy = t.v[4] - t.v[1], uz = t.v[5] - t.v[2];
            const float wx = t.v[6] - t.v[0], wy = t.v[7] - t.v[1], wz = t.v[8] - t.v[2];
            const float cx = uy * wz - uz * wy, cy = uz * wx - ux * wz, cz = ux * wy - uy * wx;
            const float twice = std::sqrt(cx * cx + cy * cy + cz * cz);
            if (!(twice > 1e-9f) || !std::isfinite(twice)) {
                continue;
            }
            Tri g{};
            for (int k = 0; k < 9; ++k) g.v[k] = t.v[k];
            g.nx = cx / twice;
            g.ny = cy / twice;
            g.nz = cz / twice;
            g.area = 0.5f * twice;
            totalArea += g.area;
            good.push_back(g);
            e.meshTriangles.insert(e.meshTriangles.end(), t.v.begin(), t.v.end());
            for (int k = 0; k < 3; ++k) {
                sx += t.v[k * 3];
                sy += t.v[k * 3 + 1];
                sz += t.v[k * 3 + 2];
            }
            corners += 3;
        }
        if (good.empty() || !(totalArea > 0)) {
            e.shape = ShapeType::Point;
            return;
        }
        const std::size_t want = static_cast<std::size_t>(std::clamp<std::int64_t>(shape.get<std::int64_t>("meshDensity"), 500, 100000));
        e.meshSpacing = static_cast<float>(std::sqrt(totalArea / static_cast<double>(want)));
        struct Sample {
            float p[3], n[3], key;
        };
        std::vector<Sample> cloud;
        cloud.reserve(want + good.size());
        // Samples to each triangle in proportion to its area, the remainder
        // carried on so the total comes out right; inside it, a low-discrepancy
        // spread (no two close together by chance).
        double carry = 0;
        const double perArea = static_cast<double>(want) / totalArea;
        for (const Tri& g : good) {
            carry += g.area * perArea;
            const std::size_t k = static_cast<std::size_t>(carry);
            carry -= static_cast<double>(k);
            for (std::size_t j = 0; j < k; ++j) {
                double a = 0.5 + 0.7548776662466927 * static_cast<double>(j + 1);
                a -= std::floor(a);
                double b = 0.5 + 0.5698402909980532 * static_cast<double>(j + 1);
                b -= std::floor(b);
                if (a + b > 1.0) {
                    a = 1.0 - a;
                    b = 1.0 - b;
                }
                const float fa = static_cast<float>(a), fb = static_cast<float>(b), fc = 1.0f - fa - fb;
                Sample s{};
                s.p[0] = fc * g.v[0] + fa * g.v[3] + fb * g.v[6];
                s.p[1] = fc * g.v[1] + fa * g.v[4] + fb * g.v[7];
                s.p[2] = fc * g.v[2] + fa * g.v[5] + fb * g.v[8];
                s.n[0] = g.nx;
                s.n[1] = g.ny;
                s.n[2] = g.nz;
                cloud.push_back(s);
            }
        }
        for (Sample& s : cloud) {
            if (e.scanMode == 2) {
                const float dx = s.p[0] - e.scanOx, dy = s.p[1] - e.scanOy, dz = s.p[2] - e.scanOz;
                s.key = std::sqrt(dx * dx + dy * dy + dz * dz);
            } else {
                s.key = s.p[1];
            }
        }
        std::stable_sort(cloud.begin(), cloud.end(), [](const Sample& a, const Sample& b) { return a.key < b.key; });
        if (cloud.empty()) {
            e.shape = ShapeType::Point;
            return;
        }
        for (const Sample& s : cloud) {
            e.meshPx.push_back(s.p[0]);
            e.meshPy.push_back(s.p[1]);
            e.meshPz.push_back(s.p[2]);
            e.meshNx.push_back(s.n[0]);
            e.meshNy.push_back(s.n[1]);
            e.meshNz.push_back(s.n[2]);
            e.meshKey.push_back(s.key);
        }
        e.meshKeyMin = e.meshKey.front();
        e.meshKeyMax = e.meshKey.back();
    }
    if (corners > 0) {
        e.meshCx = static_cast<float>(sx / static_cast<double>(corners));
        e.meshCy = static_cast<float>(sy / static_cast<double>(corners));
        e.meshCz = static_cast<float>(sz / static_cast<double>(corners));
    }
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

std::shared_ptr<const EmitterProgram> compileLayer(const Effect& effect, const Layer& layer, int depth) {
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
    double maxRate = std::max(0.0, static_cast<double>(e->rate.maxValue()));
    e->emits = emission.present() && e->duration > 0.0 && (maxRate > 0.0 || !e->bursts.empty());
    if (emission.present() && depth < 3) {
        const std::string& when = emission.get<std::string>("trigger");
        const std::string& parentName = emission.get<std::string>("parent");
        const int kind = when == "parent born" ? 1 : (when == "parent dies" ? 2 : (when == "parent bounces" ? 4 : 0));
        if (kind != 0 && !parentName.empty()) {
            for (const auto& other : effect.layers) {
                if (other.id != layer.id && other.name == parentName) {
                    e->subParent = compileLayer(effect, other, depth + 1);
                    break;
                }
            }
        }
        if (e->subParent && e->subParent->emits && e->duration > 0.0) {
            e->subTrigger = kind;
            const double count = emission.get<double>("triggerCount");
            e->subCount = std::isfinite(count) ? static_cast<int>(std::clamp(std::floor(count + 0.5), 1.0, 100.0)) : 3;
            const double chance = emission.get<double>("triggerChance");
            e->subChance = std::isfinite(chance) ? std::clamp(f(chance), 0.0f, 1.0f) : 1.0f;
            const double inherit = emission.get<double>("inheritMotion");
            e->subInheritMotion = std::isfinite(inherit) ? std::clamp(f(inherit), 0.0f, 1.0f) : 0.0f;
            e->subInheritColor = emission.get<bool>("inheritColor");
            e->emits = true;
            e->bursts.clear();
            e->rate = constantScalar(0.0f);
            maxRate = 0.0;
        } else {
            e->subParent.reset();
        }
    }

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
        const Vec3& offset = shape.get<Vec3>("offset");
        auto finite = [](double v) { return std::isfinite(v) ? static_cast<float>(v) : 0.0f; };
        e->offsetX = finite(offset.x);
        e->offsetY = finite(offset.y);
        e->offsetZ = e->flat ? 0.0f : finite(offset.z);
        e->offset = e->offsetX != 0.0f || e->offsetY != 0.0f || e->offsetZ != 0.0f;
        e->ground = !e->flat && shape.get<std::string>("plane") == "ground";
        if (e->flat && e->shape == ShapeType::Mesh) {
            e->shape = ShapeType::Point;  // a model's surface needs a 3D effect
        }
        if (e->shape == ShapeType::Mesh) {
            compileMesh(*e, shape);
            const std::string& aim = shape.get<std::string>("aim");
            e->aim = aim == "outward" ? 1 : aim == "along" ? 2 : 0;  // along: an edge's own direction
        }
        if (e->shape == ShapeType::Path) {
            compilePath(*e, shape.get<PointList>("path"), shape.get<bool>("closed"), shape.get<double>("padding"));
            e->pathThickness = std::max(0.0f, f(shape.get<double>("thickness")));
            e->travel = f(shape.get<double>("travel"));
            e->travelSpan = std::clamp(f(shape.get<double>("travelSpan")), 0.001f, 1.0f);
            e->travelHeads = static_cast<int>(std::clamp<std::int64_t>(shape.get<std::int64_t>("travelHeads"), 1, 16));
            e->travelDraw = shape.get<std::string>("travelMode") == "draw";
            const std::string& aim = shape.get<std::string>("aim");
            e->aim = aim == "outward" ? 1 : aim == "along" ? 2 : 0;
            e->pathAlign = shape.get<bool>("alignToPath");
            if (e->pathTotal <= 0.0f) {
                e->shape = ShapeType::Point;  // too short to emit along
            }
        }
    }

    // -------------------------------------------------------- initial state
    // With no Initial State module a particle still needs a lifetime and a
    // size, so the module's defaults are used.
    const Reader initial(firstOfType(layer, "initial"), "initial");
    e->lifetime = toScalar(initial.get<Scalar>("lifetime"));
    e->speed = toScalar(initial.get<Scalar>("speed"));
    e->size = toScalar(initial.get<Scalar>("size"));
    e->rotation = toScalar(initial.get<Scalar>("rotation"));
    e->opacity = toScalar(initial.get<Scalar>("opacity"));

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
    e->speedOverLife = constantScalar(1.0f);
    e->forceRamp = constantScalar(1.0f);
    e->sizeBySpeed = constantScalar(1.0f);
    if (motion.present()) {
        const Vec3& gravity = motion.get<Vec3>("gravity");
        e->gravityX = f(gravity.x);
        e->gravityY = f(gravity.y);
        e->gravityZ = e->flat ? 0.0f : f(gravity.z);
        e->drag = f(std::max(0.0, motion.get<double>("drag")));
        e->spin = toScalar(motion.get<Scalar>("spin"));
        const double turbulence = motion.get<double>("turbulence");
        const double swirl = motion.get<double>("turbulenceSize");
        const double change = motion.get<double>("turbulenceChange");
        e->turbulence = std::isfinite(turbulence) ? std::clamp(f(turbulence), 0.0f, 1000.0f) : 0.0f;
        e->turbulencePerUnit = 1.0f / (std::isfinite(swirl) ? std::clamp(f(swirl), 0.02f, 10000.0f) : 1.5f);
        e->turbulenceChange = std::isfinite(change) ? std::clamp(f(change), 0.0f, 60.0f) : 0.0f;

        const auto finite = [&](const char* key, double lo, double hi, double fallback) {
            const double v = motion.get<double>(key);
            return f(std::isfinite(v) ? std::clamp(v, lo, hi) : fallback);
        };
        const Vec3& force = motion.get<Vec3>("force");
        e->forceRamp = toScalar(motion.get<Scalar>("forceRamp"));
        e->forceX = std::isfinite(force.x) ? f(std::clamp(force.x, -10000.0, 10000.0)) : 0.0f;
        e->forceY = std::isfinite(force.y) ? f(std::clamp(force.y, -10000.0, 10000.0)) : 0.0f;
        e->forceZ = (e->flat || !std::isfinite(force.z)) ? 0.0f : f(std::clamp(force.z, -10000.0, 10000.0));
        e->hasForce = e->forceX != 0.0f || e->forceY != 0.0f || e->forceZ != 0.0f;
        const Vec3& drift = motion.get<Vec3>("drift");
        e->driftX = std::isfinite(drift.x) ? f(std::clamp(drift.x, -10000.0, 10000.0)) : 0.0f;
        e->driftY = std::isfinite(drift.y) ? f(std::clamp(drift.y, -10000.0, 10000.0)) : 0.0f;
        e->driftZ = (e->flat || !std::isfinite(drift.z)) ? 0.0f : f(std::clamp(drift.z, -10000.0, 10000.0));
        e->speedLimit = finite("speedLimit", 0.0, 10000.0, 0.0);
        e->speedLimitDampen = finite("speedLimitDampen", 0.0, 1.0, 0.5);
        e->spinBySpeed = finite("spinBySpeed", -36000.0, 36000.0, 0.0);
        e->inheritVelocity = finite("inheritVelocity", 0.0, 1.0, 0.0);
        e->collide = motion.get<bool>("collide");
        e->floorY = finite("floorHeight", -10000.0, 10000.0, 0.0);
        e->bounce = finite("bounce", 0.0, 1.0, 0.5);
        e->collDampen = finite("collisionDampen", 0.0, 1.0, 0.1);
        e->collLifeLoss = finite("collisionLifeLoss", 0.0, 1.0, 0.0);
        e->collMinSpeed = finite("collisionMinSpeed", 0.0, 10000.0, 0.0);
        const std::string& zoneMode = motion.get<std::string>("zone");
        e->zone = zoneMode == "kill inside" ? 1 : (zoneMode == "kill outside" ? 2 : 0);
        const Vec3& zc = motion.get<Vec3>("zoneCenter");
        const Vec3& zs = motion.get<Vec3>("zoneSize");
        const auto fin = [&](double v, double lo, double hi) { return std::isfinite(v) ? f(std::clamp(v, lo, hi)) : 0.0f; };
        e->zoneX = fin(zc.x, -10000.0, 10000.0);
        e->zoneY = fin(zc.y, -10000.0, 10000.0);
        e->zoneZ = e->flat ? 0.0f : fin(zc.z, -10000.0, 10000.0);
        e->zoneSX = fin(zs.x, 0.0, 10000.0);
        e->zoneSY = fin(zs.y, 0.0, 10000.0);
        e->zoneSZ = e->flat ? 1e9f : fin(zs.z, 0.0, 10000.0);

        e->speedOverLife = toScalar(motion.get<Scalar>("speedOverLife"));
        e->eases = !(e->speedOverLife.mode == ScalarF::Mode::Constant && e->speedOverLife.a == 1.0f);
        const double orbit = motion.get<double>("orbit");
        if (std::isfinite(orbit) && orbit != 0.0) {
            const double radians = std::clamp(orbit, -3600.0, 3600.0) * det::kDegreesToRadians;
            double s = 0.0, c = 1.0;
            det::sinCos(radians * e->step, s, c);
            e->orbit = f(radians);
            e->orbitSin = f(s);
            e->orbitCos = f(c);
        }
    }

    // ------------------------------------------------------------ over life
    const Reader overLife(firstOfType(layer, "overLife"), "overLife");
    e->sizeOverLife = constantScalar(1.0f);
    e->opacityOverLife = constantScalar(1.0f);
    e->squashStretch = constantScalar(0.0f);
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
        bool byTint = false;
        for (const auto& k : overLife.get<Gradient>("colorBySpeed").keys) {
            const GradientKeyF key{f(k.t), f(k.color.r), f(k.color.g), f(k.color.b), f(k.color.a)};
            byTint = byTint || key.r != 1.0f || key.g != 1.0f || key.b != 1.0f || key.a != 1.0f;
            e->colorBySpeed.push_back(key);
        }
        if (!byTint) {
            e->colorBySpeed.clear();
        }
        e->sizeBySpeed = toScalar(overLife.get<Scalar>("sizeBySpeed"));
        e->hasSizeBySpeed = false;
        if (e->sizeBySpeed.isCurve()) {
            for (const auto& k : e->sizeBySpeed.keys) e->hasSizeBySpeed = e->hasSizeBySpeed || k.v != 1.0f;
        } else {
            e->hasSizeBySpeed = !(e->sizeBySpeed.mode == ScalarF::Mode::Constant && e->sizeBySpeed.a == 1.0f);
        }
        const double smin = overLife.get<double>("speedMin"), smax = overLife.get<double>("speedMax");
        e->speedMin = std::isfinite(smin) ? std::clamp(f(smin), 0.0f, 10000.0f) : 0.0f;
        e->speedMax = std::isfinite(smax) ? std::clamp(f(smax), 0.0f, 10000.0f) : 5.0f;
        if (e->speedMax < e->speedMin + 0.05f) {
            e->speedMax = e->speedMin + 0.05f;  // never a zero-width range
        }
        const double dissolve = overLife.get<double>("dissolve");
        e->dissolve = std::isfinite(dissolve) ? std::clamp(f(dissolve), 0.0f, 1.0f) : 0.0f;
        const double soft = overLife.get<double>("dissolveSoft");
        e->dissolveSoft = std::isfinite(soft) ? std::clamp(f(soft), 0.0f, 1.0f) : 0.15f;
        const double edge = overLife.get<double>("dissolveEdge");
        e->dissolveEdge = std::isfinite(edge) ? std::clamp(f(edge), 0.0f, 1.0f) : 0.0f;
        e->squashStretch = toScalar(overLife.get<Scalar>("stretch"));
        e->squashStretches =
            !(e->squashStretch.mode == ScalarF::Mode::Constant && e->squashStretch.a == 0.0f);
    }

    // --------------------------------------------------------------- sprite
    const Reader sprite(firstOfType(layer, "sprite"), "sprite");
    if (sprite.present()) {
        e->drawn = true;
        e->texture = sprite.get<AssetRef>("texture").id;
        e->blend = blendFromName(sprite.get<std::string>("blend"));
        e->facing = sprite.get<std::string>("facing") == "plane" ? Facing::Plane : Facing::Camera;
        e->glow = f(sprite.get<double>("glow"));

        const std::string picture = sprite.get<std::string>("shape");
        const auto& options = Registry::builtin().findModule("sprite")->find("shape")->options;
        for (std::size_t i = 0; i < options.size() && i < static_cast<std::size_t>(kMaxSpriteShapes); ++i) {
            if (options[i] == picture) {
                e->spriteShape = static_cast<SpriteShape>(i);
            }
        }
        // Changes to the shape's design, each held to a whole step.
        const auto amount = [&](const char* key) {
            const double v = sprite.get<double>(key);
            const double steps = std::isfinite(v) ? std::round(std::clamp(v, -1.0, 1.0) * kShapeEditSteps) : 0.0;
            return f(steps / kShapeEditSteps);
        };
        e->shapeEdit.count = static_cast<int>(
            std::clamp<std::int64_t>(sprite.get<std::int64_t>("shapeCount"), 0, kShapeEditMaxCount));
        e->shapeEdit.thickness = amount("shapeThickness");
        e->shapeEdit.sharpness = amount("shapeSharpness");
        e->shapeEdit.round = amount("shapeRound");

        const double trail = sprite.get<double>("trail");
        e->trail = std::isfinite(trail) ? std::clamp(f(trail), 0.0f, 5.0f) : 0.0f;
        const double trailWidth = sprite.get<double>("trailWidth");
        e->trailWidth = std::isfinite(trailWidth) ? std::clamp(f(trailWidth), 0.0f, 4.0f) : 0.0f;
        auto share = [&](const char* key, float fallback) {
            const double v = sprite.get<double>(key);
            return std::isfinite(v) ? std::clamp(f(v), 0.0f, 1.0f) : fallback;
        };
        e->lightShare = share("lightShare", 0.0f);
        {
            const double li = sprite.get<double>("lightIntensity"), lr = sprite.get<double>("lightRange");
            e->lightIntensity = std::isfinite(li) ? std::clamp(f(li), 0.0f, 20.0f) : 1.0f;
            e->lightRange = std::isfinite(lr) ? std::clamp(f(lr), 0.1f, 100.0f) : 3.0f;
            e->lightSize = sprite.get<bool>("lightFollowsSize");
            e->lightFade = sprite.get<bool>("lightFollowsFade");
            e->lightMax = static_cast<int>(std::clamp<std::int64_t>(sprite.get<std::int64_t>("lightMax"), 1, 64));
        }
        e->trailRatio = share("trailRatio", 1.0f);
        e->trailTaper = share("trailTaper", 0.0f);
        e->trailFade = sprite.get<bool>("trailFade");
        const double trailMin = sprite.get<double>("trailMinDistance");
        e->trailMinDistance = std::isfinite(trailMin) ? std::clamp(f(trailMin), 0.005f, 2.0f) : 0.05f;
        e->trailTile = sprite.get<std::string>("trailTexture") == "tile";
        e->trailWorld = sprite.get<bool>("trailWorldSpace");
        e->alongMotion = sprite.get<std::string>("align") == "movement";
        e->stretch = e->alongMotion ? f(sprite.get<double>("stretch")) : 0.0f;
        const double tilt = sprite.get<double>("tilt");
        e->tilt = !e->alongMotion && std::isfinite(tilt) ? std::clamp(f(tilt), 0.0f, 89.0f) : 0.0f;
        // A drawing held for less than one step is every step: smooth.
        const double hold = sprite.get<double>("hold");
        e->hold = std::isfinite(hold) && hold > 0.0 && hold < 1.0 / e->step - 1e-6 ? f(hold) : 0.0f;

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
    if (e->subParent) {
        // At most one event's worth of particles for each of the parent's, for as long as one of them lives.
        const double parents = static_cast<double>(e->subParent->capacity);
        const double perParent = e->subTrigger == 1 ? 1.0 : 1.0;
        e->maxLifetime += e->subParent->maxLifetime;  // a sub emitter's history reaches back through the parent's
        e->wanted = toCount(parents * e->subCount * perParent * (e->lifetime.maxValue() / std::max(1e-3, static_cast<double>(e->subParent->lifetime.maxValue())) + 1.0) + 16.0);
        e->capacity = static_cast<std::uint32_t>(std::min<std::uint64_t>(e->wanted, kMaxParticlesPerEmitter));
    } else if (e->emits) {
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
        bool subEmitter = false;
        for (const auto& m : layer.modules) {
            if (m.type == "emission") {
                if (const Value* v = m.find("trigger")) {
                    subEmitter = std::get<std::string>(*v) != "time";
                }
            }
        }
        // A sub emitter carries a copy of its parent, so any edit rebuilds it.
        const bool dirty = allDirty_ || (subEmitter && !dirtyLayers_.empty()) ||
                           std::find(dirtyLayers_.begin(), dirtyLayers_.end(), layer.id) != dirtyLayers_.end();
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
