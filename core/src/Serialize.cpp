#include "vfx/Serialize.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "Checks.h"
#include "Json.h"
#include "Utf8.h"

namespace vfx {

namespace {

using detail::J;

// Limits that bound memory use when loading a damaged or hostile file.
constexpr int kMaxNesting = 64;
constexpr std::size_t kMaxLayers = 4096;
constexpr std::size_t kMaxModulesPerLayer = 256;
constexpr std::size_t kMaxControlsPerLayer = 256;
constexpr std::size_t kMaxTargetsPerControl = 64;
constexpr std::size_t kMaxAssets = 65536;

// ------------------------------------------------------------------ write

J colorToJson(const Color& c) { return J::array({c.r, c.g, c.b, c.a}); }

J valueToJson(const Value& value) {
    struct Visitor {
        J operator()(bool v) const { return J(v); }
        J operator()(std::int64_t v) const { return J(v); }
        J operator()(double v) const { return J(v); }
        J operator()(const std::string& v) const { return J(v); }
        J operator()(const Vec3& v) const { return J::array({v.x, v.y, v.z}); }
        J operator()(const Color& v) const { return colorToJson(v); }
        J operator()(const Scalar& v) const {
            switch (v.kind) {
                case Scalar::Kind::Constant:
                    return J(v.a);
                case Scalar::Kind::Random: {
                    J o = J::object();
                    o["random"] = J::array({v.a, v.b});
                    return o;
                }
                case Scalar::Kind::Curve: {
                    J keys = J::array();
                    for (const auto& k : v.keys) {
                        keys.push_back(J::array({k.t, k.v}));
                    }
                    J o = J::object();
                    o["curve"] = std::move(keys);
                    return o;
                }
            }
            return J(v.a);
        }
        J operator()(const Gradient& v) const {
            J keys = J::array();
            for (const auto& k : v.keys) {
                J key = J::array();
                key.push_back(k.t);
                key.push_back(colorToJson(k.color));
                keys.push_back(std::move(key));
            }
            J o = J::object();
            o["gradient"] = std::move(keys);
            return o;
        }
        J operator()(const BurstList& v) const {
            J items = J::array();
            for (const auto& b : v.items) {
                J o = J::object();
                o["time"] = b.time;
                o["count"] = b.count;
                items.push_back(std::move(o));
            }
            return items;
        }
        J operator()(const AssetRef& v) const {
            return v.id.valid() ? J(formatId('a', v.id)) : J(nullptr);
        }
    };
    return std::visit(Visitor{}, value);
}

// Unknown fields go back after the known ones, in the order they were read.
void appendExtras(J& object, const Extras& extra) {
    for (const auto& [key, text] : extra) {
        if (object.contains(key)) {
            continue;
        }
        J parsed = J::parse(text, nullptr, false);
        if (!parsed.is_discarded()) {
            object[key] = std::move(parsed);
        }
    }
}

J effectToJson(const Effect& e) {
    J root = J::object();
    root["format"] = kFormatTag;
    root["formatVersion"] = kFormatVersion;
    root["id"] = formatId('e', e.id);
    root["name"] = e.name;
    root["space"] = e.space;
    root["seed"] = e.seed;
    root["duration"] = e.duration;
    root["loop"] = e.loop;
    root["frameRate"] = e.frameRate;

    J assets = J::array();
    for (const auto& a : e.assets) {
        J o = J::object();
        o["id"] = formatId('a', a.id);
        o["kind"] = a.kind;
        o["path"] = a.path;
        o["hash"] = a.hash;
        appendExtras(o, a.extra);
        assets.push_back(std::move(o));
    }
    root["assets"] = std::move(assets);

    J layers = J::array();
    for (const auto& l : e.layers) {
        J lo = J::object();
        lo["id"] = formatId('l', l.id);
        lo["name"] = l.name;
        lo["enabled"] = l.enabled;
        lo["start"] = l.start;
        lo["duration"] = l.duration;
        // Written only when set, so files from before these existed read
        // back and write out unchanged.
        if (!l.role.empty()) {
            lo["role"] = l.role;
        }
        if (l.locked) {
            lo["locked"] = true;
        }

        J controls = J::array();
        for (const auto& c : l.controls) {
            J co = J::object();
            co["id"] = formatId('c', c.id);
            co["label"] = c.label;
            J targets = J::array();
            for (const auto& t : c.targets) {
                targets.push_back(formatId('m', t.module) + "/" + t.property);
            }
            co["targets"] = std::move(targets);
            appendExtras(co, c.extra);
            controls.push_back(std::move(co));
        }
        lo["controls"] = std::move(controls);

        J modules = J::array();
        for (const auto& m : l.modules) {
            J mo = J::object();
            mo["id"] = formatId('m', m.id);
            mo["type"] = m.type;
            if (m.desc) {
                // Every property is written, defaults included, so a later
                // change of default can never alter the look of a saved effect.
                for (std::size_t i = 0; i < m.values.size(); ++i) {
                    mo[m.desc->properties[i].key] = valueToJson(m.values[i]);
                }
            }
            appendExtras(mo, m.extra);
            modules.push_back(std::move(mo));
        }
        lo["modules"] = std::move(modules);

        appendExtras(lo, l.extra);
        layers.push_back(std::move(lo));
    }
    root["layers"] = std::move(layers);

    appendExtras(root, e.extra);
    return root;
}

// ------------------------------------------------------------------- read

bool readNumber(const J& j, double& out) {
    if (!j.is_number()) {
        return false;
    }
    out = j.get<double>();
    return true;
}

bool readInteger(const J& j, std::int64_t& out) {
    if (j.is_number_unsigned()) {
        const auto v = j.get<std::uint64_t>();
        if (v > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return false;
        }
        out = static_cast<std::int64_t>(v);
        return true;
    }
    if (j.is_number_integer()) {
        out = j.get<std::int64_t>();
        return true;
    }
    return false;
}

bool readColor(const J& j, Color& out) {
    if (!j.is_array() || (j.size() != 3 && j.size() != 4)) {
        return false;
    }
    out.a = 1.0;
    return readNumber(j[0], out.r) && readNumber(j[1], out.g) && readNumber(j[2], out.b) &&
           (j.size() == 3 || readNumber(j[3], out.a));
}

// Shape check only. Range and ordering are judged afterwards by
// validateValue, so the two concerns stay separate.
bool readValue(const J& j, ValueKind kind, Value& out) {
    switch (kind) {
        case ValueKind::Bool:
            if (!j.is_boolean()) {
                return false;
            }
            out = j.get<bool>();
            return true;
        case ValueKind::Int: {
            std::int64_t v = 0;
            if (!readInteger(j, v)) {
                return false;
            }
            out = v;
            return true;
        }
        case ValueKind::Float: {
            double v = 0;
            if (!readNumber(j, v)) {
                return false;
            }
            out = v;
            return true;
        }
        case ValueKind::Text:
        case ValueKind::Enum:
            if (!j.is_string()) {
                return false;
            }
            out = j.get<std::string>();
            return true;
        case ValueKind::Vec3: {
            Vec3 v;
            if (!j.is_array() || j.size() != 3 || !readNumber(j[0], v.x) ||
                !readNumber(j[1], v.y) || !readNumber(j[2], v.z)) {
                return false;
            }
            out = v;
            return true;
        }
        case ValueKind::Color: {
            Color c;
            if (!readColor(j, c)) {
                return false;
            }
            out = c;
            return true;
        }
        case ValueKind::Scalar: {
            if (j.is_number()) {
                out = Scalar::constant(j.get<double>());
                return true;
            }
            if (!j.is_object() || j.size() != 1) {
                return false;
            }
            if (auto it = j.find("random"); it != j.end()) {
                const J& r = it.value();
                double a = 0, b = 0;
                if (!r.is_array() || r.size() != 2 || !readNumber(r[0], a) ||
                    !readNumber(r[1], b)) {
                    return false;
                }
                out = Scalar::random(a, b);
                return true;
            }
            if (auto it = j.find("curve"); it != j.end()) {
                const J& keys = it.value();
                if (!keys.is_array() || keys.size() > kMaxCurveKeys) {
                    return false;
                }
                std::vector<CurveKey> parsed;
                parsed.reserve(keys.size());
                for (const auto& k : keys) {
                    CurveKey key;
                    if (!k.is_array() || k.size() != 2 || !readNumber(k[0], key.t) ||
                        !readNumber(k[1], key.v)) {
                        return false;
                    }
                    parsed.push_back(key);
                }
                out = Scalar::curve(std::move(parsed));
                return true;
            }
            return false;
        }
        case ValueKind::Gradient: {
            if (!j.is_object() || j.size() != 1) {
                return false;
            }
            auto it = j.find("gradient");
            if (it == j.end() || !it.value().is_array() || it.value().size() > kMaxCurveKeys) {
                return false;
            }
            Gradient g;
            g.keys.reserve(it.value().size());
            for (const auto& k : it.value()) {
                GradientKey key;
                if (!k.is_array() || k.size() != 2 || !readNumber(k[0], key.t) ||
                    !readColor(k[1], key.color)) {
                    return false;
                }
                g.keys.push_back(key);
            }
            out = std::move(g);
            return true;
        }
        case ValueKind::Bursts: {
            if (!j.is_array() || j.size() > kMaxBursts) {
                return false;
            }
            BurstList list;
            list.items.reserve(j.size());
            for (const auto& b : j) {
                Burst burst;
                if (!b.is_object()) {
                    return false;
                }
                auto time = b.find("time");
                auto count = b.find("count");
                if (time == b.end() || count == b.end() || !readNumber(time.value(), burst.time) ||
                    !readInteger(count.value(), burst.count)) {
                    return false;
                }
                list.items.push_back(burst);
            }
            out = std::move(list);
            return true;
        }
        case ValueKind::Asset: {
            if (j.is_null()) {
                out = AssetRef{};
                return true;
            }
            if (!j.is_string()) {
                return false;
            }
            auto id = parseId(j.get<std::string>(), 'a');
            if (!id) {
                return false;
            }
            out = AssetRef{id.value()};
            return true;
        }
    }
    return false;
}

struct Loader {
    std::vector<Diagnostic> diagnostics;
    std::vector<Id> seen;
    bool repaired = false;

