// The Shader tab: the library of families and presets, the animation, saving, the live
// preview, and the Unity export.
#include <catch2/catch_amalgamated.hpp>

#include <cmath>
#include <set>
#include <string>

#include "vfx/editor/ShaderExport.h"
#include "vfx/editor/ShaderLib.h"
#include "vfx/editor/ShaderPreview.h"

using namespace vfx::editor;
using namespace vfx::editor::shader;

namespace {

double meanDifference(const Picture& a, const Picture& b) {
    REQUIRE(a.width == b.width);
    REQUIRE(a.height == b.height);
    double sum = 0;
    for (std::size_t i = 0; i < a.rgba.size(); ++i) {
        if (i % 4 == 3) continue;
        sum += std::fabs(static_cast<double>(a.rgba[i]) - static_cast<double>(b.rgba[i]));
    }
    return sum / static_cast<double>(a.rgba.size() / 4 * 3);
}

Picture draw(const Settings& s, double time = 1.0, int size = 64) {
    PreviewOptions o;
    o.width = o.height = size;
    o.time = time;
    return renderPreview(s, o);
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

}  // namespace

TEST_CASE("the library has the six families with their presets", "[shader]") {
    REQUIRE(families().size() == 6);
    std::set<int> numbers;
    std::size_t presets = 0;
    for (const Family& f : families()) {
        numbers.insert(f.number);
        presets += f.presets.size();
        CHECK(!f.presets.empty());
        CHECK(!f.limits.empty());
    }
    CHECK(numbers == std::set<int>{11, 12, 14, 15, 16, 18});
    CHECK(presets == 39);
    CHECK(findFamily("shield")->presets.size() == 6);
    CHECK(findFamily("ui")->presets.size() == 7);
}

TEST_CASE("parameter tables are consistent", "[shader]") {
    for (const Family& f : families()) {
        INFO(f.id);
        CHECK(f.params.size() <= static_cast<std::size_t>(kMaxParams));
        std::set<std::string> keys, macros;
        for (std::size_t i = 0; i < f.params.size(); ++i) {
            const ParamDef& p = f.params[i];
            CHECK(p.index == static_cast<int>(i));
            CHECK(keys.insert(p.key).second);
            CHECK(macros.insert(p.macro).second);
            CHECK((i < static_cast<std::size_t>(kCommonCount)) == p.common);
            CHECK(!p.label.empty());
            CHECK(!p.tip.empty());
            if (p.kind == ParamKind::Number) {
                CHECK(p.min < p.max);
                CHECK(p.def >= p.min);
                CHECK(p.def <= p.max);
            }
        }
    }
}

TEST_CASE("the preview reads every parameter at the index the library gives it", "[shader]") {
    const auto& table = previewIndices();
    std::size_t checked = 0;
    for (const Family& f : families()) {
        for (const ParamDef& p : f.params) {
            INFO(p.macro);
            REQUIRE(table.count(p.macro) == 1);
            CHECK(table.at(p.macro) == p.index);
            ++checked;
        }
    }
    CHECK(checked > 100);
}

TEST_CASE("every preset starts valid and presets differ from each other", "[shader]") {
    for (const Family& f : families()) {
        std::vector<Picture> pics;
        for (const Preset& p : f.presets) {
            Settings s = presetSettings(f, p);
            INFO(f.id << "/" << p.id);
            CHECK(s.family == f.id);
            CHECK(s.preset == p.id);
            CHECK(s.values.size() == f.params.size());
            for (const std::string& n : validateSettings(s)) CHECK(n.find("outside its limits") == std::string::npos);
            pics.push_back(draw(s));
        }
        for (std::size_t a = 0; a < pics.size(); ++a) {
            for (std::size_t b = a + 1; b < pics.size(); ++b) {
                INFO(f.id << ": " << f.presets[a].id << " vs " << f.presets[b].id);
                CHECK(meanDifference(pics[a], pics[b]) > 1.5);
            }
        }
    }
}

TEST_CASE("the preview is deterministic and answers to time and parameters", "[shader]") {
    for (const Family& f : families()) {
        INFO(f.id);
        Settings s = presetSettings(f, f.presets.front());
        const Picture a = draw(s, 1.3), b = draw(s, 1.3);
        CHECK(a.rgba == b.rgba);
        // a different moment: something moves
        CHECK(meanDifference(a, draw(s, 2.1)) > 0.05);
        // the glow intensity (every family has one under its own name) changes the picture
        const std::string key = f.id == "ui" ? "glowIntensity" : f.id == "ice" ? "edgeHighlight" : "intensity";
        const ParamDef* p = findParam(f, key);
        REQUIRE(p);
        Settings dim = s, bright = s;
        dim.values[static_cast<std::size_t>(p->index)][0] = static_cast<float>(p->min);
        bright.values[static_cast<std::size_t>(p->index)][0] = static_cast<float>(p->max);
        CHECK(meanDifference(draw(dim), draw(bright)) > 0.5);
    }
}

TEST_CASE("a colour changes the preview's colour", "[shader]") {
    const Family& f = *findFamily("hologram");
    Settings s = presetSettings(f, f.presets.front());
    Settings red = s;
    const ParamDef* p = findParam(f, "color");
    REQUIRE(p);
    red.values[static_cast<std::size_t>(p->index)] = {1.0f, 0.1f, 0.1f, 1.0f};
    const Picture a = draw(s), b = draw(red);
    long ra = 0, rb = 0, ba = 0, bb = 0;
    for (std::size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        ra += a.rgba[i];
        ba += a.rgba[i + 2];
        rb += b.rgba[i];
        bb += b.rgba[i + 2];
    }
    CHECK(rb > ra);
    CHECK(bb < ba);
}

TEST_CASE("the shield flares when hit and settles after", "[shader]") {
    const Family& f = *findFamily("shield");
    Settings s = presetSettings(f, *findPreset(f, "energy-shield"));
    PreviewOptions o;
    o.width = o.height = 64;
    o.hitAge = 0.05;
    const Picture fresh = renderPreview(s, o);
    o.hitAge = 20.0;
    const Picture settled = renderPreview(s, o);
    CHECK(meanDifference(fresh, settled) > 2.0);
}

TEST_CASE("quality profiles change the work and the picture", "[shader]") {
    const Family& f = *findFamily("ice");
    Settings s = presetSettings(f, f.presets.front());
    s.quality = 0;
    const Picture mobile = draw(s);
    s.quality = 3;
    const Picture cinematic = draw(s);
    CHECK(meanDifference(mobile, cinematic) > 0.2);
    CHECK(qualityProfiles().size() == 4);
}

TEST_CASE("every preview shape draws something", "[shader]") {
    const Family& f = *findFamily("emissive");
    Settings s = presetSettings(f, f.presets.front());
    for (const std::string& shape : previewShapes()) {
        PreviewOptions o;
        o.width = o.height = 48;
        o.shape = shape;
        o.time = 1.0;
        const Picture lit = renderPreview(s, o);
        Settings off = s;
        off.values[static_cast<std::size_t>(findParam(f, "intensity")->index)][0] = 0;
        off.values[static_cast<std::size_t>(findParam(f, "rimIntensity")->index)][0] = 0;
        off.values[static_cast<std::size_t>(findParam(f, "coreGlow")->index)][0] = 0;
        INFO(shape);
        CHECK(meanDifference(lit, renderPreview(off, o)) > 0.5);
    }
}

TEST_CASE("keyframes, easing and looping", "[shader][animation]") {
    const Family& f = *findFamily("hologram");
    Settings s = presetSettings(f, f.presets.front());
    const ParamDef* p = findParam(f, "intensity");
    REQUIRE(p);
    Track t;
    t.param = "intensity";
    t.keys = {{0.0, 1.0f, Easing::Linear}, {2.0, 5.0f, Easing::Linear}};
    s.animation.tracks = {t};
    s.animation.duration = 4.0;
    const auto at = [&](double time) { return valuesAt(s, time)[static_cast<std::size_t>(p->index)][0]; };
    CHECK(at(0.0) == Catch::Approx(1.0f));
    CHECK(at(1.0) == Catch::Approx(3.0f));
    CHECK(at(2.0) == Catch::Approx(5.0f));
    CHECK(at(3.5) == Catch::Approx(5.0f));  // after the last key it holds

    CHECK(ease(Easing::Linear, 0.5f) == Catch::Approx(0.5f));
    CHECK(ease(Easing::Smooth, 0.5f) == Catch::Approx(0.5f));
    CHECK(ease(Easing::EaseIn, 0.5f) == Catch::Approx(0.25f));
    CHECK(ease(Easing::EaseOut, 0.5f) == Catch::Approx(0.75f));
    CHECK(ease(Easing::Smooth, -3.0f) == 0.0f);
    CHECK(ease(Easing::Smooth, 9.0f) == 1.0f);

    s.animation.loop = LoopMode::Loop;
    CHECK(animationTime(s.animation, 5.0) == Catch::Approx(1.0));
    s.animation.loop = LoopMode::PingPong;
    CHECK(animationTime(s.animation, 5.0) == Catch::Approx(3.0));
    CHECK(animationTime(s.animation, 1.0) == Catch::Approx(1.0));
    s.animation.loop = LoopMode::Once;
    CHECK(animationTime(s.animation, 9.0) == Catch::Approx(4.0));

    // scrubbing is exact: the same moment draws the same picture, whatever came before
    s.animation.loop = LoopMode::Loop;
    const Picture first = draw(s, 1.0);
    draw(s, 3.7);
    CHECK(draw(s, 1.0).rgba == first.rgba);
    // and the keyframes show in the picture
    CHECK(meanDifference(draw(s, 0.0), draw(s, 2.0)) > 0.5);
}

TEST_CASE("saving and loading keeps everything", "[shader]") {
    for (const Family& f : families()) {
        Settings s = presetSettings(f, f.presets.back());
        s.name = "Name with \"quotes\" and spaces";
        s.quality = 2;
        s.animation.duration = 3.5;
        s.animation.loop = LoopMode::PingPong;
        s.animation.speed = 1.5;
        Track t;
        t.param = "intensity";
        t.keys = {{0.0, 0.5f, Easing::EaseIn}, {1.5, 2.0f, Easing::Smooth}};
        if (findParam(f, "intensity")) s.animation.tracks = {t};
        const std::string text = writeSettings(s);
        auto read = readSettings(text);
        INFO(f.id);
        REQUIRE(read.ok());
        const ReadSettings& r = read.value();
        CHECK(r.notes.empty());
        CHECK(r.settings.family == s.family);
        CHECK(r.settings.preset == s.preset);
        CHECK(r.settings.name == s.name);
        CHECK(r.settings.quality == 2);
        CHECK(r.settings.blend == s.blend);
        CHECK(r.settings.animation.loop == LoopMode::PingPong);
        CHECK(r.settings.animation.duration == Catch::Approx(3.5));
        CHECK(r.settings.animation.tracks.size() == s.animation.tracks.size());
        REQUIRE(r.settings.values.size() == s.values.size());
        for (std::size_t i = 0; i < s.values.size(); ++i) {
            for (int k = 0; k < 4; ++k) CHECK(r.settings.values[i][static_cast<std::size_t>(k)] == Catch::Approx(s.values[i][static_cast<std::size_t>(k)]));
        }
        CHECK(writeSettings(r.settings) == text);
    }
}

TEST_CASE("reading repairs what is out of range and refuses what is not a shader", "[shader]") {
    const Family& f = *findFamily("hologram");
    Settings s = presetSettings(f, f.presets.front());
    std::string text = writeSettings(s);
    const auto at = text.find("\"intensity\"");
    REQUIRE(at != std::string::npos);
    const auto colon = text.find(':', at);
    const auto end = text.find_first_of(",\n}", colon);
    text.replace(colon + 1, end - colon - 1, " 9999");
    auto read = readSettings(text);
    REQUIRE(read.ok());
    CHECK(!read.value().notes.empty());
    const ParamDef* p = findParam(f, "intensity");
    CHECK(read.value().settings.values[static_cast<std::size_t>(p->index)][0] <= static_cast<float>(p->max));

    CHECK(!readSettings("not json").ok());
    CHECK(!readSettings("{\"format\":\"something else\"}").ok());
    CHECK(!readSettings("{\"format\":\"vfxforge.shader\",\"family\":\"nope\"}").ok());
}

TEST_CASE("the Unity export writes a shader whose properties match the settings", "[shader][export]") {
    for (const Family& f : families()) {
        for (const Preset& pr : f.presets) {
            Settings s = presetSettings(f, pr);
            auto made = exportShader(s);
            INFO(f.id << "/" << pr.id);
            REQUIRE(made.ok());
            const ShaderExport& e = made.value();
            std::string shader, mat, meta, anim, readme;
            bool animator = false, demo = false;
            for (const auto& file : e.files) {
                if (file.path == e.folder + e.name + ".shader") shader = file.text;
                if (file.path == e.folder + e.name + ".mat") mat = file.text;
                if (file.path == e.folder + e.name + ".shader.meta") meta = file.text;
                if (file.path == e.folder + e.name + ".anim.json") anim = file.text;
                if (file.path == e.folder + "README.txt") readme = file.text;
                if (file.path == "Runtime/VFXForgeShaderAnimator.cs") animator = true;
                if (file.path == "Editor/VFXForgeShaderDemo.cs") demo = true;
            }
            CHECK(!shader.empty());
            CHECK(!mat.empty());
            CHECK(!anim.empty());
            CHECK(!readme.empty());
            CHECK(animator);
            CHECK(demo);
            CHECK(contains(shader, "Shader \"VFX Forge/Shaders/" + e.name + "\""));
            CHECK(contains(shader, shaderCoreSource().substr(0, 200)));
            CHECK(contains(shader, f.function + "(I, P)"));
            CHECK(contains(shader, pr.blend == BlendMode::Additive ? "Blend SrcAlpha One\n" : "Blend SrcAlpha OneMinusSrcAlpha"));
            CHECK(contains(meta, "guid: " + shaderGuid(e.name)));
            CHECK(contains(mat, "guid: " + shaderGuid(e.name)));
            for (const ParamDef& p : f.params) {
                INFO(p.macro);
                CHECK(contains(shader, "_" + p.macro + " ("));
                CHECK(contains(shader, "#define " + p.macro + " " + std::to_string(p.index) + "\n"));
                CHECK(contains(shader, "P.v[" + p.macro + "] ="));
                CHECK(contains(mat, "- _" + p.macro + ": "));
            }
            CHECK(contains(mat, "m_CustomRenderQueue: 3000"));
            CHECK(!e.notes.empty());
            CHECK(contains(readme, "NOT SUPPORTED"));
        }
    }
}

TEST_CASE("the material holds the values the preview starts with", "[shader][export]") {
    const Family& f = *findFamily("emissive");
    Settings s = presetSettings(f, f.presets.front());
    const ParamDef* p = findParam(f, "intensity");
    s.values[static_cast<std::size_t>(p->index)][0] = 3.25f;
    const std::string mat = generateMaterial(s, shaderGuid("X"));
    CHECK(contains(mat, "- _EM_intensity: 3.25\n"));
    const ParamDef* c = findParam(f, "color1");
    s.values[static_cast<std::size_t>(c->index)] = {0.5f, 0.25f, 0.125f, 1.0f};
    CHECK(contains(generateMaterial(s, shaderGuid("X")), "- _EM_color1: {r: 0.5, g: 0.25, b: 0.125, a: 1}"));
}

TEST_CASE("the animation file carries the keyframes", "[shader][export]") {
    const Family& f = *findFamily("shield");
    Settings s = presetSettings(f, f.presets.front());
    Track t;
    t.param = "hitIntensity";
    t.keys = {{0.0, 1.0f, Easing::Smooth}, {1.0, 4.0f, Easing::EaseOut}};
    s.animation.tracks = {t};
    s.animation.duration = 2.0;
    s.animation.loop = LoopMode::Once;
    const std::string json = generateAnimationJson(s);
    CHECK(contains(json, "\"property\": \"FF_hitIntensity\""));
    CHECK(contains(json, "\"loop\": 2"));
    CHECK(contains(json, "\"hitRadiusProperty\": \"FF_hitRadius\""));
    CHECK(contains(json, "\"easing\": 3"));
}

TEST_CASE("unsupported things are reported, not dropped", "[shader][export]") {
    {
        const Family& f = *findFamily("ui");
        auto made = exportShader(presetSettings(f, f.presets.front()));
        REQUIRE(made.ok());
        std::string all;
        for (const auto& n : made.value().notes) all += n + "\n";
        CHECK(contains(all, "Sprite"));
        CHECK(contains(all, "HDRP"));
        const std::string shader = generateShader(presetSettings(f, f.presets.front()));
        CHECK(contains(shader, "UnityUI.cginc"));
        CHECK(contains(shader, "Stencil"));
        CHECK(contains(shader, "UNITY_UI_CLIP_RECT"));
        CHECK(contains(shader, "ZTest [unity_GUIZTestMode]"));
    }
    {
        const Family& f = *findFamily("particle");
        auto made = exportShader(presetSettings(f, f.presets.front()));
        REQUIRE(made.ok());
        std::string all;
        for (const auto& n : made.value().notes) all += n + "\n";
        CHECK(contains(all, "AgePercent"));
        CHECK(contains(all, "does not spawn"));
    }
    {
        const Family& f = *findFamily("ice");
        Settings s = presetSettings(f, f.presets.front());
        s.quality = 0;
        auto made = exportShader(s);
        REQUIRE(made.ok());
        std::string all;
        for (const auto& n : made.value().notes) all += n + "\n";
        CHECK(contains(all, "Mobile profile"));
        CHECK(contains(generateShader(s), "#define VFX_QUALITY 0"));
    }
    Settings bad;
    bad.family = "nope";
    CHECK(!exportShader(bad).ok());
    Settings wrong = presetSettings(*findFamily("ice"), findFamily("ice")->presets.front());
    wrong.values.resize(3);
    CHECK(!exportShader(wrong).ok());
}

TEST_CASE("safe names and stable GUIDs", "[shader][export]") {
    CHECK(safeName("Hologram Globe!") == "Hologram_Globe");
    CHECK(safeName("  ") == "Shader");
    CHECK(safeName("3D glow") == "S_3D_glow");
    CHECK(shaderGuid("A") == shaderGuid("A"));
    CHECK(shaderGuid("A") != shaderGuid("B"));
    CHECK(shaderGuid("A").size() == 32);
}
