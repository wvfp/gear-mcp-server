#pragma once

#include <nlohmann/json.hpp>
#include <mutex>
#include <string>

namespace gear_mcp {

// ---------------------------------------------------------------------------
// JsonRpcTcpClient — minimal JSON-RPC over TCP client with Content-Length
// framing (used by both LSP and DAP).
//
// Each call constructs a new TCP connection (handshake) — the Godot built-in
// language server and debug adapter are stateful but our operations are
// short-lived, so connection-per-request is fine and avoids the complexity
// of matching responses to requests on a shared socket.
// ---------------------------------------------------------------------------
class JsonRpcTcpClient {
public:
    /// Construct with host + port. The Godot defaults are 6005 (LSP) and
    /// 6006 (DAP), but callers may override via environment variables
    /// `GEAR_LSP_PORT` and `GEAR_DAP_PORT`.
    JsonRpcTcpClient(const std::string &p_host, int p_port);

    /// Send a JSON-RPC request and read the response. Returns true on
    /// success (r_response is the parsed JSON body). On failure r_error is
    /// populated with a human-readable description. p_id is the request id
    /// echoed back in the response.
    bool call(const std::string &p_method, const nlohmann::json &p_params,
              nlohmann::json &r_response, std::string &r_error,
              int p_id = 1, int p_timeout_ms = 5000);

    /// True if a server is reachable on host:port (used to detect whether
    /// the Godot language server / debug adapter is currently running).
    bool is_reachable(int p_timeout_ms = 500);

    int port() const { return m_port; }
    const std::string &host() const { return m_host; }

private:
    std::string m_host;
    int m_port;
};

} // namespace gear_mcp
