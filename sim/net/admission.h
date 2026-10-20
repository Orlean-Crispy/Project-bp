#pragma once

#include <cstdint>
#include <deque>
#include <utility>

#include "sim/net/ticket.h"

namespace sim {

// Admission is the one place that decides "this UDP hello may become a player".
// The control plane approves a client id and its slot; whoever owns the UDP
// socket (bridge, net_loop) only asks take(). One implementation means a
// reconnect cannot behave differently in the tool and in the game.
class Admission {
public:
    void approve(const ClientId& id, uint8_t slot) { m_pending.emplace_back(id, slot); }

    // Consumes the approval; -1 means "never vetted over TCP".
    int take(const ClientId& id) {
        for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
            if (it->first == id) {
                const int slot = static_cast<int>(it->second);
                m_pending.erase(it);
                return slot;
            }
        }
        return -1;
    }

    std::size_t pending() const { return m_pending.size(); }
    void clear() { m_pending.clear(); }

private:
    std::deque<std::pair<ClientId, uint8_t>> m_pending;
};

}  // namespace sim
