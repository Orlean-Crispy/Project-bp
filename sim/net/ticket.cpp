#include "sim/net/ticket.h"

#ifdef _WIN32
#define _CRT_RAND_S
#endif
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <random>

namespace sim {
namespace {

constexpr uint32_t kSha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

class Sha256 {
public:
    Sha256() {
        m_h[0] = 0x6a09e667u;
        m_h[1] = 0xbb67ae85u;
        m_h[2] = 0x3c6ef372u;
        m_h[3] = 0xa54ff53au;
        m_h[4] = 0x510e527fu;
        m_h[5] = 0x9b05688cu;
        m_h[6] = 0x1f83d9abu;
        m_h[7] = 0x5be0cd19u;
    }

    void update(const uint8_t* data, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            m_buf[m_fill++] = data[i];
            if (m_fill == 64) {
                block(m_buf);
                m_total += 64;
                m_fill = 0;
            }
        }
    }

    void final(uint8_t out[32]) {
        uint64_t bits = (m_total + m_fill) * 8;
        const uint8_t pad = 0x80;
        update(&pad, 1);
        const uint8_t zero = 0;
        while (m_fill != 56) update(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; ++i) len[7 - i] = static_cast<uint8_t>(bits >> (8 * i));
        update(len, 8);
        for (int i = 0; i < 8; ++i) {
            out[4 * i] = static_cast<uint8_t>(m_h[i] >> 24);
            out[4 * i + 1] = static_cast<uint8_t>(m_h[i] >> 16);
            out[4 * i + 2] = static_cast<uint8_t>(m_h[i] >> 8);
            out[4 * i + 3] = static_cast<uint8_t>(m_h[i]);
        }
    }

private:
    void block(const uint8_t* p) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(p[4 * i]) << 24) |
                   (static_cast<uint32_t>(p[4 * i + 1]) << 16) |
                   (static_cast<uint32_t>(p[4 * i + 2]) << 8) |
                   static_cast<uint32_t>(p[4 * i + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = m_h[0], b = m_h[1], c = m_h[2], d = m_h[3];
        uint32_t e = m_h[4], f = m_h[5], g = m_h[6], h = m_h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = h + s1 + ch + kSha256K[i] + w[i];
            const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = s0 + maj;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        m_h[0] += a; m_h[1] += b; m_h[2] += c; m_h[3] += d;
        m_h[4] += e; m_h[5] += f; m_h[6] += g; m_h[7] += h;
    }

    uint32_t m_h[8]{};
    uint8_t m_buf[64]{};
    size_t m_fill = 0;
    uint64_t m_total = 0;
};

void hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* data, size_t size,
                 uint8_t out[32]) {
    uint8_t k[64]{};
    if (key_len > 64) {
        Sha256 kh;
        kh.update(key, key_len);
        kh.final(k);
    } else {
        std::memcpy(k, key, key_len);
    }
    uint8_t ipad[64];
    uint8_t opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = static_cast<uint8_t>(k[i] ^ 0x36);
        opad[i] = static_cast<uint8_t>(k[i] ^ 0x5c);
    }
    uint8_t inner[32];
    Sha256 hs;
    hs.update(ipad, 64);
    hs.update(data, size);
    hs.final(inner);
    Sha256 ho;
    ho.update(opad, 64);
    ho.update(inner, 32);
    ho.final(out);
}

void make_mac(const Ticket& t, const std::array<uint8_t, 32>& key, uint8_t out[12]) {
    uint8_t msg[20];
    std::memcpy(msg, t.client_id.data(), 16);
    msg[16] = static_cast<uint8_t>(t.expiry);
    msg[17] = static_cast<uint8_t>(t.expiry >> 8);
    msg[18] = static_cast<uint8_t>(t.expiry >> 16);
    msg[19] = static_cast<uint8_t>(t.expiry >> 24);
    uint8_t mac[32];
    hmac_sha256(key.data(), key.size(), msg, sizeof(msg), mac);
    std::memcpy(out, mac, 12);
}

}  // namespace

TicketIssuer::TicketIssuer() {
    for (size_t i = 0; i < m_key.size(); i += 4) {
        uint32_t v = 0;
#ifdef _WIN32
        // MinGW's std::random_device can be deterministic; rand_s is the CRT's
        // OS-backed generator and needs no extra library.
        unsigned int raw = 0;
        if (rand_s(&raw) != 0) {
            raw = static_cast<unsigned int>(
                std::chrono::steady_clock::now().time_since_epoch().count());
        }
        v = raw;
#else
        std::random_device rd;
        v = rd();
#endif
        m_key[i] = static_cast<uint8_t>(v);
        if (i + 1 < m_key.size()) m_key[i + 1] = static_cast<uint8_t>(v >> 8);
        if (i + 2 < m_key.size()) m_key[i + 2] = static_cast<uint8_t>(v >> 16);
        if (i + 3 < m_key.size()) m_key[i + 3] = static_cast<uint8_t>(v >> 24);
    }
}

TicketIssuer::TicketIssuer(const std::array<uint8_t, 32>& key) : m_key(key) {}

Ticket TicketIssuer::issue(const ClientId& client_id, uint32_t now_s, uint32_t ttl_s) const {
    Ticket t;
    t.client_id = client_id;
    t.expiry = now_s + ttl_s;
    make_mac(t, m_key, t.mac.data());
    return t;
}

bool TicketIssuer::verify(const Ticket& ticket, uint32_t now_s) const {
    if (ticket.expiry <= now_s) return false;
    uint8_t expected[12];
    make_mac(ticket, m_key, expected);
    uint8_t diff = 0;
    for (size_t i = 0; i < 12; ++i) diff |= static_cast<uint8_t>(expected[i] ^ ticket.mac[i]);
    return diff == 0;
}

}  // namespace sim
