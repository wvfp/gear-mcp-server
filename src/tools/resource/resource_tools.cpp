#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include "shared/path_utils.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace gear_mcp {

// ---------------------------------------------------------------------------
// Internal helpers for .tres file manipulation
// ---------------------------------------------------------------------------

/// Resolve a path that may be res:// or absolute to an absolute filesystem path.
static std::string resolve_path(const std::string &p_path) {
    if (p_path.substr(0, 6) == "res://") {
        return GodotAPI::res_to_absolute(p_path);
    }
    return p_path;
}

/// Format a JSON value as a Godot .tres property value string.
static std::string json_to_tres_value(const json &p_value) {
    if (p_value.is_string()) {
        return "\"" + p_value.get<std::string>() + "\"";
    }
    if (p_value.is_boolean()) {
        return p_value.get<bool>() ? "true" : "false";
    }
    if (p_value.is_number_integer()) {
        return std::to_string(p_value.get<int64_t>());
    }
    if (p_value.is_number_float()) {
        std::ostringstream oss;
        oss << p_value.get<double>();
        return oss.str();
    }
    if (p_value.is_object()) {
        // Check for typed values: {"_type": "Color", "r": ..., "g": ..., "b": ..., "a": ...}
        if (p_value.contains("_type")) {
            std::string type = p_value.value("_type", "");
            if (type == "Color") {
                double r = p_value.value("r", 1.0);
                double g = p_value.value("g", 1.0);
                double b = p_value.value("b", 1.0);
                double a = p_value.value("a", 1.0);
                std::ostringstream oss;
                oss << "Color(" << r << ", " << g << ", " << b << ", " << a << ")";
                return oss.str();
            }
            if (type == "Vector2") {
                double x = p_value.value("x", 0.0);
                double y = p_value.value("y", 0.0);
                std::ostringstream oss;
                oss << "Vector2(" << x << ", " << y << ")";
                return oss.str();
            }
            if (type == "Vector3") {
                double x = p_value.value("x", 0.0);
                double y = p_value.value("y", 0.0);
                double z = p_value.value("z", 0.0);
                std::ostringstream oss;
                oss << "Vector3(" << x << ", " << y << ", " << z << ")";
                return oss.str();
            }
        }
    }
    if (p_value.is_array()) {
        // Generic array serialization
        std::string result = "[";
        for (size_t i = 0; i < p_value.size(); i++) {
            if (i > 0) result += ", ";
            result += json_to_tres_value(p_value[i]);
        }
        result += "]";
        return result;
    }
    // Fallback: dump as string
    return p_value.dump();
}

/// Format a color from individual r/g/b/a JSON fields (for create_material).
static std::string format_color(double r, double g, double b, double a) {
    std::ostringstream oss;
    oss << "Color(" << r << ", " << g << ", " << b << ", " << a << ")";
    return oss.str();
}

// ===========================================================================
// create_resource — create a new .tres resource file
// ===========================================================================

static void create_resource_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (...) {
        r_error = "Invalid JSON in params";
        return;
    }

    std::string resource_path = params.value("resource_path", "");
    if (resource_path.empty()) {
        r_error = "Missing required parameter: resource_path";
        return;
    }

    std::string resource_type = params.value("resource_type", "Resource");

    std::string abs_path = resolve_path(resource_path);
    path_utils::ensure_parent_dirs(abs_path);

    std::ofstream file(abs_path, std::ios::trunc);
    if (!file.is_open()) {
        r_error = "Cannot create resource file: " + abs_path;
        return;
    }

    file << "[gd_resource type=\"" << resource_type << "\" format=3]\n\n";
    file << "[resource]\n";

    // Write optional properties
    if (params.contains("properties") && params["properties"].is_object()) {
        json properties = params["properties"];
        for (auto it = properties.begin(); it != properties.end(); ++it) {
            file << it.key() << " = " << json_to_tres_value(it.value()) << "\n";
        }
    }

    file.close();

    json result = {
        {"success", true},
        {"resource_path", resource_path},
        {"resource_type", resource_type}
    };
    r_result = result.dump();
}