    void note(std::string where, std::string message) {
        diagnostics.push_back(Diagnostic{std::move(where), std::move(message)});
    }

    // Reads one described property from an object. A missing key takes the
    // default silently. A wrong or out-of-range value is repaired and reported.
    Value field(const J& object, const PropertyDesc& desc, const std::string& where) {
        auto it = object.find(desc.key);
        if (it == object.end()) {
            return desc.defaultValue;
        }
        Value value;
        if (!readValue(it.value(), desc.kind, value)) {
            repaired = true;
            note(where + "." + desc.key,
                 desc.label + " was not readable and has been reset to its default.");
            return desc.defaultValue;
        }
        if (auto s = validateValue(desc, value); !s) {
            repaired = true;
            note(where + "." + desc.key, s.error().message + " It has been corrected.");
            return repairValue(desc, value);
        }
        return value;
    }

    template <class T>
    T fieldAs(const J& object, const ModuleTypeDesc& table, const char* key,
              const std::string& where) {
        return std::get<T>(field(object, *table.find(key), where));
    }

    Result<Id> id(const J& object, char prefix, const std::string& where) {
        auto it = object.find("id");
        if (it == object.end() || !it.value().is_string()) {
            return makeError("This file is damaged: something in it has no ID.", where);
        }
        auto parsed = parseId(it.value().get<std::string>(), prefix);
        if (!parsed) {
            return makeError("This file is damaged: it contains an ID that is not valid.",
                             where + ": " + parsed.error().message);
        }
        if (std::find(seen.begin(), seen.end(), parsed.value()) != seen.end()) {
            return makeError("This file is damaged: the same ID is used twice.", where);
        }
        seen.push_back(parsed.value());
        return parsed.value();
    }

