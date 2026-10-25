// The AI Shader Artist's engine: the commands a model may send are checked against the real
// shader library and applied to a copy; the provider requests and replies are built and read
// here without a network.
#include <catch2/catch_amalgamated.hpp>

#include <nlohmann/json.hpp>

#include "vfx/editor/ShaderAi.h"
#include "vfx/editor/ShaderExport.h"
#include "vfx/editor/ShaderLib.h"

using namespace vfx::editor::shader;
using namespace vfx::editor::shader::ai;
using J = nlohmann::json;

namespace {

Plan plan(const J& j) {
    auto p = parsePlan(j.dump());
    REQUIRE(p.ok());
    return p.value();
}

Context blank(Mode mode = Mode::Generate) {
    Context c;
    c.settings = defaultSettings(*findFamily("layers"));
    c.mode = mode;
    return c;
}

const J goldenRing = J::parse(R"({
  "summary": "Golden magic ring",
  "explanation": "A glow, three rings, a beam and a shockwave.",
  "ops": [
    {"op":"new_shader","name":"Golden Ring"},
    {"op":"add_layer","element":"Radial gradient","name":"Glow","colorA":"#ffcc33","colorB":"#ff6600","params":{"size":1.2,"intensity":1.4}},
    {"op":"add_layer","element":"Ring","name":"Outer ring","colorA":"#ffd966","colorB":"#ffffff","params":{"size":0.9,"thick":0.08,"spin":-40}},
    {"op":"add_layer","element":"Segmented ring","name":"Inner ring","colorA":"#ffaa00","colorB":"#fff2b0","params":{"size":0.6,"count":12,"spin":60}},
    {"op":"add_layer","element":"Shockwave","name":"Burst","blend":"Add"},
    {"op":"animate","layer":"Burst","param":"size","keys":[{"time":0,"value":0.2},{"time":2,"value":1.6,"easing":"ease_out"}]},
    {"op":"animate","layer":"Burst","param":"opacity","keys":[{"time":0,"value":1},{"time":2,"value":0}]},
    {"op":"set_animation","duration":2,"loop":"once"}
  ]})");

}  // namespace

TEST_CASE("a generated plan builds real, editable layers and animation", "[shader][ai]") {
    const Applied a = applyPlan(blank(), plan(goldenRing));
    CHECK(a.refused.empty());
    CHECK(a.applied == 8);
    const Settings& s = a.settings;
    CHECK(s.name == "Golden Ring");
    CHECK(layerTop(s) == 4);
    CHECK(layerName(s, 0) == "Glow");
    CHECK(layerName(s, 2) == "Inner ring");
    const Family& f = *findFamily("layers");
    // The settings landed in the shader's real values, within their limits.
    CHECK(s.values[static_cast<std::size_t>(layerParamIndex(f, 1, "thick"))][0] == Catch::Approx(0.08f));
    CHECK(s.values[static_cast<std::size_t>(layerParamIndex(f, 1, "spin"))][0] == Catch::Approx(-40.0f));
    CHECK(s.values[static_cast<std::size_t>(layerParamIndex(f, 2, "count"))][0] == Catch::Approx(12.0f));
    const auto& col = s.values[static_cast<std::size_t>(layerParamIndex(f, 1, "colA"))];
    CHECK(toHex(col) == "#ffd966");
    // Animation tracks sit on the right layer and the timeline was set.
    REQUIRE(s.animation.tracks.size() == 2);
    CHECK(s.animation.tracks[0].param == "L3_size");
    CHECK(s.animation.tracks[0].keys.back().easing == Easing::EaseOut);
    CHECK(s.animation.duration == Catch::Approx(2.0));
    CHECK(s.animation.loop == LoopMode::Once);
    CHECK(a.touchedLayers.size() >= 4);
    CHECK(!a.changes.empty());
    // It can be saved, reopened and exported, like any shader.
    auto back = readSettings(writeSettings(s));
    REQUIRE(back.ok());
    CHECK(back.value().settings.values == s.values);
    CHECK(back.value().settings.layerNames == s.layerNames);
    CHECK(back.value().settings.animation.tracks.size() == 2);
    auto exported = exportShader(s);
    CHECK(exported.ok());
    CHECK(validateSettings(s).size() < 20);
}

