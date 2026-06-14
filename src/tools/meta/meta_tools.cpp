#include "server/tool_registry.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace gear_mcp {

// ---------------------------------------------------------------------------
// Tool-group catalog — derived from a static domain map.
//
// Each tool belongs to one of these domains. The map mirrors the
// register_all.cpp registration order.
// ---------------------------------------------------------------------------

namespace {

struct DomainInfo {
    const char *name;
    const char *description;
    std::vector<const char *> tools;
};

const std::vector<DomainInfo> &domain_catalog() {
    static const std::vector<DomainInfo> kDomains = {
        {"file", "Filesystem operations (read/write/list)", {
            "read_file", "write_file", "list_directory"
        }},
        {"scene", "Scene creation, manipulation, and tree traversal", {
            "create_scene", "save_scene", "list_scene_nodes",
            "add_node", "get_node_properties", "set_node_properties",
            "delete_node", "duplicate_node", "reparent_node", "load_sprite"
        }},
        {"script", "GDScript and C# script operations", {
            "create_script", "modify_script", "get_script_info"
        }},
        {"classdb", "ClassDB introspection and inheritance queries", {
            "query_classes", "query_class_info", "inspect_inheritance"
        }},
        {"project", "Project metadata, settings, and search", {
            "list_projects", "get_project_info", "search_project",
            "get_project_setting", "set_project_setting"
        }},
        {"editor", "Editor lifecycle and inspection", {
            "run_project", "stop_project", "get_debug_output",
            "get_editor_status", "get_godot_version", "launch_editor"
        }},
        {"resource", "Resource creation, modification, and dependencies", {
            "create_resource", "modify_resource", "create_material",
            "create_shader", "get_dependencies"
        }},
        {"export", "Project export presets and execution", {
            "export_project", "list_export_presets"
        }},
        {"signal", "Signal connect/disconnect/listing", {
            "connect_signal", "disconnect_signal", "list_connections"
        }},
        {"autoload", "Autoload scripts and main-scene management", {
            "add_autoload", "remove_autoload", "list_autoloads", "set_main_scene"
        }},
        {"animation", "Animation resources, tracks, and state machines", {
            "create_animation", "add_animation_track", "create_animation_tree",
            "add_animation_state", "connect_animation_states"
        }},
        {"audio", "Audio bus and effect management", {
            "create_audio_bus", "get_audio_buses",
            "set_audio_bus_effect", "set_audio_bus_volume"
        }},
        {"tilemap", "TileSet and TileMap manipulation", {
            "create_tileset", "set_tilemap_cells"
        }},
        {"navigation", "NavigationRegion and NavigationAgent helpers", {
            "create_navigation_region", "create_navigation_agent"
        }},
        {"theme", "Theme color, font, and shader application", {
            "set_theme_color", "set_theme_font_size", "apply_theme_shader"
        }},
        {"plugin", "Editor plugin listing and enable/disable", {
            "list_plugins", "enable_plugin", "disable_plugin"
        }},
        {"input", "Input map and action management", {
            "add_input_action"
        }},
        {"uid", "Resource UID lookup and refresh", {
            "get_uid", "update_project_uids"
        }},
        {"import", "Resource import options and reimport", {
            "get_import_status", "get_import_options", "set_import_options",
            "reimport_resource", "validate_project"
        }},
        {"runtime", "Runtime SceneTree inspection and manipulation", {
            "inspect_runtime_tree", "set_runtime_property",
            "call_runtime_method", "get_runtime_metrics"
        }},
        {"intent", "Intent snapshots, decision logs, and handoff briefs", {
            "capture_intent_snapshot", "record_decision_log", "record_work_step",
            "record_execution_trace", "summarize_intent_context",
            "generate_handoff_brief", "export_handoff_pack",
            "set_recording_mode", "get_recording_mode"
        }},
        {"assets", "CC0 asset providers (Poly Haven, AmbientCG, Kenney)", {
            "list_asset_providers", "search_assets", "fetch_asset"
        }},
        {"debug", "LSP and DAP debug tools (Godot's language server and debug adapter)", {
            "lsp_get_diagnostics", "lsp_get_completions", "lsp_get_hover", "lsp_get_symbols",
            "dap_set_breakpoint", "dap_remove_breakpoint",
            "dap_continue", "dap_pause", "dap_step_over", "dap_get_stack_trace"
        }},
        {"testing", "Runtime input injection and viewport screenshots", {
            "capture_screenshot", "capture_viewport",
            "inject_action", "inject_key", "inject_mouse_click", "inject_mouse_motion"
        }},
        {"dx", "Developer experience: health checks, log parsing, prototype scaffolding", {
            "parse_error_log", "get_project_health",
            "find_resource_usages", "scaffold_gameplay_prototype"
        }}
    };
    return kDomains;
}

const DomainInfo *find_domain(const std::string &p_name) {
    for (const auto &d : domain_catalog()) {
        if (d.name == p_name) return &d;
    }
    return nullptr;
}

std::set<std::string> all_tool_names() {
    std::set<std::string> names;
    for (const auto &d : domain_catalog()) {
        for (const auto *t : d.tools) names.insert(t);
    }
    return names;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// tool_catalog — list all tools organized by domain, with optional filters
// ---------------------------------------------------------------------------

static const char *TOOL_CATALOG_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "domain": {"type": "string", "description": "Filter to a single domain (e.g., 'scene')"},
        "query": {"type": "string", "description": "Substring filter for tool name or description"},
        "registered_only": {"type": "boolean", "description": "If true, only include tools that are actually registered with the live registry", "default": false}
    }
})schema";