    static Extras extras(const J& object, std::initializer_list<std::string_view> known,
                         const ModuleTypeDesc* desc = nullptr) {
        Extras out;
        for (auto it = object.begin(); it != object.end(); ++it) {
            const std::string& key = it.key();
            if (std::find(known.begin(), known.end(), key) != known.end()) {
                continue;
            }
            if (desc && desc->find(key)) {
                continue;
            }
            out.emplace_back(key, it.value().dump(-1, ' ', false, J::error_handler_t::replace));
        }
        return out;
    }

    static std::string text(const J& object, const char* key, const std::string& fallback) {
        auto it = object.find(key);
        if (it == object.end() || !it.value().is_string()) {
            return fallback;
        }
        return detail::truncateUtf8(it.value().get<std::string>(), kMaxTextLength);
    }

    static Error tooMany(const char* what) {
        return makeError(std::string("This file has more ") + what + " than VFX Forge can open.");
    }

    static Error notAList(const std::string& where) {
        return makeError("This file is damaged and cannot be read.", where + " is not a list");
    }

    static Error notAnObject(const std::string& where) {
        return makeError("This file is damaged and cannot be read.", where + " is not an object");
    }

    Result<Asset> asset(const J& j, const std::string& where) {
        if (!j.is_object()) {
            return notAnObject(where);
        }
        Asset a;
        auto parsed = id(j, 'a', where);
        if (!parsed) {
            return parsed.error();
        }
        a.id = parsed.value();
        a.kind = text(j, "kind", "texture");
        a.path = text(j, "path", "");
        a.hash = text(j, "hash", "");
        if (a.kind.empty()) {
            a.kind = "texture";
        }
        if (!detail::isPortableRelativePath(a.path)) {
            note(where + ".path", "The asset location \"" + a.path +
                                      "\" cannot be used and will be treated as missing.");
        }
        a.extra = extras(j, {"id", "kind", "path", "hash"});
        return a;
    }

