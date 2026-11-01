#pragma once

#include <cstdint>

namespace sim {

// AUD-38: joining used to be a pair of booleans spread over the bridge and the
// client, so "still connecting", "gave up" and "never started" were all the same
// state. This is the terminal state machine, kept in sim/ so the gate can drive it
// without Godot.
enum class JoinPhase : uint8_t {
    Idle = 0,        // nothing in progress
    Signalling = 1,  // TCP matchmaking request in flight
    Joining = 2,     // ticket in hand, waiting for the first full state
    Connected = 3,   // playing
    Failed = 4,      // terminal until the caller resets or retries
};

class JoinTracker {
public:
    // A UDP black hole is indistinguishable from a slow server until a deadline
    // passes; 5 s is long enough for a LAN handshake and short enough that the
    // menu can offer Retry instead of hanging forever.
    static constexpr uint32_t kJoinTimeoutMs = 5000;

    void reset();
    void begin_signalling(uint32_t now_ms);
    void begin_joining(uint32_t now_ms);
    void fail(const char* reason);
    // Called once per frame with the client's own view of the world.
    void update(bool joined, uint32_t now_ms);

    JoinPhase phase() const { return m_phase; }
    const char* reason() const { return m_reason; }
    uint32_t waited_ms(uint32_t now_ms) const;
    static const char* name(JoinPhase phase);

private:
    JoinPhase m_phase = JoinPhase::Idle;
    const char* m_reason = "";
    uint32_t m_since = 0;
};

}  // namespace sim