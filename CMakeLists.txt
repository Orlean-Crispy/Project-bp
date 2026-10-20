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

# Single source of truth shared with tools/build_bridge.ps1. Add new sim .cpp
# files to tools/sim_sources.txt, not here.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/tools/sim_sources.txt" SIM_SOURCES_RAW)
# build_bridge.ps1 already dropped blanks and '#' lines; CMake did not, so one
# comment in the shared list would become a "source file" here only.
set(SIM_SOURCES "")
foreach(line ${SIM_SOURCES_RAW})
  string(STRIP "${line}" line)
  if(line STREQUAL "" OR line MATCHES "^#")
    continue()
  endif()
  list(APPEND SIM_SOURCES "${line}")
endforeach()
add_library(sim STATIC ${SIM_SOURCES})
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
# Only the build id is injected: it changes per commit. Sim and protocol versions
# live in sim/core/version.h alone, so bumping one is a one-line change there instead
# of "the header says 5, the build script still says 4".
target_compile_definitions(sim PUBLIC BP_BUILD_ID="${BP_BUILD_ID}")

add_executable(headless_sim tools/headless_sim/main.cpp tools/headless_sim/tests_net.cpp tools/headless_sim/tests_demo.cpp tools/headless_sim/test_util.cpp tools/headless_sim/tests_move.cpp)
target_link_libraries(headless_sim PRIVATE sim)

add_executable(move_metrics tools/move_metrics/main.cpp)
target_link_libraries(move_metrics PRIVATE sim)

add_executable(net_loop tools/net_loop/main.cpp)
target_link_libraries(net_loop PRIVATE sim)

