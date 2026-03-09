/**
 * Debug overlay UI (ClearUI) for registers, disassembly, memory.
 * When NXEMU_HEADLESS, overlay is disabled; otherwise F12 toggles visibility.
 */
#ifndef NEXUS32_DEBUG_UI_H
#define NEXUS32_DEBUG_UI_H

#include "cpu/cpu.h"
#include "mem/memory.h"

/** Toggled by F12 in the main loop. Non-zero when overlay is visible. */
extern int debug_overlay_visible;

/** Build one frame of the debug UI. Returns 1 if overlay was drawn and framebuffer is valid. */
int debug_ui_frame(cpu_state_t *cpu, nexus32_mem_t *mem);

/** After a successful debug_ui_frame(), get the RGBA framebuffer (row-major, 4 bytes per pixel). */
void debug_ui_get_framebuffer(const void **out_rgba, int *out_width, int *out_height);

#endif /* NEXUS32_DEBUG_UI_H */
