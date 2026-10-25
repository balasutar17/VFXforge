#include "vfx/editor/ShaderAi.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

#include <nlohmann/json.hpp>

namespace vfx::editor::shader::ai {

using J = nlohmann::json;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3g", v);
    return buf;
}

const Family* layersFamily() { return findFamily("layers"); }

// The numbers a layer has, by the key used in commands.
const char* const kLayerNumbers[] = {"opacity", "intensity", "size", "aspect", "thick", "soft", "count",
                                     "rot", "spin", "speed", "warp", "twist", "ox", "oy"};

bool isLayerNumber(const std::string& key) {
    for (const char* k : kLayerNumbers) {
        if (key == k) return true;
    }
    return false;
}

float clampTo(const ParamDef& p, double v) {
    return static_cast<float>(std::clamp(v, p.min, p.max));
}

int findElement(const std::string& name) {
    const std::string want = lower(trim(name));
    const auto& list = layerElements();
    for (std::size_t i = 1; i < list.size(); ++i) {
        if (lower(list[i].name) == want) return static_cast<int>(i);
    }
    for (std::size_t i = 1; i < list.size(); ++i) {
        if (lower(list[i].name).find(want) != std::string::npos && !want.empty()) return static_cast<int>(i);
    }
    return -1;
}

int findBlend(const J& v) {
    const auto& names = layerBlendNames();
    if (v.is_number()) {
        const int i = static_cast<int>(v.get<double>() + 0.5);
        return i >= 0 && i < static_cast<int>(names.size()) ? i : -1;
    }
    if (!v.is_string()) return -1;
    const std::string want = lower(v.get<std::string>());
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (lower(names[i]) == want) return static_cast<int>(i);
    }
    return -1;
}

int findSpace(const J& v, const Family& f) {
    const ParamDef* p = findParam(f, "L0_space");
    if (!p) return -1;
    if (v.is_number()) {
        const int i = static_cast<int>(v.get<double>() + 0.5);
        return i >= 0 && i < static_cast<int>(p->choices.size()) ? i : -1;
    }
    if (!v.is_string()) return -1;
    const std::string want = lower(v.get<std::string>());
    for (std::size_t i = 0; i < p->choices.size(); ++i) {
        const std::string c = lower(p->choices[i]);
        if (c == want || c.find(want) == 0 || (want.size() > 2 && c.find(want) != std::string::npos)) return static_cast<int>(i);
    }
    return -1;
}

bool readColour(const J& v, std::array<float, 4>& out) {
    if (v.is_string()) return parseHexColour(v.get<std::string>(), out);
    if (v.is_array() && v.size() >= 3) {
        for (std::size_t i = 0; i < 4; ++i) {
            if (i < v.size() && v[i].is_number()) out[i] = std::max(0.0f, v[i].get<float>());
            else out[i] = 1.0f;
        }
        return true;
    }
    return false;
}

bool hasBoolTrue(const J& v) {
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number()) return v.get<double>() > 0.5;
    if (v.is_string()) {
        const std::string s = lower(v.get<std::string>());
        return s == "true" || s == "on" || s == "yes" || s == "inverted";
    }
    return false;
}

Easing readEasing(const J& v, bool& ok) {
    ok = true;
    if (!v.is_string()) return Easing::Smooth;
    const std::string s = lower(v.get<std::string>());
    if (s == "linear") return Easing::Linear;
    if (s == "smooth") return Easing::Smooth;
    if (s == "ease_in" || s == "easein" || s == "in") return Easing::EaseIn;
    if (s == "ease_out" || s == "easeout" || s == "out") return Easing::EaseOut;
    ok = false;
    return Easing::Smooth;
}

// One shader being edited by a plan.
struct Editor {
    Settings s;
    const Family& f;
    Applied& out;

    int top() const { return layerTop(s); }

    std::string name(int slot) const { return layerName(s, slot); }

    void touch(int slot) {
        if (std::find(out.touchedLayers.begin(), out.touchedLayers.end(), slot) == out.touchedLayers.end()) out.touchedLayers.push_back(slot);
    }

    float& value(int slot, const std::string& key) {
        return s.values[static_cast<std::size_t>(layerParamIndex(f, slot, key))][0];
    }

    // A layer by name or by slot number (0 is the bottom).
    int resolve(const J& v, std::string& why) const {
        const int n = top();
        if (v.is_number()) {
            const int slot = static_cast<int>(v.get<double>());
            if (slot >= 0 && slot < n) return slot;
            why = "there is no layer " + std::to_string(slot) + " (the shader has " + std::to_string(n) + ")";
            return -1;
        }
        if (v.is_string()) {
            const std::string want = lower(trim(v.get<std::string>()));
            for (int i = n - 1; i >= 0; --i) {
                if (lower(name(i)) == want) return i;
            }
            bool digits = !want.empty() && std::all_of(want.begin(), want.end(), [](unsigned char c) { return std::isdigit(c); });
            if (digits) {
                const int slot = std::stoi(want);
                if (slot >= 0 && slot < n) return slot;
            }
            why = "there is no layer called \"" + v.get<std::string>() + "\"";
            return -1;
        }
        why = "the command does not say which layer";
        return -1;
    }

