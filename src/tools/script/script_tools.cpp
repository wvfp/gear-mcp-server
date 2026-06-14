#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include "shared/path_utils.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <string>

using json = nlohmann::json;

namespace gear_mcp {

static void create_script(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    try {
        json params = json::parse(p_params_json);
        std::string script_path = params.at("script_path").get<std::string>();
        std::string class_name = params.value("class_name", "");
        std::string extends = params.value("extends", "Node");
        std::string content = params.value("content", "");

        std::string full_path = path_utils::normalize(script_path);
        if (!path_utils::ensure_parent_dirs(full_path)) {
            r_error = "Failed to create parent directories for: " + script_path;
            return;
        }

        std::ofstream file(full_path);
        if (!file.is_open()) {
            r_error = "Failed to create script file: " + script_path;
            return;
        }

        if (!class_name.empty()) {
            file << "class_name " << class_name << "\n";
        }
        file << "extends " << extends << "\n";
        if (!content.empty()) {
            file << "\n" << content;
        }

        file.close();
        GodotAPI::refresh_filesystem();

        json result;
        result["success"] = true;
        result["path"] = script_path;
        result["message"] = "Script created successfully";
        r_result = result.dump();
    } catch (const std::exception &e) {
        r_error = std::string("Failed to create script: ") + e.what();
    }
}

static void modify_script(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    try {
        json params = json::parse(p_params_json);
        std::string script_path = params.at("script_path").get<std::string>();
        std::string content = params.value("content", "");
        std::string append = params.value("append", "");
        std::string mode = params.value("mode", "write");

        std::string full_path = path_utils::normalize(script_path);
        std::ofstream::openmode flags = std::ofstream::out;
        if (mode == "append") {
            flags |= std::ofstream::app;
        }

        std::ofstream file(full_path, flags);
        if (!file.is_open()) {
            r_error = "Failed to open script file: " + script_path;
            return;
        }

        if (mode == "write") {
            file << content;
        } else {
            file << append;
        }

        file.close();
        GodotAPI::refresh_filesystem();

        json result;
        result["success"] = true;
        result["path"] = script_path;
        result["mode"] = mode;
        result["message"] = "Script modified successfully";
        r_result = result.dump();
    } catch (const std::exception &e) {
        r_error = std::string("Failed to modify script: ") + e.what();
    }
}

static void get_script_info(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    try {
        json params = json::parse(p_params_json);
        std::string script_path = params.at("script_path").get<std::string>();

        std::string full_path = path_utils::normalize(script_path);
        std::ifstream file(full_path);
        if (!file.is_open()) {
            r_error = "Failed to open script file: " + script_path;
            return;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string file_content = buffer.str();
        file.close();

        std::istringstream stream(file_content);
        std::string line;
        std::string extends_class;
        std::string class_name;
        std::vector<std::string> functions;
        std::vector<std::string> signals;
        std::vector<std::string> variables;

        while (std::getline(stream, line)) {
            if (line.find("extends ") == 0) {
                extends_class = line.substr(8);
            } else if (line.find("class_name ") == 0) {
                class_name = line.substr(11);
            } else if (line.find("func ") == 0) {
                std::string func_decl = line.substr(5);
                size_t paren_pos = func_decl.find('(');
                if (paren_pos != std::string::npos) {
                    functions.push_back(func_decl.substr(0, paren_pos));
                }
            } else if (line.find("signal ") == 0) {
                signals.push_back(line.substr(7));
            } else if (line.find("var ") == 0) {
                std::string var_decl = line.substr(4);
                size_t equal_pos = var_decl.find('=');
                size_t colon_pos = var_decl.find(':');
                size_t end_pos = std::min(equal_pos, colon_pos);
                if (end_pos != std::string::npos) {
                    variables.push_back(var_decl.substr(0, end_pos));
                } else {
                    variables.push_back(var_decl);
                }
            }
        }

        json result;
        result["path"] = script_path;
        result["content"] = file_content;
        result["extends"] = extends_class;
        result["class_name"] = class_name;
        result["functions"] = functions;
        result["signals"] = signals;
        result["variables"] = variables;
        r_result = result.dump();
    } catch (const std::exception &e) {
        r_error = std::string("Failed to get script info: ") + e.what();
    }
}

} // namespace gear_mcp

void register_script_tools(gear_mcp::ToolRegistry *p_registry) {
    const char *create_script_schema = R"JSON({"type":"object","properties":{"script_path":{"type":"string","description":"Path for .gd file"},"class_name":{"type":"string","description":"Optional class_name directive"},"extends":{"type":"string","description":"Parent class","default":"Node"},"content":{"type":"string","description":"Initial script content"}},"required":["script_path"]})JSON";
    const char *modify_script_schema = R"JSON({"type":"object","properties":{"script_path":{"type":"string","description":"Path to .gd file"},"content":{"type":"string","description":"New full content (write mode)"},"append":{"type":"string","description":"Text to append (append mode)"},"mode":{"type":"string","enum":["write","append"],"default":"write"}},"required":["script_path"]})JSON";
    const char *get_script_info_schema = R"JSON({"type":"object","properties":{"script_path":{"type":"string","description":"Path to .gd file"}},"required":["script_path"]})JSON";
    p_registry->register_tool("create_script", "Create a new GDScript file", create_script_schema, gear_mcp::create_script, false);
    p_registry->register_tool("modify_script", "Modify an existing GDScript file", modify_script_schema, gear_mcp::modify_script, false);
    p_registry->register_tool("get_script_info", "Read script content and extract info", get_script_info_schema, gear_mcp::get_script_info, false);
}
