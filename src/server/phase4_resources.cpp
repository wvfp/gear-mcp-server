#include "server/mcp_methods.h"
#include "godot_api/godot_api.h"
#include "shared/path_utils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using json = nlohmann::json;

namespace gear_mcp {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

std::string read_text_file(const std::string &p_path) {
    std::ifstream f(p_path, std::ios::binary);
    if (!f.is_open()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool ends_with(const std::string &p_s, const std::string &p_suffix) {
    return p_s.size() >= p_suffix.size() &&
           p_s.compare(p_s.size() - p_suffix.size(), p_suffix.size(), p_suffix) == 0;
}

std::string detect_language(const std::string &p_path) {
    if (ends_with(p_path, ".gd")) return "gdscript";
    if (ends_with(p_path, ".cs")) return "csharp";
    if (ends_with(p_path, ".tscn") || ends_with(p_path, ".scn")) return "godot-scene";
    if (ends_with(p_path, ".tres") || ends_with(p_path, ".res")) return "godot-resource";
    if (ends_with(p_path, ".json")) return "json";
    if (ends_with(p_path, ".md")) return "markdown";
    return "text";
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// godot://project/info — return project.godot metadata as JSON
// ---------------------------------------------------------------------------

void resource_project_info(const std::string &p_uri, std::string &r_text_out) {
    (void)p_uri;
    std::string project_path = GodotAPI::get_project_path();

    json info = json::object();
    info["project_path"] = project_path;
    info["project_godot_exists"] = false;

    if (project_path.empty()) {
        info["error"] = "No active project detected";
        r_text_out = info.dump(2);
        return;
    }

    std::string pg_path = project_path + "/project.godot";
    info["project_godot_exists"] = fs::exists(pg_path);

    // Read engine version
    GDExtensionObjectPtr engine = GodotAPI::get_global_singleton("Engine");
    if (engine) {
        info["engine_version"] = GodotAPI::call_method_string(engine, "get_version_info");
    }

    if (!fs::exists(pg_path)) {
        info["error"] = "project.godot not found at " + pg_path;
        r_text_out = info.dump(2);
        return;
    }

    // Parse a few well-known keys (we don't need a full INI parser here)
    std::ifstream f(pg_path);
    std::string line;
    json app_section = json::object();
    bool in_application = false;
    if (f.is_open()) {
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == ';') continue;
            if (line.front() == '[' && line.back() == ']') {
                in_application = (line == "[application]");
                continue;
            }
            if (!in_application) continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            // Strip leading/trailing whitespace
            size_t s = val.find_first_not_of(" \t");
            size_t e = val.find_last_not_of(" \t");
            if (s != std::string::npos) val = val.substr(s, e - s + 1);
            // Strip surrounding quotes
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
                val = val.substr(1, val.size() - 2);
            }
            app_section[key] = val;
        }
        f.close();
    }
    info["application_config"] = app_section;
    r_text_out = info.dump(2);
}

// ---------------------------------------------------------------------------
// godot://scene/{path} — return scene file content
// ---------------------------------------------------------------------------

void resource_scene(const std::string &p_uri, std::string &r_text_out) {
    // URI format: godot://scene/{path}
    const std::string prefix = "godot://scene/";
    std::string res_path = p_uri.substr(prefix.size());
    if (res_path.empty()) { r_text_out = json({{"error", "empty path"}}).dump(); return; }
    if (res_path.substr(0, 6) != "res://") res_path = "res://" + res_path;

    std::string abs = GodotAPI::res_to_absolute(res_path);
    std::string content = read_text_file(abs);
    if (content.empty()) {
        r_text_out = json({{"error", "scene not found"}, {"path", res_path}}).dump();
        return;
    }

    json result = {
        {"uri", p_uri},
        {"path", res_path},
        {"absolute_path", abs},
        {"language", "godot-scene"},
        {"size_bytes", (int)content.size()},
        {"content", content}
    };
    r_text_out = result.dump(2);
}

// ---------------------------------------------------------------------------
// godot://script/{path} — return script file content
// ---------------------------------------------------------------------------

void resource_script(const std::string &p_uri, std::string &r_text_out) {
    const std::string prefix = "godot://script/";
    std::string res_path = p_uri.substr(prefix.size());
    if (res_path.empty()) { r_text_out = json({{"error", "empty path"}}).dump(); return; }
    if (res_path.substr(0, 6) != "res://") res_path = "res://" + res_path;

    std::string abs = GodotAPI::res_to_absolute(res_path);
    std::string content = read_text_file(abs);
    if (content.empty()) {
        r_text_out = json({{"error", "script not found"}, {"path", res_path}}).dump();
        return;
    }

    json result = {
        {"uri", p_uri},
        {"path", res_path},
        {"absolute_path", abs},
        {"language", detect_language(res_path)},
        {"size_bytes", (int)content.size()},
        {"content", content}
    };
    r_text_out = result.dump(2);
}

// ---------------------------------------------------------------------------
// godot://resource/{path} — return resource file content (.tres/.res/.json/etc.)
// ---------------------------------------------------------------------------

void resource_resource(const std::string &p_uri, std::string &r_text_out) {
    const std::string prefix = "godot://resource/";
    std::string res_path = p_uri.substr(prefix.size());
    if (res_path.empty()) { r_text_out = json({{"error", "empty path"}}).dump(); return; }
    if (res_path.substr(0, 6) != "res://") res_path = "res://" + res_path;

    std::string abs = GodotAPI::res_to_absolute(res_path);
    std::string content = read_text_file(abs);
    if (content.empty()) {
        r_text_out = json({{"error", "resource not found"}, {"path", res_path}}).dump();
        return;
    }

    json result = {
        {"uri", p_uri},
        {"path", res_path},
        {"absolute_path", abs},
        {"language", detect_language(res_path)},
        {"size_bytes", (int)content.size()},
        {"content", content}
    };
    r_text_out = result.dump(2);
}

// ---------------------------------------------------------------------------
// Prompt: godot.scene_bootstrap — workflow template for scene creation
// ---------------------------------------------------------------------------

std::vector<PromptMessage> prompt_scene_bootstrap(
    const std::vector<std::pair<std::string, std::string>> &p_args) {

    // Extract arguments with defaults
    std::string name = "MyScene";
    std::string root_node = "Node2D";
    std::string children_spec = "(none specified)";

    for (const auto &kv : p_args) {
        if (kv.first == "name") name = kv.second;
        else if (kv.first == "root_node") root_node = kv.second;
        else if (kv.first == "children") children_spec = kv.second;
    }

    std::vector<PromptMessage> msgs;

    // User message (the prompt itself, presented to the LLM)
    std::ostringstream user_text;
    user_text
        << "You are helping a developer set up a new Godot scene using the Gear MCP Server.\n"
        << "\n"
        << "## Goal\n"
        << "Create a Godot scene named `res://scenes/" << name << ".tscn` with the following structure:\n"
        << "- Root node: " << root_node << " (name: " << name << ")\n"
        << "- Children: " << children_spec << "\n"
        << "\n"
        << "## Available MCP tools\n"
        << "- `create_scene` — create an empty scene file\n"
        << "- `save_scene` — persist the scene\n"
        << "- `add_node` — add child nodes with optional properties and scripts\n"
        << "- `set_node_properties` — modify node properties (position, scale, color, etc.)\n"
        << "- `create_script` — attach a GDScript to a node\n"
        << "- `connect_signal` — wire up node signals\n"
        << "- `list_scene_nodes` — verify the resulting tree\n"
        << "\n"
        << "## Recommended workflow\n"
        << "1. Call `create_scene` with `resource_path=\"res://scenes/" << name << ".tscn\"` and "
        << "`root_node_type=\"" << root_node << "\"`, `root_node_name=\"" << name << "\"`.\n"
        << "2. For each child, call `add_node` with the correct `parent_path`.\n"
        << "3. Adjust any properties via `set_node_properties`.\n"
        << "4. Attach scripts via `create_script` and reconnect to add_node if needed.\n"
        << "5. Call `save_scene` to persist.\n"
        << "6. Call `list_scene_nodes` to verify the tree.\n"
        << "\n"
        << "## Guidelines\n"
        << "- Always pass `res://` prefixed paths.\n"
        << "- Use `main_scene: true` only on the entry-point scene.\n"
        << "- Prefer ColorRect/Sprite2D/Button from the scene tree for UI prototypes.\n"
        << "- After each step, check the response's `success` field before continuing.\n";

    msgs.push_back({"user", user_text.str()});
    return msgs;
}

// ---------------------------------------------------------------------------
// Prompt: godot.debug_triage — workflow template for debugging
// ---------------------------------------------------------------------------

std::vector<PromptMessage> prompt_debug_triage(
    const std::vector<std::pair<std::string, std::string>> &p_args) {

    std::string symptom = "(unspecified)";
    std::string context = "(no context)";

    for (const auto &kv : p_args) {
        if (kv.first == "symptom") symptom = kv.second;
        else if (kv.first == "context") context = kv.second;
    }

    std::vector<PromptMessage> msgs;

    std::ostringstream user_text;
    user_text
        << "You are helping a developer debug an issue in their Godot project via the Gear MCP Server.\n"
        << "\n"
        << "## Symptom\n"
        << symptom << "\n"
        << "\n"
        << "## Context\n"
        << context << "\n"
        << "\n"
        << "## Triage loop\n"
        << "1. **Capture state** — call `capture_intent_snapshot` with a `label` like \"debug:<symptom-short>\" "
        << "and `intent` describing the suspected cause. This lets you come back later.\n"
        << "2. **Inspect project** — call:\n"
        << "   - `get_project_info` for project metadata\n"
        << "   - `validate_project` for missing files / broken references\n"
        << "   - `get_editor_status` to see what is currently open and selected\n"
        << "3. **Inspect code** — call `lsp_get_diagnostics` (if available) on the suspect script path to "
        << "retrieve compile errors and warnings. The path must be `res://`-prefixed.\n"
        << "4. **Read the file** — use `read_file` on the suspect .gd/.cs/.tscn to see the actual code.\n"
        << "5. **Test runtime** — call `run_project` then poll `get_runtime_metrics` to see FPS / "
        << "node counts. Use `inspect_runtime_tree` to confirm scene composition.\n"
        << "6. **Log every step** — call `record_work_step` with `action`, `tool`, `args`, and `outcome` "
        << "after each significant call. Use `record_decision_log` when you form a hypothesis.\n"
        << "7. **Propose a fix** — make a minimal code change, call `modify_script` to apply it, "
        << "then re-run the project and re-check diagnostics.\n"
        << "8. **Handoff** — call `generate_handoff_brief` (or `export_handoff_pack`) to capture the "
        << "session for the user.\n"
        << "\n"
        << "## Available tools (high level)\n"
        << "- `capture_intent_snapshot`, `record_decision_log`, `record_work_step` — logging\n"
        << "- `get_project_info`, `validate_project`, `get_editor_status` — introspection\n"
        << "- `lsp_get_diagnostics`, `read_file` — code inspection\n"
        << "- `run_project`, `inspect_runtime_tree`, `get_runtime_metrics` — runtime\n"
        << "- `modify_script`, `create_script` — code edits\n"
        << "- `generate_handoff_brief`, `export_handoff_pack` — handoff\n"
        << "\n"
        << "## Guidelines\n"
        << "- Work in small, verifiable steps.\n"
        << "- Always check `success` and `error` fields in responses.\n"
        << "- Prefer `read_file` over `lsp_get_diagnostics` for raw text inspection.\n";

    msgs.push_back({"user", user_text.str()});
    return msgs;
}

// ---------------------------------------------------------------------------
// Public registration entry point — called from register_types.cpp
// ---------------------------------------------------------------------------

void register_phase4_resources(MCPMethods *p_methods) {
    if (!p_methods) return;

    ResourceInfo r;
    r.uri = "godot://project/info";
    r.name = "Project Info";
    r.description = "Project path, engine version, and project.godot [application] section";
    r.mime_type = "application/json";
    r.read_handler = resource_project_info;
    p_methods->register_resource(r);

    r.uri = "godot://scene/{path}";
    r.name = "Scene File";
    r.description = "Read the contents of a .tscn/.scn scene file. URI: godot://scene/{res_path}";
    r.mime_type = "application/json";
    r.read_handler = resource_scene;
    p_methods->register_resource(r);

    r.uri = "godot://script/{path}";
    r.name = "Script File";
    r.description = "Read the contents of a script file (.gd, .cs, etc.). URI: godot://script/{res_path}";
    r.mime_type = "application/json";
    r.read_handler = resource_script;
    p_methods->register_resource(r);

    r.uri = "godot://resource/{path}";
    r.name = "Resource File";
    r.description = "Read the contents of any resource file. URI: godot://resource/{res_path}";
    r.mime_type = "application/json";
    r.read_handler = resource_resource;
    p_methods->register_resource(r);

    PromptInfo p;
    p.name = "godot.scene_bootstrap";
    p.description = "Workflow template for creating a new Godot scene with the Gear MCP tools";
    p.arguments = {
        {"name", "Scene name (default: MyScene)", false},
        {"root_node", "Root node class (default: Node2D)", false},
        {"children", "Free-form description of the desired children", false}
    };
    p_methods->register_prompt(p, prompt_scene_bootstrap);

    p.name = "godot.debug_triage";
    p.description = "Debug-triage workflow template using logging, introspection, and code inspection tools";
    p.arguments = {
        {"symptom", "One-line description of the symptom", false},
        {"context", "Free-form context (recent changes, repro steps)", false}
    };
    p_methods->register_prompt(p, prompt_debug_triage);
}

} // namespace gear_mcp
