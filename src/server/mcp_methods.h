#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace gear_mcp {

class ToolRegistry;

// ---------------------------------------------------------------------------
// Resource read handler — called when a client reads a resource.
// ---------------------------------------------------------------------------
using ResourceReadHandler = void (*)(const std::string &uri, std::string &text_out);

// ---------------------------------------------------------------------------
// Resource descriptor for MCP resources/list and resources/read
// ---------------------------------------------------------------------------
struct ResourceInfo {
    std::string uri;
    std::string name;
    std::string description;
    std::string mime_type;
    ResourceReadHandler read_handler = nullptr;
};

// ---------------------------------------------------------------------------
// Prompt descriptor for MCP prompts/list and prompts/get
// ---------------------------------------------------------------------------
struct PromptArgument {
    std::string name;
    std::string description;
    bool required = false;
};

struct PromptInfo {
    std::string name;
    std::string description;
    std::vector<PromptArgument> arguments;
};

// Prompt message content (text-only for now; the MCP spec allows more types).
struct PromptMessage {
    std::string role; // "user" or "assistant"
    std::string content;
};

using PromptGetHandler = std::vector<PromptMessage> (*)(
    const std::vector<std::pair<std::string, std::string>> &arguments);

// ---------------------------------------------------------------------------
// LogEntry — one row in the per-call log buffer consumed by the editor panel.
// ---------------------------------------------------------------------------
struct LogEntry {
    int64_t timestamp_ms = 0;   // Unix millis
    std::string level;          // "info" | "warn" | "error"
    std::string method;         // JSON-RPC method name ("tools/list", "ping", ...)
    std::string tool;           // For tools/call: the tool name; else ""
    std::string status;         // "ok" | "error" | "skipped"
    int duration_ms = 0;        // Wall clock spent in the handler (0 for cheap ops)
    std::string message;        // Short summary: params preview, error text, etc.
};

// ---------------------------------------------------------------------------
// MCPMethods — standard MCP method handlers using nlohmann/json.
// ---------------------------------------------------------------------------
class MCPMethods {
public:
    /// Maximum log entries retained in the ring buffer. The editor panel
    /// reads a window of these on each refresh.
    static constexpr size_t MAX_LOG_ENTRIES = 500;

    explicit MCPMethods(ToolRegistry *p_registry);

    /// Dispatch a parsed JSON-RPC request. Returns the full JSON-RPC response.
    /// Returns empty string if the method is not recognized.
    std::string handle(const nlohmann::json &p_req) const;

    void register_resource(const ResourceInfo &p_resource);
    void register_prompt(const PromptInfo &p_prompt, PromptGetHandler p_handler);

    /// Snapshot the most recent log entries (newest at the back). Returns
    /// up to p_max_count entries, ordered chronologically.
    void get_recent_logs(std::vector<LogEntry> &r_out, size_t p_max_count) const;

    /// Total number of log entries currently buffered.
    size_t log_count() const;

    /// Drop all log entries.
    void clear_logs();

private:
    ToolRegistry *m_tool_registry;
    mutable std::mutex m_resources_mutex;
    mutable std::vector<ResourceInfo> m_resources;

    struct PromptEntry {
        PromptInfo info;
        PromptGetHandler handler = nullptr;
    };
    mutable std::mutex m_prompts_mutex;
    mutable std::vector<PromptEntry> m_prompts;

    mutable std::mutex m_logs_mutex;
    mutable std::deque<LogEntry> m_logs; // mutable: log buffer is internal state, methods stay const

    void _log(const std::string &p_level, const std::string &p_method,
              const std::string &p_tool, const std::string &p_status,
              int p_duration_ms, const std::string &p_message) const;

    static int64_t _now_ms();

    std::string _handle_ping(const nlohmann::json &p_req) const;
    std::string _handle_initialize(const nlohmann::json &p_req) const;
    std::string _handle_tools_list(const nlohmann::json &p_req) const;
    std::string _handle_tools_call(const nlohmann::json &p_req) const;
    std::string _handle_resources_list(const nlohmann::json &p_req) const;
    std::string _handle_resources_read(const nlohmann::json &p_req) const;
    std::string _handle_prompts_list(const nlohmann::json &p_req) const;
    std::string _handle_prompts_get(const nlohmann::json &p_req) const;
};

} // namespace gear_mcp
