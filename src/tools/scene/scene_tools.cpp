#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include "shared/path_utils.h"

#include <nlohmann/json.hpp>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

using json = nlohmann::json;

namespace gear_mcp {

// ===========================================================================
// create_scene — creates a minimal .tscn file
// ===========================================================================

static void create_scene_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (...) {
        r_error = "Invalid JSON in params";
        return;
    }

    std::string scene_path = params.value("scenePath", "");
    std::string root_type = params.value("rootNodeType", "Node");
    std::string root_name = params.value("rootName", "");

    if (scene_path.empty()) {
        r_error = "Missing required parameter: scenePath";
        return;
    }

    // Default root name to the type name
    if (root_name.empty()) root_name = root_type;

    // Resolve to absolute path
    // For Phase 1, we expect an absolute path or a res:// path (project path needed)
    std::string abs_path = scene_path;
    if (abs_path.substr(0, 6) == "res://") {
        // Need project path — for now, use CWD
        char cwd[4096];
#ifdef _MSC_VER
        if (_getcwd(cwd, sizeof(cwd))) abs_path = std::string(cwd) + "/" + abs_path.substr(6);
#else
        if (getcwd(cwd, sizeof(cwd))) abs_path = std::string(cwd) + "/" + abs_path.substr(6);
#endif
    }

    // Create parent directories
    path_utils::ensure_parent_dirs(abs_path);

    // Write minimal .tscn file
    // Format: Godot scene text format
    std::ofstream file(abs_path, std::ios::trunc);
    if (!file.is_open()) {
        r_error = "Cannot create scene file: " + abs_path;
        return;
    }

    file << "[gd_scene format=3]\n\n";
    file << "[node name=\"" << root_name << "\" type=\"" << root_type << "\"]\n";
    file.close();

    json result = {
        {"scenePath", scene_path},
        {"rootNode", root_name},
        {"rootType", root_type}
    };
    r_result = result.dump();
}

// ===========================================================================
// list_scene_nodes — parses .tscn to extract node hierarchy
// ===========================================================================

static void list_scene_nodes_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (...) {
        r_error = "Invalid JSON in params";
        return;
    }

    std::string scene_path = params.value("scenePath", "");
    if (scene_path.empty()) {
        r_error = "Missing required parameter: scenePath";
        return;
    }

    // Resolve path
    std::string abs_path = scene_path;
    if (abs_path.substr(0, 6) == "res://") {
        char cwd[4096];
#ifdef _MSC_VER
        if (_getcwd(cwd, sizeof(cwd))) abs_path = std::string(cwd) + "/" + abs_path.substr(6);
#else
        if (getcwd(cwd, sizeof(cwd))) abs_path = std::string(cwd) + "/" + abs_path.substr(6);
#endif
    }

    std::ifstream file(abs_path);
    if (!file.is_open()) {
        r_error = "Cannot open scene file: " + abs_path;
        return;
    }

    // Parse [node] sections from .tscn
    // Format: [node name="NodeName" type="TypeName"]
    //         [node name="Child" type="Type" parent="."]
    //         [node name="GrandChild" type="Type" parent="Child"]
    static const std::regex node_regex(R"re(\[node\s+name="([^"]+)"\s+type="([^"]+)"(?:\s+parent="([^"]*)")?.*?\])re");
    std::string line;
    json nodes = json::array();

    while (std::getline(file, line)) {
        try {
            std::smatch match;
            if (std::regex_search(line, match, node_regex)) {
                json node = {
                    {"name", match[1].str()},
                    {"type", match[2].str()},
                    {"parent", match[3].matched ? match[3].str() : ""}
                };
                nodes.push_back(node);
            }
        } catch (const std::regex_error &) {
            // Skip malformed lines
            continue;
        }
    }
    file.close();

    json result = {
        {"scenePath", scene_path},
        {"nodes", nodes},
        {"count", (int)nodes.size()}
    };
    r_result = result.dump();
}

// ===========================================================================
// add_node — adds a node entry
// ===========================================================================
//
// Smart hybrid:
//   1. If the target .tscn is the scene currently being edited in the editor,
//      we add the node to the LIVE scene tree through EditorUndoRedoManager
//      (so Ctrl+Z in the editor undoes it). The file on disk is left alone —
//      the user can save the scene via `save_scene` when they want it persisted.
//   2. Otherwise (no editor / different scene open), we append a [node] entry
//      to the .tscn text file directly. This is the legacy Phase 1 path and
//      is not undoable in the editor's UndoRedo stack.
// ===========================================================================

