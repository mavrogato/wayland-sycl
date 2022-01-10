
cmake_minimum_required(VERSION 3.14)

set(PROJ "wayland-sycl")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_COMPILER "icpx")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3")

project(${PROJ})

find_package(IntelDPCPP REQUIRED)

add_executable(${PROJ} main.cc)

target_compile_options(${PROJ}
  PRIVATE
  -std=c++20
  -fcoroutines-ts)

target_link_libraries(${PROJ}
  PRIVATE
  wayland-client
  wayland-egl
  EGL
  GL)

add_custom_target(run
  DEPENDS ${PROJ}
  COMMAND WAYLAND_DEBUG=1 ./${PROJ})
