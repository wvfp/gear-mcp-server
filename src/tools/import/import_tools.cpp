#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include "shared/path_utils.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace gear_mcp {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helper: get the EditorFileSystem singleton
// ---------------------------------------------------------------------------

static GDExtensionObjectPtr get_efs() {
    return GodotAPI::get_global_singleton("EditorFileSystem");
}

// ---------------------------------------------------------------------------
// get_import_status — return import status (exists, is valid, dependencies)
// for a resource path.
// ---------------------------------------------------------------------------

static const char *GET_IMPORT_STATUS_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "resource_path": {"type": "string", "description": "res:// path of the imported resource"}
    },
    "required": ["resource_path"]
})schema";

static void handle_get_import_status(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string resource_path = params.value("resource_path", "");
    if (resource_path.empty()) { r_error = "Missing required parameter: resource_path"; return; }
    if (resource_path.substr(0, 6) != "res://") resource_path = "res://" + resource_path;

    GDExtensionObjectPtr efs = get_efs();
    if (!efs) { r_error = "EditorFileSystem not available"; return; }

    // get_file_type(path) -> String
    {
        uint8_t vb[64] = {0};
        GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)vb, resource_path);
        GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)vb };
        std::string ftype = GodotAPI::call_method_with_args(efs, "get_file_type", args, 1);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)vb);

        bool exists = (ftype != "");

        // Reimport is needed when ".import" file doesn't exist but source does
        std::string abs = GodotAPI::res_to_absolute(resource_path);
        std::string import_sidecar = abs + ".import";
        bool has_import_file = fs::exists(import_sidecar);

        json result = {
            {"success", true},
            {"resource_path", resource_path},
            {"file_type", ftype},
            {"exists", exists},
            {"has_import_file", has_import_file}
        };
        r_result = result.dump();
    }
}

// ---------------------------------------------------------------------------
// get_import_options — read a resource's .import file and return its options
// (best-effort parser; the format is INI-like but Godot-specific).
// ---------------------------------------------------------------------------

static const char *GET_IMPORT_OPTIONS_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "resource_path": {"type": "string", "description": "res:// path of the imported resource"}
    },
    "required": ["resource_path"]
})schema";

static void handle_get_import_options(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string resource_path = params.value("resource_path", "");
    if (resource_path.empty()) { r_error = "Missing required parameter: resource_path"; return; }
    if (resource_path.substr(0, 6) != "res://") resource_path = "res://" + resource_path;

    std::string abs = GodotAPI::res_to_absolute(resource_path);
    std::string sidecar = abs + ".import";
    if (!fs::exists(sidecar)) {
        r_error = "No .import file for: " + resource_path;
        return;
    }

    std::ifstream f(sidecar);
    if (!f.is_open()) { r_error = "Cannot open: " + sidecar; return; }
    std::stringstream ss; ss << f.rdbuf();
    std::string content = ss.str();
    f.close();

    // The .import file is INI-like:
    // [remap]
    // importer="..."
    // type="..."
    // [deps]
    // ...
    // [params]
    // key=value
    json sections = json::object();
    std::string current_section;
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        // Strip CR
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            current_section = line.substr(1, line.size() - 2);
            sections[current_section] = json::object();
        } else if (!current_section.empty()) {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                std::string value = line.substr(eq + 1);
                // Trim spaces
                size_t s = key.find_first_not_of(" \t");
                size_t e = key.find_last_not_of(" \t");
                if (s != std::string::npos) key = key.substr(s, e - s + 1);
                s = value.find_first_not_of(" \t");
                e = value.find_last_not_of(" \t");
                if (s != std::string::npos) value = value.substr(s, e - s + 1);
                // Strip surrounding quotes
                if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                    value = value.substr(1, value.size() - 2);
                }
                sections[current_section][key] = value;
            }
        }
    }

    json result = {
        {"success", true},
        {"resource_path", resource_path},
        {"options", sections}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// set_import_options — set one or more key=value pairs in the .import file's
// [params] section. Other sections (remap, deps) are left untouched.
// ---------------------------------------------------------------------------

static const char *SET_IMPORT_OPTIONS_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "resource_path": {"type": "string", "description": "res:// path of the imported resource"},
        "options": {
            "type": "object",
            "description": "key-value pairs to set in [params]"
        }
    },
    "required": ["resource_path", "options"]
})schema";