static void add_node_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (...) {
        r_error = "Invalid JSON in params";
        return;
    }

    std::string scene_path = params.value("scenePath", "");
    std::string parent_path = params.value("parentPath", ".");
    std::string node_name = params.value("nodeName", "");
    std::string node_type = params.value("nodeType", "Node");

    if (scene_path.empty()) {
        r_error = "Missing required parameter: scenePath";
        return;
    }
    if (node_name.empty()) {
        r_error = "Missing required parameter: nodeName";
        return;
    }

    // Resolve to absolute path
    std::string abs_path = scene_path;
    if (abs_path.substr(0, 6) == "res://") {
        char cwd[4096];
#ifdef _MSC_VER
        if (_getcwd(cwd, sizeof(cwd))) abs_path = std::string(cwd) + "/" + abs_path.substr(6);
#else
        if (getcwd(cwd, sizeof(cwd))) abs_path = std::string(cwd) + "/" + abs_path.substr(6);
#endif
    }

    // ---- Path 1: live editor scene + UndoRedo ----
    GDExtensionObjectPtr scene_root = GodotAPI::get_edited_scene_root();
    if (scene_root) {
        // Compare the edited scene's path to what we're being asked to modify.
        // EditorInterface::get_edited_scene_root() returns the root; its
        // scene_file_path property holds the res:// path of the on-disk file.
        std::string edited_path = GodotAPI::get_property_json(scene_root, "scene_file_path");
        // Strip surrounding quotes that come back from the JSON variant
        if (edited_path.size() >= 2 && edited_path.front() == '"' && edited_path.back() == '"') {
            edited_path = edited_path.substr(1, edited_path.size() - 2);
        }

        // Normalize the requested path the same way for comparison
        // If user passed res:// and we resolved to abs, also try the res form
        std::vector<std::string> candidates = { scene_path, abs_path };
        bool matches = false;
        for (const auto &c : candidates) {
            if (c == edited_path) { matches = true; break; }
        }
        // Also accept case where the editor's edited scene path matches the
        // absolute path of the file we were given
        if (!matches && !edited_path.empty()) {
            // Convert edited res:// to absolute for one more comparison
            std::string edited_abs = GodotAPI::res_to_absolute(edited_path);
            if (edited_abs == abs_path) matches = true;
        }

        if (matches) {
            // ---- Live scene + UndoRedo path ----
            GDExtensionObjectPtr new_node = GodotAPI::create_node(node_type);
            if (!new_node) {
                r_error = "Failed to create node of type: " + node_type + " (ClassDB may not exist)";
                return;
            }
            // Set the name via the property setter
            {
                uint8_t name_v[VARIANT_BUF_SIZE] = {0};
                GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)name_v, node_name);
                GodotAPI::set_property(new_node, "name", (GDExtensionConstVariantPtr)name_v);
                GodotAPI::destroy_variant((GDExtensionVariantPtr)name_v);
            }

            // Resolve the parent
            GDExtensionObjectPtr parent = scene_root;
            std::string root_name = GodotAPI::get_node_name(scene_root);
            if (parent_path != "." && parent_path != root_name) {
                GDExtensionObjectPtr resolved = GodotAPI::get_node_child(scene_root, parent_path);
                if (!resolved) {
                    GodotAPI::queue_free(new_node);
                    r_error = "Parent node not found: " + parent_path;
                    return;
                }
                parent = resolved;
            }

            // Open UndoRedo action
            GodotAPI::undo_redo_create_action("Add node " + node_name + " to " + parent_path);

            // do: parent.add_child(node) ; node.set_owner(scene_root)
            {
                uint8_t p_v[VARIANT_BUF_SIZE] = {0}, n_v[VARIANT_BUF_SIZE] = {0};
                GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)p_v, parent);
                GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)n_v, new_node);
                GDExtensionConstVariantPtr a[] = { (GDExtensionConstVariantPtr)p_v, (GDExtensionConstVariantPtr)n_v };
                GodotAPI::undo_redo_add_do_method(parent, "add_child", a, 2);
                GodotAPI::destroy_variant((GDExtensionVariantPtr)p_v);
                GodotAPI::destroy_variant((GDExtensionVariantPtr)n_v);
            }
            {
                uint8_t n_v[VARIANT_BUF_SIZE] = {0}, sr_v[VARIANT_BUF_SIZE] = {0};
                GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)n_v, new_node);
                GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)sr_v, scene_root);
                GDExtensionConstVariantPtr a[] = { (GDExtensionConstVariantPtr)n_v, (GDExtensionConstVariantPtr)sr_v };
                GodotAPI::undo_redo_add_do_method(new_node, "set_owner", a, 2);
                GodotAPI::destroy_variant((GDExtensionVariantPtr)n_v);
                GodotAPI::destroy_variant((GDExtensionVariantPtr)sr_v);
            }

            // undo: parent.remove_child(node)
            {
                uint8_t p_v[VARIANT_BUF_SIZE] = {0}, n_v[VARIANT_BUF_SIZE] = {0};
                GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)p_v, parent);
                GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)n_v, new_node);
                GDExtensionConstVariantPtr a[] = { (GDExtensionConstVariantPtr)p_v, (GDExtensionConstVariantPtr)n_v };
                GodotAPI::undo_redo_add_undo_method(parent, "remove_child", a, 2);
                GodotAPI::destroy_variant((GDExtensionVariantPtr)p_v);
                GodotAPI::destroy_variant((GDExtensionVariantPtr)n_v);
            }

            // Keep the new node alive across the undo boundary
            GodotAPI::undo_redo_add_do_reference(new_node);
            GodotAPI::undo_redo_add_undo_reference(new_node);

            GodotAPI::undo_redo_commit_action();

            // Apply the action for real
            GodotAPI::add_child(parent, new_node);
            GodotAPI::set_owner(new_node, scene_root);

            json result = {
                {"scenePath", scene_path},
                {"nodeName", node_name},
                {"nodeType", node_type},
                {"parentPath", parent_path},
                {"undoable", true},
                {"persisted", false}
            };
            result["hint"] = "Scene is open in the editor; node was added to the live tree only. "
                             "Use save_scene to persist to disk.";
            r_result = result.dump();
            return;
        }
    }

    // ---- Path 2: file I/O (no live editor scene / different scene open) ----

    // Read existing content
    std::ifstream in_file(abs_path);
    if (!in_file.is_open()) {
        r_error = "Cannot open scene file: " + abs_path;
        return;
    }
    std::stringstream ss;
    ss << in_file.rdbuf();
    std::string content = ss.str();
    in_file.close();

    // Append new node
    std::ofstream out_file(abs_path, std::ios::trunc);
    if (!out_file.is_open()) {
        r_error = "Cannot write to scene file: " + abs_path;
        return;
    }

    out_file << content;
    if (!content.empty() && content.back() != '\n') out_file << "\n";

    // Build the node entry
    out_file << "[node name=\"" << node_name << "\" type=\"" << node_type << "\"";
    if (parent_path == ".") {
        out_file << " parent=\".\"";
    } else {
        out_file << " parent=\"" << parent_path << "\"";
    }
    out_file << "]\n";
    out_file.close();

    json result = {
        {"scenePath", scene_path},
        {"nodeName", node_name},
        {"nodeType", node_type},
        {"parentPath", parent_path},
        {"undoable", false},
        {"persisted", true}
    };
    result["hint"] = "Scene is not open in the editor; node was appended to the .tscn file. "
                     "No editor UndoRedo entry was created.";
    r_result = result.dump();
}

} // namespace gear_mcp

