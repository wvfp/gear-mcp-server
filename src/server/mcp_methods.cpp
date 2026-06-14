#include "mcp_methods.h"
#include "main_thread_queue.h"
#include "tool_registry.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdio>
#include <sstream>

namespace gear_mcp {

using json = nlohmann::json;

// ===========================================================================
// Construction
// ===========================================================================

MCPMethods::MCPMethods(ToolRegistry *p_registry)
    : m_tool_registry(p_registry) {
    // Resources are registered externally by register_types.cpp
}

// ===========================================================================
// Time helper
// ===========================================================================

int64_t MCPMethods::_now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// ===========================================================================
// Log buffer
// ===========================================================================

void MCPMethods::_log(const std::string &p_level, const std::string &p_method,
                      const std::string &p_tool, const std::string &p_status,
                      int p_duration_ms, const std::string &p_message) const {
    LogEntry e;
    e.timestamp_ms = _now_ms();
    e.level = p_level;
    e.method = p_method;
    e.tool = p_tool;
    e.status = p_status;
    e.duration_ms = p_duration_ms;
    e.message = p_message;

    std::lock_guard<std::mutex> lock(m_logs_mutex);
    if (m_logs.size() >= MAX_LOG_ENTRIES) {
        m_logs.pop_front();
    }
    m_logs.push_back(std::move(e));
}

void MCPMethods::get_recent_logs(std::vector<LogEntry> &r_out, size_t p_max_count) const {
    r_out.clear();
    std::lock_guard<std::mutex> lock(m_logs_mutex);
    if (p_max_count == 0 || m_logs.empty()) return;
    size_t start = (m_logs.size() > p_max_count) ? (m_logs.size() - p_max_count) : 0;
    r_out.reserve(m_logs.size() - start);
    for (size_t i = start; i < m_logs.size(); ++i) {
        r_out.push_back(m_logs[i]);
    }
}

size_t MCPMethods::log_count() const {
    std::lock_guard<std::mutex> lock(m_logs_mutex);
    return m_logs.size();
}

void MCPMethods::clear_logs() {
    std::lock_guard<std::mutex> lock(m_logs_mutex);
    m_logs.clear();
}

// ===========================================================================
// Public dispatch
// ===========================================================================

std::string MCPMethods::handle(const json &p_req) const {
    std::string method = p_req.value("method", "");
    int64_t t0 = _now_ms();
    std::string response;

    if (method == "ping") {
        response = _handle_ping(p_req);
    } else if (method == "initialize") {
        response = _handle_initialize(p_req);
    } else if (method == "tools/list") {
        response = _handle_tools_list(p_req);
    } else if (method == "tools/call") {
        response = _handle_tools_call(p_req);
    } else if (method == "resources/list") {
        response = _handle_resources_list(p_req);
    } else if (method == "resources/read") {
        response = _handle_resources_read(p_req);
    } else if (method == "prompts/list") {
        response = _handle_prompts_list(p_req);
    } else if (method == "prompts/get") {
        response = _handle_prompts_get(p_req);
    }
    // Not recognized

    if (!response.empty()) {
        int dur = (int)(_now_ms() - t0);
        // Cheap heuristic: cheap ops log at info; tools/call is the only
        // method that goes through this generic path and is logged inside
        // _handle_tools_call with its own duration measurement. So we only
        // reach here for the lightweight methods.
        if (method != "tools/call") {
            _log("info", method, "", "ok", dur, "");
        }
    }

    return response;
}

void MCPMethods::register_resource(const ResourceInfo &p_resource) {
    std::lock_guard<std::mutex> lock(m_resources_mutex);
    for (auto &r : m_resources) {
        if (r.uri == p_resource.uri) {
            r = p_resource;
            return;
        }
    }
    m_resources.push_back(p_resource);
}

void MCPMethods::register_prompt(const PromptInfo &p_prompt, PromptGetHandler p_handler) {
    std::lock_guard<std::mutex> lock(m_prompts_mutex);
    for (auto &e : m_prompts) {
        if (e.info.name == p_prompt.name) {
            e.info = p_prompt;
            e.handler = p_handler;
            return;
        }
    }
    PromptEntry entry;
    entry.info = p_prompt;
    entry.handler = p_handler;
    m_prompts.push_back(entry);
}

// ===========================================================================
// Helper: safely extract JSON-RPC id (can be number, string, or null)
// ===========================================================================

static json get_id(const json &p_req) {
    auto it = p_req.find("id");
    if (it != p_req.end()) {
        return *it;
    }
    return nullptr;
}

static json get_params(const json &p_req) {
    auto it = p_req.find("params");
    if (it != p_req.end() && it->is_object()) {
        return *it;
    }
    return json::object();
}

// ===========================================================================
// Helper: build a JSON-RPC 2.0 response envelope
// ===========================================================================

static json make_response(const json &p_id, const json &p_result) {
    return {{"jsonrpc", "2.0"}, {"id", p_id}, {"result", p_result}};
}

static json make_error(const json &p_id, int p_code, const std::string &p_message) {
    return {{"jsonrpc", "2.0"}, {"id", p_id}, {"error", {{"code", p_code}, {"message", p_message}}}};
}

// ===========================================================================
// ping -> "pong"
// ===========================================================================

std::string MCPMethods::_handle_ping(const json &p_req) const {
    json id = get_id(p_req);
    json resp = make_response(id, "pong");
    return resp.dump() + "\n";
}

// ===========================================================================
// initialize — MCP handshake
//
// Per the MCP spec, returns protocolVersion + server capabilities + serverInfo.
// The client may request any protocol version; we honor it as long as it's
// recognizable, otherwise we fall back to the latest we support.
// ===========================================================================

static constexpr const char *SUPPORTED_PROTOCOL = "2024-11-05";

std::string MCPMethods::_handle_initialize(const json &p_req) const {
    json id = get_id(p_req);
    json params = get_params(p_req);

    // Client's protocolVersion is informational; we advertise what we support.
    // If they ask for something we don't know about, return our default.
    std::string client_version = params.value("protocolVersion", SUPPORTED_PROTOCOL);

    json result = {
        {"protocolVersion", SUPPORTED_PROTOCOL},
        {"capabilities", {
            {"tools", json::object()},      // we support tools/list + tools/call
            {"resources", json::object()},  // we support resources/list + resources/read
            {"prompts", json::object()}     // we support prompts/list + prompts/get
        }},
        {"serverInfo", {
            {"name", "gear-mcp-server"},
            {"version", "0.1.0"}
        }},
        {"instructions",
         "Gear MCP Server — C++ GDExtension embedded in the Godot editor. "
         "Use tools/list to discover available tools and resources/list for "
         "readable context (godot://editor/context)."}
    };
    (void)client_version; // reserved for future version negotiation
    return make_response(id, result).dump() + "\n";
}

// ===========================================================================
// tools/list
// ===========================================================================

std::string MCPMethods::_handle_tools_list(const json &p_req) const {
    json id = get_id(p_req);

    std::vector<ToolInfo> tools;
    if (m_tool_registry) {
        m_tool_registry->list_tools(tools);
    }

    json tools_arr = json::array();
    for (const auto &t : tools) {
        // Parse the input_schema string into a JSON object
        json schema;
        try {
            schema = json::parse(t.input_schema);
        } catch (...) {
            schema = json::object();
        }

        json tool_obj = {
            {"name", t.name},
            {"description", t.description},
            {"inputSchema", schema}
        };
        tools_arr.push_back(tool_obj);
    }

    json result = {{"tools", tools_arr}};
    return make_response(id, result).dump() + "\n";
}

// ===========================================================================
// tools/call
// ===========================================================================

std::string MCPMethods::_handle_tools_call(const json &p_req) const {
    json id = get_id(p_req);

    // Extract params
    json params = get_params(p_req);
    std::string tool_name = params.value("name", "");

    if (tool_name.empty()) {
        _log("error", "tools/call", "", "error", 0, "missing 'name'");
        return make_error(id, -32602, "Invalid params: missing 'name'").dump() + "\n";
    }

    // Look up tool
    ToolInfo tool_info;
    ToolHandler handler = nullptr;
    if (!m_tool_registry || !m_tool_registry->get_tool(tool_name.c_str(), tool_info, handler)) {
        _log("warn", "tools/call", tool_name, "error", 0, "tool not registered");
        return make_error(id, -32602, "Tool not found: " + tool_name).dump() + "\n";
    }

    // Check whether the panel has disabled this tool. Report the same error
    // shape a client would get for a truly missing method, so the AI
    // gracefully falls back instead of crashing.
    if (!m_tool_registry->is_tool_enabled(tool_name)) {
        _log("warn", "tools/call", tool_name, "skipped", 0, "tool disabled in panel");
        return make_error(id, -32601, "Method not found: " + tool_name).dump() + "\n";
    }

    // Get arguments as raw JSON string for the handler
    std::string args_json;
    if (params.contains("arguments")) {
        args_json = params["arguments"].dump();
    } else {
        args_json = "{}";
    }

    // Truncate args preview for the log (avoid huge entries).
    std::string args_preview = args_json;
    if (args_preview.size() > 200) {
        args_preview.resize(200);
        args_preview += "...";
    }

    // Execute the handler
    int64_t t0 = _now_ms();
    std::string result_out;
    std::string error_out;

    if (tool_info.main_thread) {
        // Dispatch to main thread via queue
        std::string captured_args = args_json;
        ToolHandler captured_handler = handler;
        result_out = MainThreadQueue::invoke_on_main([captured_handler, captured_args, &error_out]() -> std::string {
            std::string res, err;
            if (captured_handler) {
                captured_handler(captured_args, res, err);
            }
            error_out = err;
            return res;
        });
    } else {
        // Execute directly in TCP thread
        if (handler) {
            handler(args_json, result_out, error_out);
        } else {
            error_out = "Tool has no handler";
        }
    }

    int dur = (int)(_now_ms() - t0);
    m_tool_registry->increment_call_count(tool_name);

    if (!error_out.empty()) {
        // Truncate long error messages in the log preview.
        std::string err_preview = error_out;
        if (err_preview.size() > 200) {
            err_preview.resize(200);
            err_preview += "...";
        }
        _log("error", "tools/call", tool_name, "error", dur, err_preview);
        // MCP error format: return isError in content
        json result = {
            {"content", {{{"type", "text"}, {"text", error_out}}}},
            {"isError", true}
        };
        return make_response(id, result).dump() + "\n";
    }

    _log("info", "tools/call", tool_name, "ok", dur, args_preview);

    // Build MCP result
    json result = {
        {"content", {{{"type", "text"}, {"text", result_out.empty() ? "" : result_out}}}},
        {"isError", false}
    };
    return make_response(id, result).dump() + "\n";
}

// ===========================================================================
// resources/list
// ===========================================================================

std::string MCPMethods::_handle_resources_list(const json &p_req) const {
    json id = get_id(p_req);
    std::lock_guard<std::mutex> lock(m_resources_mutex);

    json arr = json::array();
    for (const auto &r : m_resources) {
        json res_obj = {
            {"uri", r.uri},
            {"name", r.name},
            {"description", r.description},
            {"mimeType", r.mime_type}
        };
        arr.push_back(res_obj);
    }

    json result = {{"resources", arr}};
    return make_response(id, result).dump() + "\n";
}

// ===========================================================================
// resources/read
// ===========================================================================

std::string MCPMethods::_handle_resources_read(const json &p_req) const {
    json id = get_id(p_req);

    json params = get_params(p_req);
    std::string uri = params.value("uri", "");

    if (uri.empty()) {
        return make_error(id, -32602, "Invalid params: missing 'uri'").dump() + "\n";
    }

    std::lock_guard<std::mutex> lock(m_resources_mutex);

    // Find the resource by URI. We support both exact matches (e.g.
    // godot://project/info) and template matches (e.g. godot://script/{path}
    // matches godot://script/res://foo.gd). The handler is then responsible
    // for extracting the path portion from the URI it receives.
    const ResourceInfo *found = nullptr;
    for (const auto &r : m_resources) {
        if (r.uri == uri) {
            found = &r;
            break;
        }
    }
    if (!found) {
        // Template fallback: find a resource whose URI contains '{' and
        // whose prefix (everything before the first '{') is a prefix of
        // the requested URI. The handler then extracts the path portion.
        for (const auto &r : m_resources) {
            size_t rbrace = r.uri.find('{');
            if (rbrace != std::string::npos) {
                std::string prefix = r.uri.substr(0, rbrace);
                if (uri.size() > prefix.size() &&
                    uri.compare(0, prefix.size(), prefix) == 0) {
                    found = &r;
                    break;
                }
            }
        }
    }

    if (!found) {
        return make_error(id, -32602, "Resource not found: " + uri).dump() + "\n";
    }

    // Get resource content
    std::string text_content;
    if (found->read_handler) {
        found->read_handler(uri, text_content);
    }

    json result = {
        {"contents", {
            {{"uri", uri}, {"mimeType", found->mime_type}, {"text", text_content}}
        }}
    };
    return make_response(id, result).dump() + "\n";
}

// ===========================================================================
// prompts/list
// ===========================================================================

std::string MCPMethods::_handle_prompts_list(const json &p_req) const {
    json id = get_id(p_req);
    std::lock_guard<std::mutex> lock(m_prompts_mutex);

    json arr = json::array();
    for (const auto &e : m_prompts) {
        json args_arr = json::array();
        for (const auto &a : e.info.arguments) {
            args_arr.push_back({
                {"name", a.name},
                {"description", a.description},
                {"required", a.required}
            });
        }
        arr.push_back({
            {"name", e.info.name},
            {"description", e.info.description},
            {"arguments", args_arr}
        });
    }

    json result = {{"prompts", arr}};
    return make_response(id, result).dump() + "\n";
}

// ===========================================================================
// prompts/get
// ===========================================================================

std::string MCPMethods::_handle_prompts_get(const json &p_req) const {
    json id = get_id(p_req);

    json params = get_params(p_req);
    std::string name = params.value("name", "");
    if (name.empty()) {
        return make_error(id, -32602, "Invalid params: missing 'name'").dump() + "\n";
    }

    // Collect arguments
    std::vector<std::pair<std::string, std::string>> args;
    if (params.contains("arguments") && params["arguments"].is_object()) {
        for (auto it = params["arguments"].begin(); it != params["arguments"].end(); ++it) {
            std::string val;
            if (it->is_string()) val = it->get<std::string>();
            else val = it->dump();
            args.emplace_back(it.key(), val);
        }
    }

    std::lock_guard<std::mutex> lock(m_prompts_mutex);
    const PromptEntry *found = nullptr;
    for (const auto &e : m_prompts) {
        if (e.info.name == name) {
            found = &e;
            break;
        }
    }
    if (!found) {
        return make_error(id, -32602, "Prompt not found: " + name).dump() + "\n";
    }

    std::vector<PromptMessage> messages;
    if (found->handler) {
        messages = found->handler(args);
    }

    json msgs_arr = json::array();
    for (const auto &m : messages) {
        msgs_arr.push_back({
            {"role", m.role},
            {"content", {{"type", "text"}, {"text", m.content}}}
        });
    }

    json result = {
        {"description", found->info.description},
        {"messages", msgs_arr}
    };
    return make_response(id, result).dump() + "\n";
}

} // namespace gear_mcp