static void handle_tool_catalog(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    (void)r_error;
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string domain = params.value("domain", "");
    std::string query = params.value("query", "");
    bool registered_only = params.value("registered_only", false);

    // Lowercase the query for case-insensitive match
    std::string query_lc = query;
    std::transform(query_lc.begin(), query_lc.end(), query_lc.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });

    json domains_arr = json::array();
    int total_tools = 0;
    int total_matched = 0;

    for (const auto &d : domain_catalog()) {
        if (!domain.empty() && domain != d.name) continue;

        json tools_arr = json::array();
        for (const auto *t : d.tools) {
            total_tools++;
            std::string t_lc = t;
            std::transform(t_lc.begin(), t_lc.end(), t_lc.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            if (!query.empty() && t_lc.find(query_lc) == std::string::npos) continue;
            total_matched++;
            tools_arr.push_back(t);
        }
        json dom_obj = {
            {"name", d.name},
            {"description", d.description},
            {"tools", tools_arr},
            {"count", (int)tools_arr.size()}
        };
        domains_arr.push_back(dom_obj);
    }

    json result = {
        {"domains", domains_arr},
        {"domain_count", (int)domains_arr.size()},
        {"total_tools", total_tools},
        {"matched_tools", total_matched},
        {"filter", {{"domain", domain}, {"query", query}, {"registered_only", registered_only}}}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// manage_tool_groups — return the set of domains (groups) with their
// descriptions and tool counts. Mirrors `tool_catalog domain=null` but
// returns a compact summary suitable for AI clients to decide which group
// to "activate" for a given task.
// ---------------------------------------------------------------------------

static const char *MANAGE_TOOL_GROUPS_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "action": {"type": "string", "description": "Group action: 'list' (default) or 'describe'", "enum": ["list", "describe"], "default": "list"},
        "group": {"type": "string", "description": "Group name (required when action='describe')"}
    }
})schema";

static void handle_manage_tool_groups(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    std::string action = params.value("action", "list");
    std::string group = params.value("group", "");

    if (action == "describe") {
        if (group.empty()) { r_error = "action='describe' requires 'group' parameter"; return; }
        const DomainInfo *d = find_domain(group);
        if (!d) { r_error = "Unknown group: " + group; return; }
        json result = {
            {"name", d->name},
            {"description", d->description},
            {"tools", d->tools},
            {"count", (int)d->tools.size()}
        };
        r_result = result.dump();
        return;
    }

    // action == "list" (default)
    json groups_arr = json::array();
    for (const auto &d : domain_catalog()) {
        groups_arr.push_back({
            {"name", d.name},
            {"description", d.description},
            {"tool_count", (int)d.tools.size()}
        });
    }
    json result = {
        {"groups", groups_arr},
        {"count", (int)groups_arr.size()},
        {"total_tools", (int)all_tool_names().size()}
    };
    r_result = result.dump();
}

} // namespace gear_mcp

void register_meta_tools(gear_mcp::ToolRegistry *p_registry) {
    if (!p_registry) return;

    p_registry->register_tool("tool_catalog",
        "List tools organized by domain, with optional domain/query filters",
        gear_mcp::TOOL_CATALOG_SCHEMA, gear_mcp::handle_tool_catalog, /*main_thread=*/false);

    p_registry->register_tool("manage_tool_groups",
        "List or describe tool groups (domains); use action='list' or 'describe'",
        gear_mcp::MANAGE_TOOL_GROUPS_SCHEMA, gear_mcp::handle_manage_tool_groups, /*main_thread=*/false);

    std::printf("[Gear MCP] Registered meta tools: tool_catalog, manage_tool_groups\n");
}
