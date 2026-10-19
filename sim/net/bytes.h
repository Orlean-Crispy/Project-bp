#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "sim/core/types.h"

namespace sim {

// Little-endian byte writer/reader. Explicit rather than memcpy of structs so the
// wire format never depends on the compiler's padding.
class ByteWriter {
public:
    void u8(uint8_t v) { m_data.push_back(v); }
    void u16(uint16_t v) { u8(static_cast<uint8_t>(v & 0xFF)); u8(static_cast<uint8_t>(v >> 8)); }
    void u32(uint32_t v) { u16(static_cast<uint16_t>(v & 0xFFFF)); u16(static_cast<uint16_t>(v >> 16)); }
    void u64(uint64_t v) { u32(static_cast<uint32_t>(v & 0xFFFFFFFFull)); u32(static_cast<uint32_t>(v >> 32)); }
    void f32(float v) {
        uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        u32(bits);
    }
    void vec3(const Vec3& v) { f32(v.x); f32(v.y); f32(v.z); }
    void raw(const void* data, size_t n) {
        const auto* p = static_cast<const uint8_t*>(data);
        m_data.insert(m_data.end(), p, p + n);
    }

    const std::vector<uint8_t>& data() const { return m_data; }
    size_t size() const { return m_data.size(); }

private:
    std::vector<uint8_t> m_data;
};

class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}

    bool ok() const { return m_ok; }
    bool eof() const { return m_pos >= m_size; }

    uint8_t u8() {
        if (m_pos + 1 > m_size) { m_ok = false; return 0; }
        return m_data[m_pos++];
    }
    uint16_t u16() {
        const uint8_t a = u8();
        const uint8_t b = u8();
        return static_cast<uint16_t>(a) | (static_cast<uint16_t>(b) << 8);
    }
    uint32_t u32() {
        const uint16_t a = u16();
        const uint16_t b = u16();
        return static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 16);
    }
    uint64_t u64() {
        const uint32_t a = u32();
        const uint32_t b = u32();
        return static_cast<uint64_t>(a) | (static_cast<uint64_t>(b) << 32);
    }
    float f32() {
        const uint32_t bits = u32();
        float v = 0.0f;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }
    Vec3 vec3() {
        Vec3 v;
        v.x = f32();
        v.y = f32();
        v.z = f32();
        return v;
    }

    const uint8_t* bytes(size_t n) {
        if (m_pos + n > m_size) {
            m_ok = false;
            return nullptr;
        }
        const uint8_t* p = m_data + m_pos;
        m_pos += n;
        return p;
    }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_pos = 0;
    bool m_ok = true;
};

}  // namespace sim
