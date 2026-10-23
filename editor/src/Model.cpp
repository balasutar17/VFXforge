#include "vfx/editor/Model.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "vfx/FileIO.h"
#include "Camera.h"

namespace vfx::editor {

namespace {

using J = nlohmann::json;
constexpr float kPi = 3.14159265358979323846f;

Error fail(const std::string& message) { return makeError(message); }

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// The clay grey a model is drawn in when its file gives no colour.
constexpr float kClay[3] = {0.74f, 0.74f, 0.78f};

// Fills in what the file left out (normals, colours) and checks what is there.
Result<Model> finish(Model m) {
    if (m.triangles.empty() || m.positions.empty()) {
        return fail("The file has no triangles in it. Faces made of lines or points are not read.");
    }
    if (m.triangleCount() > kMaxModelTriangles) {
        return fail("The model has " + std::to_string(m.triangleCount()) +
                    " triangles; at most 2 000 000 can be shown. Reduce it in your 3D program first.");
    }
    const std::size_t corners = m.corners();
    for (const std::uint32_t i : m.triangles) {
        if (i >= corners) {
            return fail("The file refers to a corner it does not have.");
        }
    }
    for (const float v : m.positions) {
        if (!std::isfinite(v)) {
            return fail("The file has a corner that is not a number.");
        }
    }
    if (m.normals.size() != m.positions.size()) {
        // Smooth normals: each corner gets the sum of the faces around it.
        m.normals.assign(m.positions.size(), 0.0f);
        for (std::size_t t = 0; t + 2 < m.triangles.size(); t += 3) {
            const float* a = &m.positions[m.triangles[t] * 3u];
            const float* b = &m.positions[m.triangles[t + 1] * 3u];
            const float* c = &m.positions[m.triangles[t + 2] * 3u];
            const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
            const float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
            const float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
            for (int k = 0; k < 3; ++k) {
                float* n = &m.normals[m.triangles[t + static_cast<std::size_t>(k)] * 3u];
                n[0] += nx;
                n[1] += ny;
                n[2] += nz;
            }
        }
    }
    for (std::size_t i = 0; i + 2 < m.normals.size(); i += 3) {
        float* n = &m.normals[i];
        const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (std::isfinite(length) && length > 1e-12f) {
            n[0] /= length;
            n[1] /= length;
            n[2] /= length;
        } else {
            n[0] = 0.0f;
            n[1] = 1.0f;
            n[2] = 0.0f;
        }
    }
    if (m.colors.size() != m.positions.size()) {
        m.colors.resize(m.positions.size());
        for (std::size_t i = 0; i + 2 < m.colors.size(); i += 3) {
            m.colors[i] = kClay[0];
            m.colors[i + 1] = kClay[1];
            m.colors[i + 2] = kClay[2];
        }
    }
    m.minX = m.minY = m.minZ = std::numeric_limits<float>::max();
    m.maxX = m.maxY = m.maxZ = std::numeric_limits<float>::lowest();
    for (std::size_t i = 0; i + 2 < m.positions.size(); i += 3) {
        m.minX = std::min(m.minX, m.positions[i]);
        m.maxX = std::max(m.maxX, m.positions[i]);
        m.minY = std::min(m.minY, m.positions[i + 1]);
        m.maxY = std::max(m.maxY, m.positions[i + 1]);
        m.minZ = std::min(m.minZ, m.positions[i + 2]);
        m.maxZ = std::max(m.maxZ, m.positions[i + 2]);
    }
    if (!(m.maxX - m.minX > 0.0f) && !(m.maxY - m.minY > 0.0f) && !(m.maxZ - m.minZ > 0.0f)) {
        return fail("Every corner of the model is in the same place.");
    }
    return m;
}

// ---------------------------------------------------------------- OBJ

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

std::string_view nextWord(std::string_view& s) {
    s = trimmed(s);
    std::size_t end = 0;
    while (end < s.size() && s[end] != ' ' && s[end] != '\t') {
        ++end;
    }
    const std::string_view word = s.substr(0, end);
    s.remove_prefix(end);
    return word;
}

bool number(std::string_view word, float& out) {
    if (word.empty()) {
        return false;
    }
    // from_chars for floats is not everywhere yet; strtof needs an end.
    const std::string copy(word);
    char* end = nullptr;
    out = std::strtof(copy.c_str(), &end);
    return end != copy.c_str() && std::isfinite(out);
}

bool wholeNumber(std::string_view word, long& out) {
    if (word.empty()) {
        return false;
    }
    const auto [ptr, ec] = std::from_chars(word.data(), word.data() + word.size(), out);
    return ec == std::errc() && ptr == word.data() + word.size();
}

template <class Each>
void eachLine(std::string_view text, const Each& each) {
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        const std::size_t hash = line.find('#');
        if (hash != std::string_view::npos) {
            line = line.substr(0, hash);
        }
        line = trimmed(line);
        if (!line.empty()) {
            each(line);
        }
    }
}

std::map<std::string, std::array<float, 3>> readMtl(std::string_view mtl) {
    std::map<std::string, std::array<float, 3>> colours;
    std::string current;
    eachLine(mtl, [&](std::string_view line) {
        const std::string_view key = nextWord(line);
        if (key == "newmtl") {
            current = std::string(trimmed(line));
            colours[current] = {kClay[0], kClay[1], kClay[2]};
        } else if (key == "Kd" && !current.empty()) {
            std::array<float, 3> c{};
            for (float& v : c) {
                if (!number(nextWord(line), v)) {
                    return;
                }
                v = clamp01(v);
            }
            colours[current] = c;
        }
    });
    return colours;
}

}  // namespace

