#include "sim/net/udp_socket.h"

#include <cstring>

#include "sim/net/winsock.h"

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

void close_socket(Socket socket) { closesocket(socket); }
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;

void close_socket(Socket socket) { ::close(socket); }
#endif

bool make_address(const std::string& host, uint16_t port, sockaddr_in& out) {
    std::memset(&out, 0, sizeof(out));
    out.sin_family = AF_INET;
    out.sin_port = htons(port);
    return inet_pton(AF_INET, host.c_str(), &out.sin_addr) == 1;
}

std::string address_to_string(const sockaddr_in& addr) {
    char text[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &addr.sin_addr, text, sizeof(text));
    return text;
}

}  // namespace

UdpSocket::~UdpSocket() {
    if (m_socket != kInvalid) close_socket(static_cast<Socket>(m_socket));
}

bool UdpSocket::valid() const { return m_socket != kInvalid; }

bool UdpSocket::open(uint16_t local_port, std::string& error) {
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

bool UdpSocket::send_to(const uint8_t* data, std::size_t size, const std::string& host,
                        uint16_t port) {
    if (!valid() || size == 0) return false;
    sockaddr_in peer{};
    if (!make_address(host, port, peer)) return false;
    return ::sendto(static_cast<Socket>(m_socket), reinterpret_cast<const char*>(data),
                    static_cast<int>(size), 0, reinterpret_cast<sockaddr*>(&peer),
                    sizeof(peer)) >= 0;
}

bool UdpSocket::recv_from(std::vector<uint8_t>& out, std::string& host, uint16_t& port) {
    if (!valid()) return false;
    uint8_t buffer[2048];
    sockaddr_in from{};
    socklen_t from_length = sizeof(from);
    const int received =
        ::recvfrom(static_cast<Socket>(m_socket), reinterpret_cast<char*>(buffer),
                   sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&from), &from_length);
    if (received <= 0) return false;
    out.assign(buffer, buffer + received);
    host = address_to_string(from);
    port = ntohs(from.sin_port);
    return true;
}

}  // namespace sim
