// VFX Forge editor: the AI Shader Artist's engine.
//
// A language model never touches the shader. It is shown the shader's real state and the real
// parts list (elements, settings and their limits), and answers with a short list of edit
// commands: "add a Ring layer called Outer ring", "key its rotation from 0 to 360 over two
// seconds". This file checks every command against the shader library, applies the valid ones to
// a copy of the shader and reports exactly what changed and what was refused. The app then lets
// the artist apply the copy (one undo step) or throw it away.
//
// Nothing here talks to a network: requests and replies for the two supported providers (Claude,
// and any OpenAI-compatible endpoint) are built and read here as JSON, and the app sends them.
// So all of it can be tested without a model.
#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "vfx/Result.h"
#include "vfx/editor/ShaderLib.h"

namespace vfx::editor::shader::ai {

// What the artist is asking for.
enum class Mode {
    Generate,  // a new layer shader from a description
    Refine,    // change the shader that is open (the selected layer only, unless told otherwise)
    Animate,   // timeline only
    Explain,   // words only; nothing changes
};

struct Context {
    Settings settings;       // the shader as it is now: the source of truth
    int selectedLayer = -1;  // the layer the artist has selected, or -1
    Mode mode = Mode::Generate;
};

// ---- what the model is told
// The rules and the parts list, written from the library itself so they cannot drift from it.
std::string systemPrompt(Mode mode);
// The shader's state as compact JSON.
std::string stateJson(const Context& context);
// The JSON schema of the one tool the model answers with ("edit_shader").
std::string toolSchemaJson();

// ---- what the model answers
// One command, as the model wrote it. `json` is the command's own JSON.
struct Op {
    std::string type;
    std::string json;
};
struct Plan {
    std::string summary;      // one line: what it did
    std::string explanation;  // why, in plain words
    bool allLayers = false;   // the artist asked for a change beyond the selected layer
    std::vector<Op> ops;
};
// Reads the tool's JSON arguments. Fails, saying why, when it is not a plan at all; commands
// that are not valid are kept and refused one by one when applied.
Result<Plan> parsePlan(const std::string& json);

// The result of applying a plan to a copy of the shader.
struct Applied {
    Settings settings;                  // the shader with the valid commands applied
    std::vector<std::string> changes;   // one plain line for each thing that changed
    std::vector<std::string> refused;   // each command not applied, with the reason
    std::vector<int> touchedLayers;     // layers added or changed, by slot
    int applied = 0;                    // how many commands worked
};
Applied applyPlan(const Context& context, const Plan& plan);

// ---- the providers
enum class Provider { Claude, OpenAiCompatible };
struct ProviderConfig {
    Provider provider = Provider::Claude;
    std::string model;    // "claude-sonnet-5-5", "gpt-4o" ...
    std::string baseUrl;  // OpenAI-compatible: "https://api.openai.com/v1"; Claude: empty for the default
};
struct Turn {
    std::string user;       // what the artist asked
    std::string assistant;  // what the assistant answered (its summary), for follow-ups
};
// The HTTP request for one question: where to post, the headers (without the key; the app adds
// it from where it keeps it) and the JSON body.
struct HttpRequest {
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};
HttpRequest buildRequest(const ProviderConfig& config, const Context& context, const std::string& prompt,
                         const std::vector<Turn>& history);
// What a reply contained: a plan, and/or text. `error` is the provider's own message when it
// refused or failed.
struct Reply {
    bool hasPlan = false;
    Plan plan;
    std::string text;
    std::string error;
};
Reply parseReply(const ProviderConfig& config, const std::string& body, int httpStatus);

// Colours: "#rrggbb" or "#rrggbbaa" to red, green, blue, alpha (0 to 1), and back.
bool parseHexColour(const std::string& text, std::array<float, 4>& out);
std::string toHex(const std::array<float, 4>& colour);

const char* modeName(Mode mode);

}  // namespace vfx::editor::shader::ai
