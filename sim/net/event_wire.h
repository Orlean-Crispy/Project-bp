#pragma once

// AUD-46: a snapshot tells a client where everyone is; it says nothing about what
// just happened. Tracers, muzzle flashes and (soon) gunshots for OTHER players
// have to cross the network, so a snapshot packet now ends with a small event
// block. Protocol v10.
//
// The block is deliberately tiny and bounded: at most kMaxEventsPerPacket events,
// appended after the snapshot body. The snapshot decoder consumes exactly its own
// bytes, so whatever is left is the block.

#include <cstddef>
#include <cstdint>

namespace sim {

class ByteWriter;
class EventQueue;
struct SimEvent;

// 4 events of 36 bytes + the count is 145 B, so even next to a full 953 B
// ten-player snapshot the packet stays at 1098 B - under the 1100 B fragmentation
// threshold. Raising this cap is a fragmentation decision, not a cosmetic one.
constexpr int kMaxEventsPerPacket = 4;

// Appends [u8 count][events...]. Events from `skip_entity` are omitted: the client
// already predicted its own shots, and echoing them would draw every tracer twice.
void append_events(ByteWriter& out, const SimEvent* events, int count, int skip_entity);

// Reads a block from data/size. Malformed or truncated input delivers nothing and
// returns 0; a valid block returns how many events were pushed.
int read_events(const uint8_t* data, std::size_t size, EventQueue& out);

// Tracks (tick, entity, type) so the same event is never delivered twice - the
// network's version of the rollback-dedup rule from AUD-37.
class EventDedup {
public:
    bool seen(uint32_t tick, uint16_t entity, uint16_t type);

private:
    struct Key {
        uint32_t tick = 0;
        uint16_t entity = 0;
        uint16_t type = 0;
    };
    static constexpr std::size_t kCapacity = 32;
    Key m_keys[kCapacity];
    std::size_t m_next = 0;
};

}  // namespace sim