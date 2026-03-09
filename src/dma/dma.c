/*
 * DMA: 8 channels, 64 bytes/cycle, descriptor format per spec §4.
 */
#include "dma.h"
#include "../mem/memory.h"
#include <string.h>

#define DMA_BASE       0x0B001000u
#define DMA_DESC_TABLE 0x0B002000u
#define DMA_CHN_STRIDE 0x10u
#define BYTES_PER_CYCLE 64u
#define FLAG_ENABLE      1u
#define FLAG_IRQ_COMPLETE 2u
#define FLAG_CHAIN        4u
#define STATUS_IDLE    0u
#define STATUS_RUNNING 1u
#define STATUS_COMPLETE 2u
#define STATUS_ERROR   3u

typedef struct {
	uint32_t src, dst, size;
	uint32_t bytes_left;
	uint16_t flags;
	int      running;
} dma_channel_t;

static dma_channel_t s_chan[8];

static uint32_t read_mem32(nexus32_mem_t *mem, uint32_t addr)
{
	return (uint32_t)(uint8_t)(mem_read8(mem, addr))
		| ((uint32_t)(uint8_t)mem_read8(mem, addr + 1) << 8)
		| ((uint32_t)(uint8_t)mem_read8(mem, addr + 2) << 16)
		| ((uint32_t)(uint8_t)mem_read8(mem, addr + 3) << 24);
}

static void write_mem32(nexus32_mem_t *mem, uint32_t addr, uint32_t val)
{
	mem_write8(mem, addr, (uint8_t)(val));
	mem_write8(mem, addr + 1, (uint8_t)(val >> 8));
	mem_write8(mem, addr + 2, (uint8_t)(val >> 16));
	mem_write8(mem, addr + 3, (uint8_t)(val >> 24));
}

static int in_ram(uint32_t addr)
{
	return (addr < 0x02000000u)
		|| (addr >= 0x04000000u && addr < 0x05000000u)
		|| (addr >= 0x06000000u && addr < 0x06800000u)
		|| (addr >= 0x08000000u && addr < 0x08010000u)
		|| (addr >= 0x0B000000u && addr < 0x0B008000u);
}

void dma_init(void)
{
	memset(s_chan, 0, sizeof(s_chan));
}

void dma_step(nexus32_mem_t *mem, uint32_t cycles)
{
	if (!mem) return;
	uint32_t total_bytes = cycles * BYTES_PER_CYCLE;
	if (total_bytes == 0) return;

	for (int ch = 0; ch < 8; ch++) {
		uint32_t base = DMA_BASE + 0x10u + (uint32_t)(ch * DMA_CHN_STRIDE);
		uint32_t desc_addr = read_mem32(mem, base + 0);
		uint32_t status_reg = base + 4;
		uint32_t bytes_left_reg = base + 8;

		if (!s_chan[ch].running) {
			if (desc_addr != 0 && in_ram(desc_addr)) {
				uint32_t src = read_mem32(mem, desc_addr + 0);
				uint32_t dst = read_mem32(mem, desc_addr + 4);
				uint32_t size = read_mem32(mem, desc_addr + 8);
				uint32_t flags_u32 = read_mem32(mem, desc_addr + 20);
				uint16_t flags = (uint16_t)(flags_u32 >> 16);
				if ((flags & FLAG_ENABLE) && size > 0 && in_ram(src) && in_ram(dst)) {
					s_chan[ch].src = src;
					s_chan[ch].dst = dst;
					s_chan[ch].size = size;
					s_chan[ch].bytes_left = size;
					s_chan[ch].flags = flags;
					s_chan[ch].running = 1;
					write_mem32(mem, status_reg, STATUS_RUNNING);
					write_mem32(mem, bytes_left_reg, size);
				}
			}
		}

		if (s_chan[ch].running) {
			uint32_t to_do = total_bytes;
			if (to_do > s_chan[ch].bytes_left) to_do = s_chan[ch].bytes_left;
			for (uint32_t i = 0; i < to_do; i++) {
				uint8_t b = mem_read8(mem, s_chan[ch].src + i);
				mem_write8(mem, s_chan[ch].dst + i, b);
			}
			s_chan[ch].src += to_do;
			s_chan[ch].dst += to_do;
			s_chan[ch].bytes_left -= to_do;
			write_mem32(mem, bytes_left_reg, s_chan[ch].bytes_left);

			if (s_chan[ch].bytes_left == 0) {
				s_chan[ch].running = 0;
				write_mem32(mem, status_reg, STATUS_COMPLETE);
				if (s_chan[ch].flags & FLAG_IRQ_COMPLETE)
					mem_set_irq_pending(mem, 1u << 3);
			}
		}
	}
}