// ===========================================================================
// modify_resource — modify properties in an existing .tres file
// ===========================================================================

static void modify_resource_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (...) {
        r_error = "Invalid JSON in params";
        return;
    }

    std::string resource_path = params.value("resource_path", "");
    if (resource_path.empty()) {
        r_error = "Missing required parameter: resource_path";
        return;
    }

    if (!params.contains("properties") || !params["properties"].is_object()) {
        r_error = "Missing or invalid required parameter: properties";
        return;
    }

    std::string abs_path = resolve_path(resource_path);

    // Read the existing file
    std::ifstream in_file(abs_path);
    if (!in_file.is_open()) {
        r_error = "Cannot open resource file: " + abs_path;
        return;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in_file, line)) {
        lines.push_back(line);
    }
    in_file.close();

    // Find the [resource] section boundaries
    int resource_section_start = -1;
    int resource_section_end = (int)lines.size();

    for (int i = 0; i < (int)lines.size(); i++) {
        if (lines[i].find("[resource]") != std::string::npos) {
            resource_section_start = i;
        } else if (resource_section_start >= 0 && i > resource_section_start) {
            // Check if this line starts a new section
            if (!lines[i].empty() && lines[i][0] == '[') {
                resource_section_end = i;
                break;
            }
        }
    }

    if (resource_section_start < 0) {
        r_error = "No [resource] section found in file";
        return;
    }

    json properties = params["properties"];
    json properties_modified = json::array();
    json properties_added = json::array();

    for (auto it = properties.begin(); it != properties.end(); ++it) {
        std::string key = it.key();
        std::string value = json_to_tres_value(it.value());
        std::string new_line = key + " = " + value;

        bool found = false;
        // Search within the resource section for existing property
        for (int i = resource_section_start + 1; i < resource_section_end; i++) {
            // Check if this line starts with "key = " or "key="
            std::string prefix = key + " = ";
            std::string prefix2 = key + "=";
            if (lines[i].substr(0, prefix.size()) == prefix ||
                lines[i].substr(0, prefix2.size()) == prefix2) {
                lines[i] = new_line;
                found = true;
                properties_modified.push_back(key);
                break;
            }
        }

        if (!found) {
            // Insert new property at the end of the resource section
            lines.insert(lines.begin() + resource_section_end, new_line);
            resource_section_end++; // Adjust end boundary
            properties_added.push_back(key);
        }
    }

    // Write the modified file
    std::ofstream out_file(abs_path, std::ios::trunc);
    if (!out_file.is_open()) {
        r_error = "Cannot write to resource file: " + abs_path;
        return;
    }

    for (size_t i = 0; i < lines.size(); i++) {
        out_file << lines[i];
        if (i + 1 < lines.size()) out_file << "\n";
    }
    out_file.close();

    json result = {
        {"success", true},
        {"resource_path", resource_path},
        {"properties_modified", properties_modified},
        {"properties_added", properties_added}
    };
    r_result = result.dump();
}

// ===========================================================================
// create_material — create a StandardMaterial3D resource
// ===========================================================================

static void create_material_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (...) {
        r_error = "Invalid JSON in params";
        return;
    }

    std::string resource_path = params.value("resource_path", "");
    if (resource_path.empty()) {
        r_error = "Missing required parameter: resource_path";
        return;
    }

    // Extract material properties with defaults
    double albedo_r = 1.0, albedo_g = 1.0, albedo_b = 1.0, albedo_a = 1.0;
    if (params.contains("albedo_color") && params["albedo_color"].is_object()) {
        json ac = params["albedo_color"];
        albedo_r = ac.value("r", 1.0);
        albedo_g = ac.value("g", 1.0);
        albedo_b = ac.value("b", 1.0);
        albedo_a = ac.value("a", 1.0);
    }

    double metallic = params.value("metallic", 0.0);
    double roughness = params.value("roughness", 1.0);

    std::string abs_path = resolve_path(resource_path);
    path_utils::ensure_parent_dirs(abs_path);

    std::ofstream file(abs_path, std::ios::trunc);
    if (!file.is_open()) {
        r_error = "Cannot create material file: " + abs_path;
        return;
    }

    file << "[gd_resource type=\"StandardMaterial3D\" format=3]\n\n";
    file << "[resource]\n";
    file << "albedo_color = " << format_color(albedo_r, albedo_g, albedo_b, albedo_a) << "\n";
    file << "metallic = " << metallic << "\n";
    file << "roughness = " << roughness << "\n";
    file.close();

    json result = {
        {"success", true},
        {"resource_path", resource_path},
        {"resource_type", "StandardMaterial3D"},
        {"albedo_color", {
            {"r", albedo_r}, {"g", albedo_g},
            {"b", albedo_b}, {"a", albedo_a}
        }},
        {"metallic", metallic},
        {"roughness", roughness}
    };
    r_result = result.dump();
}

