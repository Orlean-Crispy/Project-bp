#pragma once

// Authoritative-field registry (AUD-39).
//
// The failure this exists to stop: a new field becomes authoritative, nobody
// adds it to state_hash (rollback replays differently) or to the snapshot (the
// client never sees it), and every gate still passes. `--test-fields` mutates
// each registered field and demands that the hash changes; fields that are
// deliberately excluded are listed with their reason and must NOT change it.

#include <cstddef>

#include "sim/move/pmove.h"

namespace sim {

struct FieldProbe {
    const char* name;
    void (*mutate)(PlayerState& player);
    bool hashed;  // true: state_hash must change; false: it must not
};

const FieldProbe* player_field_probes(std::size_t& count);

}  // namespace sim