    void setName(int slot, const std::string& text) {
        if (s.layerNames.size() < static_cast<std::size_t>(kLayerCount)) s.layerNames.resize(static_cast<std::size_t>(kLayerCount));
        s.layerNames[static_cast<std::size_t>(slot)] = trim(text).substr(0, 40);
    }

    // The settings shared by add_layer and edit_layer. Lists what changed in `parts`.
    void fill(int slot, const J& op, std::vector<std::string>& parts, std::vector<std::string>& notes) {
        if (op.contains("name") && op["name"].is_string()) {
            setName(slot, op["name"].get<std::string>());
        }
        if (op.contains("element") && op["element"].is_string()) {
            const int el = findElement(op["element"].get<std::string>());
            if (el < 0) notes.push_back("element \"" + op["element"].get<std::string>() + "\" is not one of the library's elements");
            else if (static_cast<int>(value(slot, "el") + 0.5f) != el) {
                value(slot, "el") = static_cast<float>(el);
                parts.push_back("element " + layerElements()[static_cast<std::size_t>(el)].name);
            }
        }
        if (op.contains("blend")) {
            const int b = findBlend(op["blend"]);
            if (b < 0) notes.push_back("blend must be one of Add, Alpha, Multiply, Screen, Mask, Erase");
            else { value(slot, "blend") = static_cast<float>(b); parts.push_back("blend " + layerBlendNames()[static_cast<std::size_t>(b)]); }
        }
        if (op.contains("visible")) {
            value(slot, "on") = hasBoolTrue(op["visible"]) ? 1.0f : 0.0f;
            parts.push_back(hasBoolTrue(op["visible"]) ? "shown" : "hidden");
        }
        for (const char* key : {"colorA", "colorB"}) {
            if (!op.contains(key)) continue;
            std::array<float, 4> c{1, 1, 1, 1};
            if (!readColour(op[key], c)) {
                notes.push_back(std::string(key) + " is not a colour (use \"#rrggbb\")");
                continue;
            }
            const std::string slotKey = std::string(key) == "colorA" ? "colA" : "colB";
            const int at = layerParamIndex(f, slot, slotKey);
            s.values[static_cast<std::size_t>(at)] = c;
            parts.push_back(std::string(key) == "colorA" ? "colour A " + toHex(c) : "colour B " + toHex(c));
        }
        if (op.contains("color") && !op.contains("colorA")) {
            std::array<float, 4> c{1, 1, 1, 1};
            if (readColour(op["color"], c)) {
                s.values[static_cast<std::size_t>(layerParamIndex(f, slot, "colA"))] = c;
                parts.push_back("colour A " + toHex(c));
            } else {
                notes.push_back("color is not a colour (use \"#rrggbb\")");
            }
        }
        auto setNumber = [&](const std::string& key, const J& v) {
            if (!v.is_number()) { notes.push_back(key + " must be a number"); return; }
            const ParamDef* p = findParam(f, "L" + std::to_string(slot) + "_" + key);
            if (!p) return;
            const double want = v.get<double>();
            const float got = clampTo(*p, want);
            value(slot, key) = got;
            parts.push_back(p->label + " " + num(got) + (static_cast<double>(got) != want ? " (limited to " + num(p->min) + " to " + num(p->max) + ")" : ""));
        };
        if (op.contains("opacity")) setNumber("opacity", op["opacity"]);
        if (op.contains("params") && op["params"].is_object()) {
            for (auto it = op["params"].begin(); it != op["params"].end(); ++it) {
                const std::string key = it.key();
                if (isLayerNumber(key)) {
                    setNumber(key, it.value());
                } else if (key == "space") {
                    const int sp = findSpace(it.value(), f);
                    if (sp < 0) notes.push_back("space must be Mesh UV, Front, Top or Wrap");
                    else { value(slot, "space") = static_cast<float>(sp); parts.push_back("projection " + findParam(f, "L0_space")->choices[static_cast<std::size_t>(sp)]); }
                } else if (key == "invert") {
                    value(slot, "invert") = hasBoolTrue(it.value()) ? 1.0f : 0.0f;
                    parts.push_back(hasBoolTrue(it.value()) ? "inverted" : "not inverted");
                } else if (key == "colorA" || key == "colorB") {
                    // allowed inside params too
                    J holder = J::object();
                    holder[key] = it.value();
                    fill(slot, holder, parts, notes);
                } else {
                    notes.push_back("\"" + key + "\" is not a layer setting");
                }
            }
        }
        // Settings may also sit directly on the command.
        for (const char* key : kLayerNumbers) {
            if (std::string(key) != "opacity" && op.contains(key)) setNumber(key, op[key]);
        }
        if (op.contains("space")) {
            const int sp = findSpace(op["space"], f);
            if (sp < 0) notes.push_back("space must be Mesh UV, Front, Top or Wrap");
            else { value(slot, "space") = static_cast<float>(sp); parts.push_back("projection " + findParam(f, "L0_space")->choices[static_cast<std::size_t>(sp)]); }
        }
        if (op.contains("invert")) {
            value(slot, "invert") = hasBoolTrue(op["invert"]) ? 1.0f : 0.0f;
        }
    }
};

}  // namespace

// ---------------------------------------------------------------- colours

