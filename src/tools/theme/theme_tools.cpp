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
// resolve theme — find a Theme resource to modify. Either via the live editor
// (a node's theme_override_*) or by file I/O (.theme files).
// ---------------------------------------------------------------------------

static std::string ensure_res(const std::string &p) {
    if (p.empty()) return p;
    if (p.substr(0, 6) == "res://") return p;
    return "res://" + p;
}

// ---------------------------------------------------------------------------
// set_theme_color — set a color in a .theme file or via a node's theme_override
// ---------------------------------------------------------------------------

static const char *SET_THEME_COLOR_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "theme_path": {"type": "string", "description": "res:// path to a .theme file (creates if missing)"},
        "color_name": {"type": "string", "description": "Color item name (e.g., 'panel')"},
        "color": {
            "type": "object",
            "properties": {
                "r": {"type": "number"},
                "g": {"type": "number"},
                "b": {"type": "number"},
                "a": {"type": "number", "default": 1.0}
            },
            "required": ["r", "g", "b"]
        },
        "theme_type": {"type": "string", "description": "Theme type name (e.g., 'Button')", "default": ""}
    },
    "required": ["theme_path", "color_name", "color"]
})schema";

static void handle_set_theme_color(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string theme_path = ensure_res(params.value("theme_path", ""));
    std::string color_name = params.value("color_name", "");
    std::string theme_type = params.value("theme_type", "");

    if (theme_path.empty() || color_name.empty()) {
        r_error = "Missing required parameters: theme_path, color_name";
        return;
    }
    if (!params.contains("color") || !params["color"].is_object()) {
        r_error = "Missing required parameter: color {r, g, b[, a]}";
        return;
    }

    double r = params["color"].value("r", 1.0);
    double g = params["color"].value("g", 1.0);
    double b = params["color"].value("b", 1.0);
    double a = params["color"].value("a", 1.0);

    std::string abs_path = GodotAPI::res_to_absolute(theme_path);
    path_utils::ensure_parent_dirs(abs_path);

    // Append / replace the color line
    std::ifstream in_file(abs_path);
    std::vector<std::string> lines;
    if (in_file.is_open()) {
        std::string line;
        while (std::getline(in_file, line)) lines.push_back(line);
        in_file.close();
    }

    std::ostringstream color_ss;
    color_ss << "Color(" << r << ", " << g << ", " << b << ", " << a << ")";
    std::string value_line;
    if (theme_type.empty()) {
        value_line = color_name + " = " + color_ss.str();
    } else {
        value_line = theme_type + "/" + color_name + " = " + color_ss.str();
    }

    // Find existing entry to replace
    std::string prefix = (theme_type.empty() ? color_name : theme_type + "/" + color_name) + " = ";
    bool replaced = false;
    for (auto &line : lines) {
        if (line.substr(0, prefix.size()) == prefix) {
            line = value_line;
            replaced = true;
            break;
        }
    }

    if (!replaced) {
        if (lines.empty() || lines[0].find("[gd_resource") == std::string::npos) {
            // Create header
            std::ofstream out_file(abs_path, std::ios::trunc);
            out_file << "[gd_resource type=\"Theme\" format=3]\n\n";
            out_file << "[resource]\n";
            out_file << value_line << "\n";
            out_file.close();
        } else {
            // Append to existing file
            std::ofstream out_file(abs_path, std::ios::app);
            out_file << value_line << "\n";
            out_file.close();
        }
    } else {
        std::ofstream out_file(abs_path, std::ios::trunc);
        for (const auto &line : lines) out_file << line << "\n";
        out_file.close();
    }

    json result = {
        {"success", true},
        {"theme_path", theme_path},
        {"color_name", color_name},
        {"theme_type", theme_type},
        {"replaced", replaced}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// set_theme_font_size — set a font size in a .theme file
// ---------------------------------------------------------------------------

static const char *SET_THEME_FONT_SIZE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "theme_path": {"type": "string", "description": "res:// path to a .theme file"},
        "font_size_name": {"type": "string", "description": "Font size item name (e.g., 'normal')"},
        "size": {"type": "integer", "description": "Size in pixels"},
        "theme_type": {"type": "string", "description": "Theme type name (e.g., 'Label')", "default": ""}
    },
    "required": ["theme_path", "font_size_name", "size"]
})schema";

