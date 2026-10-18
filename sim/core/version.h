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
#define BP_PROTOCOL_VERSION 1
#endif

namespace sim {

constexpr const char* kBuildId = BP_BUILD_ID;
constexpr uint32_t kSimVersion = BP_SIM_VERSION;
constexpr uint32_t kProtocolVersion = BP_PROTOCOL_VERSION;
constexpr uint32_t kDemoFormatVersion = 1;

}  // namespace sim