bool parseHexColour(const std::string& text, std::array<float, 4>& out) {
    std::string t = trim(text);
    if (!t.empty() && t[0] == '#') t.erase(0, 1);
    if (t.size() != 6 && t.size() != 8) return false;
    for (char c : t) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    const auto byte = [&](std::size_t at) { return static_cast<float>(std::stoi(t.substr(at, 2), nullptr, 16)) / 255.0f; };
    out = {byte(0), byte(2), byte(4), t.size() == 8 ? byte(6) : 1.0f};
    return true;
}

std::string toHex(const std::array<float, 4>& c) {
    char buf[16];
    const auto b = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", b(c[0]), b(c[1]), b(c[2]));
    return buf;
}

const char* modeName(Mode mode) {
    switch (mode) {
        case Mode::Generate: return "generate";
        case Mode::Refine: return "refine";
        case Mode::Animate: return "animate";
        case Mode::Explain: return "explain";
    }
    return "generate";
}

// ---------------------------------------------------------------- what the model is told

std::string systemPrompt(Mode mode) {
    const Family* f = layersFamily();
    std::ostringstream o;
    o << "You are the Shader Artist inside VFX Forge, a game VFX tool. You build shaders out of the tool's own parts, "
         "for Unity (URP and Built-in). You never write shader code. You answer with the edit_shader tool: a short "
         "list of commands that the tool checks and applies. Anything the tool does not list below does not exist: "
         "say so in the explanation and use the closest listed part.\n\n";
    o << "A layer shader is up to " << kLayerCount << " layers drawn bottom to top, each one element (a shape, gradient or pattern) "
         "with two colours (A to B), a blend mode and settings. Layers are named; refer to a layer by its name or by its "
         "slot number (0 is the bottom). A shader also has global settings and a timeline of keyframes.\n\n";
    o << "ELEMENTS (element name: what it draws; settings it uses):\n";
    const auto& list = layerElements();
    for (std::size_t i = 1; i < list.size(); ++i) {
        const ElementInfo& e = list[i];
        o << "- " << e.name << " [" << e.group << "]: " << e.tip << ".";
        std::string uses;
        for (const char* key : {"thick", "count", "speed"}) {
            if (std::find(e.hide.begin(), e.hide.end(), key) != e.hide.end()) continue;
            std::string label = key;
            for (const auto& [k, l] : e.labels) {
                if (k == key) label = l;
            }
            uses += (uses.empty() ? " " : "; ") + std::string(key) + " = " + label;
        }
        if (!uses.empty()) o << " Uses:" << uses << ".";
        o << "\n";
    }
    o << "\nLAYER SETTINGS (key: meaning, range, starting value):\n";
    if (f) {
        for (const char* key : kLayerNumbers) {
            if (const ParamDef* p = findParam(*f, std::string("L0_") + key)) {
                o << "- " << key << ": " << p->label << " - " << p->tip << " (" << num(p->min) << " to " << num(p->max) << ", " << num(p->def) << ")\n";
            }
        }
        o << "- space: Mesh UV | Front | Top | Wrap (how the layer is laid on the object)\n- invert: true swaps inside and outside\n";
    }
    o << "- blend: Add (light on light), Alpha (paint over), Multiply, Screen, Mask (cuts what is below, keeping this shape), Erase (cuts a hole)\n";
    o << "- colorA / colorB: \"#rrggbb\". Brightness above 1 (intensity) is HDR and blooms.\n";
    o << "\nGLOBAL SETTINGS (set_global): ";
    if (f) {
        for (const char* key : {"rimIntensity", "rimPower", "coreGlow", "brightness", "opacity", "speed", "pulseAmp", "pulseFreq", "flowSpeed", "rotateSpeed", "flicker", "noiseAmt", "reveal"}) {
            if (const ParamDef* p = findParam(*f, key)) o << key << " (" << num(p->min) << " to " << num(p->max) << "), ";
        }
        o << "glow (\"#rrggbb\").\n";
    }
    o << "\nCOMMANDS (the \"op\" field):\n"
         "- new_shader {name}: start an empty layer shader (always first when generating).\n"
         "- add_layer {element, name, blend, opacity, colorA, colorB, params:{...}}: adds on top.\n"
         "- edit_layer {layer, ...same fields...}: changes only the fields given.\n"
         "- remove_layer {layer}, duplicate_layer {layer}, move_layer {layer, delta} (+1 is up).\n"
         "- set_global {params:{...}, glow}.\n"
         "- animate {layer, param, keys:[{time, value, easing}]}: keyframes in seconds on one number setting; easing is linear, smooth, ease_in or ease_out. "
         "For a global setting leave out layer. Clockwise is a negative spin or rotation; counter-clockwise is positive.\n"
         "- clear_animation {layer?, param?}: removes keyframes.\n"
         "- set_animation {duration, loop: loop|pingpong|once, speed}.\n\n";
    o << "RULES: use only the elements, settings and commands above. Keep within the ranges. Make a real, finished look: layered "
         "(a glow base, a main shape, detail, an accent), with a sensible palette and brightness. Use spin or keyframes on rot for "
         "rotation and keyframes on size and opacity for expansion and fades (a shockwave is the Shockwave element, or a Ring whose "
         "size and opacity are keyed). Timing: charge, flash, expand and fade are keyframes on opacity, size and intensity. Prefer few "
         "layers that look good over many. For mobile keep layers to six or fewer and avoid Noise, Cells and Plasma unless asked. "
         "summary is one short line; explanation says what each layer is for and mentions any part you had to substitute.\n";
    switch (mode) {
        case Mode::Generate:
            o << "\nTASK: build a new shader from the description. Start with new_shader.\n";
            break;
        case Mode::Refine:
            o << "\nTASK: change the open shader as asked, using the state given. Keep everything not mentioned. If a layer is selected, change "
                 "only that layer unless the artist clearly asks for more; when they do, set scope to all. Do not rebuild what works.\n";
            break;
        case Mode::Animate:
            o << "\nTASK: add or change animation only (animate, clear_animation, set_animation, and spin/speed). Do not change the look.\n";
            break;
        case Mode::Explain:
            o << "\nTASK: explain the shader in plain words for an artist: what each layer does and which settings matter most. Do not use the tool.\n";
            break;
    }
    return o.str();
}

