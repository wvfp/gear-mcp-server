#include "json_rpc_client.h"

#include <cstdio>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace gear_mcp {

using json = nlohmann::json;

namespace {

// RAII socket wrapper
struct Socket {
#ifdef _WIN32
    SOCKET fd = INVALID_SOCKET;
#else
    int fd = -1;
#endif
    ~Socket() {
#ifdef _WIN32
        if (fd != INVALID_SOCKET) closesocket(fd);
#else
        if (fd >= 0) close(fd);
#endif
    }
    bool valid() const {
#ifdef _WIN32
        return fd != INVALID_SOCKET;
#else
        return fd >= 0;
#endif
    }
};

#ifdef _WIN32
struct WsaInit {
    WsaInit() {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
};
static WsaInit g_wsa_init;
#endif

bool set_recv_timeout(Socket &p_sock, int p_ms) {
#ifdef _WIN32
    DWORD t = (DWORD)p_ms;
    return setsockopt(p_sock.fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&t, sizeof(t)) == 0;
#else
    struct timeval tv;
    tv.tv_sec = p_ms / 1000;
    tv.tv_usec = (p_ms % 1000) * 1000;
    return setsockopt(p_sock.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
#endif
}

bool set_send_timeout(Socket &p_sock, int p_ms) {
#ifdef _WIN32
    DWORD t = (DWORD)p_ms;
    return setsockopt(p_sock.fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&t, sizeof(t)) == 0;
#else
    struct timeval tv;
    tv.tv_sec = p_ms / 1000;
    tv.tv_usec = (p_ms % 1000) * 1000;
    return setsockopt(p_sock.fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
#endif
}

bool send_all(Socket &p_sock, const char *p_data, size_t p_len) {
    size_t off = 0;
    while (off < p_len) {
        int n = send(p_sock.fd, p_data + off, (int)(p_len - off), 0);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}

bool recv_some(Socket &p_sock, std::string &p_buf, size_t p_need_at_least) {
    char tmp[4096];
    int n = recv(p_sock.fd, tmp, sizeof(tmp), 0);
    if (n <= 0) return false;
    p_buf.append(tmp, (size_t)n);
    return p_buf.size() >= p_need_at_least;
}

} // anonymous namespace

JsonRpcTcpClient::JsonRpcTcpClient(const std::string &p_host, int p_port)
    : m_host(p_host), m_port(p_port) {}

bool JsonRpcTcpClient::is_reachable(int p_timeout_ms) {
    Socket sock;
#ifdef _WIN32
    sock.fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!sock.valid()) return false;
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)m_port);
    addr.sin_addr.s_addr = inet_addr(m_host.c_str());
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        // Try to resolve via getaddrinfo
        addrinfo hints, *res = nullptr;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(m_host.c_str(), nullptr, &hints, &res) != 0 || !res) {
            return false;
        }
        std::memcpy(&addr, res->ai_addr, sizeof(addr));
        freeaddrinfo(res);
    }
#else
    sock.fd = socket(AF_INET, SOCK_STREAM, 0);
    if (!sock.valid()) return false;
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)m_port);
    if (inet_pton(AF_INET, m_host.c_str(), &addr.sin_addr) != 1) {
        addrinfo hints, *res = nullptr;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(m_host.c_str(), nullptr, &hints, &res) != 0 || !res) {
            return false;
        }
        std::memcpy(&addr, res->ai_addr, sizeof(addr));
        freeaddrinfo(res);
    }
#endif
    set_send_timeout(sock, p_timeout_ms);
    set_recv_timeout(sock, p_timeout_ms);
    int rc = connect(sock.fd, (sockaddr *)&addr, sizeof(addr));
    return rc == 0;
}

bool JsonRpcTcpClient::call(const std::string &p_method, const json &p_params,
                             json &r_response, std::string &r_error,
                             int p_id, int p_timeout_ms) {
    // Build request
    json req = {
        {"jsonrpc", "2.0"},
        {"id", p_id},
        {"method", p_method}
    };
    if (!p_params.is_null()) req["params"] = p_params;
    std::string body = req.dump();

    // Connect
    Socket sock;
#ifdef _WIN32
    sock.fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!sock.valid()) { r_error = "socket() failed"; return false; }
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)m_port);
    addr.sin_addr.s_addr = inet_addr(m_host.c_str());
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        addrinfo hints, *res = nullptr;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(m_host.c_str(), nullptr, &hints, &res) != 0 || !res) {
            r_error = "getaddrinfo() failed for " + m_host;
            return false;
        }
        std::memcpy(&addr, res->ai_addr, sizeof(addr));
        freeaddrinfo(res);
    }
#else
    sock.fd = socket(AF_INET, SOCK_STREAM, 0);
    if (!sock.valid()) { r_error = "socket() failed"; return false; }
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)m_port);
    if (inet_pton(AF_INET, m_host.c_str(), &addr.sin_addr) != 1) {
        addrinfo hints, *res = nullptr;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(m_host.c_str(), nullptr, &hints, &res) != 0 || !res) {
            r_error = "getaddrinfo() failed for " + m_host;
            return false;
        }
        std::memcpy(&addr, res->ai_addr, sizeof(addr));
        freeaddrinfo(res);
    }
#endif
    set_send_timeout(sock, p_timeout_ms);
    set_recv_timeout(sock, p_timeout_ms);

    int rc = connect(sock.fd, (sockaddr *)&addr, sizeof(addr));
    if (rc != 0) { r_error = "connect() to " + m_host + ":" + std::to_string(m_port) + " failed"; return false; }

    // Send framed message
    std::ostringstream frame;
    frame << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    std::string frame_str = frame.str();
    if (!send_all(sock, frame_str.data(), frame_str.size())) {
        r_error = "send() failed";
        return false;
    }

    // Read headers
    std::string buf;
    size_t header_end = std::string::npos;
    int safety = 0;
    while (header_end == std::string::npos && safety++ < 50) {
        if (!recv_some(sock, buf, 4)) {
            r_error = "recv() failed or timed out reading headers";
            return false;
        }
        size_t pos = buf.find("\r\n\r\n");
        if (pos != std::string::npos) header_end = pos;
    }
    if (header_end == std::string::npos) {
        r_error = "Malformed response: no header terminator";
        return false;
    }

    // Parse Content-Length
    std::string headers = buf.substr(0, header_end);
    size_t cl_pos = headers.find("Content-Length:");
    if (cl_pos == std::string::npos) cl_pos = headers.find("content-length:");
    if (cl_pos == std::string::npos) {
        r_error = "Response missing Content-Length header";
        return false;
    }
    cl_pos += std::strlen("Content-Length:");
    while (cl_pos < headers.size() && (headers[cl_pos] == ' ' || headers[cl_pos] == '\t')) cl_pos++;
    size_t cl_end = headers.find("\r\n", cl_pos);
    std::string cl_str = headers.substr(cl_pos, cl_end - cl_pos);
    int content_length = std::atoi(cl_str.c_str());
    if (content_length <= 0) { r_error = "Invalid Content-Length: " + cl_str; return false; }

    // Read body
    size_t body_start = header_end + 4;
    while (buf.size() < body_start + (size_t)content_length && safety++ < 500) {
        if (!recv_some(sock, buf, 1)) {
            r_error = "recv() failed or timed out reading body";
            return false;
        }
    }
    std::string resp_body = buf.substr(body_start, (size_t)content_length);

    try {
        r_response = json::parse(resp_body);
    } catch (...) {
        r_error = "Response body is not valid JSON: " + resp_body.substr(0, 200);
        return false;
    }
    return true;
}

} // namespace gear_mcp
