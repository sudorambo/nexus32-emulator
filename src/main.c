/*
 * NEXUS-32 Emulator — entry point.
 * Phase 4: Headless (one frame) or windowed loop: input → CPU → GPU PRESENT → VBlank → 60 FPS.
 */
#include "mem/memory.h"
#include "rom_load.h"
#include "cpu/cpu.h"
#include "gpu/gpu.h"
#include "apu/apu.h"
#include "input/input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GPU_CONTROL_ADDR  0x0B000004u
#define GPU_CB_SIZE_ADDR  0x0B00000Cu
#define GPU_CB_BASE       0x0A000000u
#include "dma/dma.h"

#define EEPROM_SIZE 0x00010000u

static void save_load(const char *rom_path, nexus32_mem_t *mem)
{
	char sav_path[1024];
	size_t len = strlen(rom_path);
	if (len >= sizeof(sav_path) - 4) return;
	memcpy(sav_path, rom_path, len + 1);
	if (len > 6 && sav_path[len-6] == '.' && sav_path[len-5] == 'n' && sav_path[len-4] == 'x' && sav_path[len-3] == 'r' && sav_path[len-2] == 'o' && sav_path[len-1] == 'm') {
		sav_path[len-5] = 's'; sav_path[len-4] = 'a'; sav_path[len-3] = 'v'; sav_path[len-2] = '\0';
	} else {
		sav_path[len] = '.'; sav_path[len+1] = 's'; sav_path[len+2] = 'a'; sav_path[len+3] = 'v'; sav_path[len+4] = '\0';
	}
	FILE *f = fopen(sav_path, "rb");
	if (!f) return;
	void *eeprom = mem_eeprom_ptr(mem);
	if (eeprom && fread(eeprom, 1, EEPROM_SIZE, f) == EEPROM_SIZE) { /* loaded */ }
	fclose(f);
}

static void save_flush(const char *rom_path, nexus32_mem_t *mem)
{
	char sav_path[1024];
	size_t len = strlen(rom_path);
	if (len >= sizeof(sav_path) - 4) return;
	memcpy(sav_path, rom_path, len + 1);
	if (len > 6 && sav_path[len-6] == '.' && sav_path[len-5] == 'n' && sav_path[len-4] == 'x' && sav_path[len-3] == 'r' && sav_path[len-2] == 'o' && sav_path[len-1] == 'm') {
		sav_path[len-5] = 's'; sav_path[len-4] = 'a'; sav_path[len-3] = 'v'; sav_path[len-2] = '\0';
	} else {
		sav_path[len] = '.'; sav_path[len+1] = 's'; sav_path[len+2] = 'a'; sav_path[len+3] = 'v'; sav_path[len+4] = '\0';
	}
	FILE *f = fopen(sav_path, "wb");
	if (!f) return;
	void *eeprom = mem_eeprom_ptr(mem);
	if (eeprom) fwrite(eeprom, 1, EEPROM_SIZE, f);
	fclose(f);
}

#ifdef NXEMU_HEADLESS
static void run_headless(nexus32_mem_t *mem, const rom_meta_t *meta)
{
	cpu_state_t cpu;
	cpu_init(&cpu);
	cpu.pc = meta->entry_point;
	uint32_t cycles_used = 0;
	cpu_run(&cpu, mem, meta->cycle_budget, &cycles_used);
	mem_set_cycles(mem, cycles_used, meta->cycle_budget);
	mem_set_frame_count(mem, 1);
	mem_set_irq_pending(mem, 1u << 6);
	printf("       ran %u cycles, PC=0x%08X\n", (unsigned)cycles_used, (unsigned)cpu.pc);
}
#else
#include "platform/vulkan_init.h"
#include "debug/debug_ui.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stddef.h>