Result<Model> readObj(std::string_view obj, std::string_view mtl) {
    const auto materials = readMtl(mtl);
    std::vector<float> points, pointColours, fileNormals;
    bool pointsColoured = false;
    std::array<float, 3> colour{kClay[0], kClay[1], kClay[2]};
    bool haveMaterial = false;
    Model m;
    bool anyNormals = false, allNormals = true;
    std::vector<std::uint32_t> facePoint;  // for each corner made, the point it came from
    std::string problem;

    eachLine(obj, [&](std::string_view line) {
        if (!problem.empty()) {
            return;
        }
        const std::string_view key = nextWord(line);
        if (key == "v") {
            float xyz[3];
            for (float& v : xyz) {
                if (!number(nextWord(line), v)) {
                    problem = "A corner (\"v\" line) could not be read.";
                    return;
                }
            }
            points.insert(points.end(), xyz, xyz + 3);
            // Some programs write a colour after the position.
            float rgb[3];
            bool coloured = true;
            for (float& v : rgb) {
                coloured = coloured && number(nextWord(line), v);
            }
            if (coloured) {
                pointsColoured = true;
                for (float& v : rgb) {
                    v = clamp01(v);
                }
            } else {
                rgb[0] = kClay[0];
                rgb[1] = kClay[1];
                rgb[2] = kClay[2];
            }
            pointColours.insert(pointColours.end(), rgb, rgb + 3);
        } else if (key == "vn") {
            float xyz[3];
            for (float& v : xyz) {
                if (!number(nextWord(line), v)) {
                    problem = "A normal (\"vn\" line) could not be read.";
                    return;
                }
            }
            fileNormals.insert(fileNormals.end(), xyz, xyz + 3);
        } else if (key == "usemtl") {
            const auto found = materials.find(std::string(trimmed(line)));
            haveMaterial = found != materials.end();
            if (haveMaterial) {
                colour = found->second;
            }
        } else if (key == "f") {
            // Each corner is "v", "v/vt", "v//vn" or "v/vt/vn"; counted from
            // 1, or from the end when below 0. A face of more than three
            // corners is cut into a fan of triangles.
            std::uint32_t first = 0, previous = 0;
            int count = 0;
            const long pointCount = static_cast<long>(points.size() / 3);
            const long normalCount = static_cast<long>(fileNormals.size() / 3);
            for (std::string_view word = nextWord(line); !word.empty(); word = nextWord(line)) {
                const std::size_t slash = word.find('/');
                long index = 0;
                if (!wholeNumber(word.substr(0, slash), index) || index == 0) {
                    problem = "A face (\"f\" line) could not be read.";
                    return;
                }
                index = index < 0 ? pointCount + index : index - 1;
                if (index < 0 || index >= pointCount) {
                    problem = "A face refers to a corner the file does not have.";
                    return;
                }
                long normal = -1;
                const std::size_t second = slash == std::string_view::npos ? slash : word.find('/', slash + 1);
                if (second != std::string_view::npos) {
                    long n = 0;
                    if (wholeNumber(word.substr(second + 1), n) && n != 0) {
                        normal = n < 0 ? normalCount + n : n - 1;
                        if (normal >= normalCount) {
                            normal = -1;
                        }
                    }
                }
                const auto corner = static_cast<std::uint32_t>(m.positions.size() / 3);
                const float* p = &points[static_cast<std::size_t>(index) * 3u];
                m.positions.insert(m.positions.end(), p, p + 3);
                const float* c = haveMaterial || !pointsColoured ? colour.data()
                                                                  : &pointColours[static_cast<std::size_t>(index) * 3u];
                m.colors.insert(m.colors.end(), c, c + 3);
                if (normal >= 0) {
                    anyNormals = true;
                    const float* n = &fileNormals[static_cast<std::size_t>(normal) * 3u];
                    m.normals.insert(m.normals.end(), n, n + 3);
                } else {
                    allNormals = false;
                    m.normals.insert(m.normals.end(), {0.0f, 0.0f, 0.0f});
                }
                facePoint.push_back(static_cast<std::uint32_t>(index));
                if (count == 0) {
                    first = corner;
                } else if (count >= 2) {
                    m.triangles.insert(m.triangles.end(), {first, previous, corner});
                }
                previous = corner;
                ++count;
                if (m.triangles.size() / 3 > kMaxModelTriangles) {
                    problem = "The model has more than 2 000 000 triangles. Reduce it in your 3D program first.";
                    return;
                }
            }
        }
    });
    if (!problem.empty()) {
        return fail(problem);
    }
    if (!anyNormals || !allNormals) {
        // Smooth normals by shared point, then given to every corner made
        // from that point (a corner with a normal of its own keeps it).
        std::vector<float> smooth(points.size(), 0.0f);
        for (std::size_t t = 0; t + 2 < m.triangles.size(); t += 3) {
            const float* a = &m.positions[m.triangles[t] * 3u];
            const float* b = &m.positions[m.triangles[t + 1] * 3u];
            const float* c = &m.positions[m.triangles[t + 2] * 3u];
            const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
            const float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
            const float n[3] = {uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx};
            for (int k = 0; k < 3; ++k) {
                float* s = &smooth[facePoint[m.triangles[t + static_cast<std::size_t>(k)]] * 3u];
                s[0] += n[0];
                s[1] += n[1];
                s[2] += n[2];
            }
        }
        for (std::size_t corner = 0; corner < facePoint.size(); ++corner) {
            float* n = &m.normals[corner * 3u];
            if (n[0] == 0.0f && n[1] == 0.0f && n[2] == 0.0f) {
                const float* s = &smooth[facePoint[corner] * 3u];
                n[0] = s[0];
                n[1] = s[1];
                n[2] = s[2];
            }
        }
    }
    return finish(std::move(m));
}

// --------------------------------------------------------------- glTF

namespace {

std::string fromBase64(std::string_view text) {
    static const auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };
    std::string out;
    out.reserve(text.size() * 3 / 4);
    unsigned int bits = 0;
    int have = 0;
    for (const char c : text) {
        const int v = value(c);
        if (v < 0) {
            continue;
        }
        bits = (bits << 6) | static_cast<unsigned int>(v);
        have += 6;
        if (have >= 8) {
            have -= 8;
            out.push_back(static_cast<char>((bits >> have) & 0xffu));
        }
    }
    return out;
}

