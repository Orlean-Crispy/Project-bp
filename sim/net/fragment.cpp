#include "sim/net/fragment.h"

#include <algorithm>

namespace sim {
namespace {
constexpr uint8_t kMarker = 0xF0;
}

void FragmentedTransport::abandon() {
    if (m_total != 0 && m_received < m_total) ++m_dropped;
    m_id = 0;
    m_total = 0;
    m_received = 0;
    m_buffer.clear();
    m_sizes.clear();
    m_present.clear();
}

void FragmentedTransport::send(const std::vector<uint8_t>& packet) {
    if (packet.size() <= kPayloadBytes) {
        m_inner.send(packet);
        return;
    }

    const std::size_t total =
        (packet.size() + kPayloadBytes - 1) / kPayloadBytes;
    if (total > 255) {
        ++m_dropped;  // a single unreliable message this large is a design bug
        return;
    }

    const uint16_t id = m_next_id++;
    std::vector<uint8_t> frame;
    frame.resize(kHeaderBytes + kPayloadBytes);
    for (std::size_t index = 0; index < total; ++index) {
        const std::size_t offset = index * kPayloadBytes;
        const std::size_t chunk = packet.size() - offset < kPayloadBytes
                                      ? packet.size() - offset
                                      : kPayloadBytes;
        frame[0] = kMarker;
        frame[1] = static_cast<uint8_t>(id & 0xFF);
        frame[2] = static_cast<uint8_t>(id >> 8);
        frame[3] = static_cast<uint8_t>(index);
        frame[4] = static_cast<uint8_t>(total);
        std::copy(packet.begin() + static_cast<long>(offset),
                  packet.begin() + static_cast<long>(offset + chunk), frame.begin() + 5);
        frame.resize(kHeaderBytes + chunk);
        m_inner.send(frame);
        ++m_fragments_sent;
        frame.resize(kHeaderBytes + kPayloadBytes);
    }
}

bool FragmentedTransport::recv(std::vector<uint8_t>& out) {
    std::vector<uint8_t> packet;
    while (m_inner.recv(packet)) {
        if (packet.empty()) continue;
        if (packet[0] != kMarker) {
            out = packet;  // ordinary packet: no fragmentation overhead
            return true;
        }
        if (packet.size() < kHeaderBytes) continue;

        const uint16_t id = static_cast<uint16_t>(packet[1] | (packet[2] << 8));
        const uint8_t index = packet[3];
        const uint8_t total = packet[4];
        if (total == 0 || index >= total) continue;

        if (id != m_id) {
            abandon();
            m_id = id;
            m_total = total;
            m_buffer.assign(static_cast<std::size_t>(total) * kPayloadBytes, 0);
            m_sizes.assign(total, 0);
            m_present.assign(total, 0);
        }
        if (total != m_total) continue;  // inconsistent frame, wait for the next id
        if (m_present[index] != 0) continue;  // duplicate fragment

        const std::size_t chunk = packet.size() - kHeaderBytes;
        // A fragment with no payload can never be part of a real message: the
        // sender only splits packets above kPayloadBytes. Rejecting it also keeps
        // a zero length from masquerading as "not seen yet".
        if (chunk == 0) continue;
        const bool is_last = static_cast<int>(index) + 1 == static_cast<int>(total);
        // Every fragment except the last must be exactly full, or the offsets stop
        // lining up and the reassembled message is silently wrong.
        if (!is_last && chunk != kPayloadBytes) continue;
        if (is_last && chunk > kPayloadBytes) continue;

        const std::size_t offset = static_cast<std::size_t>(index) * kPayloadBytes;
        if (offset + chunk > m_buffer.size()) continue;
        std::copy(packet.begin() + 5, packet.end(), m_buffer.begin() + static_cast<long>(offset));
        m_sizes[index] = static_cast<uint16_t>(chunk);
        m_present[index] = 1;
        ++m_received;

        if (m_received == m_total) {
            const std::size_t last = static_cast<std::size_t>(m_total - 1) * kPayloadBytes +
                                     m_sizes[m_total - 1];
            out.assign(m_buffer.begin(), m_buffer.begin() + static_cast<long>(last));
            ++m_reassembled;
            m_id = 0;
            m_total = 0;
            m_received = 0;
            m_buffer.clear();
            m_sizes.clear();
            m_present.clear();
            return true;
        }
    }
    return false;
}

}  // namespace sim
