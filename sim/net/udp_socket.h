#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sim {

// One non-blocking UDP socket. Shared by the connected client transport and the
// server hub; address handling stays in the caller.
class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool open(uint16_t local_port, std::string& error);  // 0 = ephemeral
    bool send_to(const uint8_t* data, std::size_t size, const std::string& host, uint16_t port);
    bool recv_from(std::vector<uint8_t>& out, std::string& host, uint16_t& port);

    bool valid() const;
    uint16_t local_port() const { return m_local_port; }

private:
    static constexpr uintptr_t kInvalid = ~static_cast<uintptr_t>(0);
    uintptr_t m_socket = kInvalid;
    uint16_t m_local_port = 0;
};

}  // namespace sim
