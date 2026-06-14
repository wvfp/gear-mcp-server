#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace gear_mcp {

// ---------------------------------------------------------------------------
// Platform-independent TCP server.
//
// Uses Winsock2 on Windows, BSD sockets on POSIX (Linux/macOS).
// Binds to 127.0.0.1 only. Each accepted connection is handled in its own
// platform thread (CreateThread / pthread_create).
//
// The server reads \n-delimited JSON messages and forwards complete lines
// to the registered MessageHandler callback. The callback returns the
// response string, which is then written back to the same socket.
// ---------------------------------------------------------------------------
class TCPServer {
public:
    /// Signature: receives a request string, returns a response string.
    using MessageHandler = std::function<std::string(const std::string &p_request)>;

    TCPServer();
    ~TCPServer();

    /// Start listening on 127.0.0.1:p_port.
    bool start(uint16_t p_port, MessageHandler p_handler);

    /// Stop the server, close all connections, join threads.
    void stop();

    /// Returns true if the server is currently running.
    bool is_running() const { return m_running; }

private:
#ifdef _WIN32
    using socket_t = size_t;
    static constexpr socket_t INVALID_SOCK = (socket_t)(~0);
#else
    using socket_t = int;
    static constexpr socket_t INVALID_SOCK = -1;
#endif

    bool _create_listen_socket();

#ifdef _WIN32
    static unsigned long __stdcall _accept_thread_tramp(void *p_self);
#else
    static void *_accept_thread_tramp(void *p_self);
#endif

    void _accept_thread();

    std::atomic<bool> m_running{false};
    uint16_t m_port = 8510;
    socket_t m_listen_fd = INVALID_SOCK;
    MessageHandler m_handler;
};

} // namespace gear_mcp
