#pragma once

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
// MCPMethods — standard MCP method handlers using nlohmann/json.
// ---------------------------------------------------------------------------
class MCPMethods {
public:
    explicit MCPMethods(ToolRegistry *p_registry);

    /// Dispatch a parsed JSON-RPC request. Returns the full JSON-RPC response.
    /// Returns empty string if the method is not recognized.
    std::string handle(const nlohmann::json &p_req) const;

    void register_resource(const ResourceInfo &p_resource);
    void register_prompt(const PromptInfo &p_prompt, PromptGetHandler p_handler);

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