std::string stateJson(const Context& c) {
    const Settings& s = c.settings;
    const Family* f = findFamily(s.family);
    J j;
    j["shader"] = {{"name", s.name}, {"family", f ? f->name : s.family}, {"isLayerShader", f && f->id == "layers"}};
    j["animation"] = {{"duration", s.animation.duration},
                      {"loop", s.animation.loop == LoopMode::Loop ? "loop" : s.animation.loop == LoopMode::PingPong ? "pingpong" : "once"},
                      {"speed", s.animation.speed}};
    if (!f || f->id != "layers") {
        j["note"] = "The open shader is not a layer shader, so layers cannot be edited. A generate request will start a new layer shader.";
        return j.dump();
    }
    const int n = layerTop(s);
    J layers = J::array();
    for (int i = 0; i < n; ++i) {
        J l;
        l["slot"] = i;
        l["name"] = layerName(s, i);
        const auto at = [&](const char* key) { return s.values[static_cast<std::size_t>(layerParamIndex(*f, i, key))]; };
        const int el = static_cast<int>(at("el")[0] + 0.5f);
        l["element"] = layerElements()[static_cast<std::size_t>(el)].name;
        l["visible"] = at("on")[0] > 0.5f;
        l["blend"] = layerBlendNames()[static_cast<std::size_t>(std::clamp(static_cast<int>(at("blend")[0] + 0.5f), 0, static_cast<int>(layerBlendNames().size()) - 1))];
        l["colorA"] = toHex(at("colA"));
        l["colorB"] = toHex(at("colB"));
        J params = J::object();
        for (const char* key : kLayerNumbers) {
            const float v = at(key)[0];
            params[key] = std::round(static_cast<double>(v) * 1000.0) / 1000.0;
        }
        const ParamDef* sp = findParam(*f, "L0_space");
        const int space = static_cast<int>(at("space")[0] + 0.5f);
        if (sp && space >= 0 && space < static_cast<int>(sp->choices.size())) params["space"] = sp->choices[static_cast<std::size_t>(space)];
        l["params"] = params;
        layers.push_back(std::move(l));
    }
    j["layers"] = layers;
    if (c.selectedLayer >= 0 && c.selectedLayer < n) j["selectedLayer"] = {{"slot", c.selectedLayer}, {"name", layerName(s, c.selectedLayer)}};
    else j["selectedLayer"] = nullptr;
    J globals = J::object();
    for (const char* key : {"rimIntensity", "rimPower", "coreGlow", "brightness", "opacity"}) {
        if (const ParamDef* p = findParam(*f, key)) globals[key] = std::round(static_cast<double>(s.values[static_cast<std::size_t>(p->index)][0]) * 1000.0) / 1000.0;
    }
    if (const ParamDef* p = findParam(*f, "glow")) globals["glow"] = toHex(s.values[static_cast<std::size_t>(p->index)]);
    j["globals"] = globals;
    J tracks = J::array();
    for (const Track& t : s.animation.tracks) {
        J keys = J::array();
        for (const Key& k : t.keys) {
            keys.push_back({{"time", k.time}, {"value", k.value},
                            {"easing", k.easing == Easing::Linear ? "linear" : k.easing == Easing::Smooth ? "smooth" : k.easing == Easing::EaseIn ? "ease_in" : "ease_out"}});
        }
        tracks.push_back({{"param", t.param}, {"keys", keys}});
    }
    j["tracks"] = tracks;
    j["trackNote"] = "A track's param is L<slot>_<setting> for a layer setting, or the global setting's name.";
    return j.dump();
}

