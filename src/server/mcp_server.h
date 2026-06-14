#pragma once

#include <atomic>
#include <string>

namespace gear_mcp {

class TCPServer;
class ToolRegistry;
class MCPMethods;

// ---------------------------------------------------------------------------
// MCPServer — orchestrator that wires the TCP server, JSON-RPC parser,
// ToolRegistry, and MCP method handlers together.
// ---------------------------------------------------------------------------
class MCPServer {
public:
    MCPServer();
    ~MCPServer();

    /// Start the MCP server on TCP port 8510.
    bool start();

    /// Stop the server gracefully.
    void stop();

    bool is_running() const { return m_running; }

    ToolRegistry *get_tool_registry() { return m_tool_registry; }
    MCPMethods *get_methods() { return m_methods; }

    /// Port the TCP server is bound to (0 if not started).
    int get_port() const;

    /// Number of currently connected MCP clients.
    int get_connected_clients() const;

private:
    std::string _handle_message(const std::string &p_request);

    TCPServer *m_tcp_server = nullptr;
    ToolRegistry *m_tool_registry = nullptr;
    MCPMethods *m_methods = nullptr;
    std::atomic<bool> m_running{false};
};

} // namespace gear_mcp
