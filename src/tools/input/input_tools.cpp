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
// add_input_action — add an input action to project.godot
// ---------------------------------------------------------------------------

static const char *ADD_INPUT_ACTION_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "project_path": {"type": "string", "description": "Godot project path"},
        "action_name": {"type": "string", "description": "Input action name (e.g., 'jump')"},
        "keycode": {"type": "string", "description": "Default keycode/event identifier (e.g., 'KEY_SPACE')"},
        "deadzone": {"type": "number", "description": "Deadzone for analog inputs", "default": 0.5}
    },
    "required": ["action_name"]
})schema";

static void handle_add_input_action(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string project_path = params.value("project_path", "");
    if (project_path.empty()) project_path = GodotAPI::get_project_path();
    if (project_path.empty()) { r_error = "No project_path provided and no active project detected."; return; }

    std::string action_name = params.value("action_name", "");
    std::string keycode = params.value("keycode", "");
    double deadzone = params.value("deadzone", 0.5);

    if (action_name.empty()) { r_error = "Missing required parameter: action_name"; return; }

    namespace fs = std::filesystem;
    std::string pg_path = project_path + "/project.godot";
    std::ifstream in_file(pg_path);
    if (!in_file.is_open()) { r_error = "Cannot open project.godot at: " + pg_path; return; }
    std::stringstream ss; ss << in_file.rdbuf();
    std::string content = ss.str();
    in_file.close();

    // Build new entries
    std::ostringstream addition;
    addition << "\n[input]\n\n";
    addition << action_name << "={\n";
    addition << "\"deadzone\": " << deadzone << ",\n";
    addition << "\"events\": [";
    if (!keycode.empty()) {
        // Generate a simple InputEventKey reference
        addition << "Object(InputEventKey,\"resource_local_to_scene\":false,\"resource_name\":\"\",\"device\":-1,\"window_id\":0,\"alt_pressed\":false,\"shift_pressed\":false,\"ctrl_pressed\":false,\"meta_pressed\":false,\"pressed\":false,\"keycode\":0,\"physical_keycode\":";
        // Convert "KEY_SPACE" -> int (0x20 = 32)
        // Simplified: we emit a generic key reference; the user can fine-tune in inspector
        if (keycode == "KEY_SPACE")       addition << "0x20";
        else if (keycode == "KEY_ENTER")  addition << "0x0d";
        else if (keycode == "KEY_ESCAPE") addition << "0x1b";
        else if (keycode == "KEY_A")      addition << "0x41";
        else if (keycode == "KEY_D")      addition << "0x44";
        else if (keycode == "KEY_W")      addition << "0x57";
        else if (keycode == "KEY_S")      addition << "0x53";
        else if (keycode == "KEY_LEFT")   addition << "0x100 + 0x14";
        else if (keycode == "KEY_RIGHT")  addition << "0x100 + 0x15";
        else if (keycode == "KEY_UP")     addition << "0x100 + 0x13";
        else if (keycode == "KEY_DOWN")   addition << "0x100 + 0x16";
        else                               addition << "0";
        addition << ",\"key_label\":0,\"unicode\":0,\"location\":0,\"echo\":false,\"script\":null)\n";
    }
    addition << "]\n";
    addition << "}\n";

    // If [input] already exists, replace the line for this action
    const std::string section = "[input]";
    size_t sec_pos = content.find(section);
    bool action_replaced = false;
    if (sec_pos != std::string::npos) {
        size_t end_pos = content.find("\n[", sec_pos + 1);
        if (end_pos == std::string::npos) end_pos = content.size();

        std::string search_key = action_name + "={";
        size_t key_pos = content.find(search_key, sec_pos);
        if (key_pos != std::string::npos && key_pos < end_pos) {
            // Find end of action block: matching "}"
            int depth = 0;
            size_t cursor = key_pos;
            size_t block_start = cursor;
            while (cursor < end_pos) {
                if (content[cursor] == '{') depth++;
                else if (content[cursor] == '}') { depth--; if (depth == 0) break; }
                cursor++;
            }
            size_t block_end = cursor + 1;
            // Strip the new addition down to just the action
            std::string new_action = action_name + "={\n\"deadzone\": " + std::to_string(deadzone) + ",\n\"events\": []\n}\n";
            content.replace(block_start, block_end - block_start, new_action);
            action_replaced = true;
        }
    }

    if (!action_replaced) {
        content += addition.str();
    }

    std::ofstream out_file(pg_path, std::ios::trunc);
    if (!out_file.is_open()) { r_error = "Cannot write to: " + pg_path; return; }
    out_file << content;
    out_file.close();

    json result = {
        {"success", true},
        {"action_name", action_name},
        {"keycode", keycode},
        {"deadzone", deadzone},
        {"replaced", action_replaced}
    };
    r_result = result.dump();
}

} // namespace gear_mcp

void register_input_tools(gear_mcp::ToolRegistry *p_registry) {
    if (!p_registry) return;

    p_registry->register_tool("add_input_action",
        "Add (or replace) an input action in project.godot's [input] section",
        gear_mcp::ADD_INPUT_ACTION_SCHEMA, gear_mcp::handle_add_input_action, /*main_thread=*/false);

    std::printf("[Gear MCP] Registered input tool: add_input_action\n");
}
