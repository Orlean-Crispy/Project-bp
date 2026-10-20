#include "sim/net/signaling.h"

#include <algorithm>
#include <cstring>

#include "sim/net/bytes.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace sim {
namespace {

constexpr uint16_t kMaxFrame = 65535;

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kBadSocket = INVALID_SOCKET;

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

void close_socket(Socket s) { closesocket(s); }
int last_error() { return WSAGetLastError(); }
bool would_block(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
bool timed_out(int e) { return e == WSAETIMEDOUT || e == WSAEWOULDBLOCK; }

void set_receive_timeout(Socket s, int ms) {
    DWORD timeout = static_cast<DWORD>(std::max(ms, 1));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
               sizeof(timeout));
}

void set_send_timeout(Socket s, int ms) {
    DWORD timeout = static_cast<DWORD>(std::max(ms, 1));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout),
               sizeof(timeout));
}

bool set_blocking(Socket s, bool blocking) {
    u_long mode = blocking ? 0 : 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
}

bool start_connect(Socket s, const sockaddr_in& addr) {
    return ::connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0;
}
#else
using Socket = int;
constexpr Socket kBadSocket = -1;

bool ensure_winsock(std::string&) { return true; }
void close_socket(Socket s) { ::close(s); }
int last_error() { return errno; }
bool would_block(int e) { return e == EINPROGRESS || e == EAGAIN; }
bool timed_out(int e) { return e == EAGAIN || e == EWOULDBLOCK; }

void set_receive_timeout(Socket s, int ms) {
    timeval tv{};
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

void set_send_timeout(Socket s, int ms) {
    timeval tv{};
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

bool set_blocking(Socket s, bool blocking) {
    const int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(s, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK)) == 0;
}

bool start_connect(Socket s, const sockaddr_in& addr) {
    return ::connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0;
}
#endif

void set_nodelay(Socket s) {
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
}

bool make_address(const std::string& host, uint16_t port, sockaddr_in& out) {
    std::memset(&out, 0, sizeof(out));
    out.sin_family = AF_INET;
    out.sin_port = htons(port);
    return inet_pton(AF_INET, host.c_str(), &out.sin_addr) == 1;
}

bool send_all(Socket s, const uint8_t* data, size_t size, std::string& error) {
    size_t sent = 0;
    while (sent < size) {
        const int rc = ::send(s, reinterpret_cast<const char*>(data + sent),
                              static_cast<int>(size - sent), 0);
        if (rc <= 0) {
            const int e = last_error();
            error = timed_out(e) ? "send timeout" : "send failed";
            return false;
        }
        sent += static_cast<size_t>(rc);
    }
    return true;
}

bool recv_all(Socket s, uint8_t* data, size_t size, std::string& error) {
    size_t got = 0;
    while (got < size) {
        const int rc = ::recv(s, reinterpret_cast<char*>(data + got),
                              static_cast<int>(size - got), 0);
        if (rc == 0) {
            error = "connection closed";
            return false;
        }
        if (rc < 0) {
            const int e = last_error();
            error = timed_out(e) ? "receive timeout" : "receive failed";
            return false;
        }
        got += static_cast<size_t>(rc);
    }
    return true;
}

void put_string(ByteWriter& w, const std::string& value) {
    const uint16_t n = static_cast<uint16_t>(std::min<size_t>(value.size(), 65535));
    w.u16(n);
    w.raw(value.data(), n);
}

void put_ticket(ByteWriter& w, const Ticket& t) {
    w.raw(t.client_id.data(), t.client_id.size());
    w.u32(t.expiry);
    w.raw(t.mac.data(), t.mac.size());
}

bool get_ticket(ByteReader& r, Ticket& t) {
    const uint8_t* id = r.bytes(t.client_id.size());
    if (id == nullptr) return false;
    std::memcpy(t.client_id.data(), id, t.client_id.size());
    t.expiry = r.u32();
    const uint8_t* mac = r.bytes(t.mac.size());
    if (mac == nullptr) return false;
    std::memcpy(t.mac.data(), mac, t.mac.size());
    return true;
}

std::string get_string(ByteReader& r) {
    const uint16_t n = r.u16();
    const uint8_t* p = r.bytes(n);
    if (p == nullptr) return {};
    return std::string(reinterpret_cast<const char*>(p), n);
}

}  // namespace

