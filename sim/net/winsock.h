#pragma once

#include <string>

#ifdef _WIN32
#include <winsock2.h>
#endif

namespace sim {

// One process-wide WSAStartup for the whole net layer. The UDP socket and the
// TCP signalling path both need it; keeping two copies meant a change could land
// in one and be forgotten in the other (audit A3).
inline bool ensure_winsock(std::string& error) {
#ifdef _WIN32
    static const bool ready = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!ready) {
        error = "WSAStartup failed";
        return false;
    }
#else
    (void)error;
#endif
    return true;
}

}  // namespace sim
