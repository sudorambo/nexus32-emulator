#include "memory.h"
#include <stdlib.h>
#include <string.h>

struct nexus32_mem {
	uint8_t *main_ram;
	uint8_t *vram;
	uint8_t *audio_ram;
	uint8_t *eeprom;
	uint8_t *gpu_cb;
	uint8_t *io;
	uint32_t frame_count;
	uint32_t cycles_this_frame;
	uint32_t cycle_max;
	uint32_t rng_seed;
	uint8_t  irq_pending;
};

static int in_region(uint32_t addr, uint32_t base, uint32_t size)
{
	return addr >= base && addr < base + size;
}

nexus32_mem_t *mem_create(void)
{
	nexus32_mem_t *mem = calloc(1, sizeof(nexus32_mem_t));
	if (!mem) return NULL;
	mem->main_ram   = calloc(1, MAIN_RAM_SIZE);
	mem->vram       = calloc(1, VRAM_SIZE);
	mem->audio_ram  = calloc(1, AUDIO_RAM_SIZE);
	mem->eeprom     = calloc(1, EEPROM_SIZE);
	mem->gpu_cb     = calloc(1, GPU_CB_SIZE);
	mem->io         = calloc(1, IO_SIZE);
	if (!mem->main_ram || !mem->vram || !mem->audio_ram || !mem->eeprom || !mem->gpu_cb || !mem->io) {
		mem_destroy(mem);
		return NULL;
	}
	return mem;
}

void mem_destroy(nexus32_mem_t *mem)
{
	if (!mem) return;
	free(mem->main_ram);
	free(mem->vram);
	free(mem->audio_ram);
	free(mem->eeprom);
	free(mem->gpu_cb);
	free(mem->io);
	free(mem);
}

/* Simple LCG for SYS_RNG (32-bit output) */
static uint32_t rng_next(uint32_t *seed)
{
	*seed = *seed * 1103515245u + 12345u;
	return *seed;
}

static int io_read32(nexus32_mem_t *mem, uint32_t addr, uint32_t *out)
{
	if (addr == TIMER_BASE + FRAME_COUNT_OFF) {
		*out = mem->frame_count;
		return 1;
	}
	if (addr >= SYS_BASE && addr < SYS_BASE + 0x40u) {
		uint32_t off = addr - SYS_BASE;
		if (off == 0x00) { *out = SYS_VERSION; return 1; }
		if (off == 0x04) { *out = MAIN_RAM_SIZE; return 1; }
		if (off == 0x08) { *out = VRAM_SIZE; return 1; }
		if (off == 0x0C) { *out = AUDIO_RAM_SIZE; return 1; }
		if (off == SYS_RNG_OFF) { *out = rng_next(&mem->rng_seed); return 1; }
		if (off == SYS_CYCLES_OFF) { *out = mem->cycles_this_frame; return 1; }
		if (off == SYS_CYCLE_MAX_OFF) { *out = mem->cycle_max; return 1; }
	}
	if (addr >= IRQ_BASE && addr < IRQ_BASE + 32u) {
		uint32_t off = addr - IRQ_BASE;
		if (off == IRQ_PENDING_OFF) { *out = mem->irq_pending; return 1; }
		if (off == IRQ_ENABLE_OFF) {
			*out = (uint32_t)mem->io[addr - IO_BASE] | ((uint32_t)mem->io[addr - IO_BASE + 1] << 8) |
				((uint32_t)mem->io[addr - IO_BASE + 2] << 16) | ((uint32_t)mem->io[addr - IO_BASE + 3] << 24);
			return 1;
		}
	}
	return 0;
}

static int io_write32(nexus32_mem_t *mem, uint32_t addr, uint32_t val)
{
	if (addr >= IRQ_BASE && addr < IRQ_BASE + 32u) {
		uint32_t off = addr - IRQ_BASE;
		if (off == IRQ_ACK_OFF) {
			mem->irq_pending &= (uint8_t)(~(val & 0xFFu));
			return 1;
		}
	}
	return 0;
}