std::uint32_t le32(const char* p) {
    std::uint32_t v = 0;
    std::memcpy(&v, p, 4);  // glTF is little-endian, as is every machine this runs on
    return v;
}

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // column-major, as glTF writes
    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0;
                for (int k = 0; k < 4; ++k) {
                    sum += m[k * 4 + row] * b.m[c * 4 + k];
                }
                r.m[c * 4 + row] = sum;
            }
        }
        return r;
    }
};

Mat4 nodeMatrix(const J& node) {
    Mat4 out;
    if (node.contains("matrix") && node["matrix"].is_array() && node["matrix"].size() == 16) {
        for (int i = 0; i < 16; ++i) {
            out.m[i] = node["matrix"][static_cast<std::size_t>(i)].get<float>();
        }
        return out;
    }
    Mat4 t, r, s;
    if (node.contains("translation") && node["translation"].size() == 3) {
        for (int i = 0; i < 3; ++i) {
            t.m[12 + i] = node["translation"][static_cast<std::size_t>(i)].get<float>();
        }
    }
    if (node.contains("rotation") && node["rotation"].size() == 4) {
        const float x = node["rotation"][0].get<float>(), y = node["rotation"][1].get<float>(),
                    z = node["rotation"][2].get<float>(), w = node["rotation"][3].get<float>();
        r.m[0] = 1 - 2 * (y * y + z * z);
        r.m[1] = 2 * (x * y + z * w);
        r.m[2] = 2 * (x * z - y * w);
        r.m[4] = 2 * (x * y - z * w);
        r.m[5] = 1 - 2 * (x * x + z * z);
        r.m[6] = 2 * (y * z + x * w);
        r.m[8] = 2 * (x * z + y * w);
        r.m[9] = 2 * (y * z - x * w);
        r.m[10] = 1 - 2 * (x * x + y * y);
    }
    if (node.contains("scale") && node["scale"].size() == 3) {
        for (int i = 0; i < 3; ++i) {
            s.m[i * 5] = node["scale"][static_cast<std::size_t>(i)].get<float>();
        }
    }
    return t * r * s;
}

class Gltf {
public:
    Gltf(J doc, std::vector<std::string> buffers) : doc_(std::move(doc)), buffers_(std::move(buffers)) {}

    // One accessor's numbers, `width` to an element, as floats; normalised
    // integers come back between 0 and 1.
    bool read(std::size_t index, std::vector<float>& out, int& width) const {
        const J& accessors = doc_["accessors"];
        if (!accessors.is_array() || index >= accessors.size()) {
            return false;
        }
        const J& a = accessors[index];
        if (a.contains("sparse")) {
            return false;
        }
        static const std::map<std::string, int> widths = {{"SCALAR", 1}, {"VEC2", 2}, {"VEC3", 3}, {"VEC4", 4}};
        const auto w = widths.find(a.value("type", ""));
        if (w == widths.end() || !a.contains("bufferView")) {
            return false;
        }
        width = w->second;
        const int type = a.value("componentType", 0);
        const int size = type == 5126 || type == 5125 ? 4 : type == 5123 || type == 5122 ? 2 : type == 5121 || type == 5120 ? 1 : 0;
        if (size == 0) {
            return false;
        }
        const bool normalized = a.value("normalized", false);
        const std::size_t count = a.value("count", std::size_t{0});
        const J& views = doc_["bufferViews"];
        const std::size_t viewIndex = a["bufferView"].get<std::size_t>();
        if (!views.is_array() || viewIndex >= views.size()) {
            return false;
        }
        const J& view = views[viewIndex];
        const std::size_t buffer = view.value("buffer", std::size_t{0});
        if (buffer >= buffers_.size()) {
            return false;
        }
        const std::string& bytes = buffers_[buffer];
        const std::size_t start = view.value("byteOffset", std::size_t{0}) + a.value("byteOffset", std::size_t{0});
        const std::size_t stride = view.value("byteStride", static_cast<std::size_t>(size * width));
        const std::size_t viewEnd = view.value("byteOffset", std::size_t{0}) + view.value("byteLength", std::size_t{0});
        if (count == 0 || stride == 0 || viewEnd > bytes.size() ||
            start + (count - 1) * stride + static_cast<std::size_t>(size * width) > viewEnd) {
            return false;
        }
        out.resize(count * static_cast<std::size_t>(width));
        for (std::size_t i = 0; i < count; ++i) {
            const char* at = bytes.data() + start + i * stride;
            for (int k = 0; k < width; ++k) {
                const char* p = at + k * size;
                float v = 0;
                switch (type) {
                    case 5126: std::memcpy(&v, p, 4); break;
                    case 5125: v = static_cast<float>(le32(p)); break;
                    case 5123: {
                        std::uint16_t u = 0;
                        std::memcpy(&u, p, 2);
                        v = normalized ? static_cast<float>(u) / 65535.0f : static_cast<float>(u);
                        break;
                    }
                    case 5122: {
                        std::int16_t u = 0;
                        std::memcpy(&u, p, 2);
                        v = normalized ? std::max(static_cast<float>(u) / 32767.0f, -1.0f) : static_cast<float>(u);
                        break;
                    }
                    case 5121: {
                        const auto u = static_cast<std::uint8_t>(*p);
                        v = normalized ? static_cast<float>(u) / 255.0f : static_cast<float>(u);
                        break;
                    }
                    default: {
                        const auto u = static_cast<std::int8_t>(*p);
                        v = normalized ? std::max(static_cast<float>(u) / 127.0f, -1.0f) : static_cast<float>(u);
                        break;
                    }
                }
                out[i * static_cast<std::size_t>(width) + static_cast<std::size_t>(k)] = v;
            }
        }
        return true;
    }

