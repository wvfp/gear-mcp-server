#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include "shared/path_utils.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <string>
#include <regex>
#include <vector>

using json = nlohmann::json;

namespace gear_mcp {

static void connect_signal(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    try {
        json params = json::parse(p_params_json);
        std::string scene_path = params.at("scene_path").get<std::string>();
        std::string source_node = params.at("source_node").get<std::string>();
        std::string signal_name = params.at("signal_name").get<std::string>();
        std::string target_node = params.at("target_node").get<std::string>();
        std::string method_name = params.at("method_name").get<std::string>();

        std::string full_path = path_utils::normalize(scene_path);
        std::ifstream in_file(full_path);
        if (!in_file.is_open()) {
            r_error = "Failed to open scene file: " + scene_path;
            return;
        }

        std::stringstream buffer;
        buffer << in_file.rdbuf();
        std::string file_content = buffer.str();
        in_file.close();

        // Build the connection line.
        std::string connection_line = "[connection signal=\"" + signal_name +
            "\" from=\"" + source_node +
            "\" to=\"" + target_node +
            "\" method=\"" + method_name + "\"]";

        // Check if this exact connection already exists.
        if (file_content.find(connection_line) != std::string::npos) {
            r_error = "Signal connection already exists in scene file";
            return;
        }

        // Ensure there is a newline before the connection section if content doesn't end with one.
        if (!file_content.empty() && file_content.back() != '\n') {
            file_content += "\n";
        }

        file_content += connection_line + "\n";

        std::ofstream out_file(full_path);
        if (!out_file.is_open()) {
            r_error = "Failed to write to scene file: " + scene_path;
            return;
        }
        out_file << file_content;
        out_file.close();

        GodotAPI::refresh_filesystem();

        json result;
        result["success"] = true;
        result["scene_path"] = scene_path;
        result["signal"] = signal_name;
        result["from"] = source_node;
        result["to"] = target_node;
        result["method"] = method_name;
        result["message"] = "Signal connection added successfully";
        r_result = result.dump();
    } catch (const std::exception &e) {
        r_error = std::string("Failed to connect signal: ") + e.what();
    }
}

static void disconnect_signal(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    try {
        json params = json::parse(p_params_json);
        std::string scene_path = params.at("scene_path").get<std::string>();
        std::string source_node = params.at("source_node").get<std::string>();
        std::string signal_name = params.at("signal_name").get<std::string>();
        std::string target_node = params.at("target_node").get<std::string>();
        std::string method_name = params.at("method_name").get<std::string>();

        std::string full_path = path_utils::normalize(scene_path);
        std::ifstream in_file(full_path);
        if (!in_file.is_open()) {
            r_error = "Failed to open scene file: " + scene_path;
            return;
        }

        std::stringstream buffer;
        buffer << in_file.rdbuf();
        std::string file_content = buffer.str();
        in_file.close();

        // Build the target connection line to remove.
        std::string target_line = "[connection signal=\"" + signal_name +
            "\" from=\"" + source_node +
            "\" to=\"" + target_node +
            "\" method=\"" + method_name + "\"]";

        // Find and remove the matching connection line.
        size_t pos = file_content.find(target_line);
        if (pos == std::string::npos) {
            r_error = "Signal connection not found in scene file";
            return;
        }

        // Remove the line and its trailing newline if present.
        size_t remove_end = pos + target_line.length();
        if (remove_end < file_content.length() && file_content[remove_end] == '\n') {
            remove_end++;
        }
        file_content.erase(pos, remove_end - pos);

        std::ofstream out_file(full_path);
        if (!out_file.is_open()) {
            r_error = "Failed to write to scene file: " + scene_path;
            return;
        }
        out_file << file_content;
        out_file.close();

        GodotAPI::refresh_filesystem();

        json result;
        result["success"] = true;
        result["scene_path"] = scene_path;
        result["signal"] = signal_name;
        result["from"] = source_node;
        result["to"] = target_node;
        result["method"] = method_name;
        result["message"] = "Signal connection removed successfully";
        r_result = result.dump();
    } catch (const std::exception &e) {
        r_error = std::string("Failed to disconnect signal: ") + e.what();
    }
}

static void list_connections(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    try {
        json params = json::parse(p_params_json);
        std::string scene_path = params.at("scene_path").get<std::string>();

        std::string full_path = path_utils::normalize(scene_path);
        std::ifstream file(full_path);
        if (!file.is_open()) {
            r_error = "Failed to open scene file: " + scene_path;
            return;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string file_content = buffer.str();
        file.close();

        // Regex to match [connection signal="..." from="..." to="..." method="..."]
        static const std::regex connection_regex(
            R"re(\[connection\s+signal="([^"]*)"\s+from="([^"]*)"\s+to="([^"]*)"\s+method="([^"]*)"\])re"
        );

        json connections = json::array();
        std::string search_str = file_content;
        std::smatch match;
        while (std::regex_search(search_str, match, connection_regex)) {
            json conn;
            conn["signal"] = match[1].str();
            conn["from"] = match[2].str();
            conn["to"] = match[3].str();
            conn["method"] = match[4].str();
            connections.push_back(conn);
            search_str = match.suffix().str();
        }

        json result;
        result["success"] = true;
        result["scene_path"] = scene_path;
        result["connections"] = connections;
        result["count"] = connections.size();
        r_result = result.dump();
    } catch (const std::exception &e) {
        r_error = std::string("Failed to list connections: ") + e.what();
    }
}

} // namespace gear_mcp

void register_signal_tools(gear_mcp::ToolRegistry *p_registry) {
    const char *connect_signal_schema = R"({"type":"object","properties":{"scene_path":{"type":"string","description":"Path to .tscn file"},"source_node":{"type":"string","description":"Node path of signal source"},"signal_name":{"type":"string","description":"Signal name"},"target_node":{"type":"string","description":"Node path of target"},"method_name":{"type":"string","description":"Method to call"}},"required":["scene_path","source_node","signal_name","target_node","method_name"]})";

    const char *disconnect_signal_schema = R"({"type":"object","properties":{"scene_path":{"type":"string"},"source_node":{"type":"string"},"signal_name":{"type":"string"},"target_node":{"type":"string"},"method_name":{"type":"string"}},"required":["scene_path","source_node","signal_name","target_node","method_name"]})";

    const char *list_connections_schema = R"({"type":"object","properties":{"scene_path":{"type":"string","description":"Path to .tscn file"}},"required":["scene_path"]})";

    p_registry->register_tool("connect_signal", "Add a signal connection to a scene file", connect_signal_schema, gear_mcp::connect_signal, false);
    p_registry->register_tool("disconnect_signal", "Remove a signal connection from a scene file", disconnect_signal_schema, gear_mcp::disconnect_signal, false);
    p_registry->register_tool("list_connections", "List all signal connections in a scene file", list_connections_schema, gear_mcp::list_connections, false);
}