// ===========================================================================
// create_shader — create a .gdshader file and optional ShaderMaterial .tres
// ===========================================================================

static void create_shader_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (...) {
        r_error = "Invalid JSON in params";
        return;
    }

    std::string shader_path = params.value("shader_path", "");
    if (shader_path.empty()) {
        r_error = "Missing required parameter: shader_path";
        return;
    }

    std::string shader_type = params.value("shader_type", "spatial");
    std::string shader_code = params.value("shader_code", "");

    // Write the .gdshader file
    std::string shader_abs = resolve_path(shader_path);
    path_utils::ensure_parent_dirs(shader_abs);

    std::ofstream shader_file(shader_abs, std::ios::trunc);
    if (!shader_file.is_open()) {
        r_error = "Cannot create shader file: " + shader_abs;
        return;
    }

    shader_file << "shader_type " << shader_type << ";\n\n";
    if (!shader_code.empty()) {
        shader_file << shader_code << "\n";
    }
    shader_file.close();

    json result = {
        {"success", true},
        {"shader_path", shader_path},
        {"shader_type", shader_type}
    };

    // Optionally create a ShaderMaterial .tres that references this shader
    std::string material_path = params.value("material_path", "");
    if (!material_path.empty()) {
        std::string mat_abs = resolve_path(material_path);
        path_utils::ensure_parent_dirs(mat_abs);

        std::ofstream mat_file(mat_abs, std::ios::trunc);
        if (!mat_file.is_open()) {
            r_error = "Cannot create material file: " + mat_abs;
            return;
        }

        // The ExtResource reference to the shader
        mat_file << "[gd_resource type=\"ShaderMaterial\" load_steps=2 format=3]\n\n";
        mat_file << "[ext_resource type=\"Shader\" path=\"" << shader_path << "\" id=\"1\"]\n\n";
        mat_file << "[resource]\n";
        mat_file << "shader = ExtResource(\"1\")\n";
        mat_file.close();

        result["material_path"] = material_path;
    }

    r_result = result.dump();
}

// ===========================================================================
// get_dependencies — list resource dependencies from a .tres/.tscn file
// ===========================================================================

static void get_dependencies_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (...) {
        r_error = "Invalid JSON in params";
        return;
    }

    std::string resource_path = params.value("resource_path", "");
    if (resource_path.empty()) {
        r_error = "Missing required parameter: resource_path";
        return;
    }

    std::string abs_path = resolve_path(resource_path);

    std::ifstream file(abs_path);
    if (!file.is_open()) {
        r_error = "Cannot open resource file: " + abs_path;
        return;
    }

    // Parse [ext_resource ...] and [sub_resource ...] sections
    // ext_resource format: [ext_resource type="Type" path="res://..." id="1"]
    // sub_resource format: [sub_resource type="Type" id="1"]
    static const std::regex ext_regex(
        R"re(\[ext_resource\s+type="([^"]*)"(?:\s+path="([^"]*)")?(?:\s+id="([^"]*)")?.*?\])re");
    static const std::regex sub_regex(
        R"re(\[sub_resource\s+type="([^"]*)"(?:\s+id="([^"]*)")?.*?\])re");

    json ext_resources = json::array();
    json sub_resources = json::array();

    std::string line;
    while (std::getline(file, line)) {
        try {
            std::smatch match;
            if (std::regex_search(line, match, ext_regex)) {
                json dep = json::object();
                dep["kind"] = "ext_resource";
                dep["type"] = match[1].str();
                if (match[2].matched) dep["path"] = match[2].str();
                if (match[3].matched) dep["id"] = match[3].str();
                ext_resources.push_back(dep);
            } else if (std::regex_search(line, match, sub_regex)) {
                json dep = json::object();
                dep["kind"] = "sub_resource";
                dep["type"] = match[1].str();
                if (match[2].matched) dep["id"] = match[2].str();
                sub_resources.push_back(dep);
            }
        } catch (const std::regex_error &) {
            continue;
        }
    }
    file.close();

    json result = {
        {"resource_path", resource_path},
        {"ext_resources", ext_resources},
        {"sub_resources", sub_resources},
        {"total_dependencies", (int)(ext_resources.size() + sub_resources.size())}
    };
    r_result = result.dump();
}

} // namespace gear_mcp