// ===========================================================================
// Registration
// ===========================================================================

void register_scene_tools(gear_mcp::ToolRegistry *p_registry) {
    if (!p_registry) return;

    static const char *CREATE_SCENE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "scenePath": {"type": "string", "description": "Path for the new .tscn file"},
        "rootNodeType": {"type": "string", "description": "Root node class name", "default": "Node"},
        "rootName": {"type": "string", "description": "Root node name", "default": ""}
    },
    "required": ["scenePath"]
})schema";

    static const char *LIST_SCENE_NODES_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "scenePath": {"type": "string", "description": "Path to the .tscn file"}
    },
    "required": ["scenePath"]
})schema";

    static const char *ADD_NODE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "scenePath": {"type": "string", "description": "Path to the .tscn file"},
        "parentPath": {"type": "string", "description": "Parent node path in scene (default: root)", "default": "."},
        "nodeName": {"type": "string", "description": "Name for the new node"},
        "nodeType": {"type": "string", "description": "ClassDB type for the new node", "default": "Node"}
    },
    "required": ["scenePath", "nodeName"]
})schema";

    p_registry->register_tool("create_scene",
        "Create a new .tscn scene file with specified root node type",
        CREATE_SCENE_SCHEMA, gear_mcp::create_scene_handler, false);

    p_registry->register_tool("list_scene_nodes",
        "List all nodes in a .tscn scene file by parsing the text format",
        LIST_SCENE_NODES_SCHEMA, gear_mcp::list_scene_nodes_handler, false);

    p_registry->register_tool("add_node",
        "Add a new node entry — uses live editor scene + UndoRedo when the scene is open, otherwise writes the .tscn file directly",
        ADD_NODE_SCHEMA, gear_mcp::add_node_handler, true);

    std::printf("[Gear MCP] Registered scene tools: create_scene, list_scene_nodes, add_node\n");
}