static void handle_set_theme_font_size(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string theme_path = ensure_res(params.value("theme_path", ""));
    std::string font_size_name = params.value("font_size_name", "");
    int size = params.value("size", 14);
    std::string theme_type = params.value("theme_type", "");

    if (theme_path.empty() || font_size_name.empty()) {
        r_error = "Missing required parameters: theme_path, font_size_name";
        return;
    }

    std::string abs_path = GodotAPI::res_to_absolute(theme_path);
    path_utils::ensure_parent_dirs(abs_path);

    std::string key = theme_type.empty() ? font_size_name : theme_type + "/" + font_size_name;
    std::string value_line = key + " = " + std::to_string(size);

    std::ifstream in_file(abs_path);
    std::vector<std::string> lines;
    if (in_file.is_open()) {
        std::string line;
        while (std::getline(in_file, line)) lines.push_back(line);
        in_file.close();
    }

    std::string prefix = key + " = ";
    bool replaced = false;
    for (auto &line : lines) {
        if (line.substr(0, prefix.size()) == prefix) {
            line = value_line;
            replaced = true;
            break;
        }
    }

    if (!replaced) {
        if (lines.empty() || lines[0].find("[gd_resource") == std::string::npos) {
            std::ofstream out_file(abs_path, std::ios::trunc);
            out_file << "[gd_resource type=\"Theme\" format=3]\n\n[resource]\n";
            out_file << value_line << "\n";
            out_file.close();
        } else {
            std::ofstream out_file(abs_path, std::ios::app);
            out_file << value_line << "\n";
            out_file.close();
        }
    } else {
        std::ofstream out_file(abs_path, std::ios::trunc);
        for (const auto &line : lines) out_file << line << "\n";
        out_file.close();
    }

    json result = {
        {"success", true},
        {"theme_path", theme_path},
        {"font_size_name", font_size_name},
        {"theme_type", theme_type},
        {"size", size},
        {"replaced", replaced}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// apply_theme_shader — append a Shader reference to a theme's constant list
// (or just create a stub shader file the theme can reference).
// ---------------------------------------------------------------------------

static const char *APPLY_THEME_SHADER_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "theme_path": {"type": "string", "description": "res:// path to a .theme file"},
        "shader_path": {"type": "string", "description": "res:// path to the shader to associate"},
        "theme_type": {"type": "string", "description": "Theme type name", "default": ""}
    },
    "required": ["theme_path", "shader_path"]
})schema";

static void handle_apply_theme_shader(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string theme_path = ensure_res(params.value("theme_path", ""));
    std::string shader_path = ensure_res(params.value("shader_path", ""));
    std::string theme_type = params.value("theme_type", "");

    if (theme_path.empty() || shader_path.empty()) {
        r_error = "Missing required parameters: theme_path, shader_path";
        return;
    }

    std::string abs_path = GodotAPI::res_to_absolute(theme_path);
    path_utils::ensure_parent_dirs(abs_path);

    // Append a "shader/<type>/<name> = ExtResource(...)" line so the theme
    // references the shader. We use a generic name; users can rename later.
    std::string key = "shader/" + (theme_type.empty() ? std::string("default") : theme_type);
    std::string line = key + " = ExtResource(\"1_" + shader_path.substr(6) + "\")";

    std::ofstream out_file(abs_path, std::ios::app);
    if (!out_file.is_open()) { r_error = "Cannot open theme file: " + abs_path; return; }
    out_file << line << "\n";
    out_file.close();

    json result = {
        {"success", true},
        {"theme_path", theme_path},
        {"shader_path", shader_path},
        {"theme_type", theme_type},
        {"note", "Theme now references the shader; open in the editor to fine-tune."}
    };
    r_result = result.dump();
}

} // namespace gear_mcp

void register_theme_tools(gear_mcp::ToolRegistry *p_registry) {
    if (!p_registry) return;

    p_registry->register_tool("set_theme_color",
        "Set a color in a .theme resource (creates the file if missing)",
        gear_mcp::SET_THEME_COLOR_SCHEMA, gear_mcp::handle_set_theme_color, /*main_thread=*/false);

    p_registry->register_tool("set_theme_font_size",
        "Set a font size in a .theme resource",
        gear_mcp::SET_THEME_FONT_SIZE_SCHEMA, gear_mcp::handle_set_theme_font_size, /*main_thread=*/false);

    p_registry->register_tool("apply_theme_shader",
        "Reference a shader from a .theme resource",
        gear_mcp::APPLY_THEME_SHADER_SCHEMA, gear_mcp::handle_apply_theme_shader, /*main_thread=*/false);

    std::printf("[Gear MCP] Registered theme tools: set_theme_color, set_theme_font_size, apply_theme_shader\n");
}