    // Adds one mesh, placed by `place`, to the model.
    bool addMesh(std::size_t index, const Mat4& place, Model& m, std::string& problem) const {
        const J& meshes = doc_["meshes"];
        if (!meshes.is_array() || index >= meshes.size()) {
            problem = "A node refers to a mesh the file does not have.";
            return false;
        }
        for (const J& prim : meshes[index].value("primitives", J::array())) {
            if (prim.value("mode", 4) != 4 || !prim.contains("attributes") ||
                !prim["attributes"].contains("POSITION")) {
                continue;  // lines and points are not drawn
            }
            std::vector<float> pos, nor, col, idx;
            int pw = 0, nw = 0, cw = 0, iw = 0;
            if (!read(prim["attributes"]["POSITION"].get<std::size_t>(), pos, pw) || pw != 3) {
                problem = "The corners of a mesh could not be read.";
                return false;
            }
            const std::size_t count = pos.size() / 3;
            const bool hasNormals = prim["attributes"].contains("NORMAL") &&
                                    read(prim["attributes"]["NORMAL"].get<std::size_t>(), nor, nw) && nw == 3 &&
                                    nor.size() == pos.size();
            const bool hasColours = prim["attributes"].contains("COLOR_0") &&
                                    read(prim["attributes"]["COLOR_0"].get<std::size_t>(), col, cw) && cw >= 3 &&
                                    col.size() / static_cast<std::size_t>(cw) == count;
            // The material's base colour, which glTF keeps in linear light.
            float base[3] = {1.0f, 1.0f, 1.0f};
            bool textured = false;
            if (prim.contains("material") && doc_.contains("materials")) {
                const std::size_t mi = prim["material"].get<std::size_t>();
                if (mi < doc_["materials"].size()) {
                    const J& pbr = doc_["materials"][mi].value("pbrMetallicRoughness", J::object());
                    const J factor = pbr.value("baseColorFactor", J::array({1, 1, 1, 1}));
                    for (int k = 0; k < 3 && k < static_cast<int>(factor.size()); ++k) {
                        base[k] = factor[static_cast<std::size_t>(k)].get<float>();
                    }
                    textured = pbr.contains("baseColorTexture");
                }
            } else {
                base[0] = base[1] = base[2] = -1.0f;  // no material at all: clay
            }
            const auto first = static_cast<std::uint32_t>(m.positions.size() / 3);
            for (std::size_t i = 0; i < count; ++i) {
                const float* p = &pos[i * 3];
                float out[3];
                for (int r = 0; r < 3; ++r) {
                    out[r] = place.m[r] * p[0] + place.m[4 + r] * p[1] + place.m[8 + r] * p[2] + place.m[12 + r];
                }
                m.positions.insert(m.positions.end(), out, out + 3);
                if (hasNormals) {
                    const float* n = &nor[i * 3];
                    for (int r = 0; r < 3; ++r) {
                        out[r] = place.m[r] * n[0] + place.m[4 + r] * n[1] + place.m[8 + r] * n[2];
                    }
                    m.normals.insert(m.normals.end(), out, out + 3);
                }
                for (int k = 0; k < 3; ++k) {
                    float c = base[k] < 0.0f ? kClay[k] : base[k];
                    if (hasColours) {
                        c *= col[i * static_cast<std::size_t>(cw) + static_cast<std::size_t>(k)];
                    }
                    if (base[k] >= 0.0f) {
                        // Linear light to a screen colour.
                        c = std::pow(clamp01(c), 1.0f / 2.2f);
                    }
                    if (textured && !hasColours && base[0] >= 0.99f && base[1] >= 0.99f && base[2] >= 0.99f) {
                        c = kClay[k];  // its colour is in a picture, which is not read
                    }
                    m.colors.push_back(clamp01(c));
                }
            }
            if (!hasNormals) {
                m.normals.clear();  // worked out for the whole model at the end
            }
            if (prim.contains("indices")) {
                if (!read(prim["indices"].get<std::size_t>(), idx, iw) || iw != 1) {
                    problem = "The triangles of a mesh could not be read.";
                    return false;
                }
                for (std::size_t i = 0; i + 2 < idx.size(); i += 3) {
                    for (int k = 0; k < 3; ++k) {
                        const float v = idx[i + static_cast<std::size_t>(k)];
                        if (!(v >= 0.0f) || static_cast<std::size_t>(v) >= count) {
                            problem = "A mesh refers to a corner it does not have.";
                            return false;
                        }
                        m.triangles.push_back(first + static_cast<std::uint32_t>(v));
                    }
                }
            } else {
                for (std::size_t i = 0; i + 2 < count; i += 3) {
                    m.triangles.insert(m.triangles.end(), {first + static_cast<std::uint32_t>(i),
                                                           first + static_cast<std::uint32_t>(i + 1),
                                                           first + static_cast<std::uint32_t>(i + 2)});
                }
            }
            if (m.triangles.size() / 3 > kMaxModelTriangles) {
                problem = "The model has more than 2 000 000 triangles. Reduce it in your 3D program first.";
                return false;
            }
        }
        return true;
    }

    bool addNode(std::size_t index, const Mat4& parent, Model& m, std::string& problem, int depth) const {
        const J& nodes = doc_["nodes"];
        if (!nodes.is_array() || index >= nodes.size() || depth > 64) {
            problem = "The file's scene refers to a node it does not have.";
            return false;
        }
        const J& node = nodes[index];
        const Mat4 here = parent * nodeMatrix(node);
        if (node.contains("mesh") && !addMesh(node["mesh"].get<std::size_t>(), here, m, problem)) {
            return false;
        }
        for (const J& child : node.value("children", J::array())) {
            if (!addNode(child.get<std::size_t>(), here, m, problem, depth + 1)) {
                return false;
            }
        }
        return true;
    }

    const J& doc() const { return doc_; }

private:
    J doc_;
    std::vector<std::string> buffers_;
};

}  // namespace

