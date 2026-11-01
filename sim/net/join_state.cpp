#include "sim/net/join_state.h"

namespace sim {

void JoinTracker::reset() {
    m_phase = JoinPhase::Idle;
    m_reason = "";
    m_since = 0;
}

void JoinTracker::begin_signalling(uint32_t now_ms) {
    m_phase = JoinPhase::Signalling;
    m_reason = "";
    m_since = now_ms;
}

void JoinTracker::begin_joining(uint32_t now_ms) {
    m_phase = JoinPhase::Joining;
    m_reason = "";
    m_since = now_ms;
}

void JoinTracker::fail(const char* reason) {
    m_phase = JoinPhase::Failed;
    m_reason = reason != nullptr ? reason : "failed";
}

void JoinTracker::update(bool joined, uint32_t now_ms) {
    if (joined) {
        m_phase = JoinPhase::Connected;
        m_reason = "";
        return;
    }
    if (m_phase != JoinPhase::Joining && m_phase != JoinPhase::Signalling) return;
    if (static_cast<uint32_t>(now_ms - m_since) < kJoinTimeoutMs) return;
    const bool was_joining = m_phase == JoinPhase::Joining;
    m_phase = JoinPhase::Failed;
    m_reason = was_joining ? "no full state" : "no server";
}

}  // namespace sim