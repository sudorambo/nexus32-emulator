cmake_minimum_required(VERSION 3.14)
project(nexus32-emulator C CXX)

set(CMAKE_C_STANDARD 17)

# Vulkan and SDL2 required for windowed mode (Phase 4+). If not found, build headless (no window).
find_package(Vulkan QUIET)
find_package(SDL2 QUIET)
if(NOT Vulkan_FOUND OR NOT SDL2_FOUND)
  add_compile_definitions(NXEMU_HEADLESS)
endif()

set(EMU_SOURCES
  src/main.c
  src/rom_load.c
  src/mem/memory.c
  src/cpu/cpu.c
  src/cpu/vector.c
  src/cpu/disasm.c
  src/gpu/gpu.c
  src/gpu/shader_compiler.c
  src/gpu/texture_cache.c
  src/gpu/pipeline_cache.c
  src/apu/apu.c
  src/dma/dma.c
  src/input/input.c
  src/debug/debugger.c
  src/debug/profiler.c
  src/platform/vulkan_init.c
  src/platform/audio_backend.c
)

add_executable(nxemu ${EMU_SOURCES})
target_include_directories(nxemu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(Vulkan_FOUND AND SDL2_FOUND)
  target_link_libraries(nxemu Vulkan::Vulkan SDL2::SDL2)
endif()

install(TARGETS nxemu RUNTIME DESTINATION bin)
