#include "tcp_server.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Self-pointer: the per-instance counter the client worker threads decrement on exit.
// This is a process-wide static, but it works because there's only one TCPServer
// in the editor. If the design ever needs multiple servers, refactor to a per-
// instance refcounted handle.
namespace { gear_mcp::TCPServer *g_tcp_server_for_counter = nullptr; }

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET platform_socket_t;
#define PLT_SOCKET_ERROR SOCKET_ERROR
#define PLT_INVALID_SOCKET INVALID_SOCKET
#define PLT_LAST_ERROR WSAGetLastError()
static const char *socket_strerror(int err) {
    static char buf[64];
    std::snprintf(buf, sizeof(buf), "Winsock error %d", err);
    return buf;
}
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int platform_socket_t;
#define PLT_SOCKET_ERROR (-1)
#define PLT_INVALID_SOCKET (-1)
#define PLT_LAST_ERROR errno
static const char *socket_strerror(int err) {
    return std::strerror(err);
}
#endif

namespace gear_mcp {

// ===========================================================================
// Platform thread helpers
// ===========================================================================

#ifdef _WIN32
using thread_handle_t = void *;

static thread_handle_t create_thread(unsigned long (__stdcall *p_func)(void *), void *p_arg) {
    HANDLE h = CreateThread(NULL, 0, p_func, p_arg, 0, NULL);
    if (h) CloseHandle(h);
    return h;
}
#else
using thread_handle_t = pthread_t;

static thread_handle_t create_thread(void *(*p_func)(void *), void *p_arg) {
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&thread, &attr, p_func, p_arg) != 0) thread = 0;
    pthread_attr_destroy(&attr);
    return thread;
}
#endif

// ===========================================================================
// Client context
// ===========================================================================

struct ClientContext {
    platform_socket_t fd;
    TCPServer::MessageHandler handler;
    ClientContext(platform_socket_t p_fd, TCPServer::MessageHandler p_handler)
        : fd(p_fd), handler(p_handler) {}
};

// ===========================================================================
// Construction / Destruction
// ===========================================================================

TCPServer::TCPServer() : m_running(false), m_port(8510), m_listen_fd(INVALID_SOCK) {}

TCPServer::~TCPServer() {
    stop();
}

// ===========================================================================
// Start
// ===========================================================================

bool TCPServer::start(uint16_t p_port, MessageHandler p_handler) {
    if (m_running) return false;

    m_port = p_port;
    m_handler = p_handler;

#ifdef _WIN32
    WSADATA wsaData;
    int wsa_err = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsa_err != 0) {
        std::fprintf(stderr, "[Gear MCP] WSAStartup failed: %d\n", wsa_err);
        return false;
    }
#endif

    if (!_create_listen_socket()) {
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    m_running = true;

    thread_handle_t th = create_thread(_accept_thread_tramp, this);

#ifdef _WIN32
    if (!th) {
#else
    if (th == 0) {
#endif
        std::fprintf(stderr, "[Gear MCP] Failed to create accept thread\n");
        m_running = false;
#ifdef _WIN32
        closesocket((platform_socket_t)m_listen_fd);
        WSACleanup();
#else
        ::close((int)m_listen_fd);
#endif
        m_listen_fd = INVALID_SOCK;
        return false;
    }

    std::printf("[Gear MCP] TCP server listening on 127.0.0.1:%u\n", (unsigned)p_port);
    return true;
}

// ===========================================================================
// Stop
// ===========================================================================

void TCPServer::stop() {
    if (!m_running) return;
    m_running = false;

    if (m_listen_fd != INVALID_SOCK) {
#ifdef _WIN32
        closesocket((platform_socket_t)m_listen_fd);
#else
        ::close((int)m_listen_fd);
#endif
        m_listen_fd = INVALID_SOCK;
    }

#ifdef _WIN32
    WSACleanup();
#endif

    g_tcp_server_for_counter = nullptr;
}

// ===========================================================================
// Create listen socket
// ===========================================================================

bool TCPServer::_create_listen_socket() {
#ifdef _WIN32
    platform_socket_t fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == PLT_INVALID_SOCKET) {
        std::fprintf(stderr, "[Gear MCP] socket() failed: %s\n", socket_strerror(PLT_LAST_ERROR));
        return false;
    }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    addr.sin_addr.s_addr = htonl(0x7F000001);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == PLT_SOCKET_ERROR) {
        std::fprintf(stderr, "[Gear MCP] bind() failed: %s\n", socket_strerror(PLT_LAST_ERROR));
        closesocket(fd);
        return false;
    }
    if (listen(fd, 10) == PLT_SOCKET_ERROR) {
        std::fprintf(stderr, "[Gear MCP] listen() failed: %s\n", socket_strerror(PLT_LAST_ERROR));
        closesocket(fd);
        return false;
    }
    m_listen_fd = (socket_t)fd;
    return true;
