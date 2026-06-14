#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include "shared/path_utils.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace gear_mcp {

// ---------------------------------------------------------------------------
// inspect_runtime_tree — return a recursive description of the running
// scene's node tree (best-effort: works with the currently edited scene root
// even in editor mode).
// ---------------------------------------------------------------------------

static const char *INSPECT_RUNTIME_TREE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "max_depth": {"type": "integer", "description": "Maximum recursion depth (0 = no recursion)", "default": 3}
    }
})schema";

static json describe_node(GDExtensionObjectPtr p_node, int p_max_depth, int p_current_depth) {
    if (!p_node) return nullptr;

    json obj = json::object();
    obj["name"] = GodotAPI::get_node_name(p_node);
    obj["class"] = GodotAPI::get_node_class(p_node);

    if (p_current_depth < p_max_depth) {
        auto children = GodotAPI::get_children(p_node);
        json arr = json::array();
        for (auto c : children) {
            arr.push_back(describe_node(c, p_max_depth, p_current_depth + 1));
        }
        obj["children"] = arr;
    } else {
        obj["children"] = json::array();
    }
    return obj;
}

static void handle_inspect_runtime_tree(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    int max_depth = params.value("max_depth", 3);

    GDExtensionObjectPtr scene_root = GodotAPI::get_edited_scene_root();
    if (!scene_root) {
        r_error = "No scene is currently being edited (editor mode required)";
        return;
    }

    json tree = describe_node(scene_root, max_depth, 0);

    json result = {
        {"success", true},
        {"root_name", GodotAPI::get_node_name(scene_root)},
        {"tree", tree}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// set_runtime_property — set a property on a node in the current scene
// ---------------------------------------------------------------------------

static const char *SET_RUNTIME_PROPERTY_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "node_path": {"type": "string", "description": "Path to the node (relative to scene root)"},
        "property": {"type": "string", "description": "Property name"},
        "value": {"description": "JSON value to set (string/number/bool/null)"}
    },
    "required": ["node_path", "property", "value"]
})schema";

static void handle_set_runtime_property(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string node_path = params.value("node_path", "");
    std::string prop_name = params.value("property", "");

    if (node_path.empty() || prop_name.empty()) {
        r_error = "Missing required parameters: node_path, property";
        return;
    }
    if (!params.contains("value")) {
        r_error = "Missing required parameter: value";
        return;
    }

    GDExtensionObjectPtr scene_root = GodotAPI::get_edited_scene_root();
    if (!scene_root) { r_error = "No scene is currently being edited"; return; }

    GDExtensionObjectPtr node = GodotAPI::get_node_child(scene_root, node_path);
    if (!node) { r_error = "Node not found at: " + node_path; return; }

    std::string value_json = params["value"].dump();
    bool ok = GodotAPI::set_property_from_json(node, prop_name, value_json);

    json result = {
        {"success", ok},
        {"node_path", node_path},
        {"property", prop_name}
    };
    if (!ok) result["error"] = "Failed to set property (type mismatch or read-only?)";
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// call_runtime_method — call a method on a node with JSON-encoded args
// ---------------------------------------------------------------------------

static const char *CALL_RUNTIME_METHOD_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "node_path": {"type": "string", "description": "Path to the node (relative to scene root)"},
        "method": {"type": "string", "description": "Method name to call"},
        "args": {"type": "array", "description": "Array of JSON arguments", "default": []}
    },
    "required": ["node_path", "method"]
})schema";

