#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include "shared/path_utils.h"
#include "shared/config_parser.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using json = nlohmann::json;

namespace gear_mcp {

// ---------------------------------------------------------------------------
// Helper: parse plugin.cfg-style [plugin] section to extract name/version
// ---------------------------------------------------------------------------

struct PluginInfo {
    std::string name;
    std::string path;     // absolute filesystem path of plugin.cfg
    std::string script;   // "script = ..."
    bool enabled = false; // whether the project has it enabled
};

static std::string get_project_path_or_die(const std::string &p_input, std::string &r_error) {
    std::string pp = p_input;
    if (pp.empty()) pp = GodotAPI::get_project_path();
    if (pp.empty()) { r_error = "No project_path provided and no active project detected."; return {}; }
    return pp;
}

// ---------------------------------------------------------------------------
// list_plugins — list addons in the project's addons/ directory
// ---------------------------------------------------------------------------

static const char *LIST_PLUGINS_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "project_path": {"type": "string", "description": "Godot project path"}
    }
})schema";

static void handle_list_plugins(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string project_path = params.value("project_path", "");
    if (project_path.empty()) project_path = GodotAPI::get_project_path();
    if (project_path.empty()) { r_error = "No project_path provided and no active project detected."; return; }

    std::string addons_path = project_path + "/addons";
    json entries = config_parser::parse_ini_file(addons_path + "/../project.godot");

    // Find enabled_plugins from project.godot [editor_plugins]
    json enabled_map = json::object();
    if (entries.contains("editor_plugins") && entries["editor_plugins"].is_object()) {
        enabled_map = entries["editor_plugins"];
    }

    // List subdirectories of addons/
    json plugins = json::array();
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(addons_path, ec) && fs::is_directory(addons_path, ec)) {
        for (const auto &entry : fs::directory_iterator(addons_path, ec)) {
            if (!entry.is_directory()) continue;
            std::string plugin_dir = entry.path().string();
            std::string plugin_cfg = plugin_dir + "/plugin.cfg";
            if (!fs::exists(plugin_cfg)) continue;

            json cfg = config_parser::parse_ini_file(plugin_cfg);
            std::string pname;
            if (cfg.contains("plugin") && cfg["plugin"].is_object()) {
                pname = cfg["plugin"].value("name", entry.path().filename().string());
            } else {
                pname = entry.path().filename().string();
            }

            // Check if enabled (EditorPlugins enabled=)
            bool enabled = false;
            if (enabled_map.contains("enabled") && enabled_map["enabled"].is_object()) {
                if (enabled_map["enabled"].contains("PackedStringArray")) {
                    // Enabled list is stored under enabled/PackedStringArray/...
                    // We just use the key "plugin/" + pname
                    enabled = enabled_map["enabled"].contains("PackedStringArray/" + pname);
                }
            }

            // Simpler: use keys like "plugin/MyPlugin" (the standard Godot form)
            enabled = enabled || enabled_map.contains("plugin/" + pname);

            plugins.push_back({
                {"name", pname},
                {"path", plugin_dir},
                {"enabled", enabled}
            });
        }
    }

    json result = {
        {"success", true},
        {"project_path", project_path},
        {"plugins", plugins}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// enable_plugin / disable_plugin — toggle a plugin in project.godot
// ---------------------------------------------------------------------------

static const char *TOGGLE_PLUGIN_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "project_path": {"type": "string", "description": "Godot project path"},
        "plugin_name": {"type": "string", "description": "Plugin name (folder name or plugin.cfg [plugin] name)"}
    },
    "required": ["plugin_name"]
})schema";