static int run_windowed(nexus32_mem_t *mem, const rom_meta_t *meta, const char *rom_path)
{
	int width = meta->screen_width > 0 ? (int)meta->screen_width : 640;
	int height = meta->screen_height > 0 ? (int)meta->screen_height : 480;
	if (vulkan_init(width, height, meta->title[0] ? meta->title : "NEXUS-32") != 0) {
		fprintf(stderr, "nxemu: failed to init Vulkan/window\n");
		return -1;
	}
		input_init();

	static int window_raised;
	window_raised = 0;

	cpu_state_t cpu;
	cpu_init(&cpu);
	cpu.pc = meta->entry_point;

	uint32_t frame_count = 0;
	int quit = 0;
	while (!quit) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) quit = 1;
			if (e.type == SDL_KEYDOWN) {
				if (e.key.keysym.sym == SDLK_ESCAPE) quit = 1;
				if (e.key.keysym.sym == SDLK_F12) debug_overlay_visible = !debug_overlay_visible;
			}
		}
		if (quit) break;

		input_poll(mem);
		mem_set_irq_pending(mem, 1u << 2);

		uint32_t cycles_used = 0;
		cpu_run(&cpu, mem, meta->cycle_budget, &cycles_used);
		mem_set_cycles(mem, cycles_used, meta->cycle_budget);

		/* Always present a frame so the window stays visible and can receive input (e.g. F12). */
		uint32_t gpu_ctrl = mem_read32(mem, GPU_CONTROL_ADDR);
		float r = 0.12f, g = 0.12f, b = 0.18f, a = 1.0f;
		if (gpu_ctrl & 1) {
			uint32_t cb_size = mem_read32(mem, GPU_CB_SIZE_ADDR);
			if (cb_size > 0x200000u) cb_size = 0x200000u;
			gpu_frame_state_t gpu_state;
			gpu_process_command_buffer(mem, GPU_CB_BASE, cb_size, &gpu_state);
			if (gpu_state.clear_color) {
				r = gpu_state.clear_r;
				g = gpu_state.clear_g;
				b = gpu_state.clear_b;
				a = gpu_state.clear_a;
			}
			mem_write32(mem, GPU_CONTROL_ADDR, 0);
		}
		if (vulkan_begin_frame()) {
			if (!window_raised) {
				vulkan_raise_window();
				window_raised = 1;
			}
			/* Dim background when overlay is on so F12 is visibly doing something even if overlay draw fails. */
			if (debug_overlay_visible) {
				r *= 0.5f;
				g *= 0.5f;
				b *= 0.5f;
			}
			vulkan_clear_screen(r, g, b, a);
			if (debug_overlay_visible) {
				const void *fb = NULL;
				int fw = 0, fh = 0;
				if (debug_ui_frame(&cpu, mem)) {
					debug_ui_get_framebuffer(&fb, &fw, &fh);
				}
				if (fb && fw > 0 && fh > 0) {
					vulkan_draw_overlay(fb, fw, fh);
				} else {
					/* Fallback: draw a visible panel so F12 clearly does something (e.g. no ClearUI or overlay init failed). */
					static uint8_t fallback_panel[128 * 128 * 4];
					static int fallback_filled;
					if (!fallback_filled) {
						for (size_t i = 0; i < sizeof(fallback_panel); i += 4) {
							fallback_panel[i + 0] = 0x28;
							fallback_panel[i + 1] = 0x28;
							fallback_panel[i + 2] = 0x30;
							fallback_panel[i + 3] = 0xE0;
						}
						fallback_filled = 1;
					}
					vulkan_draw_overlay(fallback_panel, 128, 128);
				}
			} else {
				vulkan_end_render_pass();
			}
			vulkan_end_frame();
			save_flush(rom_path, mem);
		}

		dma_step(mem, cycles_used);

		apu_mix(mem, cycles_used);
		mem_set_irq_pending(mem, 1u << 6);
		frame_count++;
		mem_set_frame_count(mem, frame_count);

		SDL_Delay(16);
	}

	input_shutdown();
	vulkan_shutdown();
	return 0;
}
#endif

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: nxemu <rom.nxrom>\n");
		return 1;
	}
	const char *rom_path = argv[1];

	nexus32_mem_t *mem = mem_create();
	if (!mem) {
		fprintf(stderr, "nxemu: out of memory\n");
		return 1;
	}

	rom_meta_t meta;
	memset(&meta, 0, sizeof(meta));
	if (rom_load(rom_path, mem_main_ram_ptr(mem), MAIN_RAM_SIZE, &meta) != 0) {
		mem_destroy(mem);
		return 1;
	}
	save_load(rom_path, mem);

	mem_set_cycles(mem, 0, meta.cycle_budget);

	uint32_t sys_version = mem_read32(mem, 0x0B006000u);
	printf("nxemu: loaded \"%s\" by %s\n", meta.title[0] ? meta.title : "(no title)", meta.author[0] ? meta.author : "(no author)");
	printf("       entry_point=0x%08X cycle_budget=%u SYS_VERSION=0x%08X\n",
		(unsigned)meta.entry_point, (unsigned)meta.cycle_budget, (unsigned)sys_version);

#ifdef NXEMU_HEADLESS
	run_headless(mem, &meta);
#else
	if (run_windowed(mem, &meta, rom_path) != 0) {
		mem_destroy(mem);
		return 1;
	}
#endif

	save_flush(rom_path, mem);
	mem_destroy(mem);
	return 0;
}