static void handle_call_runtime_method(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string node_path = params.value("node_path", "");
    std::string method = params.value("method", "");

    if (node_path.empty() || method.empty()) {
        r_error = "Missing required parameters: node_path, method";
        return;
    }

    GDExtensionObjectPtr scene_root = GodotAPI::get_edited_scene_root();
    if (!scene_root) { r_error = "No scene is currently being edited"; return; }

    GDExtensionObjectPtr node = GodotAPI::get_node_child(scene_root, node_path);
    if (!node) { r_error = "Node not found at: " + node_path; return; }

    // Build argument variants (limited support: string/int/float/bool)
    std::vector<std::vector<uint8_t>> arg_bufs;
    std::vector<GDExtensionConstVariantPtr> arg_ptrs;
    if (params.contains("args") && params["args"].is_array()) {
        for (const auto &a : params["args"]) {
            arg_bufs.emplace_back(VARIANT_BUF_SIZE, 0);
            if (a.is_string()) {
                GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)arg_bufs.back().data(), a.get<std::string>());
            } else if (a.is_boolean()) {
                GodotAPI::make_variant_bool((GDExtensionUninitializedVariantPtr)arg_bufs.back().data(), a.get<bool>());
            } else if (a.is_number_integer()) {
                GodotAPI::make_variant_int((GDExtensionUninitializedVariantPtr)arg_bufs.back().data(), a.get<int64_t>());
            } else if (a.is_number_float()) {
                GodotAPI::make_variant_float((GDExtensionUninitializedVariantPtr)arg_bufs.back().data(), a.get<double>());
            } else {
                GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)arg_bufs.back().data(), a.dump());
            }
            arg_ptrs.push_back((GDExtensionConstVariantPtr)arg_bufs.back().data());
        }
    }

    std::string return_str = GodotAPI::call_method_with_args(node, method.c_str(), arg_ptrs.data(), (int)arg_ptrs.size());

    // Cleanup variants
    for (auto &buf : arg_bufs) {
        GodotAPI::destroy_variant((GDExtensionVariantPtr)buf.data());
    }

    json result = {
        {"success", true},
        {"node_path", node_path},
        {"method", method},
        {"return_value", return_str}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// get_runtime_metrics — return basic performance counters from Engine
// ---------------------------------------------------------------------------

static const char *GET_RUNTIME_METRICS_SCHEMA = R"schema({
    "type": "object",
    "properties": {}
})schema";

static void handle_get_runtime_metrics(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    (void)p_params_json;

    GDExtensionObjectPtr engine = GodotAPI::get_global_singleton("Engine");
    if (!engine) { r_error = "Engine singleton not available"; return; }

    int fps = GodotAPI::call_method_int(engine, "get_frames_per_second");
    int frame = GodotAPI::call_method_int(engine, "get_frames_drawn");
    double physics_ticks = GodotAPI::call_method_float(engine, "get_physics_frames");
    bool is_editor = GodotAPI::call_method_bool(engine, "is_editor_hint");

    json result = {
        {"success", true},
        {"fps", fps},
        {"frames_drawn", frame},
        {"physics_frames", physics_ticks},
        {"is_editor_hint", is_editor}
    };
    r_result = result.dump();
}

} // namespace gear_mcp

void register_runtime_tools(gear_mcp::ToolRegistry *p_registry) {
    if (!p_registry) return;

    p_registry->register_tool("inspect_runtime_tree",
        "Return a JSON description of the currently edited scene tree",
        gear_mcp::INSPECT_RUNTIME_TREE_SCHEMA, gear_mcp::handle_inspect_runtime_tree, /*main_thread=*/true);

    p_registry->register_tool("set_runtime_property",
        "Set a property on a node in the current scene (string/int/float/bool/null values)",
        gear_mcp::SET_RUNTIME_PROPERTY_SCHEMA, gear_mcp::handle_set_runtime_property, /*main_thread=*/true);

    p_registry->register_tool("call_runtime_method",
        "Call a method on a node in the current scene with JSON arguments",
        gear_mcp::CALL_RUNTIME_METHOD_SCHEMA, gear_mcp::handle_call_runtime_method, /*main_thread=*/true);

    p_registry->register_tool("get_runtime_metrics",
        "Return basic performance metrics (FPS, frames drawn, physics frames)",
        gear_mcp::GET_RUNTIME_METRICS_SCHEMA, gear_mcp::handle_get_runtime_metrics, /*main_thread=*/true);

    std::printf("[Gear MCP] Registered runtime tools: inspect_runtime_tree, set_runtime_property, call_runtime_method, get_runtime_metrics\n");
}
