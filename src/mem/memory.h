#ifndef NEXUS32_MEMORY_H
#define NEXUS32_MEMORY_H

#include <stdint.h>
#include <stddef.h>

/* Memory map per spec §3.1 */
#define MAIN_RAM_BASE   0x00000000u
#define MAIN_RAM_SIZE   0x02000000u   /* 32 MB */
#define VRAM_BASE       0x04000000u
#define VRAM_SIZE       0x01000000u   /* 16 MB */
#define AUDIO_RAM_BASE  0x06000000u
#define AUDIO_RAM_SIZE  0x00800000u   /* 8 MB */
#define EEPROM_BASE     0x08000000u
#define EEPROM_SIZE     0x00010000u   /* 64 KB */
#define GPU_CB_BASE     0x0A000000u
#define GPU_CB_SIZE     0x00200000u   /* 2 MB */
#define IO_BASE         0x0B000000u
#define IO_SIZE         0x00008000u   /* 32 KB total for all I/O */

/* I/O register offsets (from spec reference) */
#define GPU_CTRL_BASE   0x0B000000u
#define DMA_BASE        0x0B001000u
#define INPUT_BASE      0x0B004000u
#define TIMER_BASE      0x0B005000u
#define SYS_BASE        0x0B006000u
#define IRQ_BASE        0x0B007000u

#define SYS_VERSION     0x00010000u   /* spec 1.0 */
#define FRAME_COUNT_OFF 0x20u
#define SYS_RNG_OFF     0x10u
#define SYS_CYCLES_OFF  0x14u
#define SYS_CYCLE_MAX_OFF 0x18u
#define IRQ_PENDING_OFF 0x00u
#define IRQ_ENABLE_OFF  0x04u
#define IRQ_ACK_OFF     0x08u

typedef struct nexus32_mem nexus32_mem_t;

nexus32_mem_t *mem_create(void);
void mem_destroy(nexus32_mem_t *mem);

/* Access: address must be in a valid region. Unmapped reads return 0, writes discarded. */
uint8_t  mem_read8 (nexus32_mem_t *mem, uint32_t addr);
uint16_t mem_read16(nexus32_mem_t *mem, uint32_t addr);
uint32_t mem_read32(nexus32_mem_t *mem, uint32_t addr);

void mem_write8 (nexus32_mem_t *mem, uint32_t addr, uint8_t  val);
void mem_write16(nexus32_mem_t *mem, uint32_t addr, uint16_t val);
void mem_write32(nexus32_mem_t *mem, uint32_t addr, uint32_t val);

/* Direct region access for ROM load and bulk operations */
void *mem_main_ram_ptr (nexus32_mem_t *mem);
void *mem_vram_ptr     (nexus32_mem_t *mem);
void *mem_audio_ram_ptr(nexus32_mem_t *mem);
void *mem_eeprom_ptr   (nexus32_mem_t *mem);
void *mem_gpu_cb_ptr   (nexus32_mem_t *mem);

/* I/O state (set by emulator main loop) */
void mem_set_frame_count(nexus32_mem_t *mem, uint32_t count);
void mem_set_cycles(nexus32_mem_t *mem, uint32_t cycles_this_frame, uint32_t cycle_max);
void mem_set_irq_pending(nexus32_mem_t *mem, uint8_t bit_mask_add);
uint8_t mem_get_irq_pending(nexus32_mem_t *mem);

#endif /* NEXUS32_MEMORY_H */