TEST_CASE("commands that are not valid are refused one by one and the rest still apply", "[shader][ai]") {
    const Plan p = plan(J::parse(R"({"summary":"x","ops":[
        {"op":"new_shader"},
        {"op":"add_layer","element":"Dragon","name":"Nope"},
        {"op":"add_layer","element":"Ring","name":"Fine","params":{"size":99,"bogus":1}},
        {"op":"edit_layer","layer":"Missing","params":{"size":1}},
        {"op":"edit_layer","layer":"Fine","blend":"Wobble"},
        {"op":"animate","layer":"Fine","param":"colA","keys":[{"time":0,"value":1}]},
        {"op":"animate","layer":"Fine","param":"size","keys":[{"time":"soon","value":1}]},
        {"op":"explode"},
        {"op":"set_global","params":{"brightness":50,"nonsense":1}}
    ]})"));
    const Applied a = applyPlan(blank(), p);
    CHECK(layerTop(a.settings) == 1);
    CHECK(a.refused.size() >= 7);
    const Family& f = *findFamily("layers");
    // 99 was limited to the setting's own maximum.
    CHECK(a.settings.values[static_cast<std::size_t>(layerParamIndex(f, 0, "size"))][0] == Catch::Approx(3.0f));
    const ParamDef* brightness = findParam(f, "brightness");
    CHECK(a.settings.values[static_cast<std::size_t>(brightness->index)][0] == Catch::Approx(static_cast<float>(brightness->max)));
    CHECK(a.settings.animation.tracks.empty());
}

TEST_CASE("the shader holds at most eight layers", "[shader][ai]") {
    J ops = J::array({{{"op", "new_shader"}}});
    for (int i = 0; i < 10; ++i) ops.push_back({{"op", "add_layer"}, {"element", "Circle"}, {"name", "L" + std::to_string(i)}});
    const Applied a = applyPlan(blank(), plan({{"summary", "x"}, {"ops", ops}}));
    CHECK(layerTop(a.settings) == kLayerCount);
    CHECK(a.refused.size() == 2);
}

TEST_CASE("a selected layer is the only one a refinement may change", "[shader][ai]") {
    Context c = blank();
    c.settings = applyPlan(c, plan(goldenRing)).settings;
    c.mode = Mode::Refine;
    c.selectedLayer = 1;  // Outer ring
    const J edit = J::parse(R"({"summary":"x","ops":[
        {"op":"edit_layer","layer":"Outer ring","params":{"thick":0.2}},
        {"op":"edit_layer","layer":"Glow","params":{"intensity":3}},
        {"op":"set_global","params":{"brightness":2}},
        {"op":"animate","layer":"Outer ring","param":"rot","keys":[{"time":0,"value":0},{"time":2,"value":180}]},
        {"op":"animate","layer":"Inner ring","param":"rot","keys":[{"time":0,"value":0},{"time":2,"value":180}]}
    ]})");
    const Applied a = applyPlan(c, plan(edit));
    CHECK(a.applied == 2);
    CHECK(a.refused.size() == 3);
    const Family& f = *findFamily("layers");
    CHECK(a.settings.values[static_cast<std::size_t>(layerParamIndex(f, 1, "thick"))][0] == Catch::Approx(0.2f));
    // The glow was not touched.
    CHECK(a.settings.values[static_cast<std::size_t>(layerParamIndex(f, 0, "intensity"))][0] ==
          c.settings.values[static_cast<std::size_t>(layerParamIndex(f, 0, "intensity"))][0]);
    // Everything else the shader had is kept.
    CHECK(layerTop(a.settings) == 4);
    CHECK(a.settings.animation.tracks.size() == 3);

    // When the artist asks for more, the model says scope "all".
    J broad = edit;
    broad["scope"] = "all";
    const Applied b = applyPlan(c, plan(broad));
    CHECK(b.refused.empty());
    CHECK(b.applied == 5);
}

