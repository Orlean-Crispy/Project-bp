#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "sim/net/ticket.h"

namespace sim {

// A1 control plane: a small length-prefixed binary protocol over TCP (27015),
// deliberately separate from the UDP snapshot channel. v0.1 only needs the six
// messages below; assets (maps/material tables) are listed here and fetched out
// of band, never carried inline.
// Bumped whenever the frame layout changes (v2 carries the 32-byte A5 ticket).
constexpr uint16_t kSignalProtocol = 2;

enum class SignalType : uint8_t {
    Hello = 1,
    HelloAck = 2,
    AssetManifest = 3,
    AssetReady = 4,
    MatchStart = 5,
    FullState = 6,
};

struct SignalAsset {
    uint32_t hash = 0;
    uint32_t size = 0;
    std::string name;
};

struct SignalMessage {
    SignalType type = SignalType::Hello;
    uint16_t protocol = 0;
    ClientId client_id{};   // 16 bytes, same identity the ticket MACs over
    bool has_ticket = false;
    Ticket ticket{};        // A5: only meaningful when has_ticket
    uint8_t slot = 0;
    bool accepted = false;
    uint8_t reason = 0;
    uint32_t server_tick = 0;
    uint32_t config_hash = 0;
    std::vector<SignalAsset> assets;
    std::vector<uint32_t> ready;
    std::string map;
    uint32_t seed = 0;
    uint8_t players = 0;
    uint16_t game_port = 0;  // MATCH_START: the UDP port the client joins
    uint32_t tick = 0;
};

std::vector<uint8_t> encode_signal(const SignalMessage& message);
bool decode_signal(const uint8_t* data, size_t size, SignalMessage& out, std::string& error);

// Blocking TCP stream with explicit per-call timeouts. Not thread-safe; A1 runs
// it on its own thread and hands events to the sim thread.
class TcpStream {
public:
    TcpStream() = default;
    ~TcpStream();
    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;

    bool connect(const std::string& host, uint16_t port, int timeout_ms, std::string& error);
    bool send_message(const SignalMessage& message, std::string& error);
    bool recv_message(SignalMessage& out, int timeout_ms, std::string& error);
    bool valid() const { return m_socket != kInvalid; }
    void close();

private:
    friend class TcpListener;
    explicit TcpStream(uintptr_t socket) : m_socket(socket) {}
    static constexpr uintptr_t kInvalid = static_cast<uintptr_t>(~0ull);
    uintptr_t m_socket = kInvalid;
};

class TcpListener {
public:
    TcpListener() = default;
    ~TcpListener();
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    bool listen(uint16_t port, std::string& error);
    uint16_t local_port() const { return m_port; }
    // Returns nullptr on timeout; error is set only for real failures.
    std::unique_ptr<TcpStream> accept(int timeout_ms, std::string& error);
    void close();

private:
    static constexpr uintptr_t kInvalid = static_cast<uintptr_t>(~0ull);
    uintptr_t m_socket = kInvalid;
    uint16_t m_port = 0;
};

}  // namespace sim
