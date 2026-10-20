#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sim {

// A2: a minimal reliable ordered stream that can share a socket with the
// snapshot channel. Sequence space is independent of snapshot seq. Window 32,
// cumulative ACK + 32-bit bitmap of the packets above it, RFC 6298 RTO.
class ReliableSender {
public:
    explicit ReliableSender(uint16_t window = 32);

    bool send(const std::vector<uint8_t>& payload, uint32_t now_ms);
    // Emits due packets as [u8 1][u16 seq][payload].
    void poll(uint32_t now_ms, std::vector<std::vector<uint8_t>>& out);
    void on_ack(uint16_t ack, uint32_t bitmap, uint32_t now_ms);

    bool dead() const { return m_dead; }
    int retransmits() const { return m_retransmits; }
    int in_flight() const;

private:
    struct Entry {
        uint16_t seq = 0;
        std::vector<uint8_t> payload;
        uint32_t last_sent = 0;
        int retries = 0;
        bool sent = false;
    };
    uint16_t m_window;
    uint16_t m_next_seq = 1;  // 0 means nothing acknowledged
    std::vector<Entry> m_entries;
    uint32_t m_srtt = 0;
    uint32_t m_rttvar = 0;
    uint32_t m_rto = 250;
    bool m_dead = false;
    int m_retransmits = 0;
};

class ReliableReceiver {
public:
    explicit ReliableReceiver(uint16_t window = 32);
    void on_packet(const uint8_t* data, size_t size,
                   std::vector<std::vector<uint8_t>>& delivered);
    void make_ack(std::vector<uint8_t>& out) const;

private:
    // The ACK bitmap is 32 bits wide, so the window is capped at 32 and a whole
    // window of out-of-order packets fits in a fixed array. Slots are addressed
    // by seq % kWindow and validated by the stored seq, so make_ack is a fixed
    // 32-iteration scan instead of a std::map walk on every packet (audit R19).
    static constexpr uint16_t kWindow = 32;

    struct Pending {
        uint16_t seq = 0;
        bool valid = false;
        std::vector<uint8_t> payload;
    };

    uint16_t m_window;
    uint16_t m_next = 1;
    Pending m_pending[kWindow];
};

uint32_t crc32(const uint8_t* data, size_t size);

// Splits data into <= chunk_size payloads. Wire chunk:
// [u16 index][u16 total][u32 crc32(payload)][payload].
std::vector<std::vector<uint8_t>> chunk_config(const std::vector<uint8_t>& data,
                                               size_t chunk_size = 512);

class ConfigAssembler {
public:
    bool add(const uint8_t* data, size_t size, std::string& error);
    bool complete() const {
        if (m_total == 0 || m_chunks.size() != m_total) return false;
        for (const auto& chunk : m_chunks) {
            if (chunk.empty()) return false;
        }
        return true;
    }
    uint16_t total() const { return m_total; }
    std::vector<uint8_t> assemble() const;

private:
    uint16_t m_total = 0;
    std::vector<std::vector<uint8_t>> m_chunks;
};

}  // namespace sim
