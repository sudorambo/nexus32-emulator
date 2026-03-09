cmake_minimum_required(VERSION 3.14)
project(nexus32-emulator C CXX)

set(CMAKE_C_STANDARD 17)

# Vulkan and SDL2 required for windowed mode. If not found, build headless (no window).
# -DNXEMU_HEADLESS=ON forces headless (e.g. for CI/smoke test).
option(NXEMU_HEADLESS "Build headless (no window/display)" OFF)
find_package(Vulkan QUIET)
find_package(SDL2 QUIET)
if(NXEMU_HEADLESS OR NOT Vulkan_FOUND OR NOT SDL2_FOUND)
  add_compile_definitions(NXEMU_HEADLESS)
endif()

# ClearUI for debug overlay (only when not headless). Override with -DCLEARUI_DIR=
set(CLEARUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../clearui-1.1.1" CACHE PATH "Path to ClearUI")
set(CLEARUI_FOUND FALSE)
if(NOT NXEMU_HEADLESS AND EXISTS "${CLEARUI_DIR}/include/clearui.h")
  set(CLEARUI_FOUND TRUE)
  set(CLEARUI_SOURCES
    ${CLEARUI_DIR}/src/core/arena.c
    ${CLEARUI_DIR}/src/core/frame_alloc.c
    ${CLEARUI_DIR}/src/core/vault.c
    ${CLEARUI_DIR}/src/core/utf8.c
    ${CLEARUI_DIR}/src/core/version.c
    ${CLEARUI_DIR}/src/core/context.c
    ${CLEARUI_DIR}/src/core/draw_cmd.c
    ${CLEARUI_DIR}/src/core/node.c
    ${CLEARUI_DIR}/src/core/diff.c
    ${CLEARUI_DIR}/src/core/render.c
    ${CLEARUI_DIR}/src/core/a11y.c
    ${CLEARUI_DIR}/src/font/atlas.c
    ${CLEARUI_DIR}/src/layout/layout.c
    ${CLEARUI_DIR}/src/widget/layout.c
    ${CLEARUI_DIR}/src/widget/label.c
    ${CLEARUI_DIR}/src/widget/button.c
    ${CLEARUI_DIR}/src/widget/checkbox.c
    ${CLEARUI_DIR}/src/widget/spacer.c
    ${CLEARUI_DIR}/src/widget/icon_button.c
    ${CLEARUI_DIR}/src/widget/scroll.c
    ${CLEARUI_DIR}/src/widget/text_input.c
    ${CLEARUI_DIR}/src/widget/canvas.c
    ${CLEARUI_DIR}/src/rdi/clearui_rdi_soft.c
    ${CLEARUI_DIR}/src/platform/cui_platform_stub.c
  )
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
  src/debug/debug_ui.c
  src/platform/vulkan_init.c
  src/platform/audio_backend.c
)

if(CLEARUI_FOUND)
  list(APPEND EMU_SOURCES ${CLEARUI_SOURCES})
endif()

add_executable(nxemu ${EMU_SOURCES})
target_include_directories(nxemu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(CLEARUI_FOUND)
  target_compile_definitions(nxemu PRIVATE NXEMU_CLEARUI CLEARUI_EMBED_FONT)
  target_include_directories(nxemu PRIVATE
    ${CLEARUI_DIR}/include
    ${CLEARUI_DIR}/src
    ${CLEARUI_DIR}/src/core
    ${CLEARUI_DIR}/src/layout
    ${CLEARUI_DIR}/src/font
    ${CLEARUI_DIR}/src/widget
    ${CLEARUI_DIR}/src/platform
    ${CLEARUI_DIR}/src/rdi
  )
  target_compile_options(nxemu PRIVATE -Wno-unused-parameter)
endif()

if(Vulkan_FOUND AND SDL2_FOUND)
  target_link_libraries(nxemu Vulkan::Vulkan SDL2::SDL2)
endif()
if(CLEARUI_FOUND)
  target_link_libraries(nxemu m)
endif()

install(TARGETS nxemu RUNTIME DESTINATION bin)