std::vector<uint8_t> encode_signal(const SignalMessage& message) {
    ByteWriter w;
    w.u8(static_cast<uint8_t>(message.type));
    switch (message.type) {
        case SignalType::Hello:
            w.u16(message.protocol);
            w.raw(message.client_id.data(), message.client_id.size());
            w.u8(message.has_ticket ? 1 : 0);
            if (message.has_ticket) put_ticket(w, message.ticket);
            break;
        case SignalType::HelloAck:
            w.u8(message.accepted ? 1 : 0);
            w.u8(message.reason);
            w.u32(message.server_tick);
            w.u32(message.config_hash);
            w.u8(message.slot);
            w.u8(message.has_ticket ? 1 : 0);
            if (message.has_ticket) put_ticket(w, message.ticket);
            break;
        case SignalType::AssetManifest:
            w.u16(static_cast<uint16_t>(message.assets.size()));
            for (const SignalAsset& asset : message.assets) {
                w.u32(asset.hash);
                w.u32(asset.size);
                put_string(w, asset.name);
            }
            break;
        case SignalType::AssetReady:
            w.u16(static_cast<uint16_t>(message.ready.size()));
            for (uint32_t hash : message.ready) w.u32(hash);
            break;
        case SignalType::MatchStart:
            put_string(w, message.map);
            w.u32(message.seed);
            w.u8(message.players);
            w.u16(message.game_port);
            break;
        case SignalType::FullState:
            w.u32(message.tick);
            break;
    }
    return w.data();
}

bool decode_signal(const uint8_t* data, size_t size, SignalMessage& out, std::string& error) {
    ByteReader r(data, size);
    SignalMessage m;
    m.type = static_cast<SignalType>(r.u8());
    switch (m.type) {
        case SignalType::Hello: {
            m.protocol = r.u16();
            const uint8_t* id = r.bytes(m.client_id.size());
            if (id == nullptr) break;
            std::memcpy(m.client_id.data(), id, m.client_id.size());
            m.has_ticket = r.u8() != 0;
            if (m.has_ticket && !get_ticket(r, m.ticket)) break;
            break;
        }
        case SignalType::HelloAck:
            m.accepted = r.u8() != 0;
            m.reason = r.u8();
            m.server_tick = r.u32();
            m.config_hash = r.u32();
            m.slot = r.u8();
            m.has_ticket = r.u8() != 0;
            if (m.has_ticket) get_ticket(r, m.ticket);
            break;
        case SignalType::AssetManifest: {
            const uint16_t count = r.u16();
            m.assets.reserve(count);
            for (uint16_t i = 0; i < count; ++i) {
                SignalAsset asset;
                asset.hash = r.u32();
                asset.size = r.u32();
                asset.name = get_string(r);
                m.assets.push_back(std::move(asset));
            }
            break;
        }
        case SignalType::AssetReady: {
            const uint16_t count = r.u16();
            m.ready.reserve(count);
            for (uint16_t i = 0; i < count; ++i) m.ready.push_back(r.u32());
            break;
        }
        case SignalType::MatchStart:
            m.map = get_string(r);
            m.seed = r.u32();
            m.players = r.u8();
            m.game_port = r.u16();
            break;
        case SignalType::FullState:
            m.tick = r.u32();
            break;
        default:
            error = "unknown signal type";
            return false;
    }
    if (!r.ok()) {
        error = "truncated signal";
        return false;
    }
    out = std::move(m);
    return true;
}

TcpStream::~TcpStream() { close(); }

void TcpStream::close() {
    if (m_socket != kInvalid) close_socket(static_cast<Socket>(m_socket));
    m_socket = kInvalid;
}