static uint8_t *resolve(nexus32_mem_t *mem, uint32_t addr, uint32_t *out_region_size)
{
	if (in_region(addr, MAIN_RAM_BASE, MAIN_RAM_SIZE)) {
		*out_region_size = MAIN_RAM_SIZE - (addr - MAIN_RAM_BASE);
		return mem->main_ram + (addr - MAIN_RAM_BASE);
	}
	if (in_region(addr, VRAM_BASE, VRAM_SIZE)) {
		*out_region_size = VRAM_SIZE - (addr - VRAM_BASE);
		return mem->vram + (addr - VRAM_BASE);
	}
	if (in_region(addr, AUDIO_RAM_BASE, AUDIO_RAM_SIZE)) {
		*out_region_size = AUDIO_RAM_SIZE - (addr - AUDIO_RAM_BASE);
		return mem->audio_ram + (addr - AUDIO_RAM_BASE);
	}
	if (in_region(addr, EEPROM_BASE, EEPROM_SIZE)) {
		*out_region_size = EEPROM_SIZE - (addr - EEPROM_BASE);
		return mem->eeprom + (addr - EEPROM_BASE);
	}
	if (in_region(addr, GPU_CB_BASE, GPU_CB_SIZE)) {
		*out_region_size = GPU_CB_SIZE - (addr - GPU_CB_BASE);
		return mem->gpu_cb + (addr - GPU_CB_BASE);
	}
	if (in_region(addr, IO_BASE, IO_SIZE)) {
		*out_region_size = IO_SIZE - (addr - IO_BASE);
		return mem->io + (addr - IO_BASE);
	}
	*out_region_size = 0;
	return NULL;
}

uint8_t mem_read8(nexus32_mem_t *mem, uint32_t addr)
{
	uint32_t remain;
	uint8_t *p = resolve(mem, addr, &remain);
	return p && remain >= 1 ? p[0] : 0;
}

uint16_t mem_read16(nexus32_mem_t *mem, uint32_t addr)
{
	uint32_t remain;
	uint8_t *p = resolve(mem, addr, &remain);
	if (!p || remain < 2) return 0;
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t mem_read32(nexus32_mem_t *mem, uint32_t addr)
{
	uint32_t val;
	if (in_region(addr, IO_BASE, IO_SIZE) && io_read32(mem, addr, &val))
		return val;
	uint32_t remain;
	uint8_t *p = resolve(mem, addr, &remain);
	if (!p || remain < 4) return 0;
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void mem_write8(nexus32_mem_t *mem, uint32_t addr, uint8_t val)
{
	uint32_t remain;
	uint8_t *p = resolve(mem, addr, &remain);
	if (p && remain >= 1) p[0] = val;
}

void mem_write16(nexus32_mem_t *mem, uint32_t addr, uint16_t val)
{
	uint32_t remain;
	uint8_t *p = resolve(mem, addr, &remain);
	if (p && remain >= 2) {
		p[0] = (uint8_t)(val);
		p[1] = (uint8_t)(val >> 8);
	}
}

void mem_write32(nexus32_mem_t *mem, uint32_t addr, uint32_t val)
{
	if (in_region(addr, IO_BASE, IO_SIZE) && io_write32(mem, addr, val))
		return;
	uint32_t remain;
	uint8_t *p = resolve(mem, addr, &remain);
	if (p && remain >= 4) {
		p[0] = (uint8_t)(val);
		p[1] = (uint8_t)(val >> 8);
		p[2] = (uint8_t)(val >> 16);
		p[3] = (uint8_t)(val >> 24);
	}
}

void *mem_main_ram_ptr(nexus32_mem_t *mem)  { return mem ? mem->main_ram : NULL; }
void *mem_vram_ptr(nexus32_mem_t *mem)      { return mem ? mem->vram : NULL; }
void *mem_audio_ram_ptr(nexus32_mem_t *mem) { return mem ? mem->audio_ram : NULL; }
void *mem_eeprom_ptr(nexus32_mem_t *mem)    { return mem ? mem->eeprom : NULL; }
void *mem_gpu_cb_ptr(nexus32_mem_t *mem)    { return mem ? mem->gpu_cb : NULL; }

void mem_set_frame_count(nexus32_mem_t *mem, uint32_t count)
{
	if (mem) mem->frame_count = count;
}
void mem_set_cycles(nexus32_mem_t *mem, uint32_t cycles_this_frame, uint32_t cycle_max)
{
	if (!mem) return;
	mem->cycles_this_frame = cycles_this_frame;
	mem->cycle_max = cycle_max;
}
void mem_set_irq_pending(nexus32_mem_t *mem, uint8_t bit_mask_add)
{
	if (mem) mem->irq_pending |= bit_mask_add;
}
uint8_t mem_get_irq_pending(nexus32_mem_t *mem)
{
	return mem ? mem->irq_pending : 0;
}