std::string toolSchemaJson() {
    J op = {
        {"type", "object"},
        {"properties",
         {{"op", {{"type", "string"}, {"enum", J::array({"new_shader", "add_layer", "edit_layer", "remove_layer", "duplicate_layer", "move_layer", "set_global", "animate", "clear_animation", "set_animation"})}}},
          {"layer", {{"description", "Layer name or slot number"}}},
          {"name", {{"type", "string"}}},
          {"element", {{"type", "string"}}},
          {"blend", {{"type", "string"}}},
          {"opacity", {{"type", "number"}}},
          {"visible", {{"type", "boolean"}}},
          {"colorA", {{"type", "string"}, {"description", "#rrggbb"}}},
          {"colorB", {{"type", "string"}, {"description", "#rrggbb"}}},
          {"glow", {{"type", "string"}, {"description", "#rrggbb, set_global only"}}},
          {"params", {{"type", "object"}, {"description", "Setting key to value"}}},
          {"param", {{"type", "string"}, {"description", "animate / clear_animation: the setting key"}}},
          {"keys", {{"type", "array"}, {"items", {{"type", "object"}, {"properties", {{"time", {{"type", "number"}}}, {"value", {{"type", "number"}}}, {"easing", {{"type", "string"}}}}}, {"required", J::array({"time", "value"})}}}}},
          {"delta", {{"type", "integer"}}},
          {"duration", {{"type", "number"}}},
          {"loop", {{"type", "string"}}},
          {"speed", {{"type", "number"}}}}},
        {"required", J::array({"op"})}};
    J schema = {{"type", "object"},
                {"properties",
                 {{"summary", {{"type", "string"}, {"description", "One short line: what this does"}}},
                  {"explanation", {{"type", "string"}, {"description", "What each layer is for, and any part that was substituted"}}},
                  {"scope", {{"type", "string"}, {"enum", J::array({"selected", "all"})}}},
                  {"ops", {{"type", "array"}, {"items", op}}}}},
                {"required", J::array({"summary", "ops"})}};
    return schema.dump();
}

// ---------------------------------------------------------------- what the model answers

