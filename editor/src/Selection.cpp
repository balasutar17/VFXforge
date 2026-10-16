#include "vfx/editor/Selection.h"

#include <algorithm>
#include <unordered_set>

namespace vfx::editor {

bool inGroup(const Layer& layer, std::string_view path) {
    if (path.empty() || layer.group.size() < path.size()) {
        return false;
    }
    if (std::string_view(layer.group).substr(0, path.size()) != path) {
        return false;
    }
    return layer.group.size() == path.size() || layer.group[path.size()] == '/';
}

std::string parentGroup(std::string_view path) {
    const auto slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string() : std::string(path.substr(0, slash));
}

std::string groupName(std::string_view path) {
    const auto slash = path.rfind('/');
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

int groupDepth(std::string_view path) {
    return path.empty() ? 0 : 1 + static_cast<int>(std::count(path.begin(), path.end(), '/'));
}

std::vector<std::string> groupPaths(const Effect& effect) {
    std::vector<std::string> paths;
    for (const Layer& layer : effect.layers) {
        // Each step of the layer's path, outermost first.
        std::string_view g = layer.group;
        std::size_t at = 0;
        while (!g.empty()) {
            const auto slash = g.find('/', at);
            const std::string step(g.substr(0, slash));
            if (std::find(paths.begin(), paths.end(), step) == paths.end()) {
                paths.push_back(step);
            }
            if (slash == std::string_view::npos) {
                break;
            }
            at = slash + 1;
        }
    }
    return paths;
}

std::string cleanGroupName(std::string_view name) {
    std::string out;
    for (const char c : name) {
        out += c == '/' ? '-' : c;
    }
    const auto first = out.find_first_not_of(' ');
    const auto last = out.find_last_not_of(' ');
    out = first == std::string::npos ? std::string() : out.substr(first, last - first + 1);
    return out.empty() ? std::string("Group") : out;
}

ResolvedSelection resolveSelection(const Effect& effect, const Selection& selection) {
    ResolvedSelection out;
    std::unordered_set<std::uint64_t> picked;
    for (const Id id : selection.layers) {
        picked.insert(id.value);
    }
    for (const Layer& layer : effect.layers) {
        bool chosen = picked.count(layer.id.value) > 0;
        for (const std::string& g : selection.groups) {
            chosen = chosen || inGroup(layer, g);
        }
        if (!chosen) {
            continue;
        }
        (layer.locked ? out.locked : out.targets).push_back(layer.id);
    }
    return out;
}

}  // namespace vfx::editor
