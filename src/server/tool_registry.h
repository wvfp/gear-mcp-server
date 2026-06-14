#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
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

    /// Number of tools currently enabled (i.e. not in the disabled set).
    size_t enabled_count() const;

    /// Set the enabled state of a single tool. Returns false if the tool
    /// does not exist. Disabling a tool causes its tools/call to return
    /// a "Method not found" error.
    bool set_tool_enabled(const std::string &p_name, bool p_enabled);

    /// Returns the enabled state of a tool (defaults to true for unknown).
    bool is_tool_enabled(const std::string &p_name) const;

    /// Mark every registered tool as enabled. Returns the number flipped.
    size_t enable_all_tools();

    /// Mark every registered tool as disabled. Returns the number flipped.
    size_t disable_all_tools();

    /// Increment the per-tool call counter. Called by MCPMethods after each
    /// successful or failed tools/call dispatch.
    void increment_call_count(const std::string &p_name);

    /// Total number of tools/call invocations across all tools.
    int64_t total_call_count() const;

    /// Returns the call count for a single tool (0 if never called).
    int64_t get_call_count(const std::string &p_name) const;

private:
    struct Entry {
        ToolInfo info;
        ToolHandler handler = nullptr;
    };

    std::map<std::string, Entry> m_tools;
    std::set<std::string> m_disabled;
    std::map<std::string, int64_t> m_call_counts;
    mutable std::mutex m_mutex;
};

} // namespace gear_mcp