    Result<Module> module(const J& j, const std::string& where) {
        if (!j.is_object()) {
            return notAnObject(where);
        }
        Module m;
        auto parsed = id(j, 'm', where);
        if (!parsed) {
            return parsed.error();
        }
        m.id = parsed.value();
        auto type = j.find("type");
        if (type == j.end() || !type.value().is_string() ||
            type.value().get_ref<const std::string&>().empty() ||
            type.value().get_ref<const std::string&>().size() > kMaxTextLength) {
            return makeError("This file is damaged: a module has no type.", where);
        }
        m.type = type.value().get<std::string>();
        m.desc = Registry::builtin().findModule(m.type);
        if (!m.desc) {
            note(where, "The module type \"" + m.type +
                            "\" is not supported by this version. It is kept unchanged.");
            m.extra = extras(j, {"id", "type"});
            return m;
        }
        m.values.reserve(m.desc->properties.size());
        for (const auto& p : m.desc->properties) {
            m.values.push_back(field(j, p, where));
        }
        m.extra = extras(j, {"id", "type"}, m.desc);
        return m;
    }

    Result<SimpleControl> control(const J& j, const std::string& where) {
        if (!j.is_object()) {
            return notAnObject(where);
        }
        SimpleControl c;
        auto parsed = id(j, 'c', where);
        if (!parsed) {
            return parsed.error();
        }
        c.id = parsed.value();
        c.label = text(j, "label", "");
        if (auto targets = j.find("targets"); targets != j.end() && targets.value().is_array()) {
            if (targets.value().size() > kMaxTargetsPerControl) {
                return tooMany("control targets");
            }
            for (const auto& t : targets.value()) {
                bool ok = false;
                if (t.is_string()) {
                    const auto& s = t.get_ref<const std::string&>();
                    const std::size_t slash = s.find('/');
                    if (slash != std::string::npos && slash + 1 < s.size() &&
                        s.find('/', slash + 1) == std::string::npos &&
                        s.size() <= kMaxTextLength) {
                        if (auto module = parseId(std::string_view(s).substr(0, slash), 'm')) {
                            c.targets.push_back(ControlTarget{module.value(), s.substr(slash + 1)});
                            ok = true;
                        }
                    }
                }
                if (!ok) {
                    repaired = true;
                    note(where + ".targets", "A control binding was not readable and was dropped.");
                }
            }
        }
        c.extra = extras(j, {"id", "label", "targets"});
        return c;
    }

    Result<Layer> layer(const J& j, const std::string& where) {
        if (!j.is_object()) {
            return notAnObject(where);
        }
        const auto& fields = Registry::builtin().layerFields();
        Layer l;
        auto parsed = id(j, 'l', where);
        if (!parsed) {
            return parsed.error();
        }
        l.id = parsed.value();
        l.name = fieldAs<std::string>(j, fields, "name", where);
        l.enabled = fieldAs<bool>(j, fields, "enabled", where);
        l.start = fieldAs<double>(j, fields, "start", where);
        l.duration = fieldAs<double>(j, fields, "duration", where);
        l.role = fieldAs<std::string>(j, fields, "role", where);
        l.locked = fieldAs<bool>(j, fields, "locked", where);

        if (auto controls = j.find("controls"); controls != j.end()) {
            if (!controls.value().is_array()) {
                return notAList(where + ".controls");
            }
            if (controls.value().size() > kMaxControlsPerLayer) {
                return tooMany("controls");
            }
            std::size_t i = 0;
            for (const auto& c : controls.value()) {
                auto read = control(c, where + ".controls[" + std::to_string(i++) + "]");
                if (!read) {
                    return read.error();
                }
                l.controls.push_back(std::move(read.value()));
            }
        }

        if (auto modules = j.find("modules"); modules != j.end()) {
            if (!modules.value().is_array()) {
                return notAList(where + ".modules");
            }
            if (modules.value().size() > kMaxModulesPerLayer) {
                return tooMany("modules");
            }
            std::size_t i = 0;
            for (const auto& m : modules.value()) {
                const std::string at = where + ".modules[" + std::to_string(i++) + "]";
                auto read = module(m, at);
                if (!read) {
                    return read.error();
                }
                if (read.value().desc) {
                    for (const auto& existing : l.modules) {
                        if (existing.type == read.value().type) {
                            note(at, "This layer has more than one " + read.value().desc->label +
                                         " module. Only the first is used.");
                            break;
                        }
                    }
                }
                l.modules.push_back(std::move(read.value()));
            }
        }

        l.extra = extras(j, {"id", "name", "enabled", "start", "duration", "role", "locked", "controls", "modules"});
        return l;
    }

