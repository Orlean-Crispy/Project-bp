cmake_minimum_required(VERSION 3.20)
project(breakpoint LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# Determinism guard: never allow fast-math, never allow FP contraction
# differences inside the simulation.
add_compile_options(-Wall -Wextra -Werror -ffp-contract=off)

# Static runtime so the exe runs without the MinGW bin directory on PATH.
if(MINGW)
  add_link_options(-static -static-libgcc -static-libstdc++)
endif()

# Keep the core files in sync with tools/build_bridge.ps1: the DLL links only the
# simulation core, while the headless library also has net/demo/game.
add_library(sim STATIC
  sim/core/event.cpp
  sim/core/tick.cpp
  sim/core/world.cpp
  sim/collision/brush.cpp
  sim/collision/map_parse.cpp
  sim/collision/trace.cpp
  sim/move/pmove.cpp
  sim/data/config.cpp
  sim/input/keycodes.cpp
  sim/input/bindings.cpp
  sim/demo/demo.cpp
  sim/net/transport.cpp
  sim/net/udp_transport.cpp
  sim/net/fragment.cpp
  sim/net/netchan.cpp
  sim/net/snapshot.cpp
  sim/net/interp.cpp
  sim/net/session.cpp
  sim/game/combat.cpp
  sim/game/hitbox.cpp
)
target_include_directories(sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
  target_link_libraries(sim PUBLIC ws2_32)
endif()

# Demo headers carry the build id, so "a demo from yesterday" can say exactly
# which binary recorded it (plan section 10.3 / B3).
find_package(Git QUIET)
set(BP_BUILD_ID "nogit")
if(GIT_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                  OUTPUT_VARIABLE BP_GIT_HASH
                  OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_QUIET)
  if(BP_GIT_HASH)
    string(STRIP "${BP_GIT_HASH}" BP_BUILD_ID)
  endif()
endif()
target_compile_definitions(sim PUBLIC
  BP_BUILD_ID="${BP_BUILD_ID}"
  BP_SIM_VERSION=1
  BP_PROTOCOL_VERSION=1)

add_executable(headless_sim tools/headless_sim/main.cpp)
target_link_libraries(headless_sim PRIVATE sim)

add_executable(move_metrics tools/move_metrics/main.cpp)
target_link_libraries(move_metrics PRIVATE sim)