TEST_CASE("animation commands make real tracks, replace and clear them", "[shader][ai]") {
    Context c = blank();
    c.settings = applyPlan(c, plan(goldenRing)).settings;
    c.mode = Mode::Animate;
    const Applied a = applyPlan(c, plan(J::parse(R"({"summary":"x","ops":[
        {"op":"animate","layer":1,"param":"rot","keys":[{"time":1.5,"value":90},{"time":0,"value":0},{"time":9,"value":360}]},
        {"op":"animate","param":"brightness","keys":[{"time":0,"value":1},{"time":0.4,"value":3,"easing":"linear"},{"time":1.5,"value":0.5}]},
        {"op":"edit_layer","layer":"Glow","params":{"size":2}}
    ]})")));
    // Animation mode changes no look.
    CHECK(a.refused.size() == 1);
    REQUIRE(a.settings.animation.tracks.size() == 4);
    const Track* rot = nullptr;
    for (const Track& t : a.settings.animation.tracks) if (t.param == "L1_rot") rot = &t;
    REQUIRE(rot != nullptr);
    REQUIRE(rot->keys.size() == 3);
    CHECK(rot->keys[0].time == 0.0);
    CHECK(rot->keys[2].time == Catch::Approx(2.0));  // limited to the timeline's length
    CHECK(rot->keys[2].value == Catch::Approx(180.0f));  // limited to the setting's range
    // The values at a time on the timeline follow the keys.
    const auto at = valuesAt(a.settings, 0.0);
    CHECK(at[static_cast<std::size_t>(layerParamIndex(*findFamily("layers"), 1, "rot"))][0] == Catch::Approx(0.0f));
    // Clearing removes them.
    Context d = c;
    d.settings = a.settings;
    const Applied cleared = applyPlan(d, plan(J::parse(R"({"summary":"x","ops":[{"op":"clear_animation"}]})")));
    CHECK(cleared.settings.animation.tracks.empty());
}

TEST_CASE("an explain request changes nothing, and a non-layer shader is not edited", "[shader][ai]") {
    Context c = blank(Mode::Explain);
    const Applied a = applyPlan(c, plan(goldenRing));
    CHECK(a.applied == 0);
    CHECK(layerTop(a.settings) == 0);

    Context h;
    h.settings = presetSettings(*findFamily("hologram"), findFamily("hologram")->presets.front());
    h.mode = Mode::Refine;
    const Applied r = applyPlan(h, plan(J::parse(R"({"summary":"x","ops":[{"op":"add_layer","element":"Ring"}]})")));
    CHECK(r.applied == 0);
    CHECK(r.settings.family == "hologram");
    // Generating starts a layer shader, which replaces the open one only in the copy.
    h.mode = Mode::Generate;
    const Applied g = applyPlan(h, plan(J::parse(R"({"summary":"x","ops":[{"op":"add_layer","element":"Ring"}]})")));
    CHECK(g.settings.family == "layers");
    CHECK(layerTop(g.settings) == 1);
    CHECK(h.settings.family == "hologram");
}

TEST_CASE("the model is told the real parts list and the real state", "[shader][ai]") {
    const std::string sys = systemPrompt(Mode::Generate);
    for (const ElementInfo& e : layerElements()) {
        if (e.name != "None") CHECK(sys.find(e.name) != std::string::npos);
    }
    for (const std::string& b : layerBlendNames()) CHECK(sys.find(b) != std::string::npos);
    Context c = blank(Mode::Refine);
    c.settings = applyPlan(c, plan(goldenRing)).settings;
    c.selectedLayer = 2;
    const J state = J::parse(stateJson(c));
    CHECK(state["layers"].size() == 4);
    CHECK(state["layers"][1]["name"] == "Outer ring");
    CHECK(state["layers"][1]["params"]["spin"] == Catch::Approx(-40.0));
    CHECK(state["selectedLayer"]["name"] == "Inner ring");
    CHECK(state["animation"]["duration"] == Catch::Approx(2.0));
    CHECK(state["tracks"].size() == 2);
    const J schema = J::parse(toolSchemaJson());
    CHECK(schema["properties"]["ops"]["items"]["properties"]["op"]["enum"].size() == 10);
}