    Result<Effect> effect(const J& root) {
        const auto& fields = Registry::builtin().effectFields();
        const std::string where = "effect";
        Effect e;
        auto parsed = id(root, 'e', where);
        if (!parsed) {
            return parsed.error();
        }
        e.id = parsed.value();
        e.name = fieldAs<std::string>(root, fields, "name", where);
        e.space = fieldAs<std::string>(root, fields, "space", where);
        e.seed = fieldAs<std::int64_t>(root, fields, "seed", where);
        e.duration = fieldAs<double>(root, fields, "duration", where);
        e.loop = fieldAs<std::string>(root, fields, "loop", where);
        e.frameRate = fieldAs<double>(root, fields, "frameRate", where);

        if (auto assets = root.find("assets"); assets != root.end()) {
            if (!assets.value().is_array()) {
                return notAList("assets");
            }
            if (assets.value().size() > kMaxAssets) {
                return tooMany("assets");
            }
            std::size_t i = 0;
            for (const auto& a : assets.value()) {
                auto read = asset(a, "assets[" + std::to_string(i++) + "]");
                if (!read) {
                    return read.error();
                }
                e.assets.push_back(std::move(read.value()));
            }
        }

        if (auto layers = root.find("layers"); layers != root.end()) {
            if (!layers.value().is_array()) {
                return notAList("layers");
            }
            if (layers.value().size() > kMaxLayers) {
                return tooMany("layers");
            }
            std::size_t i = 0;
            for (const auto& l : layers.value()) {
                auto read = layer(l, "layers[" + std::to_string(i++) + "]");
                if (!read) {
                    return read.error();
                }
                e.layers.push_back(std::move(read.value()));
            }
        }

        // References to assets that are not in the list are kept. They show
        // as a placeholder with a Locate Asset prompt rather than failing.
        for (std::size_t li = 0; li < e.layers.size(); ++li) {
            const auto& l = e.layers[li];
            for (std::size_t mi = 0; mi < l.modules.size(); ++mi) {
                const auto& m = l.modules[mi];
                for (std::size_t vi = 0; vi < m.values.size(); ++vi) {
                    const auto* ref = std::get_if<AssetRef>(&m.values[vi]);
                    if (ref && ref->id.valid() && !findAsset(e, ref->id)) {
                        note("layers[" + std::to_string(li) + "].modules[" + std::to_string(mi) +
                                 "]." + m.desc->properties[vi].key,
                             "This refers to an asset that is not in the effect.");
                    }
                }
            }
        }

        e.extra = extras(root, {"format", "formatVersion", "id", "name", "space", "seed",
                                "duration", "loop", "frameRate", "assets", "layers"});
        return e;
    }
};

Result<LoadedEffect> readEffectImpl(std::string_view text) {
    if (text.size() > kMaxFileBytes) {
        return makeError("This file is too large to be a VFX Forge effect.");
    }
    if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF") {
        text.remove_prefix(3);  // tolerate a UTF-8 byte-order mark
    }

    J root;
    try {
        root = J::parse(text.begin(), text.end());
    } catch (const J::exception& e) {
        return makeError("This file is damaged and cannot be read.", e.what());
    }
    if (!root.is_object()) {
        return makeError("This is not a VFX Forge effect file.", "top level is not an object");
    }
    if (detail::nestsDeeperThan(root, kMaxNesting)) {
        return makeError("This file is damaged and cannot be read.", "nesting is too deep");
    }

    auto format = root.find("format");
    if (format == root.end() || !format.value().is_string() ||
        format.value().get_ref<const std::string&>() != kFormatTag) {
        return makeError("This is not a VFX Forge effect file.",
                         std::string("missing format tag '") + kFormatTag + "'");
    }

