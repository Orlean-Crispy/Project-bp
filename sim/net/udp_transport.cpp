#include "sim/net/udp_transport.h"

#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace sim {
namespace {

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;

bool ensure_winsock(std::string& error) {
    static const bool ready = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!ready) {
        error = "WSAStartup failed";
        return false;
    }
    return true;
}

void close_socket(Socket socket) { closesocket(socket); }
bool would_block() { return WSAGetLastError() == WSAEWOULDBLOCK; }
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;

bool ensure_winsock(std::string&) { return true; }
void close_socket(Socket socket) { ::close(socket); }
bool would_block() { return errno == EAGAIN || errno == EWOULDBLOCK; }
#endif

bool make_address(const std::string& host, uint16_t port, sockaddr_in& out) {
    std::memset(&out, 0, sizeof(out));
    out.sin_family = AF_INET;
    out.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &out.sin_addr) != 1) return false;
    return true;
}

std::string address_to_string(const sockaddr_in& addr) {
    char text[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &addr.sin_addr, text, sizeof(text));
    return text;
}

}  // namespace

UdpTransport::~UdpTransport() {
    if (m_socket != kInvalid) close_socket(static_cast<Socket>(m_socket));
}

bool UdpTransport::open(uint16_t local_port, std::string& error) {
    if (!ensure_winsock(error)) return false;
    m_socket = static_cast<uintptr_t>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (m_socket == static_cast<uintptr_t>(kInvalidSocket)) {
        error = "socket() failed";
        return false;
    }

#ifdef _WIN32
    u_long nonblocking = 1;
    ioctlsocket(static_cast<Socket>(m_socket), FIONBIO, &nonblocking);
#else
    const int flags = fcntl(static_cast<Socket>(m_socket), F_GETFL, 0);
    fcntl(static_cast<Socket>(m_socket), F_SETFL, flags | O_NONBLOCK);
#endif

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(local_port);
    if (::bind(static_cast<Socket>(m_socket), reinterpret_cast<sockaddr*>(&local),
               sizeof(local)) != 0) {
        error = "bind() failed";
        return false;
    }

    sockaddr_in bound{};
    socklen_t length = sizeof(bound);
    if (::getsockname(static_cast<Socket>(m_socket), reinterpret_cast<sockaddr*>(&bound),
                      &length) == 0) {
        m_local_port = ntohs(bound.sin_port);
    }
    return true;
}

bool UdpTransport::connect_peer(const std::string& host, uint16_t port, std::string& error) {
    if (!valid()) {
        error = "socket is not open";
        return false;
    }
    sockaddr_in peer{};
    if (!make_address(host, port, peer)) {
        error = "invalid peer address";
        return false;
    }
    if (::connect(static_cast<Socket>(m_socket), reinterpret_cast<sockaddr*>(&peer),
                  sizeof(peer)) != 0) {
        error = "connect() failed";
        return false;
    }
    m_peer_addr = peer.sin_addr.s_addr;
    m_peer_port = peer.sin_port;
    m_has_peer = true;
    return true;
}

bool UdpTransport::adopt_peer(const std::string& host, uint16_t port, std::string& error) {
    sockaddr_in peer{};
    if (!make_address(host, port, peer)) {
        error = "invalid peer address";
        return false;
    }
    m_peer_addr = peer.sin_addr.s_addr;
    m_peer_port = peer.sin_port;
    m_has_peer = true;
    return true;
}

void UdpTransport::send(const std::vector<uint8_t>& data) {
    if (!valid() || !m_has_peer || data.empty()) return;
    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    peer.sin_addr.s_addr = m_peer_addr;
    peer.sin_port = m_peer_port;
    ::sendto(static_cast<Socket>(m_socket), reinterpret_cast<const char*>(data.data()),
             static_cast<int>(data.size()), 0, reinterpret_cast<sockaddr*>(&peer),
             sizeof(peer));
}

bool UdpTransport::recv(std::vector<uint8_t>& out) {
    std::string host;
    uint16_t port = 0;
    return recv_from(out, host, port);
}

bool UdpTransport::recv_from(std::vector<uint8_t>& out, std::string& host, uint16_t& port) {
    if (!valid()) return false;
    uint8_t buffer[2048];
    sockaddr_in from{};
    socklen_t from_length = sizeof(from);
    const int received =
        ::recvfrom(static_cast<Socket>(m_socket), reinterpret_cast<char*>(buffer),
                   sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&from), &from_length);
    if (received == static_cast<int>(SOCKET_ERROR) || received <= 0) {
        (void)would_block();
        return false;
    }
    out.assign(buffer, buffer + received);
    host = address_to_string(from);
    port = ntohs(from.sin_port);
    return true;
}

}  // namespace sim