Result<Plan> parsePlan(const std::string& json) {
    J j = J::parse(json, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return makeError("The model's answer was not a set of edit commands.");
    if (!j.contains("ops") || !j["ops"].is_array()) return makeError("The model's answer had no list of commands.");
    Plan p;
    if (j.contains("summary") && j["summary"].is_string()) p.summary = j["summary"].get<std::string>();
    if (j.contains("explanation") && j["explanation"].is_string()) p.explanation = j["explanation"].get<std::string>();
    p.allLayers = j.contains("scope") && j["scope"].is_string() && lower(j["scope"].get<std::string>()) == "all";
    for (const J& o : j["ops"]) {
        Op op;
        if (o.is_object() && o.contains("op") && o["op"].is_string()) op.type = lower(o["op"].get<std::string>());
        op.json = o.dump();
        p.ops.push_back(std::move(op));
    }
    if (p.ops.size() > 60) p.ops.resize(60);
    return p;
}

Applied applyPlan(const Context& context, const Plan& plan) {
    Applied out;
    const Family* f = layersFamily();
    out.settings = context.settings;
    if (!f) {
        out.refused.push_back("This build has no layer shaders, so nothing can be edited.");
        return out;
    }
    Editor ed{context.settings, *f, out};
    bool layered = ed.s.family == "layers";
    const bool scoped = context.mode == Mode::Refine && context.selectedLayer >= 0 && !plan.allLayers;

    auto startFresh = [&](const std::string& name) {
        ed.s = defaultSettings(*f);
        ed.s.name = name.empty() ? std::string("AI shader") : trim(name).substr(0, 40);
        ed.s.shape = context.settings.shape.empty() ? ed.s.shape : context.settings.shape;
        ed.s.quality = context.settings.quality;
        ed.s.layerNames.assign(static_cast<std::size_t>(kLayerCount), std::string());
        layered = true;
        out.changes.push_back("Started a new layer shader \"" + ed.s.name + "\"");
    };
    if (context.mode == Mode::Generate) {
        bool first = false;
        for (const Op& op : plan.ops) {
            if (op.type == "new_shader") { first = true; break; }
        }
        if (!first) startFresh("");
    }

    for (const Op& op : plan.ops) {
        const J j = J::parse(op.json, nullptr, false);
        const std::string label = op.type.empty() ? std::string("(a command)") : op.type;
        auto refuse = [&](const std::string& why) { out.refused.push_back(label + ": " + why); };
        if (j.is_discarded() || !j.is_object() || op.type.empty()) { refuse("not a command the tool knows"); continue; }
        if (context.mode == Mode::Explain) { refuse("this request was only to explain, so nothing was changed"); continue; }
        if (context.mode == Mode::Animate && op.type != "animate" && op.type != "clear_animation" && op.type != "set_animation") {
            refuse("this request was only about animation");
            continue;
        }
        if (op.type == "new_shader") {
            startFresh(j.value("name", std::string()));
            ++out.applied;
            continue;
        }
        if (!layered) { refuse("the open shader is not a layer shader; ask for a new shader to build one"); continue; }

        std::vector<std::string> parts, notes;
        std::string why;

        if (op.type == "add_layer") {
            if (!j.contains("element") || !j["element"].is_string()) { refuse("no element given"); continue; }
            const int el = findElement(j["element"].get<std::string>());
            if (el < 0) { refuse("\"" + j["element"].get<std::string>() + "\" is not one of the library's elements"); continue; }
            if (!layerAdd(ed.s, el)) { refuse("the shader already has " + std::to_string(kLayerCount) + " layers"); continue; }
            const int slot = layerTop(ed.s) - 1;
            if (ed.s.layerNames.size() < static_cast<std::size_t>(kLayerCount)) ed.s.layerNames.resize(static_cast<std::size_t>(kLayerCount));
            ed.s.layerNames[static_cast<std::size_t>(slot)].clear();
            J rest = j;
            rest.erase("element");
            ed.fill(slot, rest, parts, notes);
            ed.touch(slot);
            std::string line = "Added " + layerElements()[static_cast<std::size_t>(el)].name + " layer \"" + ed.name(slot) + "\"";
            if (!parts.empty()) {
                line += ": ";
                for (std::size_t i = 0; i < parts.size(); ++i) line += (i ? ", " : "") + parts[i];
            }
            out.changes.push_back(line);
            for (const std::string& n : notes) out.refused.push_back("add_layer: " + n);
            ++out.applied;
            continue;
        }

        if (op.type == "edit_layer" || op.type == "remove_layer" || op.type == "duplicate_layer" || op.type == "move_layer") {
            const int slot = ed.resolve(j.contains("layer") ? j["layer"] : J(), why);
            if (slot < 0) { refuse(why); continue; }
            if (scoped && slot != context.selectedLayer) {
                refuse("only the selected layer \"" + ed.name(context.selectedLayer) + "\" was to change, not \"" + ed.name(slot) + "\"");
                continue;
            }
            const std::string was = ed.name(slot);
            if (op.type == "edit_layer") {
                ed.fill(slot, j, parts, notes);
                ed.touch(slot);
                if (parts.empty() && notes.empty()) { refuse("nothing to change was given for \"" + was + "\""); continue; }
                std::string line = (ed.name(slot) == was ? was : was + " (now \"" + ed.name(slot) + "\")") + ": ";
                for (std::size_t i = 0; i < parts.size(); ++i) line += (i ? ", " : "") + parts[i];
                if (parts.empty()) line += "renamed";
                out.changes.push_back(line);
                for (const std::string& n : notes) out.refused.push_back("edit_layer: " + n);
                ++out.applied;
            } else if (op.type == "remove_layer") {
                if (!layerRemove(ed.s, slot)) { refuse("could not remove \"" + was + "\""); continue; }
                // Names follow their layers.
                if (ed.s.layerNames.size() >= static_cast<std::size_t>(kLayerCount)) {
                    for (int i = slot; i + 1 < kLayerCount; ++i) ed.s.layerNames[static_cast<std::size_t>(i)] = ed.s.layerNames[static_cast<std::size_t>(i + 1)];
                    ed.s.layerNames[static_cast<std::size_t>(kLayerCount - 1)].clear();
                }
                out.changes.push_back("Removed layer \"" + was + "\"");
                ++out.applied;
            } else if (op.type == "duplicate_layer") {
                if (!layerDuplicate(ed.s, slot)) { refuse("could not duplicate \"" + was + "\" (the shader may be full)"); continue; }
                ed.touch(slot + 1);
                out.changes.push_back("Duplicated layer \"" + was + "\"");
                ++out.applied;
            } else {
                const int delta = j.contains("delta") && j["delta"].is_number() ? static_cast<int>(j["delta"].get<double>()) : 0;
                if (delta == 0 || !layerMove(ed.s, slot, delta > 0 ? 1 : -1)) { refuse("could not move \"" + was + "\" that way"); continue; }
                ed.touch(slot + (delta > 0 ? 1 : -1));
                out.changes.push_back("Moved layer \"" + was + "\" " + (delta > 0 ? "up" : "down"));
                ++out.applied;
            }
            continue;
        }

        if (op.type == "set_global") {
            if (scoped) { refuse("only the selected layer was to change, not the whole shader"); continue; }
            std::vector<std::string> done;
            if (j.contains("glow")) {
                std::array<float, 4> c{1, 1, 1, 1};
                const ParamDef* p = findParam(*f, "glow");
                if (p && readColour(j["glow"], c)) { ed.s.values[static_cast<std::size_t>(p->index)] = c; done.push_back("glow colour " + toHex(c)); }
                else refuse("glow is not a colour (use \"#rrggbb\")");
            }
            if (j.contains("params") && j["params"].is_object()) {
                for (auto it = j["params"].begin(); it != j["params"].end(); ++it) {
                    const ParamDef* p = findParam(*f, it.key());
                    const bool allowed = p && (p->common || it.key() == "rimIntensity" || it.key() == "rimPower" || it.key() == "coreGlow" || it.key() == "brightness" || it.key() == "opacity");
                    if (!allowed || p->kind != ParamKind::Number || !it.value().is_number()) {
                        out.refused.push_back("set_global: \"" + it.key() + "\" is not a global number setting");
                        continue;
                    }
                    const float got = clampTo(*p, it.value().get<double>());
                    ed.s.values[static_cast<std::size_t>(p->index)][0] = got;
                    done.push_back(p->label + " " + num(got));
                }
            }
            if (done.empty()) { refuse("nothing to change was given"); continue; }
            std::string line = "Whole shader: ";
            for (std::size_t i = 0; i < done.size(); ++i) line += (i ? ", " : "") + done[i];
            out.changes.push_back(line);
            ++out.applied;
            continue;
        }

        if (op.type == "set_animation") {
            std::vector<std::string> done;
            if (j.contains("duration") && j["duration"].is_number()) {
                ed.s.animation.duration = std::clamp(j["duration"].get<double>(), 0.2, 60.0);
                done.push_back("duration " + num(ed.s.animation.duration) + " s");
                for (Track& t : ed.s.animation.tracks) {
                    for (Key& k : t.keys) k.time = std::min(k.time, ed.s.animation.duration);
                }
            }
            if (j.contains("loop") && j["loop"].is_string()) {
                const std::string l = lower(j["loop"].get<std::string>());
                if (l == "loop") ed.s.animation.loop = LoopMode::Loop;
                else if (l == "pingpong" || l == "ping-pong") ed.s.animation.loop = LoopMode::PingPong;
                else if (l == "once") ed.s.animation.loop = LoopMode::Once;
                else { out.refused.push_back("set_animation: loop must be loop, pingpong or once"); }
                done.push_back("plays " + l);
            }
            if (j.contains("speed") && j["speed"].is_number()) {
                ed.s.animation.speed = std::clamp(j["speed"].get<double>(), 0.1, 4.0);
                done.push_back("speed " + num(ed.s.animation.speed));
            }
            if (done.empty()) { refuse("nothing to change was given"); continue; }
            std::string line = "Timeline: ";
            for (std::size_t i = 0; i < done.size(); ++i) line += (i ? ", " : "") + done[i];
            out.changes.push_back(line);
            ++out.applied;
            continue;
        }

        if (op.type == "animate" || op.type == "clear_animation") {
            std::string track;
            std::string niceLayer;
            int slot = -1;
            if (j.contains("layer") && !j["layer"].is_null()) {
                slot = ed.resolve(j["layer"], why);
                if (slot < 0) { refuse(why); continue; }
                if (scoped && slot != context.selectedLayer) {
                    refuse("only the selected layer \"" + ed.name(context.selectedLayer) + "\" was to change, not \"" + ed.name(slot) + "\"");
                    continue;
                }
                niceLayer = ed.name(slot);
            } else if (scoped) {
                refuse("only the selected layer was to change, not the whole shader");
                continue;
            }
            if (j.contains("param") && j["param"].is_string()) {
                std::string key = j["param"].get<std::string>();
                if (slot >= 0) {
                    if (key.rfind("L", 0) == 0 && key.size() > 3 && key[2] == '_') key = key.substr(3);
                    track = "L" + std::to_string(slot) + "_" + key;
                } else {
                    track = key;
                }
            }
            if (op.type == "clear_animation") {
                std::size_t before = ed.s.animation.tracks.size();
                const std::string prefix = slot >= 0 ? "L" + std::to_string(slot) + "_" : std::string();
                auto& tr = ed.s.animation.tracks;
                tr.erase(std::remove_if(tr.begin(), tr.end(), [&](const Track& t) {
                    if (!track.empty()) return t.param == track;
                    if (slot >= 0) return t.param.rfind(prefix, 0) == 0;
                    return true;
                }), tr.end());
                if (tr.size() == before) { refuse("there was no animation to clear"); continue; }
                out.changes.push_back("Cleared " + std::to_string(before - tr.size()) + " animation track(s)");
                ++out.applied;
                continue;
            }
            const ParamDef* p = track.empty() ? nullptr : findParam(*f, track);
            if (!p || p->kind != ParamKind::Number) {
                refuse("\"" + (j.contains("param") && j["param"].is_string() ? j["param"].get<std::string>() : std::string()) + "\" is not a number setting that can be keyframed");
                continue;
            }
            if (!j.contains("keys") || !j["keys"].is_array() || j["keys"].empty()) { refuse("no keyframes given"); continue; }
            Track t;
            t.param = track;
            bool bad = false;
            for (const J& k : j["keys"]) {
                if (!k.is_object() || !k.contains("time") || !k["time"].is_number() || !k.contains("value") || !k["value"].is_number()) { bad = true; break; }
                bool easeOk = true;
                Key key;
                key.time = std::clamp(k["time"].get<double>(), 0.0, ed.s.animation.duration);
                key.value = clampTo(*p, k["value"].get<double>());
                key.easing = k.contains("easing") ? readEasing(k["easing"], easeOk) : Easing::Smooth;
                if (!easeOk) out.refused.push_back("animate: easing must be linear, smooth, ease_in or ease_out; used smooth");
                t.keys.push_back(key);
            }
            if (bad) { refuse("every keyframe needs a time and a value in numbers"); continue; }
            std::stable_sort(t.keys.begin(), t.keys.end(), [](const Key& a, const Key& b) { return a.time < b.time; });
            auto& tr = ed.s.animation.tracks;
            tr.erase(std::remove_if(tr.begin(), tr.end(), [&](const Track& x) { return x.param == t.param; }), tr.end());
            std::string line = (niceLayer.empty() ? std::string("Whole shader") : niceLayer) + " " + p->label + " keyed: ";
            for (std::size_t i = 0; i < t.keys.size(); ++i) line += (i ? ", " : "") + num(t.keys[i].value) + " at " + num(t.keys[i].time) + " s";
            tr.push_back(std::move(t));
            if (slot >= 0) ed.touch(slot);
            out.changes.push_back(line);
            ++out.applied;
            continue;
        }

        refuse("not a command the tool knows");
    }

    out.settings = ed.s;
    // Tracks that name a layer the shader no longer has are dropped so nothing dangles.
    return out;
}

// ---------------------------------------------------------------- the providers

HttpRequest buildRequest(const ProviderConfig& config, const Context& context, const std::string& prompt,
                         const std::vector<Turn>& history) {
    HttpRequest r;
    const std::string system = systemPrompt(context.mode);
    const std::string state = "Current shader state (this is the truth; ignore anything earlier that disagrees):\n" + stateJson(context) +
                              "\n\nRequest (" + modeName(context.mode) + "): " + prompt;
    const bool tools = context.mode != Mode::Explain;
    const J schema = J::parse(toolSchemaJson());
    const std::string description = "Apply edits to the open shader. Every command is checked against the real parts list.";
    J body;
    auto base = [](const std::string& url, const std::string& def) {
        std::string b = url.empty() ? def : url;
        while (!b.empty() && b.back() == '/') b.pop_back();
        return b;
    };
    if (config.provider == Provider::Claude) {
        r.url = base(config.baseUrl, "https://api.anthropic.com") + "/v1/messages";
        r.headers = {{"content-type", "application/json"}, {"anthropic-version", "2023-06-01"}};
        body["model"] = config.model.empty() ? "claude-sonnet-5-5" : config.model;
        body["max_tokens"] = 4096;
        body["system"] = system;
        J msgs = J::array();
        for (const Turn& t : history) {
            if (t.user.empty() || t.assistant.empty()) continue;
            msgs.push_back({{"role", "user"}, {"content", t.user}});
            msgs.push_back({{"role", "assistant"}, {"content", t.assistant}});
        }
        msgs.push_back({{"role", "user"}, {"content", state}});
        body["messages"] = msgs;
        if (tools) {
            body["tools"] = J::array({{{"name", "edit_shader"}, {"description", description}, {"input_schema", schema}}});
            body["tool_choice"] = {{"type", "tool"}, {"name", "edit_shader"}};
        }
    } else {
        r.url = base(config.baseUrl, "https://api.openai.com/v1") + "/chat/completions";
        r.headers = {{"content-type", "application/json"}};
        body["model"] = config.model.empty() ? "gpt-4o" : config.model;
        J msgs = J::array();
        msgs.push_back({{"role", "system"}, {"content", system}});
        for (const Turn& t : history) {
            if (t.user.empty() || t.assistant.empty()) continue;
            msgs.push_back({{"role", "user"}, {"content", t.user}});
            msgs.push_back({{"role", "assistant"}, {"content", t.assistant}});
        }
        msgs.push_back({{"role", "user"}, {"content", state}});
        body["messages"] = msgs;
        if (tools) {
            body["tools"] = J::array({{{"type", "function"}, {"function", {{"name", "edit_shader"}, {"description", description}, {"parameters", schema}}}}});
            body["tool_choice"] = {{"type", "function"}, {"function", {{"name", "edit_shader"}}}};
        }
    }
    r.body = body.dump();
    return r;
}

Reply parseReply(const ProviderConfig& config, const std::string& bodyText, int status) {
    Reply r;
    const J j = J::parse(bodyText, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        r.error = status >= 400 ? "The AI service answered with an error (HTTP " + std::to_string(status) + ")." : "The AI service's reply could not be read.";
        return r;
    }
    if (status >= 400 || (j.contains("error") && !j["error"].is_null())) {
        std::string message;
        if (j.contains("error")) {
            const J& e = j["error"];
            if (e.is_object() && e.contains("message") && e["message"].is_string()) message = e["message"].get<std::string>();
            else if (e.is_string()) message = e.get<std::string>();
        }
        if (message.empty()) message = "HTTP " + std::to_string(status);
        r.error = message;
        return r;
    }
    std::string planJson;
    if (config.provider == Provider::Claude) {
        if (j.contains("content") && j["content"].is_array()) {
            for (const J& block : j["content"]) {
                if (!block.is_object() || !block.contains("type")) continue;
                if (block["type"] == "text" && block.contains("text") && block["text"].is_string()) r.text += block["text"].get<std::string>();
                if (block["type"] == "tool_use" && block.value("name", std::string()) == "edit_shader" && block.contains("input")) planJson = block["input"].dump();
            }
        }
    } else if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
        const J& m = j["choices"][0]["message"];
        if (m.is_object()) {
            if (m.contains("content") && m["content"].is_string()) r.text = m["content"].get<std::string>();
            if (m.contains("tool_calls") && m["tool_calls"].is_array() && !m["tool_calls"].empty()) {
                const J& fn = m["tool_calls"][0]["function"];
                if (fn.is_object() && fn.value("name", std::string()) == "edit_shader" && fn.contains("arguments") && fn["arguments"].is_string()) {
                    planJson = fn["arguments"].get<std::string>();
                }
            }
        }
    }
    if (!planJson.empty()) {
        auto plan = parsePlan(planJson);
        if (plan) {
            r.hasPlan = true;
            r.plan = plan.value();
        } else {
            r.error = plan.error().message;
        }
    }
    return r;
}

}  // namespace vfx::editor::shader::ai