TEST_CASE("requests and replies for both providers are built and read without a network", "[shader][ai]") {
    Context c = blank();
    const std::vector<Turn> history = {{"make a ring", "Added a ring."}};
    ProviderConfig claude;
    claude.provider = Provider::Claude;
    const HttpRequest rc = buildRequest(claude, c, "a blue energy ring", history);
    CHECK(rc.url == "https://api.anthropic.com/v1/messages");
    const J bc = J::parse(rc.body);
    CHECK(bc["model"] == "claude-sonnet-5-5");
    CHECK(bc["tool_choice"]["name"] == "edit_shader");
    CHECK(bc["messages"].size() == 3);
    CHECK(bc["messages"][2]["content"].get<std::string>().find("a blue energy ring") != std::string::npos);
    CHECK(rc.body.find("api-key") == std::string::npos);  // the key is added by the app, never here

    ProviderConfig oa;
    oa.provider = Provider::OpenAiCompatible;
    oa.baseUrl = "http://localhost:1234/v1/";
    oa.model = "local-model";
    const HttpRequest ro = buildRequest(oa, c, "a blue energy ring", {});
    CHECK(ro.url == "http://localhost:1234/v1/chat/completions");
    const J bo = J::parse(ro.body);
    CHECK(bo["messages"][0]["role"] == "system");
    CHECK(bo["tools"][0]["function"]["name"] == "edit_shader");

    // Explain asks for words: no tool.
    Context e = c;
    e.mode = Mode::Explain;
    CHECK(!J::parse(buildRequest(claude, e, "what is this", {}).body).contains("tools"));

    const std::string input = R"({"summary":"A ring","ops":[{"op":"new_shader"},{"op":"add_layer","element":"Ring"}]})";
    J claudeReply = {{"content", J::array({{{"type", "text"}, {"text", "Here you go."}},
                                           {{"type", "tool_use"}, {"name", "edit_shader"}, {"input", J::parse(input)}}})}};
    const Reply a = parseReply(claude, claudeReply.dump(), 200);
    CHECK(a.error.empty());
    REQUIRE(a.hasPlan);
    CHECK(a.plan.ops.size() == 2);
    CHECK(a.text == "Here you go.");

    J openReply = {{"choices", J::array({{{"message", {{"content", nullptr},
                                                        {"tool_calls", J::array({{{"function", {{"name", "edit_shader"}, {"arguments", input}}}}})}}}}})}};
    const Reply b = parseReply(oa, openReply.dump(), 200);
    REQUIRE(b.hasPlan);
    CHECK(b.plan.summary == "A ring");

    // Errors come back as the provider's own message.
    const Reply bad = parseReply(claude, R"({"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}})", 401);
    CHECK(!bad.hasPlan);
    CHECK(bad.error == "invalid x-api-key");
    CHECK(!parseReply(oa, "<html>bad gateway</html>", 502).error.empty());
    // A reply that is not a plan at all is reported, not applied.
    CHECK(!parsePlan("not json").ok());
    CHECK(!parsePlan(R"({"summary":"x"})").ok());
}

TEST_CASE("colours read as hex and write back", "[shader][ai]") {
    std::array<float, 4> c{};
    CHECK(parseHexColour("#ff8000", c));
    CHECK(c[0] == Catch::Approx(1.0f));
    CHECK(c[1] == Catch::Approx(128.0f / 255.0f));
    CHECK(c[3] == Catch::Approx(1.0f));
    CHECK(toHex(c) == "#ff8000");
    CHECK(!parseHexColour("orange", c));
    CHECK(!parseHexColour("#12345", c));
}
