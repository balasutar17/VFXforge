// A 3D model to make effects on: reading OBJ and glTF, drawing it, and
// hiding the particles behind it.
#include <catch2/catch_amalgamated.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "vfx/Simulation.h"
#include "vfx/editor/Model.h"
#include "vfx/editor/Picture.h"
#include "vfx/editor/SpriteMesh.h"

using namespace vfx;
using namespace vfx::editor;
using J = nlohmann::json;

namespace {

// A cube two units across, its faces written as squares, with the top in
// a colour of its own.
const char* kCube = R"(# a cube
mtllib cube.mtl
v -1 -1 -1
v  1 -1 -1
v  1  1 -1
v -1  1 -1
v -1 -1  1
v  1 -1  1
v  1  1  1
v -1  1  1
usemtl grey
f 1 2 6 5
f 2 3 7 6
f 3 4 8 7
f 4 1 5 8
f 5 6 7 8
usemtl red
f -8 -5 -6 -7
)";

const char* kCubeMtl = R"(newmtl grey
Kd 0.5 0.5 0.5
newmtl red
Kd 1 0 0
)";

std::string bytesOf(const void* p, std::size_t n) { return std::string(static_cast<const char*>(p), n); }

// A glTF with one square (two triangles) in the given colour, moved up by
// `lift`, as a .glb or with its data written into the text.
std::string squareGltf(bool glb, float lift, const char* required = nullptr) {
    const float positions[12] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
    const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    std::string bin = bytesOf(positions, sizeof positions) + bytesOf(indices, sizeof indices);
    J doc = {{"asset", {{"version", "2.0"}}},
             {"scene", 0},
             {"scenes", J::array({{{"nodes", J::array({0})}}})},
             {"nodes", J::array({{{"mesh", 0}, {"translation", J::array({0, lift, 0})}}})},
             {"meshes", J::array({{{"primitives", J::array({{{"attributes", {{"POSITION", 0}}},
                                                              {"indices", 1},
                                                              {"material", 0}}})}}})},
             {"materials", J::array({{{"pbrMetallicRoughness", {{"baseColorFactor", J::array({1, 0, 0, 1})}}}}})},
             {"accessors", J::array({{{"bufferView", 0}, {"componentType", 5126}, {"count", 4}, {"type", "VEC3"}},
                                     {{"bufferView", 1}, {"componentType", 5123}, {"count", 6}, {"type", "SCALAR"}}})},
             {"bufferViews", J::array({{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 48}},
                                       {{"buffer", 0}, {"byteOffset", 48}, {"byteLength", 12}}})},
             {"buffers", J::array({{{"byteLength", bin.size()}}})}};
    if (required) {
        doc["extensionsRequired"] = J::array({required});
    }
    if (!glb) {
        static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string b64;
        for (std::size_t i = 0; i < bin.size(); i += 3) {
            const auto byte = [&](std::size_t k) { return k < bin.size() ? static_cast<unsigned char>(bin[k]) : 0u; };
            const unsigned v = (byte(i) << 16) | (byte(i + 1) << 8) | byte(i + 2);
            b64 += table[(v >> 18) & 63];
            b64 += table[(v >> 12) & 63];
            b64 += i + 1 < bin.size() ? table[(v >> 6) & 63] : '=';
            b64 += i + 2 < bin.size() ? table[v & 63] : '=';
        }
        doc["buffers"][0]["uri"] = "data:application/octet-stream;base64," + b64;
        return doc.dump();
    }
    std::string json = doc.dump();
    while (json.size() % 4) {
        json += ' ';
    }
    while (bin.size() % 4) {
        bin += '\0';
    }
    const auto u32 = [](std::uint32_t v) { return bytesOf(&v, 4); };
    return "glTF" + u32(2) + u32(static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + bin.size())) +
           u32(static_cast<std::uint32_t>(json.size())) + u32(0x4E4F534Au) + json +
           u32(static_cast<std::uint32_t>(bin.size())) + u32(0x004E4942u) + bin;
}

View flatView(int pixels, float unitsHigh, float centerY) {
    View v;
    v.width = v.height = static_cast<float>(pixels);
    v.centerX = 0;
    v.centerY = centerY;
    v.unitsHigh = unitsHigh;
    return v;
}