Result<Model> readGltf(std::string_view bytes, const std::filesystem::path& folder) {
    J doc;
    std::string binary;
    bool glb = false;
    try {
        if (bytes.size() >= 12 && bytes.substr(0, 4) == "glTF") {
            glb = true;
            std::size_t at = 12;
            const std::size_t total = std::min<std::size_t>(le32(bytes.data() + 8), bytes.size());
            while (at + 8 <= total) {
                const std::uint32_t length = le32(bytes.data() + at);
                const std::uint32_t type = le32(bytes.data() + at + 4);
                if (at + 8 + length > total) {
                    return fail("The .glb file is cut short.");
                }
                const std::string_view chunk = bytes.substr(at + 8, length);
                if (type == 0x4E4F534Au) {
                    doc = J::parse(chunk);
                } else if (type == 0x004E4942u && binary.empty()) {
                    binary = std::string(chunk);
                }
                at += 8 + ((length + 3u) & ~3u);
            }
            if (doc.is_null()) {
                return fail("The .glb file has no description in it.");
            }
        } else {
            doc = J::parse(bytes);
        }
    } catch (const std::exception&) {
        return fail("The file is not a glTF model, or is damaged.");
    }
    if (!doc.is_object()) {
        return fail("The file is not a glTF model.");
    }
    for (const J& ext : doc.value("extensionsRequired", J::array())) {
        const std::string name = ext.is_string() ? ext.get<std::string>() : std::string();
        if (name.find("draco") != std::string::npos || name.find("meshopt") != std::string::npos ||
            name.find("quantization") != std::string::npos) {
            return fail("The model is saved compressed (" + name +
                        "), which cannot be read. Export it again with compression turned off.");
        }
    }
    try {
        std::vector<std::string> buffers;
        const J list = doc.value("buffers", J::array());
        for (std::size_t i = 0; i < list.size(); ++i) {
            const std::string uri = list[i].value("uri", "");
            if (uri.empty()) {
                if (!glb || i != 0) {
                    return fail("The model's data is missing.");
                }
                buffers.push_back(binary);
            } else if (uri.rfind("data:", 0) == 0) {
                const std::size_t comma = uri.find(',');
                buffers.push_back(comma == std::string::npos ? std::string() : fromBase64(uri.substr(comma + 1)));
            } else {
                // A file beside the .gltf. Spaces and the like may be written %20.
                std::string name;
                for (std::size_t k = 0; k < uri.size(); ++k) {
                    if (uri[k] == '%' && k + 2 < uri.size()) {
                        name.push_back(static_cast<char>(std::stoi(uri.substr(k + 1, 2), nullptr, 16)));
                        k += 2;
                    } else {
                        name.push_back(uri[k]);
                    }
                }
                if (name.find("..") != std::string::npos) {
                    return fail("The model's data file is outside its folder: " + name);
                }
                auto data = readFile(folder / pathFromUtf8(name));
                if (!data) {
                    return fail("The model's data file " + name + " could not be read. Keep it beside the .gltf file.");
                }
                buffers.push_back(std::move(data.value()));
            }
        }
        const Gltf gltf(std::move(doc), std::move(buffers));
        Model m;
        std::string problem;
        const J& d = gltf.doc();
        const std::size_t sceneIndex = d.value("scene", std::size_t{0});
        if (d.contains("scenes") && d["scenes"].is_array() && sceneIndex < d["scenes"].size()) {
            for (const J& n : d["scenes"][sceneIndex].value("nodes", J::array())) {
                if (!gltf.addNode(n.get<std::size_t>(), Mat4{}, m, problem, 0)) {
                    return fail(problem);
                }
            }
        } else {
            for (std::size_t i = 0; i < d.value("meshes", J::array()).size(); ++i) {
                if (!gltf.addMesh(i, Mat4{}, m, problem)) {
                    return fail(problem);
                }
            }
        }
        return finish(std::move(m));
    } catch (const std::exception&) {
        return fail("The glTF file is damaged: a value is missing or of the wrong kind.");
    }
}

Result<Model> loadModel(const std::filesystem::path& file) {
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto bytes = readFile(file);
    if (!bytes) {
        return fail("The file could not be read.");
    }
    Result<Model> model = fail("");
    if (ext == ".obj") {
        // Its colours are in the .mtl file it names.
        std::string mtl;
        eachLine(bytes.value(), [&](std::string_view line) {
            if (nextWord(line) == "mtllib" && mtl.empty()) {
                auto read = readFile(file.parent_path() / pathFromUtf8(std::string(trimmed(line))));
                if (read) {
                    mtl = std::move(read.value());
                }
            }
        });
        model = readObj(bytes.value(), mtl);
    } else if (ext == ".glb" || ext == ".gltf") {
        model = readGltf(bytes.value(), file.parent_path());
    } else {
        return fail("Models are read from .obj, .glb and .gltf files. Export one of those from your 3D program.");
    }
    if (model) {
        model.value().name = file.filename().string();
    }
    return model;
}

// ------------------------------------------------------------ drawing

float ModelPicture::seen(float viewX, float viewY, float particleDepth, float viewWidth, float viewHeight) const {
    if (empty() || !(viewWidth > 0.0f) || !(viewHeight > 0.0f) || !std::isfinite(particleDepth)) {
        return 1.0f;
    }
    const int x = static_cast<int>(std::floor(viewX * static_cast<float>(picture.width) / viewWidth));
    const int y = static_cast<int>(std::floor(viewY * static_cast<float>(picture.height) / viewHeight));
    if (x < 0 || y < 0 || x >= picture.width || y >= picture.height) {
        return 1.0f;
    }
    const float surface = depth[static_cast<std::size_t>(y) * static_cast<std::size_t>(picture.width) +
                                static_cast<std::size_t>(x)];
    if (!std::isfinite(surface) || particleDepth <= surface) {
        return 1.0f;
    }
    return clamp01(1.0f - (particleDepth - surface) / kBehindFade);
}