bool TcpStream::connect(const std::string& host, uint16_t port, int timeout_ms,
                        std::string& error) {
    if (!ensure_winsock(error)) return false;
    sockaddr_in addr;
    if (!make_address(host, port, addr)) {
        error = "invalid host";
        return false;
    }
    const Socket s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kBadSocket) {
        error = "socket() failed";
        return false;
    }
    if (!set_blocking(s, false)) {
        error = "nonblocking mode failed";
        close_socket(s);
        return false;
    }
    if (!start_connect(s, addr)) {
        const int e = last_error();
        if (!would_block(e)) {
            error = "connect failed";
            close_socket(s);
            return false;
        }
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(s, &writable);
        timeval tv{};
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
#ifdef _WIN32
        const int selected = select(0, nullptr, &writable, nullptr, &tv);
#else
        const int selected = select(s + 1, nullptr, &writable, nullptr, &tv);
#endif
        if (selected <= 0) {
            error = "connect timeout";
            close_socket(s);
            return false;
        }
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &len);
        if (so_error != 0) {
            error = "connect refused";
            close_socket(s);
            return false;
        }
    }
    if (!set_blocking(s, true)) {
        error = "blocking mode failed";
        close_socket(s);
        return false;
    }
    set_nodelay(s);
    set_send_timeout(s, 2000);
    m_socket = static_cast<uintptr_t>(s);
    return true;
}

bool TcpStream::send_message(const SignalMessage& message, std::string& error) {
    if (!valid()) {
        error = "not connected";
        return false;
    }
    const std::vector<uint8_t> payload = encode_signal(message);
    if (payload.size() > kMaxFrame) {
        error = "signal frame too large";
        return false;
    }
    const uint8_t header[2] = {static_cast<uint8_t>(payload.size() & 0xFF),
                               static_cast<uint8_t>(payload.size() >> 8)};
    const Socket s = static_cast<Socket>(m_socket);
    return send_all(s, header, sizeof(header), error) &&
           send_all(s, payload.data(), payload.size(), error);
}

bool TcpStream::recv_message(SignalMessage& out, int timeout_ms, std::string& error) {
    if (!valid()) {
        error = "not connected";
        return false;
    }
    const Socket s = static_cast<Socket>(m_socket);
    set_receive_timeout(s, timeout_ms);
    uint8_t header[2];
    if (!recv_all(s, header, sizeof(header), error)) return false;
    const uint16_t size = static_cast<uint16_t>(header[0]) |
                          static_cast<uint16_t>(header[1] << 8);
    if (size == 0) {
        error = "empty signal frame";
        return false;
    }
    std::vector<uint8_t> payload(size);
    if (!recv_all(s, payload.data(), payload.size(), error)) return false;
    return decode_signal(payload.data(), payload.size(), out, error);
}

TcpListener::~TcpListener() { close(); }

void TcpListener::close() {
    if (m_socket != kInvalid) close_socket(static_cast<Socket>(m_socket));
    m_socket = kInvalid;
}

bool TcpListener::listen(uint16_t port, std::string& error) {
    if (!ensure_winsock(error)) return false;
    const Socket s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kBadSocket) {
        error = "socket() failed";
        return false;
    }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(s, 4) != 0) {
        error = "bind/listen failed";
        close_socket(s);
        return false;
    }
    sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len);
    m_port = ntohs(bound.sin_port);
    m_socket = static_cast<uintptr_t>(s);
    return true;
}

std::unique_ptr<TcpStream> TcpListener::accept(int timeout_ms, std::string& error) {
    if (m_socket == kInvalid) {
        error = "listener not open";
        return nullptr;
    }
    const Socket s = static_cast<Socket>(m_socket);
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(s, &readable);
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
#ifdef _WIN32
    const int selected = select(0, &readable, nullptr, nullptr, &tv);
#else
    const int selected = select(s + 1, &readable, nullptr, nullptr, &tv);
#endif
    if (selected == 0) return nullptr;
    if (selected < 0) {
        error = "select failed";
        return nullptr;
    }
    const Socket client = ::accept(s, nullptr, nullptr);
    if (client == kBadSocket) {
        error = "accept failed";
        return nullptr;
    }
    set_nodelay(client);
    set_send_timeout(client, 2000);
    return std::unique_ptr<TcpStream>(new TcpStream(static_cast<uintptr_t>(client)));
}

}  // namespace sim
