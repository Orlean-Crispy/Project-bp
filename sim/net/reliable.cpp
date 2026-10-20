#include "sim/net/reliable.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace sim {
namespace {

constexpr int kMaxRetries = 5;
constexpr uint32_t kMinRto = 100;
constexpr uint32_t kMaxRto = 1000;

void put_u16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
void put_u32(std::vector<uint8_t>& v, uint32_t x) {
    put_u16(v, static_cast<uint16_t>(x & 0xFFFF));
    put_u16(v, static_cast<uint16_t>(x >> 16));
}

}  // namespace

ReliableSender::ReliableSender(uint16_t window) : m_window(window == 0 ? 1 : window) {}

int ReliableSender::in_flight() const { return static_cast<int>(m_entries.size()); }

bool ReliableSender::send(const std::vector<uint8_t>& payload, uint32_t now_ms) {
    if (m_dead || in_flight() >= m_window) return false;
    Entry e;
    e.seq = m_next_seq++;
    e.payload = payload;
    e.last_sent = now_ms;
    m_entries.push_back(std::move(e));
    return true;
}

void ReliableSender::poll(uint32_t now_ms, std::vector<std::vector<uint8_t>>& out) {
    if (m_dead) return;
    for (Entry& e : m_entries) {
        if (e.sent && now_ms - e.last_sent < m_rto) continue;
        if (e.sent) {
            ++e.retries;
            ++m_retransmits;
            if (e.retries > kMaxRetries) {
                m_dead = true;
                return;
            }
        }
        std::vector<uint8_t> pkt;
        pkt.reserve(e.payload.size() + 3);
        pkt.push_back(1);
        put_u16(pkt, e.seq);
        pkt.insert(pkt.end(), e.payload.begin(), e.payload.end());
        out.push_back(std::move(pkt));
        e.sent = true;
        e.last_sent = now_ms;
    }
}

void ReliableSender::on_ack(uint16_t ack, uint32_t bitmap, uint32_t now_ms) {
    if (ack == 0) return;  // nothing delivered yet
    for (size_t i = 0; i < m_entries.size();) {
        const Entry& e = m_entries[i];
        const uint16_t delta = static_cast<uint16_t>(e.seq - ack);
        bool acked = e.seq <= ack;
        if (!acked && delta <= 32) acked = (bitmap & (1u << (delta - 1))) != 0;
        if (!acked) {
            ++i;
            continue;
        }
        if (e.retries == 0) {
            const uint32_t sample = now_ms - e.last_sent;
            if (m_srtt == 0) {
                m_srtt = sample;
                m_rttvar = sample / 2;
            } else {
                const uint32_t diff = sample > m_srtt ? sample - m_srtt : m_srtt - sample;
                m_rttvar = (3 * m_rttvar + diff) / 4;
                m_srtt = (7 * m_srtt + sample) / 8;
            }
            const uint32_t rto = m_srtt + std::max<uint32_t>(1, 4 * m_rttvar);
            m_rto = std::min(kMaxRto, std::max(kMinRto, rto));
        }
        m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

ReliableReceiver::ReliableReceiver(uint16_t window)
    : m_window(window == 0 ? 1 : (window > kWindow ? kWindow : window)) {}

void ReliableReceiver::on_packet(const uint8_t* data, size_t size,
                                 std::vector<std::vector<uint8_t>>& delivered) {
    if (size < 3 || data[0] != 1) return;
    const uint16_t seq = static_cast<uint16_t>(data[1]) |
                         static_cast<uint16_t>(data[2] << 8);
    if (static_cast<uint16_t>(seq - m_next) > 0x8000) return;  // already delivered
    if (seq > m_next && static_cast<uint16_t>(seq - m_next) < m_window) {
        Pending& slot = m_pending[seq % kWindow];
        slot.seq = seq;
        slot.payload.assign(data + 3, data + size);
        slot.valid = true;
        return;
    }
    if (seq != m_next) return;
    delivered.emplace_back(data + 3, data + size);
    ++m_next;
    for (;;) {
        Pending& slot = m_pending[m_next % kWindow];
        if (!slot.valid || slot.seq != m_next) break;
        delivered.push_back(std::move(slot.payload));
        slot.valid = false;
        ++m_next;
    }
}

void ReliableReceiver::make_ack(std::vector<uint8_t>& out) const {
    const uint16_t ack = static_cast<uint16_t>(m_next - 1);
    out.clear();
    out.push_back(2);
    put_u16(out, ack);
    uint32_t bitmap = 0;
    // Fixed 32 slots, so this never allocates or walks a tree (audit R19). The
    // delta test keeps the wire format identical to the old map scan.
    for (uint16_t i = 0; i < kWindow; ++i) {
        const Pending& slot = m_pending[i];
        if (!slot.valid) continue;
        const uint16_t delta = static_cast<uint16_t>(slot.seq - ack);
        if (delta >= 1 && delta <= 32) bitmap |= 1u << (delta - 1);
    }
    put_u32(out, bitmap);
}

uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

std::vector<std::vector<uint8_t>> chunk_config(const std::vector<uint8_t>& data,
                                               size_t chunk_size) {
    std::vector<std::vector<uint8_t>> out;
    if (data.empty()) return out;
    if (chunk_size == 0) chunk_size = 512;
    const size_t total = (data.size() + chunk_size - 1) / chunk_size;
    if (total > 65535) return out;
    for (size_t index = 0; index < total; ++index) {
        const size_t begin = index * chunk_size;
        const size_t end = std::min(data.size(), begin + chunk_size);
        const uint32_t crc = crc32(data.data() + begin, end - begin);
        std::vector<uint8_t> chunk;
        chunk.reserve(8 + end - begin);
        put_u16(chunk, static_cast<uint16_t>(index));
        put_u16(chunk, static_cast<uint16_t>(total));
        put_u32(chunk, crc);
        chunk.insert(chunk.end(), data.begin() + static_cast<std::ptrdiff_t>(begin),
                     data.begin() + static_cast<std::ptrdiff_t>(end));
        out.push_back(std::move(chunk));
    }
    return out;
}

bool ConfigAssembler::add(const uint8_t* data, size_t size, std::string& error) {
    if (size < 8) {
        error = "short config chunk";
        return false;
    }
    const uint16_t index = static_cast<uint16_t>(data[0]) |
                           static_cast<uint16_t>(data[1] << 8);
    const uint16_t total = static_cast<uint16_t>(data[2]) |
                           static_cast<uint16_t>(data[3] << 8);
    const uint32_t expected = static_cast<uint32_t>(data[4]) |
                              (static_cast<uint32_t>(data[5]) << 8) |
                              (static_cast<uint32_t>(data[6]) << 16) |
                              (static_cast<uint32_t>(data[7]) << 24);
    if (total == 0 || index >= total) {
        error = "bad config chunk index";
        return false;
    }
    if (crc32(data + 8, size - 8) != expected) {
        error = "config chunk crc mismatch";
        return false;
    }
    if (m_total == 0) m_total = total;
    if (m_total != total) {
        error = "config chunk total mismatch";
        return false;
    }
    if (m_chunks.size() < total) m_chunks.resize(total);
    if (m_chunks[index].empty()) {
        m_chunks[index].assign(data + 8, data + size);
    }
    return true;
}

std::vector<uint8_t> ConfigAssembler::assemble() const {
    std::vector<uint8_t> out;
    for (const auto& chunk : m_chunks) out.insert(out.end(), chunk.begin(), chunk.end());
    return out;
}

}  // namespace sim
