/**
 * Debug overlay UI: registers, disassembly, memory. Uses ClearUI (soft RDI) when NXEMU_CLEARUI.
 */
#include "debug_ui.h"
#include <string.h>

int debug_overlay_visible = 0;

#ifdef NXEMU_HEADLESS

int debug_ui_frame(cpu_state_t *cpu, nexus32_mem_t *mem) { (void)cpu; (void)mem; return 0; }
void debug_ui_get_framebuffer(const void **out_rgba, int *out_width, int *out_height) {
	if (out_rgba) *out_rgba = NULL;
	if (out_width) *out_width = 0;
	if (out_height) *out_height = 0;
}

#elif defined(NXEMU_CLEARUI)

#include <clearui.h>
#include <clearui_rdi.h>
#include <clearui_platform.h>
#include <stdlib.h>
#include <stdio.h>

#define OVERLAY_W 480
#define OVERLAY_H 360

static cui_ctx *s_ctx;
static cui_rdi_context *s_rdi_ctx;
static int s_initialized;

static void ensure_initialized(void)
{
	if (s_initialized) return;
	if (cui_rdi_soft_get()->init(&s_rdi_ctx) != 0) return;
	cui_config config = { "Debug", OVERLAY_W, OVERLAY_H, 1.0f, 0, NULL, NULL };
	s_ctx = cui_create(&config);
	if (!s_ctx) return;
	cui_set_rdi(s_ctx, cui_rdi_soft_get(), s_rdi_ctx);
	cui_set_platform(s_ctx, cui_platform_stub_get(), NULL);
	cui_theme dark;
	cui_theme_dark(&dark);
	cui_set_theme(s_ctx, &dark);
	s_initialized = 1;
}

static void build_debug_ui(cui_ctx *ctx, cpu_state_t *cpu, nexus32_mem_t *mem)
{
	cui_layout col = { .gap = 4, .padding = 8 };
	cui_column(ctx, &col);

	cui_label(ctx, "Registers");
	{
		cui_layout row_opts = { .gap = 8 };
		cui_row(ctx, &row_opts);
		for (int i = 0; i < 32; i += 8) {
			cui_layout c = { .gap = 2 };
			cui_column(ctx, &c);
			for (int r = i; r < i + 8 && r < 32; r++) {
				const char *line = cui_frame_printf(ctx, "r%2d 0x%08X", r, (unsigned)cpu->r[r]);
				cui_label(ctx, line);
			}
			cui_end(ctx);
		}
		cui_end(ctx);
	}
	cui_label(ctx, cui_frame_printf(ctx, "PC  0x%08X  SR 0x%08X", (unsigned)cpu->pc, (unsigned)cpu->sr));

	cui_label(ctx, "Disassembly");
	{
		char buf[128];
		for (uint32_t off = 0; off < 20; off += 4) {
			uint32_t addr = cpu->pc + off;
			uint32_t insn = mem_read32(mem, addr);
			disasm_instruction(insn, addr, buf, sizeof(buf));
			cui_label(ctx, buf);
		}
	}

	cui_label(ctx, "Memory at PC");
	{
		uint32_t addr = cpu->pc & ~15u;
		for (int row = 0; row < 8; row++) {
			char line[80];
			int n = snprintf(line, sizeof(line), "%08X", (unsigned)addr);
			for (int col_byte = 0; col_byte < 16 && n < (int)sizeof(line) - 4; col_byte++) {
				n += snprintf(line + n, sizeof(line) - (size_t)n, " %02X", (unsigned)mem_read8(mem, addr + (uint32_t)col_byte));
			}
			cui_label(ctx, line);
			addr += 16;
		}
	}

	cui_end(ctx);
}

int debug_ui_frame(cpu_state_t *cpu, nexus32_mem_t *mem)
{
	if (!debug_overlay_visible || !cpu || !mem) return 0;
	ensure_initialized();
	if (!s_ctx) return 0;
	cui_rdi_soft_set_viewport(s_rdi_ctx, OVERLAY_W, OVERLAY_H);
	cui_begin_frame(s_ctx);
	build_debug_ui(s_ctx, cpu, mem);
	cui_end_frame(s_ctx);
	return 1;
}

void debug_ui_get_framebuffer(const void **out_rgba, int *out_width, int *out_height)
{
	if (!out_rgba && !out_width && !out_height) return;
	if (out_rgba) *out_rgba = NULL;
	if (out_width) *out_width = 0;
	if (out_height) *out_height = 0;
	if (!s_ctx) return;
	cui_rdi_soft_get_framebuffer(s_rdi_ctx, out_rgba, out_width, out_height);
}

#else

int debug_ui_frame(cpu_state_t *cpu, nexus32_mem_t *mem) { (void)cpu; (void)mem; return 0; }
void debug_ui_get_framebuffer(const void **out_rgba, int *out_width, int *out_height) {
	if (out_rgba) *out_rgba = NULL;
	if (out_width) *out_width = 0;
	if (out_height) *out_height = 0;
}

#endif /* NXEMU_CLEARUI */
