#include "vfx/Path.h"

#include <utility>
#include <vector>

namespace vfx {

Path Path::effect(std::string field) {
    Path p;
    p.field = std::move(field);
    return p;
}

Path Path::layerField(Id layer, std::string field) {
    Path p;
    p.layer = layer;
    p.field = std::move(field);
    return p;
}

Path Path::property(Id layer, Id module, std::string field) {
    Path p;
    p.layer = layer;
    p.module = module;
    p.field = std::move(field);
    return p;
}

std::string Path::str() const {
    if (isEffectField()) {
        return "effect/" + field;
    }
    if (isLayerField()) {
        return "layer/" + formatId('l', layer) + "/" + field;
    }
    return "layer/" + formatId('l', layer) + "/module/" + formatId('m', module) + "/" + field;
}

Result<Path> Path::parse(std::string_view text) {
    auto fail = [&]() {
        return makeError("\"" + std::string(text) + "\" is not a valid property path.",
                         "expected effect/<field>, layer/<id>/<field> or "
                         "layer/<id>/module/<id>/<property>");
    };

    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t slash = text.find('/', start);
        if (slash == std::string_view::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, slash - start));
        start = slash + 1;
    }
    for (auto part : parts) {
        if (part.empty()) {
            return fail();
        }
    }

    if (parts.size() == 2 && parts[0] == "effect") {
        return Path::effect(std::string(parts[1]));
    }
    if (parts.size() == 3 && parts[0] == "layer") {
        auto layer = parseId(parts[1], 'l');
        if (!layer) {
            return fail();
        }
        return Path::layerField(layer.value(), std::string(parts[2]));
    }
    if (parts.size() == 5 && parts[0] == "layer" && parts[2] == "module") {
        auto layer = parseId(parts[1], 'l');
        auto module = parseId(parts[3], 'm');
        if (!layer || !module) {
            return fail();
        }
        return Path::property(layer.value(), module.value(), std::string(parts[4]));
    }
    return fail();
}

}  // namespace vfx