// Pixel coordinates of a world point in a flat view.
int pixelX(const View& v, float x) { return static_cast<int>(v.width * 0.5f + (x - v.centerX) * v.height / v.unitsHigh); }
int pixelY(const View& v, float y) { return static_cast<int>(v.height * 0.5f - (y - v.centerY) * v.height / v.unitsHigh); }

RenderFrame oneParticle(float x, float y, float z, bool flat) {
    RenderFrame frame;
    frame.flat = flat;
    SpriteInstance p;
    p.x = x;
    p.y = y;
    p.z = z;
    p.size = 0.2f;
    frame.instances.push_back(p);
    RenderBatch batch;
    batch.count = 1;
    frame.batches.push_back(batch);
    return frame;
}

}  // namespace

TEST_CASE("an OBJ model is read with its colours", "[model]") {
    auto cube = readObj(kCube, kCubeMtl);
    REQUIRE(cube.ok());
    const Model& m = cube.value();
    CHECK(m.triangleCount() == 12);  // six squares, each cut in two
    CHECK(m.minX == -1.0f);
    CHECK(m.maxY == 1.0f);
    CHECK(m.normals.size() == m.positions.size());
    CHECK(m.colors.size() == m.positions.size());
    int red = 0, grey = 0;
    for (std::size_t i = 0; i < m.corners(); ++i) {
        red += m.colors[i * 3] == 1.0f && m.colors[i * 3 + 1] == 0.0f ? 1 : 0;
        grey += m.colors[i * 3] == 0.5f ? 1 : 0;
        // Every normal has length 1.
        const float* n = &m.normals[i * 3];
        REQUIRE(std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) == Catch::Approx(1.0f).margin(1e-4));
    }
    CHECK(red == 4);   // the last face's four corners
    CHECK(grey == 20);

    // Without its MTL it is clay grey; a colour after a corner's position is used.
    auto plain = readObj("v 0 0 0 1 0 0\nv 1 0 0 1 0 0\nv 0 1 0 1 0 0\nf 1 2 3\n");
    REQUIRE(plain.ok());
    CHECK(plain.value().colors[0] == 1.0f);
    CHECK(plain.value().colors[1] == 0.0f);
    // Normals given in the file are used: this one faces down, against the
    // winding of the face.
    auto given = readObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 -1\nf 1//1 2//1 3//1\n");
    REQUIRE(given.ok());
    CHECK(given.value().normals[2] == -1.0f);
    auto computed = readObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1/1 2/2 3/3\n");
    REQUIRE(computed.ok());
    CHECK(computed.value().normals[2] == 1.0f);

    // What cannot be read says so.
    CHECK_FALSE(readObj("v 0 0 0\nv 1 0 0\n").ok());                  // no faces
    CHECK_FALSE(readObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 9\n").ok());  // a corner it does not have
    CHECK_FALSE(readObj("v 0 zero 0\nf 1 1 1\n").ok());
    CHECK_FALSE(readObj("v 0 0 0\nv 0 0 0\nv 0 0 0\nf 1 2 3\n").ok());  // all in one place
    const auto problem = readObj("");
    REQUIRE_FALSE(problem.ok());
    CHECK(problem.error().message.find("no triangles") != std::string::npos);
}

TEST_CASE("a glTF model is read, from a .glb or from text", "[model]") {
    for (const bool glb : {true, false}) {
        INFO(glb);
        auto square = readGltf(squareGltf(glb, 2.0f));
        REQUIRE(square.ok());
        const Model& m = square.value();
        CHECK(m.triangleCount() == 2);
        // The node's move is applied.
        CHECK(m.minY == 2.0f);
        CHECK(m.maxY == 3.0f);
        // The material's red, from linear light to a screen colour.
        CHECK(m.colors[0] == 1.0f);
        CHECK(m.colors[1] == 0.0f);
        // Normals are worked out: the square faces +z.
        CHECK(m.normals[2] == Catch::Approx(1.0f));
    }
    const auto compressed = readGltf(squareGltf(true, 0.0f, "KHR_draco_mesh_compression"));
    REQUIRE_FALSE(compressed.ok());
    CHECK(compressed.error().message.find("compress") != std::string::npos);
    CHECK_FALSE(readGltf("not a model").ok());
    CHECK_FALSE(readGltf("{\"asset\": {\"version\": \"2.0\"}}").ok());
    std::string cut = squareGltf(true, 0.0f);
    cut.resize(cut.size() - 20);
    CHECK_FALSE(readGltf(cut).ok());
}