static bool set_plugin_enabled(const std::string &p_project_path, const std::string &p_plugin, bool p_enabled) {
    namespace fs = std::filesystem;
    std::string cfg_path = p_project_path + "/addons/" + p_plugin + "/plugin.cfg";
    if (!fs::exists(cfg_path)) {
        // Try matching by [plugin] name
        std::string addons_path = p_project_path + "/addons";
        std::error_code ec;
        for (const auto &entry : fs::directory_iterator(addons_path, ec)) {
            if (!entry.is_directory()) continue;
            std::string try_cfg = entry.path().string() + "/plugin.cfg";
            if (!fs::exists(try_cfg)) continue;
            json cfg = config_parser::parse_ini_file(try_cfg);
            if (cfg.contains("plugin") && cfg["plugin"].is_object()) {
                if (cfg["plugin"].value("name", "") == p_plugin) {
                    cfg_path = try_cfg;
                    break;
                }
            }
        }
    }
    if (!fs::exists(cfg_path)) return false;

    // Read project.godot
    std::string pg_path = p_project_path + "/project.godot";
    std::ifstream in_file(pg_path);
    if (!in_file.is_open()) return false;
    std::stringstream ss; ss << in_file.rdbuf();
    std::string content = ss.str();
    in_file.close();

    // Find [editor_plugins] section
    const std::string section = "[editor_plugins]";
    size_t section_pos = content.find(section);
    std::string key = "plugin/" + p_plugin + "=";
    std::string new_line = "plugin/" + p_plugin + "=" + (p_enabled ? "true" : "false");

    if (section_pos == std::string::npos) {
        // Add the section
        content += "\n[editor_plugins]\n";
        content += new_line + "\n";
    } else {
        // Find end of [editor_plugins] section
        size_t end_pos = content.find("\n[", section_pos + 1);
        if (end_pos == std::string::npos) end_pos = content.size();

        // Search for the key in the section
        size_t key_pos = content.find(key, section_pos);
        if (key_pos != std::string::npos && key_pos < end_pos) {
            size_t eol = content.find('\n', key_pos);
            if (eol == std::string::npos) eol = content.size();
            content.replace(key_pos, eol - key_pos, new_line);
        } else {
            // Insert at end of section
            content.insert(end_pos, new_line + "\n");
        }
    }

    std::ofstream out_file(pg_path, std::ios::trunc);
    if (!out_file.is_open()) return false;
    out_file << content;
    out_file.close();
    return true;
}

static void handle_enable_plugin(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string project_path;
    {
        std::string err;
        project_path = get_project_path_or_die(params.value("project_path", ""), err);
        if (project_path.empty()) { r_error = err; return; }
    }
    std::string plugin_name = params.value("plugin_name", "");
    if (plugin_name.empty()) { r_error = "Missing required parameter: plugin_name"; return; }

    bool ok = set_plugin_enabled(project_path, plugin_name, true);

    json result = {
        {"success", ok},
        {"plugin_name", plugin_name},
        {"enabled", ok}
    };
    if (!ok) result["error"] = "Failed to enable plugin. Verify the plugin is installed in addons/.";
    r_result = result.dump();
}

static void handle_disable_plugin(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string project_path;
    {
        std::string err;
        project_path = get_project_path_or_die(params.value("project_path", ""), err);
        if (project_path.empty()) { r_error = err; return; }
    }
    std::string plugin_name = params.value("plugin_name", "");
    if (plugin_name.empty()) { r_error = "Missing required parameter: plugin_name"; return; }

    bool ok = set_plugin_enabled(project_path, plugin_name, false);

    json result = {
        {"success", ok},
        {"plugin_name", plugin_name},
        {"enabled", false}
    };
    if (!ok) result["error"] = "Failed to disable plugin.";
    r_result = result.dump();
}

} // namespace gear_mcp

void register_plugin_tools(gear_mcp::ToolRegistry *p_registry) {
    if (!p_registry) return;

    p_registry->register_tool("list_plugins",
        "List all addons/plugins in the project's addons/ folder with enabled state",
        gear_mcp::LIST_PLUGINS_SCHEMA, gear_mcp::handle_list_plugins, /*main_thread=*/false);

    p_registry->register_tool("enable_plugin",
        "Enable a plugin in project.godot (plugin/<name>=true)",
        gear_mcp::TOGGLE_PLUGIN_SCHEMA, gear_mcp::handle_enable_plugin, /*main_thread=*/false);

    p_registry->register_tool("disable_plugin",
        "Disable a plugin in project.godot (plugin/<name>=false)",
        gear_mcp::TOGGLE_PLUGIN_SCHEMA, gear_mcp::handle_disable_plugin, /*main_thread=*/false);

    std::printf("[Gear MCP] Registered plugin tools: list_plugins, enable_plugin, disable_plugin\n");
}
