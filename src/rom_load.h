#ifndef NEXUS32_ROM_LOAD_H
#define NEXUS32_ROM_LOAD_H

#include <stdint.h>
#include <stddef.h>

#define NXROM_HEADER_SIZE 128u
#define FORMAT_VERSION_1_0 0x0100u
#define CYCLE_BUDGET_DEFAULT 3000000u

typedef struct {
	uint32_t entry_point;
	uint32_t cycle_budget;
	uint16_t screen_width;
	uint16_t screen_height;
	char     title[32];
	char     author[32];
} rom_meta_t;

/* Load ROM from file into memory. mem must have been created; main_ram_base is typically 0.
 * Validates magic, format_version 0x0100, checksums, segment bounds. Returns 0 on success.
 * On success, meta is filled; code is at entry_point, data after code per spec §9.
 * On failure, prints error to stderr and returns -1. */
int rom_load(const char *path, void *main_ram_base, uint32_t main_ram_size, rom_meta_t *meta);

#endif /* NEXUS32_ROM_LOAD_H */