ModelPicture drawModel(const Model& model, const ModelPlace& place, const View& view, bool flat, int width,
                       int height, int smooth) {
    ModelPicture out;
    out.flat = flat;
    if (width <= 0 || height <= 0 || model.triangles.empty()) {
        return out;
    }
    width = std::min(width, 8192);
    height = std::min(height, 8192);
    const int k = std::clamp(smooth, 1, 4);
    const int W = width * k, H = height * k;
    View v = view;
    v.width = static_cast<float>(W);
    v.height = static_cast<float>(H);
    const camera::Camera cam = camera::makeCamera(v, flat);

    // Fitted to its box, then turned and placed.
    const float tall = model.maxY - model.minY;
    const float scale = (place.height > 0.0f ? place.height : 1.0f) / (tall > 1e-6f ? tall : 1.0f);
    const float midX = 0.5f * (model.minX + model.maxX), midZ = 0.5f * (model.minZ + model.maxZ);
    const float turn = place.turn * kPi / 180.0f;
    const float ct = std::cos(turn), st = std::sin(turn);

    // The light comes from over the viewer's left shoulder, wherever the
    // camera is, so the model always reads.
    float rx = 1, ry = 0, rz = 0, ux = 0, uy = 1, uz = 0, bx = 0, by = 0, bz = 1;
    if (!flat) {
        rx = cam.rx; ry = cam.ry; rz = cam.rz;
        ux = cam.ux; uy = cam.uy; uz = cam.uz;
        bx = -cam.fx; by = -cam.fy; bz = -cam.fz;
    }
    float lx = -0.45f * rx + 0.65f * ux + 0.6f * bx, ly = -0.45f * ry + 0.65f * uy + 0.6f * by,
          lz = -0.45f * rz + 0.65f * uz + 0.6f * bz;
    const float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
    lx /= ll;
    ly /= ll;
    lz /= ll;

    // Every corner in the world and on the screen.
    const std::size_t corners = model.corners();
    std::vector<float> sx(corners), sy(corners), sd(corners), wn(corners * 3);
    std::vector<unsigned char> ok(corners);
    for (std::size_t i = 0; i < corners; ++i) {
        const float px = (model.positions[i * 3] - midX) * scale;
        const float py = (model.positions[i * 3 + 1] - model.minY) * scale;
        const float pz = (model.positions[i * 3 + 2] - midZ) * scale;
        const float x = ct * px + st * pz + place.x, y = py + place.y, z = -st * px + ct * pz + place.z;
        float s = 1;
        ok[i] = camera::project(cam, x, y, flat ? 0.0f : z, sx[i], sy[i], s) ? 1 : 0;
        sd[i] = camera::depthOf(cam, x, y, z);
        const float nx = model.normals[i * 3], ny = model.normals[i * 3 + 1], nz = model.normals[i * 3 + 2];
        wn[i * 3] = ct * nx + st * nz;
        wn[i * 3 + 1] = ny;
        wn[i * 3 + 2] = -st * nx + ct * nz;
    }

    const std::size_t samples = static_cast<std::size_t>(W) * static_cast<std::size_t>(H);
    std::vector<float> depth(samples, std::numeric_limits<float>::infinity());
    std::vector<float> colour(samples * 3, 0.0f);
    const auto band = [](float x, float at) { return clamp01((x - at) / 0.08f + 0.5f); };

    for (std::size_t t = 0; t + 2 < model.triangles.size(); t += 3) {
        const std::uint32_t a = model.triangles[t], b = model.triangles[t + 1], c = model.triangles[t + 2];
        if (!ok[a] || !ok[b] || !ok[c]) {
            continue;  // reaches behind the camera
        }
        const float area = (sx[b] - sx[a]) * (sy[c] - sy[a]) - (sy[b] - sy[a]) * (sx[c] - sx[a]);
        if (!(std::fabs(area) > 1e-9f)) {
            continue;
        }
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({sx[a], sx[b], sx[c]}))));
        const int x1 = std::min(W - 1, static_cast<int>(std::ceil(std::max({sx[a], sx[b], sx[c]}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({sy[a], sy[b], sy[c]}))));
        const int y1 = std::min(H - 1, static_cast<int>(std::ceil(std::max({sy[a], sy[b], sy[c]}))));
        if (x0 > x1 || y0 > y1) {
            continue;
        }
        const float inv = 1.0f / area;
        for (int py = y0; py <= y1; ++py) {
            const float fy = static_cast<float>(py) + 0.5f;
            for (int px = x0; px <= x1; ++px) {
                const float fx = static_cast<float>(px) + 0.5f;
                float w0 = ((sx[b] - fx) * (sy[c] - fy) - (sy[b] - fy) * (sx[c] - fx)) * inv;
                float w1 = ((sx[c] - fx) * (sy[a] - fy) - (sy[c] - fy) * (sx[a] - fx)) * inv;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                    continue;
                }
                float d = 0;
                if (flat) {
                    d = w0 * sd[a] + w1 * sd[b] + w2 * sd[c];
                } else {
                    // Across the screen, 1 / depth changes evenly.
                    const float id = w0 / sd[a] + w1 / sd[b] + w2 / sd[c];
                    d = 1.0f / id;
                    w0 = w0 / sd[a] * d;
                    w1 = w1 / sd[b] * d;
                    w2 = 1.0f - w0 - w1;
                }
                const std::size_t at = static_cast<std::size_t>(py) * static_cast<std::size_t>(W) +
                                       static_cast<std::size_t>(px);
                if (!(d < depth[at])) {
                    continue;
                }
                depth[at] = d;
                float nx = w0 * wn[a * 3] + w1 * wn[b * 3] + w2 * wn[c * 3];
                float ny = w0 * wn[a * 3 + 1] + w1 * wn[b * 3 + 1] + w2 * wn[c * 3 + 1];
                float nz = w0 * wn[a * 3 + 2] + w1 * wn[b * 3 + 2] + w2 * wn[c * 3 + 2];
                const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (nl > 1e-12f) {
                    nx /= nl;
                    ny /= nl;
                    nz /= nl;
                }
                float facing = nx * bx + ny * by + nz * bz;
                if (facing < 0.0f) {
                    // The inside of the model, seen through a hole: lit as its outside.
                    nx = -nx;
                    ny = -ny;
                    nz = -nz;
                    facing = -facing;
                }
                // Toon shading: three tones, and a light rim at the edge.
                const float lit = nx * lx + ny * ly + nz * lz;
                const float tone = 0.5f + 0.28f * band(lit, -0.2f) + 0.22f * band(lit, 0.45f);
                const float rim = 0.28f * band(1.0f - facing, 0.72f);
                for (int ch = 0; ch < 3; ++ch) {
                    const float base = w0 * model.colors[a * 3 + static_cast<std::size_t>(ch)] +
                                       w1 * model.colors[b * 3 + static_cast<std::size_t>(ch)] +
                                       w2 * model.colors[c * 3 + static_cast<std::size_t>(ch)];
                    colour[at * 3 + static_cast<std::size_t>(ch)] = std::min(1.0f, base * tone + rim);
                }
            }
        }
    }

    // Each pixel from its k by k samples: the colour of those covered, how
    // many were covered, and the nearest surface among them.
    out.picture.width = width;
    out.picture.height = height;
    out.picture.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0);
    out.depth.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                     std::numeric_limits<float>::infinity());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float r = 0, g = 0, bl = 0, nearest = std::numeric_limits<float>::infinity();
            int covered = 0;
            for (int j = 0; j < k; ++j) {
                for (int i = 0; i < k; ++i) {
                    const std::size_t at = static_cast<std::size_t>(y * k + j) * static_cast<std::size_t>(W) +
                                           static_cast<std::size_t>(x * k + i);
                    if (std::isfinite(depth[at])) {
                        ++covered;
                        r += colour[at * 3];
                        g += colour[at * 3 + 1];
                        bl += colour[at * 3 + 2];
                        nearest = std::min(nearest, depth[at]);
                    }
                }
            }
            const std::size_t o = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            out.depth[o] = nearest;
            if (covered > 0) {
                const float n = static_cast<float>(covered);
                out.picture.rgba[o * 4] = static_cast<std::uint8_t>(std::lround(clamp01(r / n) * 255.0f));
                out.picture.rgba[o * 4 + 1] = static_cast<std::uint8_t>(std::lround(clamp01(g / n) * 255.0f));
                out.picture.rgba[o * 4 + 2] = static_cast<std::uint8_t>(std::lround(clamp01(bl / n) * 255.0f));
                out.picture.rgba[o * 4 + 3] =
                    static_cast<std::uint8_t>(std::lround(n / static_cast<float>(k * k) * 255.0f));
            }
        }
    }
    return out;
}

