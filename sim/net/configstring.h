#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace sim {

// The join configstring (v0.4 3.5). For v0.1 it carries the two hashes that
// decide "same rules, same map"; the payload grows when assets exist. A client
// that receives a different pair refuses to play instead of silently diverging.
// Version 2: params_hash now folds in combat.toml (R8), so the same 18 bytes mean
// "movement + combat + geometry".
constexpr uint16_t kConfigVersion = 2;

struct ConfigBlob {
    uint16_t version = kConfigVersion;
    uint64_t params_hash = 0;
    uint64_t geometry_hash = 0;
};
constexpr std::size_t kConfigBytes = 2 + 8 + 8;

inline std::vector<uint8_t> encode_config(const ConfigBlob& blob) {
    std::vector<uint8_t> out(kConfigBytes, 0);
    out[0] = static_cast<uint8_t>(blob.version & 0xFF);
    out[1] = static_cast<uint8_t>(blob.version >> 8);
    for (int i = 0; i < 8; ++i) {
        out[2 + i] = static_cast<uint8_t>((blob.params_hash >> (i * 8)) & 0xFF);
        out[10 + i] = static_cast<uint8_t>((blob.geometry_hash >> (i * 8)) & 0xFF);
    }
    return out;
}

inline bool decode_config(const std::vector<uint8_t>& data, ConfigBlob& out) {
    if (data.size() != kConfigBytes) return false;
    out.version = static_cast<uint16_t>(data[0] | (data[1] << 8));
    out.params_hash = 0;
    out.geometry_hash = 0;
    for (int i = 0; i < 8; ++i) {
        out.params_hash |= static_cast<uint64_t>(data[2 + i]) << (i * 8);
        out.geometry_hash |= static_cast<uint64_t>(data[10 + i]) << (i * 8);
    }
    return true;
}

}  // namespace sim