static void handle_set_import_options(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string resource_path = params.value("resource_path", "");
    if (resource_path.empty()) { r_error = "Missing required parameter: resource_path"; return; }
    if (resource_path.substr(0, 6) != "res://") resource_path = "res://" + resource_path;
    if (!params.contains("options") || !params["options"].is_object()) {
        r_error = "Missing required parameter: options (object)";
        return;
    }

    std::string abs = GodotAPI::res_to_absolute(resource_path);
    std::string sidecar = abs + ".import";
    if (!fs::exists(sidecar)) {
        r_error = "No .import file for: " + resource_path;
        return;
    }

    std::ifstream f(sidecar);
    std::vector<std::string> lines;
    if (f.is_open()) {
        std::string line;
        while (std::getline(f, line)) lines.push_back(line);
        f.close();
    }

    // Find [params] section bounds
    int params_start = -1;
    int params_end = (int)lines.size();
    for (int i = 0; i < (int)lines.size(); i++) {
        std::string trimmed = lines[i];
        if (!trimmed.empty() && trimmed.back() == '\r') trimmed.pop_back();
        if (trimmed == "[params]") {
            params_start = i;
        } else if (params_start >= 0 && i > params_start && trimmed.size() > 2 && trimmed.front() == '[' && trimmed.back() == ']') {
            params_end = i;
            break;
        }
    }
    if (params_start < 0) {
        // Append a [params] section
        std::ofstream append(sidecar, std::ios::app);
        append << "\n[params]\n";
        for (auto it = params["options"].begin(); it != params["options"].end(); ++it) {
            append << it.key() << "=" << it.value().dump() << "\n";
        }
        append.close();
    } else {
        // Replace or insert options
        json options = params["options"];
        for (auto it = options.begin(); it != options.end(); ++it) {
            const std::string &key = it.key();
            std::string value = it.value().dump();
            // Keep JSON-formatted (string values already include quotes)
            std::string line = key + "=" + value;
            bool replaced = false;
            std::string prefix = key + "=";
            for (int i = params_start + 1; i < params_end; i++) {
                std::string trimmed = lines[i];
                if (!trimmed.empty() && trimmed.back() == '\r') trimmed.pop_back();
                if (trimmed.substr(0, prefix.size()) == prefix) {
                    lines[i] = line;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                lines.insert(lines.begin() + params_end, line);
                params_end++;
            }
        }

        std::ofstream out_file(sidecar, std::ios::trunc);
        for (const auto &line : lines) out_file << line << "\n";
        out_file.close();
    }

    json result = {
        {"success", true},
        {"resource_path", resource_path},
        {"options", params["options"]}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// reimport_resource — call reimport_files() on the given resource
// ---------------------------------------------------------------------------

static const char *REIMPORT_RESOURCE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "resource_paths": {
            "type": "array",
            "items": {"type": "string"},
            "description": "List of res:// paths to reimport"
        }
    },
    "required": ["resource_paths"]
})schema";

static void handle_reimport_resource(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    if (!params.contains("resource_paths") || !params["resource_paths"].is_array()) {
        r_error = "Missing or invalid 'resource_paths' (array of strings)";
        return;
    }

    GDExtensionObjectPtr efs = get_efs();
    if (!efs) { r_error = "EditorFileSystem not available"; return; }

    // Call update_file() for each resource to mark it dirty, then scan().
    int updated = 0;
    for (const auto &p : params["resource_paths"]) {
        if (!p.is_string()) continue;
        std::string path = p.get<std::string>();
        if (path.substr(0, 6) != "res://") path = "res://" + path;

        uint8_t vb[64] = {0};
        GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)vb, path);
        GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)vb };
        GodotAPI::call_method_void(efs, "update_file", args, 1);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)vb);
        updated++;
    }

    // Trigger a scan
    GodotAPI::call_method_void(efs, "scan", nullptr, 0);

    json result = {
        {"success", true},
        {"updated", updated},
        {"note", "Files marked dirty; EditorFileSystem.scan() triggered."}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// validate_project — return a basic sanity check: project.godot exists,
// Godot version, addon count, scene count.
// ---------------------------------------------------------------------------

static const char *VALIDATE_PROJECT_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "project_path": {"type": "string", "description": "Godot project path"}
    }
})schema";