    std::int64_t version = 0;
    auto versionField = root.find("formatVersion");
    if (versionField == root.end() || !readInteger(versionField.value(), version) ||
        version < 1 || version > 1000000) {
        return makeError("This file is damaged and cannot be read.", "bad formatVersion");
    }

    LoadedEffect loaded;
    loaded.fileVersion = static_cast<int>(version);

    Loader loader;
    if (version > kFormatVersion) {
        loaded.readOnly = true;
        loader.note("formatVersion",
                    "This effect was saved by a newer version of VFX Forge. It can be viewed "
                    "here but not saved, so nothing in it is lost.");
    } else if (version < kFormatVersion) {
        if (auto s = detail::runMigrations(root, static_cast<int>(version), kFormatVersion,
                                           detail::builtinMigrations());
            !s) {
            return s.error();
        }
    }

    auto effect = loader.effect(root);
    if (!effect) {
        return effect.error();
    }
    loaded.effect = std::move(effect.value());
    loaded.diagnostics = std::move(loader.diagnostics);
    loaded.repaired = loader.repaired;
    return loaded;
}

const char* levelName(Level level) {
    switch (level) {
        case Level::Simple: return "simple";
        case Level::Advanced: return "advanced";
        case Level::Expert: return "expert";
    }
    return "advanced";
}

const char* stageName(Stage stage) {
    switch (stage) {
        case Stage::Spawn: return "spawn";
        case Stage::Update: return "update";
        case Stage::Render: return "render";
        case Stage::Audio: return "audio";
    }
    return "update";
}

J describeTable(const ModuleTypeDesc& table, bool withStage) {
    J o = J::object();
    o["type"] = table.type;
    o["label"] = table.label;
    if (withStage) {
        o["stage"] = stageName(table.stage);
    }
    J properties = J::array();
    for (const auto& p : table.properties) {
        J po = J::object();
        po["key"] = p.key;
        po["label"] = p.label;
        po["kind"] = kindName(p.kind);
        po["level"] = levelName(p.level);
        po["default"] = valueToJson(p.defaultValue);
        if (!p.unit.empty()) {
            po["unit"] = p.unit;
        }
        if (p.min) {
            po["min"] = *p.min;
        }
        if (p.max) {
            po["max"] = *p.max;
        }
        if (p.uiMin) {
            po["sliderMin"] = *p.uiMin;
        }
        if (p.uiMax) {
            po["sliderMax"] = *p.uiMax;
        }
        if (!p.options.empty()) {
            po["options"] = p.options;
        }
        if (!p.help.empty()) {
            po["help"] = p.help;
        }
        properties.push_back(std::move(po));
    }
    o["properties"] = std::move(properties);
    return o;
}

}  // namespace

std::string writeEffect(const Effect& effect) {
    return detail::writeCanonical(effectToJson(effect));
}

Result<LoadedEffect> readEffect(std::string_view text) {
    // Nothing a file contains may crash the application. Any failure not
    // already turned into an error above is reported the same way.
    try {
        return readEffectImpl(text);
    } catch (const std::exception& e) {
        return makeError("This file is damaged and cannot be read.", e.what());
    }
}

std::string valueToText(const Value& value) { return detail::writeInline(valueToJson(value)); }

Result<Value> valueFromText(const PropertyDesc& desc, std::string_view text) {
    J parsed = J::parse(text.begin(), text.end(), nullptr, false);
    if (parsed.is_discarded() || detail::nestsDeeperThan(parsed, kMaxNesting)) {
        return makeError("That is not a value VFX Forge can read.", "not valid JSON");
    }
    Value value;
    try {
        if (!readValue(parsed, desc.kind, value)) {
            return makeError(desc.label + " needs " + kindDescription(desc.kind) + ".");
        }
    } catch (const std::exception& e) {
        return makeError("That is not a value VFX Forge can read.", e.what());
    }
    if (auto s = validateValue(desc, value); !s) {
        return s.error();
    }
    return value;
}

std::string schemaText() {
    const auto& registry = Registry::builtin();
    J root = J::object();
    root["formatVersion"] = kFormatVersion;
    root["effect"] = describeTable(registry.effectFields(), false);
    root["layer"] = describeTable(registry.layerFields(), false);
    J modules = J::array();
    for (const auto& m : registry.modules()) {
        modules.push_back(describeTable(m, true));
    }
    root["modules"] = std::move(modules);
    return detail::writeCanonical(root);
}

}  // namespace vfx
