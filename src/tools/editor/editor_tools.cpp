#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace gear_mcp {

// ---------------------------------------------------------------------------
// Handler: run_project
// ---------------------------------------------------------------------------
static void handle_run_project(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) {
        r_error = "Invalid JSON parameters.";
        return;
    }

    GDExtensionObjectPtr editor = GodotAPI::get_editor_interface();
    if (!editor) {
        r_error = "Editor interface not available.";
        return;
    }

    std::string scene;
    if (params.contains("scene") && params["scene"].is_string()) {
        scene = params["scene"].get<std::string>();
    }

    if (!scene.empty()) {
        uint8_t var_buf[64] = {0};
        GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)var_buf, scene);
        GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)var_buf };
        GodotAPI::call_method_void(editor, "play_custom_scene", args, 1);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)var_buf);
        json res;
        res["status"] = "running";
        res["scene"] = scene;
        r_result = res.dump();
    } else {
        GodotAPI::call_method_void(editor, "play_main_scene", nullptr, 0);
        json res;
        res["status"] = "running";
        res["scene"] = "main";
        r_result = res.dump();
    }
}

// ---------------------------------------------------------------------------
// Handler: stop_project
// ---------------------------------------------------------------------------
static void handle_stop_project(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    GDExtensionObjectPtr editor = GodotAPI::get_editor_interface();
    if (!editor) {
        r_error = "Editor interface not available.";
        return;
    }

    GodotAPI::call_method_void(editor, "stop_playing_scene", nullptr, 0);

    json res;
    res["status"] = "stopped";
    r_result = res.dump();
}

// ---------------------------------------------------------------------------
// Handler: get_debug_output
// ---------------------------------------------------------------------------
static void handle_get_debug_output(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) {
        r_error = "Invalid JSON parameters.";
        return;
    }

    int max_lines = 100;
    if (params.contains("max_lines") && params["max_lines"].is_number_integer()) {
        max_lines = params["max_lines"].get<int>();
    }

    GDExtensionObjectPtr editor = GodotAPI::get_editor_interface();
    if (!editor) {
        r_error = "Editor interface not available.";
        return;
    }

    std::string log_output = GodotAPI::call_method_string(editor, "get_editor_log");

    if (log_output.empty()) {
        json res;
        res["output"] = "";
        res["message"] = "Editor log not available or empty.";
        r_result = res.dump();
        return;
    }

    // Split into lines and apply max_lines limit.
    std::vector<std::string> lines;
    std::string::size_type start = 0;
    std::string::size_type pos = 0;
    while ((pos = log_output.find('\n', start)) != std::string::npos) {
        lines.push_back(log_output.substr(start, pos - start));
        start = pos + 1;
    }
    if (start < log_output.size()) {
        lines.push_back(log_output.substr(start));
    }

    int total_lines = (int)lines.size();
    int begin_idx = total_lines > max_lines ? total_lines - max_lines : 0;

    std::string trimmed;
    for (int i = begin_idx; i < total_lines; ++i) {
        if (!trimmed.empty()) {
            trimmed += "\n";
        }
        trimmed += lines[i];
    }

    json res;
    res["output"] = trimmed;
    res["total_lines"] = total_lines;
    res["returned_lines"] = total_lines - begin_idx;
    r_result = res.dump();
}

// ---------------------------------------------------------------------------
// Handler: get_editor_status
// ---------------------------------------------------------------------------
static void handle_get_editor_status(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    GDExtensionObjectPtr editor = GodotAPI::get_editor_interface();
    if (!editor) {
        r_error = "Editor interface not available.";
        return;
    }

    json res;

    // Scene open
    GDExtensionObjectPtr scene_root = GodotAPI::get_edited_scene_root();
    if (scene_root) {
        std::string scene_name = GodotAPI::call_method_string(scene_root, "get_scene_file_path");
        if (scene_name.empty()) {
            scene_name = GodotAPI::call_method_string(scene_root, "get_name");
        }
        res["scene_open"] = scene_name.empty() ? json(nullptr) : json(scene_name);
    } else {
        res["scene_open"] = false;
    }

    // Playing state
    std::string playing_str = GodotAPI::call_method_string(editor, "is_playing_scene");
    res["is_playing"] = (playing_str == "true" || playing_str == "1");

    // Selected nodes
    std::vector<GDExtensionObjectPtr> selected = GodotAPI::get_selected_nodes();
    json selected_arr = json::array();
    for (GDExtensionObjectPtr node : selected) {
        std::string node_name = GodotAPI::call_method_string(node, "get_name");
        if (!node_name.empty()) {
            selected_arr.push_back(node_name);
        }
    }
    res["selected_nodes"] = selected_arr;

    // Project path
    res["project_path"] = GodotAPI::get_project_path();

    r_result = res.dump();
}

// ---------------------------------------------------------------------------
// Handler: get_godot_version
// ---------------------------------------------------------------------------
static void handle_get_godot_version(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    std::string version = GodotAPI::get_godot_version();

    if (version.empty()) {
        r_error = "Unable to retrieve Godot version.";
        return;
    }

    json res;
    res["version"] = version;
    r_result = res.dump();
}

// ---------------------------------------------------------------------------
// Handler: launch_editor
// ---------------------------------------------------------------------------
static void handle_launch_editor(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) {
        r_error = "Invalid JSON parameters.";
        return;
    }

    if (!params.contains("scene_path") || !params["scene_path"].is_string()) {
        r_error = "Missing required parameter: scene_path";
        return;
    }

    std::string scene_path = params["scene_path"].get<std::string>();

    GDExtensionObjectPtr loaded = GodotAPI::load_scene(scene_path);
    if (!loaded) {
        r_error = "Failed to load scene: " + scene_path;
        return;
    }

    json res;
    res["status"] = "loaded";
    res["scene_path"] = scene_path;
    r_result = res.dump();
}

} // namespace gear_mcp

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
void register_editor_tools(gear_mcp::ToolRegistry *p_registry) {

    // run_project
    p_registry->register_tool("run_project",
        "Run the current Godot project or a specific scene.",
        R"({"type":"object","properties":{"scene":{"type":"string","description":"Optional specific scene to run"}}})",
        gear_mcp::handle_run_project, /*main_thread=*/true);

    // stop_project
    p_registry->register_tool("stop_project",
        "Stop the currently running project.",
        R"({"type":"object","properties":{}})",
        gear_mcp::handle_stop_project, /*main_thread=*/true);

    // get_debug_output
    p_registry->register_tool("get_debug_output",
        "Get the editor output/debug log.",
        R"({"type":"object","properties":{"max_lines":{"type":"integer","description":"Max lines to return","default":100}}})",
        gear_mcp::handle_get_debug_output, /*main_thread=*/true);

    // get_editor_status
    p_registry->register_tool("get_editor_status",
        "Get current editor status including open scene, running state, and selected nodes.",
        R"({"type":"object","properties":{}})",
        gear_mcp::handle_get_editor_status, /*main_thread=*/true);

    // get_godot_version
    p_registry->register_tool("get_godot_version",
        "Get the Godot engine version information.",
        R"({"type":"object","properties":{}})",
        gear_mcp::handle_get_godot_version, /*main_thread=*/true);

    // launch_editor
    p_registry->register_tool("launch_editor",
        "Open a specific scene in the Godot editor.",
        R"({"type":"object","properties":{"scene_path":{"type":"string","description":"res:// path to scene file"}},"required":["scene_path"]})",
        gear_mcp::handle_launch_editor, /*main_thread=*/true);
}
