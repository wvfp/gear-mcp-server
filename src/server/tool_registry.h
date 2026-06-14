#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace gear_mcp {

// ---------------------------------------------------------------------------
// Tool descriptor
// ---------------------------------------------------------------------------
struct ToolInfo {
    std::string name;
    std::string description;
    std::string input_schema; // JSON Schema string
    bool main_thread = false; // true = must run on Godot main thread
};

// Handler signature: receives raw params JSON string, fills result or error.
using ToolHandler = void (*)(const std::string &params_json, std::string &result_out, std::string &error_out);

// ---------------------------------------------------------------------------
// ToolRegistry — maps tool name -> handler, thread-safe via mutex.
// ---------------------------------------------------------------------------
class ToolRegistry {
public:
    /// Register a tool. p_main_thread indicates whether the tool requires
    /// the Godot main thread (scene/resource ops) or can run in TCP thread (file I/O).
    void register_tool(const char *p_name, const char *p_description,
                       const char *p_input_schema, ToolHandler p_handler,
                       bool p_main_thread = false);

    /// Look up a tool by name. Returns true if found.
    bool get_tool(const char *p_name, ToolInfo &r_info, ToolHandler &r_handler) const;

    /// Fill a vector with all registered tools.
    void list_tools(std::vector<ToolInfo> &r_tools) const;

    /// Number of registered tools.
    size_t tool_count() const;

private:
    struct Entry {
        ToolInfo info;
        ToolHandler handler = nullptr;
    };

    std::map<std::string, Entry> m_tools;
    mutable std::mutex m_mutex;
};

} // namespace gear_mcp
