#include "mcp_server.h"
#include "mcp_methods.h"
#include "tcp_server.h"
#include "tool_registry.h"

#include <nlohmann/json.hpp>
#include <cstdio>

namespace gear_mcp {

MCPServer::MCPServer() {
    m_tool_registry = new ToolRegistry();
    m_methods = new MCPMethods(m_tool_registry);
    m_tcp_server = new TCPServer();
}

MCPServer::~MCPServer() {
    stop();
    delete m_tcp_server;
    delete m_methods;
    delete m_tool_registry;
}

bool MCPServer::start() {
    if (m_running) return true;

    TCPServer::MessageHandler handler = [this](const std::string &p_request) -> std::string {
        return _handle_message(p_request);
    };

    if (!m_tcp_server->start(8510, handler)) {
        std::fprintf(stderr, "[Gear MCP] Failed to start TCP server\n");
        return false;
    }

    m_running = true;
    return true;
}

void MCPServer::stop() {
    if (!m_running) return;
    m_running = false;
    if (m_tcp_server) m_tcp_server->stop();
}

int MCPServer::get_port() const {
    return m_tcp_server ? m_tcp_server->get_port() : 0;
}

int MCPServer::get_connected_clients() const {
    return m_tcp_server ? m_tcp_server->connected_clients() : 0;
}

std::string MCPServer::_handle_message(const std::string &p_request) {
    using json = nlohmann::json;

    if (p_request.empty() || p_request.find_first_not_of(" \t\r\n") == std::string::npos) {
        return {};
    }

    try {
        // Parse the JSON-RPC request
        json req;
        try {
            req = json::parse(p_request);
        } catch (const json::parse_error &e) {
            json err_resp = {
                {"jsonrpc", "2.0"},
                {"id", nullptr},
                {"error", {{"code", -32700}, {"message", std::string("Parse error: ") + e.what()}}}
            };
            return err_resp.dump() + "\n";
        }

        // Validate JSON-RPC 2.0
        if (!req.contains("method") || !req["method"].is_string()) {
            json id_val = req.contains("id") ? req["id"] : json(nullptr);
            json err_resp = {
                {"jsonrpc", "2.0"},
                {"id", id_val},
                {"error", {{"code", -32600}, {"message", "Invalid Request: missing 'method'"}}}
            };
            return err_resp.dump() + "\n";
        }

        std::string method = req["method"].get<std::string>();
        bool has_id = req.contains("id");

        // Notification (no id)
        if (!has_id) {
            return {};
        }

        // Dispatch to MCP methods
        std::string response = m_methods->handle(req);
        if (!response.empty()) {
            return response;
        }

        // Method not found
        json err_resp = {
            {"jsonrpc", "2.0"},
            {"id", req.contains("id") ? req["id"] : json(nullptr)},
            {"error", {{"code", -32601}, {"message", "Method not found: " + method}}}
        };
        return err_resp.dump() + "\n";

    } catch (const std::exception &e) {
        std::fprintf(stderr, "[Gear MCP] Exception: %s\n", e.what());
        return std::string("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error: ") + e.what() + "\"}}\n";
    } catch (...) {
        std::fprintf(stderr, "[Gear MCP] Unknown exception in _handle_message\n");
        return "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Unknown internal error\"}}\n";
    }
}

} // namespace gear_mcp