#else
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::fprintf(stderr, "[Gear MCP] socket() failed: %s\n", socket_strerror(errno));
        return false;
    }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    addr.sin_addr.s_addr = htonl(0x7F000001);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        std::fprintf(stderr, "[Gear MCP] bind() failed: %s\n", socket_strerror(errno));
        ::close(fd);
        return false;
    }
    if (listen(fd, 10) < 0) {
        std::fprintf(stderr, "[Gear MCP] listen() failed: %s\n", socket_strerror(errno));
        ::close(fd);
        return false;
    }
    m_listen_fd = (socket_t)fd;
    return true;
#endif
}

// ===========================================================================
// Thread trampolines
// ===========================================================================

#ifdef _WIN32
unsigned long __stdcall TCPServer::_accept_thread_tramp(void *p_self) {
    reinterpret_cast<TCPServer *>(p_self)->_accept_thread();
    return 0;
}
#else
void *TCPServer::_accept_thread_tramp(void *p_self) {
    reinterpret_cast<TCPServer *>(p_self)->_accept_thread();
    return nullptr;
}
#endif

// ===========================================================================
// Accept thread
// ===========================================================================

void TCPServer::_accept_thread() {
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    while (m_running) {
        platform_socket_t listen_fd = (platform_socket_t)m_listen_fd;
        if (listen_fd == PLT_INVALID_SOCKET) break;

        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        std::memset(&client_addr, 0, sizeof(client_addr));

#ifdef _WIN32
        platform_socket_t client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd == PLT_INVALID_SOCKET) {
            int err = PLT_LAST_ERROR;
            if (!m_running) break;
            if (err == WSAEINTR) continue;
            std::fprintf(stderr, "[Gear MCP] accept() failed: %s\n", socket_strerror(err));
            break;
        }
#else
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (!m_running) break;
            if (errno == EINTR) continue;
            std::fprintf(stderr, "[Gear MCP] accept() failed: %s\n", socket_strerror(errno));
            break;
        }
#endif

        ClientContext *ctx = new ClientContext(client_fd, m_handler);
        m_connected_clients.fetch_add(1);

#ifdef _WIN32
        HANDLE hThread = CreateThread(NULL, 0, [](void *p) -> DWORD {
            ClientContext *c = static_cast<ClientContext *>(p);
            platform_socket_t fd = c->fd;
            TCPServer::MessageHandler handler = c->handler;
            delete c;

            char buf[4096];
            std::string line_buffer;

            // ----------------------------------------------------------------
            // Buffer-boundary JSON parser
            //
            // We previously split messages on '\n', which broke for any
            // pretty-printed JSON containing newlines inside a string value
            // (e.g. GDScript source passed as "content"). Instead, we
            // accumulate bytes into line_buffer and extract the next
            // depth-balanced top-level {...} object, properly skipping over
            // strings (and backslash-escaped chars inside them).
            // ----------------------------------------------------------------
            auto extract_next_message = [](std::string &buf) -> std::string {
                // Skip leading whitespace
                size_t start = 0;
                while (start < buf.size() &&
                       (buf[start] == ' ' || buf[start] == '\t' ||
                        buf[start] == '\r' || buf[start] == '\n')) {
                    ++start;
                }
                if (start >= buf.size()) {
                    buf.clear();
                    return std::string();
                }
                // Non-JSON garbage up to next newline: skip
                if (buf[start] != '{') {
                    size_t nl = buf.find('\n', start);
                    if (nl == std::string::npos) {
                        buf.clear();
                        return std::string();
                    }
                    buf.erase(0, nl + 1);
                    return std::string();
                }
                // Scan for balanced '}' at depth 0, respecting strings.
                int depth = 0;
                bool in_string = false;
                bool escaped = false;
                size_t end = std::string::npos;
                for (size_t i = start; i < buf.size(); ++i) {
                    char ch = buf[i];
                    if (escaped) { escaped = false; continue; }
                    if (ch == '\\') { escaped = true; continue; }
                    if (ch == '"') { in_string = !in_string; continue; }
                    if (in_string) continue;
                    if (ch == '{') ++depth;
                    else if (ch == '}') {
                        --depth;
                        if (depth == 0) { end = i; break; }
                    }
                }
                if (end == std::string::npos) {
                    // Incomplete message: keep the partial bytes and wait
                    // for more data on the next recv().
                    std::string remaining = buf.substr(start);
                    buf = std::move(remaining);
                    return std::string();
                }
                std::string msg = buf.substr(start, end - start + 1);
                buf.erase(0, end + 1);
                return msg;
            };

            while (true) {
                int n = recv(fd, buf, sizeof(buf) - 1, 0);
                if (n <= 0) break;
                buf[n] = '\0';
                line_buffer.append(buf, (size_t)n);

                // Drain as many complete messages as the buffer holds.
                while (true) {
                    std::string msg = extract_next_message(line_buffer);
                    if (msg.empty()) break;
                    if (handler) {
                        std::string response = handler(msg);
                        if (!response.empty()) {
                            send(fd, response.data(), (int)response.size(), 0);
                        }
                    }
                }
            }
            closesocket(fd);
            return 0;
        }, ctx, 0, NULL);

        if (hThread) {
            CloseHandle(hThread);
        } else {
            std::fprintf(stderr, "[Gear MCP] Failed to create client thread\n");
            closesocket(client_fd);
            delete ctx;
            m_connected_clients.fetch_sub(1);
        }
