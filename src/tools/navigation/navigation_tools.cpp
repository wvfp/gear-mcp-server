#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include "shared/path_utils.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using json = nlohmann::json;

namespace gear_mcp {

// ---------------------------------------------------------------------------
// create_navigation_region — adds a NavigationRegion2D or NavigationRegion3D
// ---------------------------------------------------------------------------

static const char *CREATE_NAVIGATION_REGION_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "scene_path": {"type": "string", "description": "res:// path of the scene"},
        "parent_path": {"type": "string", "description": "Parent node path", "default": "."},
        "node_name": {"type": "string", "description": "Name for the new region", "default": "NavigationRegion"},
        "is_3d": {"type": "boolean", "description": "Use NavigationRegion3D instead of 2D", "default": false}
    },
    "required": ["scene_path"]
})schema";

static void handle_create_navigation_region(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string scene_path = params.value("scene_path", "");
    std::string parent_path = params.value("parent_path", ".");
    std::string node_name = params.value("node_name", "NavigationRegion");
    bool is_3d = params.value("is_3d", false);

    if (scene_path.empty()) { r_error = "Missing required parameter: scene_path"; return; }

    GDExtensionObjectPtr scene_root = GodotAPI::get_edited_scene_root();
    if (scene_root) {
        std::string edited = GodotAPI::get_property_json(scene_root, "scene_file_path");
        if (edited.size() >= 2 && edited.front() == '"' && edited.back() == '"') {
            edited = edited.substr(1, edited.size() - 2);
        }
        if (edited == scene_path) {
            std::string class_name = is_3d ? "NavigationRegion3D" : "NavigationRegion2D";
            GDExtensionObjectPtr nav = GodotAPI::create_node(class_name);
            if (!nav) { r_error = "Failed to create " + class_name; return; }

            // Resolve parent
            GDExtensionObjectPtr parent = scene_root;
            std::string root_name = GodotAPI::get_node_name(scene_root);
            if (parent_path != "." && parent_path != root_name) {
                GDExtensionObjectPtr resolved = GodotAPI::get_node_child(scene_root, parent_path);
                if (!resolved) {
                    GodotAPI::queue_free(nav);
                    r_error = "Parent node not found: " + parent_path;
                    return;
                }
                parent = resolved;
            }

            {
                uint8_t name_v[64] = {0};
                GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)name_v, node_name);
                GodotAPI::set_property(nav, "name", (GDExtensionConstVariantPtr)name_v);
                GodotAPI::destroy_variant((GDExtensionVariantPtr)name_v);
            }

            GodotAPI::add_child(parent, nav);
            GodotAPI::set_owner(nav, scene_root);

            json result = {
                {"success", true},
                {"node_name", node_name},
                {"is_3d", is_3d},
                {"persisted", false}
            };
            r_result = result.dump();
            return;
        }
    }

    // Fallback: file I/O (only safe if scene file already has a [gd_scene] header)
    std::string abs_path = GodotAPI::res_to_absolute(scene_path);
    std::ifstream check(abs_path);
    bool has_header = false;
    if (check.is_open()) {
        std::string first_line;
        std::getline(check, first_line);
        has_header = (first_line.find("[gd_scene") != std::string::npos);
        check.close();
    }
    if (!has_header) {
        r_error = "Cannot append to scene file: missing [gd_scene] header. Create the scene first via create_scene().";
        return;
    }
    std::ofstream file(abs_path, std::ios::app);
    if (!file.is_open()) { r_error = "Cannot open scene file: " + abs_path; return; }

    std::string class_name = is_3d ? "NavigationRegion3D" : "NavigationRegion2D";
    file << "[node name=\"" << node_name << "\" type=\"" << class_name << "\"";
    if (parent_path == ".") file << " parent=\".\"";
    else file << " parent=\"" << parent_path << "\"";
    file << "]\n";
    file.close();

    json result = {
        {"success", true},
        {"node_name", node_name},
        {"is_3d", is_3d},
        {"persisted", true}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// create_navigation_agent — adds a NavigationAgent2D or 3D
// ---------------------------------------------------------------------------

static const char *CREATE_NAVIGATION_AGENT_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "scene_path": {"type": "string", "description": "res:// path of the scene"},
        "parent_path": {"type": "string", "description": "Parent node path", "default": "."},
        "node_name": {"type": "string", "description": "Name for the new agent", "default": "NavigationAgent"},
        "is_3d": {"type": "boolean", "description": "Use NavigationAgent3D instead of 2D", "default": false},
        "path_desired_distance": {"type": "number", "description": "Distance at which the agent considers the path reached", "default": 1.0},
        "target_desired_distance": {"type": "number", "description": "Distance at which the agent considers the target reached", "default": 1.0}
    },
    "required": ["scene_path"]
})schema";