static void handle_validate_project(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string project_path = params.value("project_path", "");
    if (project_path.empty()) project_path = GodotAPI::get_project_path();
    if (project_path.empty()) { r_error = "No project_path provided and no active project detected."; return; }

    namespace fs = std::filesystem;
    std::error_code ec;

    // Read project.godot
    std::string pg_path = project_path + "/project.godot";
    bool has_pg = fs::exists(pg_path, ec);
    int scene_count = 0;
    int script_count = 0;
    int addon_count = 0;
    if (fs::exists(project_path, ec) && fs::is_directory(project_path, ec)) {
        for (auto &e : fs::recursive_directory_iterator(project_path, ec)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            if (ext == ".tscn" || ext == ".scn") scene_count++;
            else if (ext == ".gd" || ext == ".cs") script_count++;
        }
        std::string addons = project_path + "/addons";
        if (fs::exists(addons, ec) && fs::is_directory(addons, ec)) {
            for (auto &e : fs::directory_iterator(addons, ec)) {
                if (e.is_directory()) addon_count++;
            }
        }
    }

    // Try to get the engine version
    std::string engine_version;
    GDExtensionObjectPtr engine_singleton = GodotAPI::get_global_singleton("Engine");
    if (engine_singleton) {
        engine_version = GodotAPI::call_method_string(engine_singleton, "get_version_info");
    }

    json issues = json::array();
    if (!has_pg) issues.push_back("project.godot missing");
    if (scene_count == 0) issues.push_back("No .tscn/.scn scenes found");
    if (script_count == 0) issues.push_back("No .gd/.cs scripts found");

    json result = {
        {"success", true},
        {"project_path", project_path},
        {"has_project_godot", has_pg},
        {"scene_count", scene_count},
        {"script_count", script_count},
        {"addon_count", addon_count},
        {"engine_version", engine_version},
        {"issues", issues}
    };
    r_result = result.dump();
}

} // namespace gear_mcp

void register_import_tools(gear_mcp::ToolRegistry *p_registry) {
    if (!p_registry) return;

    p_registry->register_tool("get_import_status",
        "Return import status (file_type, has_import_file) for a resource",
        gear_mcp::GET_IMPORT_STATUS_SCHEMA, gear_mcp::handle_get_import_status, /*main_thread=*/true);

    p_registry->register_tool("get_import_options",
        "Read a resource's .import file and return its option sections",
        gear_mcp::GET_IMPORT_OPTIONS_SCHEMA, gear_mcp::handle_get_import_options, /*main_thread=*/false);

    p_registry->register_tool("set_import_options",
        "Set or replace keys in a resource's .import [params] section",
        gear_mcp::SET_IMPORT_OPTIONS_SCHEMA, gear_mcp::handle_set_import_options, /*main_thread=*/false);

    p_registry->register_tool("reimport_resource",
        "Mark resource files dirty in EditorFileSystem and trigger a scan",
        gear_mcp::REIMPORT_RESOURCE_SCHEMA, gear_mcp::handle_reimport_resource, /*main_thread=*/true);

    p_registry->register_tool("validate_project",
        "Sanity-check a Godot project: project.godot, scene/script/addon counts, engine version",
        gear_mcp::VALIDATE_PROJECT_SCHEMA, gear_mcp::handle_validate_project, /*main_thread=*/true);

    std::printf("[Gear MCP] Registered import tools: get_import_status, get_import_options, set_import_options, reimport_resource, validate_project\n");
}
