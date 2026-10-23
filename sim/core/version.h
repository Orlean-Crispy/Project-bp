#pragma once

#include <cstdint>

// Injected by CMake (see CMakeLists.txt). Defaults keep a plain g++ build working.
#ifndef BP_BUILD_ID
#define BP_BUILD_ID "dev"
#endif
#ifndef BP_SIM_VERSION
#define BP_SIM_VERSION 1
#endif
#ifndef BP_PROTOCOL_VERSION
// 5: PlayerSnap carries every input edge and the charge/speed pair (2026-10-20).
// 6: 16-bit mask/flags, plus the Vitals group (hp/armor) and the reserved Weapon
//    group. M6 adds combat fields without another bump (2026-10-23).
#define BP_PROTOCOL_VERSION 6
#endif

namespace sim {

constexpr const char* kBuildId = BP_BUILD_ID;
constexpr uint32_t kSimVersion = BP_SIM_VERSION;
constexpr uint32_t kProtocolVersion = BP_PROTOCOL_VERSION;
constexpr uint32_t kDemoFormatVersion = 1;

}  // namespace sim