// ------------------------------------------------------- the practice dummy

namespace {

// A surface turned about an axis: `profile` is pairs of (distance from the
// axis, height along it), from one end to the other. `place` puts a point
// given as (across, along, across) where it belongs.
template <class Place>
void turned(Model& m, const std::vector<std::array<float, 2>>& profile, int around, const float colour[3],
            const Place& place) {
    const auto first = static_cast<std::uint32_t>(m.positions.size() / 3);
    const int rows = static_cast<int>(profile.size());
    for (int r = 0; r < rows; ++r) {
        // The slope of the profile here, for the normal.
        const auto& prev = profile[static_cast<std::size_t>(std::max(r - 1, 0))];
        const auto& next = profile[static_cast<std::size_t>(std::min(r + 1, rows - 1))];
        const float dr = next[0] - prev[0], dh = next[1] - prev[1];
        for (int a = 0; a <= around; ++a) {
            const float angle = 2.0f * kPi * static_cast<float>(a) / static_cast<float>(around);
            const float c = std::cos(angle), s = std::sin(angle);
            const auto& p = profile[static_cast<std::size_t>(r)];
            float pos[3], nor[3];
            place(p[0] * c, p[1], p[0] * s, pos);
            // Across the profile's slope: (dh, -dr) in (out, along).
            float n0 = dh * c, n1 = -dr, n2 = dh * s;
            float o[3];
            place(0.0f, 0.0f, 0.0f, o);
            float tip[3];
            place(n0, n1, n2, tip);
            nor[0] = tip[0] - o[0];
            nor[1] = tip[1] - o[1];
            nor[2] = tip[2] - o[2];
            m.positions.insert(m.positions.end(), pos, pos + 3);
            m.normals.insert(m.normals.end(), nor, nor + 3);
            m.colors.insert(m.colors.end(), colour, colour + 3);
        }
    }
    for (int r = 0; r + 1 < rows; ++r) {
        for (int a = 0; a < around; ++a) {
            const auto i = first + static_cast<std::uint32_t>(r * (around + 1) + a);
            const auto j = i + static_cast<std::uint32_t>(around + 1);
            m.triangles.insert(m.triangles.end(), {i, j, i + 1, i + 1, j, j + 1});
        }
    }
}

std::vector<std::array<float, 2>> capsuleProfile(float radius, float from, float to, int steps) {
    // Round at both ends, straight between.
    std::vector<std::array<float, 2>> out;
    for (int i = 0; i <= steps; ++i) {
        const float a = -0.5f * kPi + 0.5f * kPi * static_cast<float>(i) / static_cast<float>(steps);
        out.push_back({radius * std::cos(a), from + radius + radius * std::sin(a)});
    }
    for (int i = 0; i <= steps; ++i) {
        const float a = 0.5f * kPi * static_cast<float>(i) / static_cast<float>(steps);
        out.push_back({radius * std::cos(a), to - radius + radius * std::sin(a)});
    }
    return out;
}

std::vector<std::array<float, 2>> cylinderProfile(float radius, float from, float to) {
    // Flat ends: in to the axis, along, and in again.
    return {{0.0f, from}, {radius * 0.98f, from}, {radius, from + 0.005f}, {radius, to - 0.005f},
            {radius * 0.98f, to}, {0.0f, to}};
}

}  // namespace

