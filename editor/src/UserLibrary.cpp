#include "vfx/editor/UserLibrary.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <system_error>

#include <nlohmann/json.hpp>

#include "vfx/FileIO.h"
#include "vfx/editor/SoundLibrary.h"

namespace vfx::editor {

namespace {

using J = nlohmann::json;

std::filesystem::path metaFile(const std::filesystem::path& root, const std::string& id) {
    return userEffectFolder(root, id) / "card.json";
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool safeId(const std::string& id) {
    if (id.empty() || id.size() > 100) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) || c == '-'; });
}

}  // namespace

std::filesystem::path userEffectFolder(const std::filesystem::path& root, const std::string& id) {
    return root / id;
}

std::filesystem::path userEffectFile(const std::filesystem::path& root, const std::string& id) {
    return userEffectFolder(root, id) / "effect.vfx";
}

std::string userEffectSlug(const std::string& name) {
    std::string out;
    for (const char ch : name) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) && c < 0x80) {
            out.push_back(static_cast<char>(std::tolower(c)));
        } else if (!out.empty() && out.back() != '-') {
            out.push_back('-');
        }
    }
    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    return out.empty() ? std::string("effect") : out.substr(0, 60);
}

Result<SavedUserEffect> saveUserEffect(const std::filesystem::path& root, const Effect& effect,
                                       const std::filesystem::path& projectFolder, float viewX, float viewY,
                                       float viewHeight, double previewTime) {
    if (root.empty()) {
        return makeError("There is nowhere to keep the library on this computer.");
    }
    // The same name replaces the saved one; a different name that makes the
    // same folder name gets a number.
    std::string id = userEffectSlug(effect.name);
    bool replaced = false;
    std::error_code error;
    for (int n = 2;; ++n) {
        const auto existing = metaFile(root, id);
        if (!std::filesystem::exists(existing, error)) {
            break;
        }
        auto text = readFile(existing);
        std::string savedName;
        if (text) {
            const J meta = J::parse(text.value(), nullptr, false);
            if (meta.is_object()) {
                savedName = meta.value("name", "");
            }
        }
        if (lower(savedName) == lower(effect.name)) {
            replaced = true;
            break;
        }
        id = userEffectSlug(effect.name) + "-" + std::to_string(n);
    }
    const std::filesystem::path folder = userEffectFolder(root, id);
    std::filesystem::create_directories(folder, error);
    if (error) {
        return makeError("The library folder could not be made.", error.message());
    }
    // Pictures and sounds travel with it. A library sound is made again
    // when it is needed, so it is not copied.
    for (const Asset& asset : effect.assets) {
        const auto source = projectFolder / pathFromUtf8(asset.path);
        const auto target = folder / pathFromUtf8(asset.path);
        if (!librarySoundId(asset.path).empty() || !std::filesystem::exists(source, error)) {
            continue;
        }
        std::filesystem::create_directories(target.parent_path(), error);
        auto bytes = readFile(source);
        if (!bytes) {
            return makeError("A picture or sound of the effect could not be read: " + asset.path);
        }
        if (Status written = writeFileAtomic(target, bytes.value()); !written) {
            return written.error();
        }
    }
    if (Status saved = saveEffect(userEffectFile(root, id), effect); !saved) {
        return saved.error();
    }
    J meta = J::object();
    meta["name"] = effect.name;
    meta["space"] = effect.space;
    meta["duration"] = effect.duration;
    meta["layers"] = static_cast<int>(effect.layers.size());
    meta["viewX"] = viewX;
    meta["viewY"] = viewY;
    meta["viewHeight"] = viewHeight;
    meta["previewTime"] = previewTime;
    if (Status written = writeFileAtomic(metaFile(root, id), meta.dump(2) + "\n"); !written) {
        return written.error();
    }
    return SavedUserEffect{id, replaced};
}

Result<UserEffectInfo> userEffectInfo(const std::filesystem::path& root, const std::string& id) {
    std::error_code error;
    if (!safeId(id) || !std::filesystem::exists(userEffectFile(root, id), error)) {
        return makeError("That effect is not in your library.");
    }
    auto text = readFile(metaFile(root, id));
    if (!text) {
        return makeError("That effect's card could not be read.");
    }
    const J meta = J::parse(text.value(), nullptr, false);
    if (!meta.is_object()) {
        return makeError("That effect's card is damaged.");
    }
    UserEffectInfo info;
    info.id = id;
    info.name = meta.value("name", id);
    info.space = meta.value("space", "2d");
    info.duration = meta.value("duration", 2.0);
    info.layers = meta.value("layers", 0);
    info.viewX = meta.value("viewX", 0.0f);
    info.viewY = meta.value("viewY", 0.0f);
    info.viewHeight = meta.value("viewHeight", 6.0f);
    info.previewTime = meta.value("previewTime", 0.5);
    if (!(info.viewHeight > 0.0f) || !std::isfinite(info.viewHeight)) {
        info.viewHeight = 6.0f;
    }
    return info;
}

std::vector<UserEffectInfo> listUserEffects(const std::filesystem::path& root) {
    std::vector<UserEffectInfo> out;
    std::error_code error;
    if (root.empty() || !std::filesystem::is_directory(root, error)) {
        return out;
    }
    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        if (!entry.is_directory(error)) {
            continue;
        }
        if (auto info = userEffectInfo(root, entry.path().filename().string())) {
            out.push_back(std::move(info.value()));
        }
    }
    std::sort(out.begin(), out.end(), [](const UserEffectInfo& a, const UserEffectInfo& b) {
        return lower(a.name) < lower(b.name);
    });
    return out;
}

Result<Effect> loadUserEffect(const std::filesystem::path& root, const std::string& id) {
    if (!safeId(id)) {
        return makeError("That is not an effect in your library.");
    }
    auto loaded = loadEffect(userEffectFile(root, id));
    if (!loaded) {
        return loaded.error();
    }
    return std::move(loaded.value().effect);
}

Status removeUserEffect(const std::filesystem::path& root, const std::string& id) {
    if (!safeId(id)) {
        return makeError("That is not an effect in your library.");
    }
    std::error_code error;
    std::filesystem::remove_all(userEffectFolder(root, id), error);
    if (error) {
        return makeError("The effect could not be removed from the library.", error.message());
    }
    return {};
}

Effect withFreshIds(Effect effect, const IdSource& newId) {
    std::map<std::uint64_t, Id> same;
    const auto fresh = [&](Id old) {
        if (!old.valid()) {
            return old;
        }
        const auto it = same.find(old.value);
        if (it != same.end()) {
            return it->second;
        }
        const Id made = newId();
        same.emplace(old.value, made);
        return made;
    };
    effect.id = fresh(effect.id);
    for (Asset& asset : effect.assets) {
        asset.id = fresh(asset.id);
    }
    effect.target.id = fresh(effect.target.id);
    for (Layer& layer : effect.layers) {
        layer.id = fresh(layer.id);
        for (Module& module : layer.modules) {
            module.id = fresh(module.id);
        }
    }
    // References come after every ID has its new value.
    for (Layer& layer : effect.layers) {
        for (SimpleControl& control : layer.controls) {
            control.id = fresh(control.id);
            for (ControlTarget& target : control.targets) {
                target.module = fresh(target.module);
            }
        }
        for (Module& module : layer.modules) {
            for (Value& value : module.values) {
                if (auto* ref = std::get_if<AssetRef>(&value)) {
                    ref->id = fresh(ref->id);
                }
            }
        }
    }
    return effect;
}

}  // namespace vfx::editor