static void handle_create_navigation_agent(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string scene_path = params.value("scene_path", "");
    std::string parent_path = params.value("parent_path", ".");
    std::string node_name = params.value("node_name", "NavigationAgent");
    bool is_3d = params.value("is_3d", false);
    double path_desired = params.value("path_desired_distance", 1.0);
    double target_desired = params.value("target_desired_distance", 1.0);

    if (scene_path.empty()) { r_error = "Missing required parameter: scene_path"; return; }

    GDExtensionObjectPtr scene_root = GodotAPI::get_edited_scene_root();
    if (scene_root) {
        std::string edited = GodotAPI::get_property_json(scene_root, "scene_file_path");
        if (edited.size() >= 2 && edited.front() == '"' && edited.back() == '"') {
            edited = edited.substr(1, edited.size() - 2);
        }
        if (edited == scene_path) {
            std::string class_name = is_3d ? "NavigationAgent3D" : "NavigationAgent2D";
            GDExtensionObjectPtr agent = GodotAPI::create_node(class_name);
            if (!agent) { r_error = "Failed to create " + class_name; return; }

            GDExtensionObjectPtr parent = scene_root;
            std::string root_name = GodotAPI::get_node_name(scene_root);
            if (parent_path != "." && parent_path != root_name) {
                GDExtensionObjectPtr resolved = GodotAPI::get_node_child(scene_root, parent_path);
                if (!resolved) {
                    GodotAPI::queue_free(agent);
                    r_error = "Parent node not found: " + parent_path;
                    return;
                }
                parent = resolved;
            }

            {
                uint8_t name_v[64] = {0};
                GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)name_v, node_name);
                GodotAPI::set_property(agent, "name", (GDExtensionConstVariantPtr)name_v);
                GodotAPI::destroy_variant((GDExtensionVariantPtr)name_v);
            }

            // Set distances
            {
                uint8_t vb[64] = {0};
                GodotAPI::make_variant_float((GDExtensionUninitializedVariantPtr)vb, path_desired);
                GodotAPI::set_property(agent, "path_desired_distance", (GDExtensionConstVariantPtr)vb);
                GodotAPI::destroy_variant((GDExtensionVariantPtr)vb);
            }
            {
                uint8_t vb[64] = {0};
                GodotAPI::make_variant_float((GDExtensionUninitializedVariantPtr)vb, target_desired);
                GodotAPI::set_property(agent, "target_desired_distance", (GDExtensionConstVariantPtr)vb);
                GodotAPI::destroy_variant((GDExtensionVariantPtr)vb);
            }

            GodotAPI::add_child(parent, agent);
            GodotAPI::set_owner(agent, scene_root);

            json result = {
                {"success", true},
                {"node_name", node_name},
                {"is_3d", is_3d},
                {"persisted", false}
            };
            r_result = result.dump();
            return;
        }
    }

    // Fallback: file I/O (only safe if scene file already has a [gd_scene] header)
    std::string abs_path = GodotAPI::res_to_absolute(scene_path);
    std::ifstream check(abs_path);
    bool has_header = false;
    if (check.is_open()) {
        std::string first_line;
        std::getline(check, first_line);
        has_header = (first_line.find("[gd_scene") != std::string::npos);
        check.close();
    }
    if (!has_header) {
        r_error = "Cannot append to scene file: missing [gd_scene] header. Create the scene first via create_scene().";
        return;
    }
    std::ofstream file(abs_path, std::ios::app);
    if (!file.is_open()) { r_error = "Cannot open scene file: " + abs_path; return; }

    std::string class_name = is_3d ? "NavigationAgent3D" : "NavigationAgent2D";
    file << "[node name=\"" << node_name << "\" type=\"" << class_name << "\"";
    if (parent_path == ".") file << " parent=\".\"";
    else file << " parent=\"" << parent_path << "\"";
    file << "]\n";
    file << "path_desired_distance = " << path_desired << "\n";
    file << "target_desired_distance = " << target_desired << "\n";
    file.close();

    json result = {
        {"success", true},
        {"node_name", node_name},
        {"is_3d", is_3d},
        {"persisted", true}
    };
    r_result = result.dump();
}

} // namespace gear_mcp

void register_navigation_tools(gear_mcp::ToolRegistry *p_registry) {
    if (!p_registry) return;

    p_registry->register_tool("create_navigation_region",
        "Add a NavigationRegion2D/3D to a scene (live editor or file I/O)",
        gear_mcp::CREATE_NAVIGATION_REGION_SCHEMA, gear_mcp::handle_create_navigation_region, /*main_thread=*/true);

    p_registry->register_tool("create_navigation_agent",
        "Add a NavigationAgent2D/3D to a scene (live editor or file I/O)",
        gear_mcp::CREATE_NAVIGATION_AGENT_SCHEMA, gear_mcp::handle_create_navigation_agent, /*main_thread=*/true);

    std::printf("[Gear MCP] Registered navigation tools: create_navigation_region, create_navigation_agent\n");
}