// ===========================================================================
// Registration
// ===========================================================================

void register_resource_tools(gear_mcp::ToolRegistry *p_registry) {
    if (!p_registry) return;

    static const char *CREATE_RESOURCE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "resource_path": {"type": "string", "description": "Output .tres file path"},
        "resource_type": {"type": "string", "description": "Resource class name", "default": "Resource"},
        "properties": {"type": "object", "description": "Initial properties"}
    },
    "required": ["resource_path"]
})schema";

    static const char *MODIFY_RESOURCE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "resource_path": {"type": "string", "description": "Path to .tres file"},
        "properties": {"type": "object", "description": "Properties to set"}
    },
    "required": ["resource_path", "properties"]
})schema";

    static const char *CREATE_MATERIAL_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "resource_path": {"type": "string", "description": "Output .tres path"},
        "albedo_color": {
            "type": "object",
            "properties": {
                "r": {"type": "number"},
                "g": {"type": "number"},
                "b": {"type": "number"},
                "a": {"type": "number"}
            }
        },
        "metallic": {"type": "number", "default": 0},
        "roughness": {"type": "number", "default": 1}
    },
    "required": ["resource_path"]
})schema";

    static const char *CREATE_SHADER_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "shader_path": {"type": "string", "description": "Output .gdshader path"},
        "material_path": {"type": "string", "description": "Output .tres ShaderMaterial path"},
        "shader_type": {"type": "string", "enum": ["spatial", "canvas_item", "particles", "sky", "fog"], "default": "spatial"},
        "shader_code": {"type": "string", "description": "Shader source code"}
    },
    "required": ["shader_path"]
})schema";

    static const char *GET_DEPENDENCIES_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "resource_path": {"type": "string", "description": "Path to resource file"}
    },
    "required": ["resource_path"]
})schema";

    p_registry->register_tool("create_resource",
        "Create a new .tres resource file with specified type and properties",
        CREATE_RESOURCE_SCHEMA, gear_mcp::create_resource_handler, false);

    p_registry->register_tool("modify_resource",
        "Modify properties in an existing .tres resource file",
        MODIFY_RESOURCE_SCHEMA, gear_mcp::modify_resource_handler, false);

    p_registry->register_tool("create_material",
        "Create a StandardMaterial3D resource with albedo color, metallic, and roughness",
        CREATE_MATERIAL_SCHEMA, gear_mcp::create_material_handler, false);

    p_registry->register_tool("create_shader",
        "Create a .gdshader file and optionally a ShaderMaterial .tres referencing it",
        CREATE_SHADER_SCHEMA, gear_mcp::create_shader_handler, false);

    p_registry->register_tool("get_dependencies",
        "List resource dependencies (ext_resource and sub_resource) from a .tres or .tscn file",
        GET_DEPENDENCIES_SCHEMA, gear_mcp::get_dependencies_handler, false);

    std::printf("[Gear MCP] Registered resource tools: create_resource, modify_resource, create_material, create_shader, get_dependencies\n");
}