TEST_CASE("a model file is read by its extension, with what lies beside it", "[model]") {
    const auto folder = std::filesystem::temp_directory_path() / "vfxforge_model_test";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "cube.obj") << kCube;
    std::ofstream(folder / "cube.mtl") << kCubeMtl;
    {
        std::ofstream glb(folder / "square.glb", std::ios::binary);
        glb << squareGltf(true, 0.0f);
    }
    std::ofstream(folder / "notes.txt") << "hello";

    auto cube = loadModel(folder / "cube.obj");
    REQUIRE(cube.ok());
    CHECK(cube.value().name == "cube.obj");
    CHECK(cube.value().colors[cube.value().colors.size() - 3] == 1.0f);  // red, from the MTL beside it
    auto square = loadModel(folder / "square.glb");
    REQUIRE(square.ok());
    CHECK(square.value().triangleCount() == 2);
    const auto text = loadModel(folder / "notes.txt");
    REQUIRE_FALSE(text.ok());
    CHECK(text.error().message.find(".obj") != std::string::npos);
    CHECK_FALSE(loadModel(folder / "missing.obj").ok());
    std::filesystem::remove_all(folder);
}

TEST_CASE("a model is drawn standing where it is placed, and knows how far away it is", "[model]") {
    const Model dummy = practiceDummy();
    CHECK(dummy.triangleCount() > 1500);
    CHECK(dummy.triangleCount() < 6000);
    CHECK(dummy.minY == Catch::Approx(0.0f).margin(1e-4));
    CHECK(dummy.maxY == Catch::Approx(2.94f).margin(0.01));

    // Three units tall, feet on y = 0, seen from the front.
    const View view = flatView(200, 4.0f, 1.5f);  // 50 pixels a unit
    ModelPlace place;
    const ModelPicture drawn = drawModel(dummy, place, view, true, 200, 200);
    REQUIRE(drawn.picture.width == 200);
    REQUIRE(drawn.depth.size() == 200u * 200u);
    const auto alpha = [&](float x, float y) { return drawn.picture.pixel(pixelX(view, x), pixelY(view, y))[3]; };
    const auto depthAt = [&](float x, float y) {
        return drawn.depth[static_cast<std::size_t>(pixelY(view, y)) * 200u + static_cast<std::size_t>(pixelX(view, x))];
    };
    CHECK(alpha(0.0f, 1.6f) == 255);   // the body
    CHECK(alpha(0.0f, 2.9f) == 255);   // the top of the head, 3 units up
    CHECK(alpha(0.0f, 3.1f) == 0);     // above it
    CHECK(alpha(0.0f, 0.05f) == 255);  // the base, on the ground
    CHECK(alpha(1.2f, 1.6f) == 0);     // beside it
    CHECK(alpha(-1.85f, 3.4f) == 0);
    // The front of the body is nearer than the middle: in a flat view a
    // larger z is nearer, and the body is about 0.45 units deep.
    CHECK(depthAt(0.0f, 1.6f) == Catch::Approx(-0.45f).margin(0.05));
    CHECK(std::isinf(depthAt(1.2f, 1.6f)));
    // Smoothed edges: somewhere along the side of the body is part-covered.
    int part = 0;
    for (int x = 100; x < 160; ++x) {
        const auto a = drawn.picture.pixel(x, pixelY(view, 1.6f))[3];
        part += a > 0 && a < 255 ? 1 : 0;
    }
    CHECK(part >= 1);
    // Lit from the upper left: its left side is lighter than its right.
    const auto light = [&](float x, float y) {
        const std::uint8_t* p = drawn.picture.pixel(pixelX(view, x), pixelY(view, y));
        return p[0] + p[1] + p[2];
    };
    CHECK(light(-0.3f, 1.8f) > light(0.33f, 1.4f));

    // Moved and made bigger.
    place.x = 1.0f;
    place.height = 1.5f;
    const ModelPicture moved = drawModel(dummy, place, view, true, 200, 200);
    CHECK(moved.picture.pixel(pixelX(view, 1.0f), pixelY(view, 0.8f))[3] == 255);
    CHECK(moved.picture.pixel(pixelX(view, 1.0f), pixelY(view, 1.7f))[3] == 0);
    CHECK(moved.picture.pixel(pixelX(view, 0.0f), pixelY(view, 0.8f))[3] == 0);

    // Turned a quarter: the arms point at the viewer, so the figure is narrow.
    ModelPlace turned;
    turned.turn = 90.0f;
    const ModelPicture side = drawModel(dummy, turned, view, true, 200, 200);
    CHECK(drawn.picture.pixel(pixelX(view, 0.9f), pixelY(view, 1.96f))[3] == 255);  // an arm, from the front
    CHECK(side.picture.pixel(pixelX(view, 0.9f), pixelY(view, 1.96f))[3] == 0);

    // Nothing to draw, or nowhere to draw it.
    CHECK(drawModel(Model{}, place, view, true, 200, 200).empty());
    CHECK(drawModel(dummy, place, view, true, 0, 200).empty());
}

