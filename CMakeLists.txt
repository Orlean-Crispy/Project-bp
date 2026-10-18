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

add_library(sim STATIC
  sim/core/tick.cpp
  sim/core/world.cpp
  sim/collision/brush.cpp
  sim/collision/map_parse.cpp
  sim/collision/trace.cpp
  sim/move/pmove.cpp
  sim/data/config.cpp
)
target_include_directories(sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(headless_sim tools/headless_sim/main.cpp)
target_link_libraries(headless_sim PRIVATE sim)

add_executable(move_metrics tools/move_metrics/main.cpp)
target_link_libraries(move_metrics PRIVATE sim)