#else
        pthread_t thread;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        auto thread_fn = [](void *p) -> void * {
            ClientContext *c = static_cast<ClientContext *>(p);
            int fd = c->fd;
            TCPServer::MessageHandler handler = c->handler;
            delete c;

            char buf[4096];
            std::string line_buffer;

            // Buffer-boundary JSON parser: extract the next depth-balanced
            // top-level {...} object, properly skipping over strings
            // (and backslash-escaped chars inside them). This makes the
            // server tolerant of pretty-printed JSON that contains literal
            // newlines inside string values.
            auto extract_next_message = [](std::string &buf) -> std::string {
                size_t start = 0;
                while (start < buf.size() &&
                       (buf[start] == ' ' || buf[start] == '\t' ||
                        buf[start] == '\r' || buf[start] == '\n')) {
                    ++start;
                }
                if (start >= buf.size()) {
                    buf.clear();
                    return std::string();
                }
                if (buf[start] != '{') {
                    size_t nl = buf.find('\n', start);
                    if (nl == std::string::npos) {
                        buf.clear();
                        return std::string();
                    }
                    buf.erase(0, nl + 1);
                    return std::string();
                }
                int depth = 0;
                bool in_string = false;
                bool escaped = false;
                size_t end = std::string::npos;
                for (size_t i = start; i < buf.size(); ++i) {
                    char ch = buf[i];
                    if (escaped) { escaped = false; continue; }
                    if (ch == '\\') { escaped = true; continue; }
                    if (ch == '"') { in_string = !in_string; continue; }
                    if (in_string) continue;
                    if (ch == '{') ++depth;
                    else if (ch == '}') {
                        --depth;
                        if (depth == 0) { end = i; break; }
                    }
                }
                if (end == std::string::npos) {
                    std::string remaining = buf.substr(start);
                    buf = std::move(remaining);
                    return std::string();
                }
                std::string msg = buf.substr(start, end - start + 1);
                buf.erase(0, end + 1);
                return msg;
            };

            while (true) {
                ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
                if (n <= 0) break;
                buf[n] = '\0';
                line_buffer.append(buf, (size_t)n);

                while (true) {
                    std::string msg = extract_next_message(line_buffer);
                    if (msg.empty()) break;
                    if (handler) {
                        std::string response = handler(msg);
                        if (!response.empty()) {
                            ::send(fd, response.data(), response.size(), 0);
                        }
                    }
                }
            }
            ::close(fd);
            return nullptr;
        };

        if (pthread_create(&thread, &attr, thread_fn, ctx) != 0) {
            std::fprintf(stderr, "[Gear MCP] Failed to create client thread\n");
            ::close(client_fd);
            delete ctx;
            m_connected_clients.fetch_sub(1);
        }
        pthread_attr_destroy(&attr);
#endif
    }
}

} // namespace gear_mcp