Model practiceDummy() {
    Model m;
    m.name = "Practice dummy";
    const float stone[3] = {0.56f, 0.57f, 0.62f};
    const float wood[3] = {0.58f, 0.38f, 0.22f};
    const float canvas[3] = {0.88f, 0.76f, 0.52f};
    const float rope[3] = {0.47f, 0.31f, 0.18f};
    const auto upright = [](float a, float h, float b, float* out) {
        out[0] = a;
        out[1] = h;
        out[2] = b;
    };
    turned(m, cylinderProfile(0.72f, 0.0f, 0.16f), 40, stone, upright);
    turned(m, cylinderProfile(0.1f, 0.16f, 1.2f), 16, wood, upright);
    turned(m, capsuleProfile(0.44f, 1.0f, 2.32f, 8), 32, canvas, upright);
    turned(m, cylinderProfile(0.455f, 1.33f, 1.39f), 32, rope, upright);
    turned(m, cylinderProfile(0.455f, 1.93f, 1.99f), 32, rope, upright);
    turned(m, capsuleProfile(0.3f, 2.34f, 2.94f, 10), 28, canvas, upright);
    // The arms: a pole across, along x, with a knob at each end.
    const auto across = [](float a, float h, float b, float* out) {
        out[0] = h;
        out[1] = 1.96f + a;
        out[2] = b;
    };
    turned(m, cylinderProfile(0.075f, -0.88f, 0.88f), 14, wood, across);
    turned(m, capsuleProfile(0.12f, -1.0f, -0.76f, 6), 14, wood, across);
    turned(m, capsuleProfile(0.12f, 0.76f, 1.0f, 6), 14, wood, across);
    return finish(std::move(m)).value();
}

namespace {

struct Builder {
    Model& m;
    const float* colour;
    // A flat polygon, counter-clockwise seen from outside, as triangles from its first corner.
    void poly(const std::vector<std::array<float, 3>>& p) {
        const auto first = static_cast<std::uint32_t>(m.positions.size() / 3);
        const auto& a = p[0];
        const auto& b = p[1];
        const auto& c = p[2];
        const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        const float wx = c[0] - a[0], wy = c[1] - a[1], wz = c[2] - a[2];
        float nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
        const float l = std::sqrt(nx * nx + ny * ny + nz * nz);
        nx /= l; ny /= l; nz /= l;
        for (const auto& v : p) {
            m.positions.insert(m.positions.end(), v.begin(), v.end());
            m.normals.insert(m.normals.end(), {nx, ny, nz});
            m.colors.insert(m.colors.end(), colour, colour + 3);
        }
        for (std::uint32_t i = 1; i + 1 < p.size(); ++i) {
            m.triangles.insert(m.triangles.end(), {first, first + i, first + i + 1});
        }
    }
};

}  // namespace

Model hardSurfaceSample() {
    Model m;
    m.name = "Hard-surface sample";
    const float grey[3] = {0.72f, 0.73f, 0.76f};
    Builder b{m, grey};
    using P = std::array<float, 3>;
    // The box: 2 wide, 1.6 tall, 2 deep.
    const float hw = 1.0f, bh = 1.6f, hd = 1.0f;
    b.poly({P{-hw, 0, hd}, P{hw, 0, hd}, P{hw, bh, hd}, P{-hw, bh, hd}});     // front
    b.poly({P{hw, 0, -hd}, P{-hw, 0, -hd}, P{-hw, bh, -hd}, P{hw, bh, -hd}}); // back
    b.poly({P{hw, 0, hd}, P{hw, 0, -hd}, P{hw, bh, -hd}, P{hw, bh, hd}});     // right
    b.poly({P{-hw, 0, -hd}, P{-hw, 0, hd}, P{-hw, bh, hd}, P{-hw, bh, -hd}}); // left
    b.poly({P{-hw, 0, -hd}, P{hw, 0, -hd}, P{hw, 0, hd}, P{-hw, 0, hd}});     // bottom
    b.poly({P{-hw, bh, hd}, P{hw, bh, hd}, P{hw, bh, -hd}, P{-hw, bh, -hd}}); // top
    // The tower: a square frustum, 1.2 across at the foot, 0.8 at the top, 1.7 tall.
    const float f = 0.6f, t = 0.4f, th = bh + 1.7f;
    b.poly({P{-f, bh, f}, P{f, bh, f}, P{t, th, t}, P{-t, th, t}});
    b.poly({P{f, bh, -f}, P{-f, bh, -f}, P{-t, th, -t}, P{t, th, -t}});
    b.poly({P{f, bh, f}, P{f, bh, -f}, P{t, th, -t}, P{t, th, t}});
    b.poly({P{-f, bh, -f}, P{-f, bh, f}, P{-t, th, t}, P{-t, th, -t}});
    b.poly({P{-t, th, t}, P{t, th, t}, P{t, th, -t}, P{-t, th, -t}});
    // The pipe: along z from inside the tower out to z = 1.3, with a bore.
    const float ro = 0.34f, ri = 0.22f, py = 2.55f, px = 0.0f, z0 = 0.2f, z1 = 1.3f;
    const int around = 24;
    for (int i = 0; i < around; ++i) {
        const float a0 = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(around);
        const float a1 = 2.0f * kPi * static_cast<float>(i + 1) / static_cast<float>(around);
        const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
        b.poly({P{px + ro * c0, py + ro * s0, z0}, P{px + ro * c1, py + ro * s1, z0},
                P{px + ro * c1, py + ro * s1, z1}, P{px + ro * c0, py + ro * s0, z1}});           // outside
        b.poly({P{px + ri * c1, py + ri * s1, z0}, P{px + ri * c0, py + ri * s0, z0},
                P{px + ri * c0, py + ri * s0, z1}, P{px + ri * c1, py + ri * s1, z1}});           // bore
        b.poly({P{px + ri * c0, py + ri * s0, z1}, P{px + ro * c0, py + ro * s0, z1},
                P{px + ro * c1, py + ro * s1, z1}, P{px + ri * c1, py + ri * s1, z1}});           // rim
        b.poly({P{px + ri * c1, py + ri * s1, z0}, P{px + ro * c1, py + ro * s1, z0},
                P{px + ro * c0, py + ro * s0, z0}, P{px + ri * c0, py + ri * s0, z0}});           // inside the tower
    }
    return finish(std::move(m)).value();
}

}  // namespace vfx::editor
