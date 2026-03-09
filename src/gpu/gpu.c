/*
 * GPU: parse command buffer; fill frame state (clear color, present).
 * Phase 5: All CMD_* parsed; CMD_CLEAR and CMD_PRESENT drive render; CMD_DRAW_* stubbed.
 */
#include "gpu.h"
#include "../mem/memory.h"
#include <stdint.h>
#include <string.h>

#define CMD_CLEAR        0x0001u
#define CMD_BIND_TEXTURE 0x0002u
#define CMD_SET_STATE    0x0003u
#define CMD_DRAW_TRIS    0x0004u
#define CMD_DRAW_INDEXED 0x0005u
#define CMD_DRAW_SPRITES 0x0006u
#define CMD_SET_VIEWPORT 0x0007u
#define CMD_SET_SHADER   0x0008u
#define CMD_SET_UNIFORM  0x0009u
#define CMD_COPY_REGION  0x000Au
#define CMD_SET_SCISSOR  0x000Bu
#define CMD_SET_RENDER_TARGET 0x000Cu
#define CMD_PRESENT      0x00FFu

#define GPU_CB_BASE 0x0A000000u
#define VRAM_BASE   0x04000000u

extern void texture_cache_init(void);
extern void texture_cache_shutdown(void);
extern void pipeline_cache_init(void);
extern void pipeline_cache_shutdown(void);
extern void shader_compiler_init(void);
extern void shader_compiler_shutdown(void);

void gpu_init(void)
{
	texture_cache_init();
	pipeline_cache_init();
	shader_compiler_init();
}
void gpu_shutdown(void)
{
	shader_compiler_shutdown();
	pipeline_cache_shutdown();
	texture_cache_shutdown();
}

static uint16_t read_u16(const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void gpu_process_command_buffer(nexus32_mem_t *mem, uint32_t cb_addr, uint32_t cb_size, gpu_frame_state_t *frame_state)
{
	if (!mem || !frame_state) return;
	memset(frame_state, 0, sizeof(*frame_state));
	frame_state->clear_r = 0.0f;
	frame_state->clear_g = 0.0f;
	frame_state->clear_b = 0.0f;
	frame_state->clear_a = 1.0f;
	frame_state->clear_depth_val = 1.0f;

	uint8_t *base = (uint8_t *)mem_gpu_cb_ptr(mem);
	if (!base) return;
	uint32_t off = cb_addr - GPU_CB_BASE;
	if (off >= 0x200000u) return;

	while (off + 4 <= cb_size) {
		uint16_t cmd_type = read_u16(base + off);
		uint16_t cmd_size = read_u16(base + off + 2);
		if (cmd_size < 4) cmd_size = 4;

		switch (cmd_type) {
		case CMD_CLEAR: {
			if (cmd_size >= 16) {
				uint8_t r = base[off + 8], g = base[off + 9], b = base[off + 10], a = base[off + 11];
				frame_state->clear_r = r / 255.0f;
				frame_state->clear_g = g / 255.0f;
				frame_state->clear_b = b / 255.0f;
				frame_state->clear_a = a / 255.0f;
				memcpy(&frame_state->clear_depth_val, base + off + 12, 4);
				frame_state->clear_color = (base[off + 16] & 1) != 0;
				frame_state->clear_depth = (base[off + 16] & 2) != 0;
			}
			break;
		}
		case CMD_BIND_TEXTURE:
		case CMD_SET_STATE:
		case CMD_DRAW_INDEXED:
		case CMD_DRAW_SPRITES:
		case CMD_SET_VIEWPORT:
		case CMD_SET_SHADER:
			break;
		case CMD_DRAW_TRIS: {
			if (cmd_size >= 20) {
				uint32_t vram_off = read_u32(base + off + 8);
				uint32_t vertex_count = read_u32(base + off + 12);
				uint32_t vertex_format = read_u32(base + off + 16);
				(void)vram_off;
				(void)vertex_count;
				(void)vertex_format;
				/* Phase 5: draw not yet translated to Vulkan */
			}
			break;
		}
		case CMD_SET_UNIFORM:
			/* Variable size: cmd_size from header */
			break;
		case CMD_COPY_REGION:
		case CMD_SET_SCISSOR:
		case CMD_SET_RENDER_TARGET:
			break;
		case CMD_PRESENT:
			frame_state->present = 1;
			off += cmd_size;
			goto done;
		default:
			break;
		}
		off += cmd_size;
	}
done:
	(void)mem;
}
