// VFX Forge core: property metadata.
//
// Each module type declares its properties once. The inspector, file
// validation, the command layer and (later) scripting and AI edits all read
// this one table, so a new module needs no new editor code.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "vfx/Result.h"
#include "vfx/Value.h"

namespace vfx {

// Which editor level shows a property. Lower levels are always included.
enum class Level { Simple, Advanced, Expert };

enum class Stage { Spawn, Update, Render };

struct PropertyDesc {
    // Name in files and property paths. "id" and "type" are reserved: every
    // module already stores those two beside its properties.
    std::string key;
    std::string label;  // name shown to artists
    ValueKind kind = ValueKind::Float;
    Value defaultValue;
    Level level = Level::Advanced;
    std::string unit;
    std::string help;

    // Hard limits, enforced on every edit and on load. For Scalar, Vec3 and
    // curve values they apply to each number.
    std::optional<double> min;
    std::optional<double> max;

    // Suggested slider range. Values outside it are still legal.
    std::optional<double> uiMin;
    std::optional<double> uiMax;

    std::vector<std::string> options;  // Enum only
};

struct ModuleTypeDesc {
    std::string type;
    std::string label;
    Stage stage = Stage::Update;
    std::vector<PropertyDesc> properties;

    const PropertyDesc* find(std::string_view key) const;
    int indexOf(std::string_view key) const;  // -1 when absent
};

class Registry {
public:
    // The module types built into this version of VFX Forge.
    static const Registry& builtin();

    const ModuleTypeDesc* findModule(std::string_view type) const;
    const std::vector<ModuleTypeDesc>& modules() const { return modules_; }

    // Effect-level and layer-level fields are described the same way as
    // module properties so one set-property operation covers all three.
    const ModuleTypeDesc& effectFields() const { return effect_; }
    const ModuleTypeDesc& layerFields() const { return layer_; }

private:
    Registry();

    std::vector<ModuleTypeDesc> modules_;
    ModuleTypeDesc effect_;
    ModuleTypeDesc layer_;
};

// Checks type, finiteness, range, enum membership and curve ordering.
Status validateValue(const PropertyDesc& desc, const Value& value);

// Used by the loader: returns the nearest legal value (clamped into range)
// or the default when the value cannot be repaired.
Value repairValue(const PropertyDesc& desc, const Value& value);

// Sanity limits that keep a damaged or hostile file from exhausting memory.
inline constexpr std::size_t kMaxCurveKeys = 1024;
inline constexpr std::size_t kMaxBursts = 1024;
inline constexpr std::size_t kMaxTextLength = 1024;

}  // namespace vfx