TEST_CASE("particles behind the model are hidden, and those just behind fade", "[model]") {
    const Model dummy = practiceDummy();
    const View view = flatView(200, 4.0f, 1.5f);
    const ModelPicture drawn = drawModel(dummy, ModelPlace{}, view, true, 200, 200);
    const float cx = static_cast<float>(pixelX(view, 0.0f)) + 0.5f, cy = static_cast<float>(pixelY(view, 1.6f)) + 0.5f;
    const float surface = drawn.depth[static_cast<std::size_t>(cy) * 200u + static_cast<std::size_t>(cx)];
    CHECK(drawn.seen(cx, cy, surface - 1.0f, 200, 200) == 1.0f);  // in front
    CHECK(drawn.seen(cx, cy, surface, 200, 200) == 1.0f);         // on it
    CHECK(drawn.seen(cx, cy, surface + kBehindFade * 0.5f, 200, 200) == Catch::Approx(0.5f).margin(0.01));
    CHECK(drawn.seen(cx, cy, surface + 1.0f, 200, 200) == 0.0f);  // behind
    CHECK(drawn.seen(10, 10, 100.0f, 200, 200) == 1.0f);          // nothing there
    CHECK(drawn.seen(-5, 10, 100.0f, 200, 200) == 1.0f);          // off the picture
    // A view drawn at twice its size (a sharp screen) is looked up in proportion.
    const ModelPicture sharp = drawModel(dummy, ModelPlace{}, view, true, 400, 400);
    CHECK(sharp.seen(cx, cy, surface + 1.0f, 200, 200) == 0.0f);
    CHECK(sharp.seen(10, 10, surface + 1.0f, 200, 200) == 1.0f);

    // Drawn by the picture renderer: a red particle behind the body is not
    // seen, one in front of it is, one beside it is; from the other side of
    // a 3D camera the two swap. Pixel by pixel, so one part of a particle
    // can be hidden and the rest not.
    View orbit;
    orbit.width = orbit.height = 200;
    orbit.targetX = 0;
    orbit.targetY = 1.6f;
    orbit.targetZ = 0;
    orbit.yaw = 0;
    orbit.pitch = 0;
    orbit.distance = 8;
    const auto redAt = [&](const View& v, float x, float y, float z, float size) {
        RenderFrame frame = oneParticle(x, y, z, false);
        frame.instances[0].r = 1.0f;
        frame.instances[0].g = frame.instances[0].b = 0.0f;
        frame.instances[0].size = size;
        frame.batches[0].shape = SpriteShape::Disc;
        const ModelPicture drawnHere = drawModel(dummy, ModelPlace{}, v, false, 200, 200);
        SpriteMesh m;
        buildSpriteMesh(frame, v, m);
        const Picture out = drawPictureOver(m, drawnHere, true, 200, 200, ScreenColor{0, 0, 0});
        float px = 0, py = 0, sc = 0;
        REQUIRE(projectPoint(v, false, x, y, z, px, py, sc));
        const std::uint8_t* c = out.pixel(static_cast<int>(px), static_cast<int>(py));
        return c[0] > 200 && c[1] < 60;
    };
    CHECK_FALSE(redAt(orbit, 0.0f, 1.6f, -1.0f, 0.2f));  // behind the body
    CHECK(redAt(orbit, 0.0f, 1.6f, 1.0f, 0.2f));         // in front of it
    CHECK(redAt(orbit, 1.6f, 1.6f, -1.0f, 0.2f));        // beside it, though further back
    orbit.yaw = 180;
    CHECK(redAt(orbit, 0.0f, 1.6f, -1.0f, 0.2f));
    CHECK_FALSE(redAt(orbit, 0.0f, 1.6f, 1.0f, 0.2f));
    orbit.yaw = 0;
    // A big particle behind the body: hidden where the body is, seen round it.
    {
        const ModelPicture front = drawModel(dummy, ModelPlace{}, orbit, false, 200, 200);
        RenderFrame frame = oneParticle(0.0f, 1.6f, -1.0f, false);
        frame.instances[0].r = 1.0f;
        frame.instances[0].g = frame.instances[0].b = 0.0f;
        frame.instances[0].size = 2.4f;
        frame.batches[0].shape = SpriteShape::Disc;
        SpriteMesh m;
        buildSpriteMesh(frame, orbit, m);
        const Picture out = drawPictureOver(m, front, true, 200, 200, ScreenColor{0, 0, 0});
        float px = 0, py = 0, sc = 0;
        REQUIRE(projectPoint(orbit, false, 0.0f, 1.6f, -1.0f, px, py, sc));
        CHECK(out.pixel(static_cast<int>(px), static_cast<int>(py))[0] < 255);  // the body, not red
        CHECK(out.pixel(static_cast<int>(px), static_cast<int>(py))[1] > 60);
        const int side = static_cast<int>(px + 0.8f * sc);  // beside the body, inside the disc
        CHECK(out.pixel(side, static_cast<int>(py))[0] > 200);
        CHECK(out.pixel(side, static_cast<int>(py))[1] < 60);
        // Not hiding: the disc is drawn over the body.
        const Picture over = drawPictureOver(m, front, false, 200, 200, ScreenColor{0, 0, 0});
        CHECK(over.pixel(static_cast<int>(px), static_cast<int>(py))[1] < 60);
    }
    // Every corner of a particle knows how far away it is: in 3D as one
    // over the distance.
    {
        SpriteMesh m;
        buildSpriteMesh(oneParticle(0.0f, 1.6f, 1.0f, false), orbit, m);
        REQUIRE(m.vertices.size() == 4);
        for (const SpriteVertex& v : m.vertices) {
            CHECK(depthFromKey(v.depth, false) == Catch::Approx(7.0f).margin(1e-3));
        }
        buildSpriteMesh(oneParticle(0.0f, 1.6f, 0.0f, true), view, m);
        CHECK(depthFromKey(m.vertices[0].depth, true) == 0.0f);
    }
}

TEST_CASE("an effect is drawn over its model", "[model][picture]") {
    const Model dummy = practiceDummy();
    const View view = flatView(100, 4.0f, 1.5f);
    const ModelPicture drawn = drawModel(dummy, ModelPlace{}, view, true, 200, 200);  // drawn sharper
    SpriteMesh mesh;
    RenderFrame frame = oneParticle(1.4f, 0.5f, 0.0f, true);
    frame.instances[0].r = 1.0f;
    frame.instances[0].g = frame.instances[0].b = 0.0f;
    frame.instances[0].size = 0.6f;
    buildSpriteMesh(frame, view, mesh);
    const Picture over = drawPictureOver(mesh, drawn, true, 100, 100, ScreenColor{0, 0, 1});
    REQUIRE(over.width == 100);
    // The model where it is, the background beside it, the particle on top.
    const std::uint8_t* body = over.pixel(50, pixelY(view, 1.6f));
    CHECK(body[0] > 120);
    CHECK(body[2] < 200);
    CHECK(over.pixel(5, 5)[2] == 255);
    const std::uint8_t* particle = over.pixel(pixelX(view, 1.4f), pixelY(view, 0.5f));
    CHECK(particle[0] > 200);
    CHECK(particle[2] < 60);
}
