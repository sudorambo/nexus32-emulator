#include "rom_load.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc32_table[256];
static int crc32_initialized;

static void crc32_init(void)
{
	for (uint32_t i = 0; i < 256; i++) {
		uint32_t c = i;
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xEDB88320u & -(uint32_t)(c & 1));
		crc32_table[i] = c;
	}
	crc32_initialized = 1;
}

static uint32_t crc32_update(uint32_t crc, const void *data, size_t len)
{
	const uint8_t *p = (const uint8_t *)data;
	if (!crc32_initialized) crc32_init();
	crc ^= 0xFFFFFFFFu;
	while (len--) crc = crc32_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
	return crc ^ 0xFFFFFFFFu;
}

static uint32_t crc32(const void *data, size_t len)
{
	return crc32_update(0, data, len);
}

static uint32_t read_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int rom_load(const char *path, void *main_ram_base, uint32_t main_ram_size, rom_meta_t *meta)
{
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "nxemu: cannot open %s\n", path);
		return -1;
	}
	fseek(f, 0, SEEK_END);
	long flen = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (flen < (long)NXROM_HEADER_SIZE) {
		fprintf(stderr, "nxemu: file too small for ROM header\n");
		fclose(f);
		return -1;
	}
	size_t file_size = (size_t)flen;
	uint8_t *buf = malloc(file_size);
	if (!buf) {
		fprintf(stderr, "nxemu: out of memory\n");
		fclose(f);
		return -1;
	}
	if (fread(buf, 1, file_size, f) != file_size) {
		fprintf(stderr, "nxemu: read failed\n");
		free(buf);
		fclose(f);
		return -1;
	}
	fclose(f);

	if (buf[0] != 0x4E || buf[1] != 0x58 || buf[2] != 0x33 || buf[3] != 0x32) {
		fprintf(stderr, "nxemu: invalid ROM magic (expected NX32)\n");
		free(buf);
		return -1;
	}
	uint16_t format_version = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
	if (format_version != FORMAT_VERSION_1_0) {
		fprintf(stderr, "nxemu: unsupported format_version 0x%04X (emulator supports 0x0100)\n", (unsigned)format_version);
		free(buf);
		return -1;
	}
	uint32_t total_rom_size = read_u32(buf + 36);
	if (total_rom_size != file_size) {
		fprintf(stderr, "nxemu: total_rom_size in header (%u) does not match file size (%zu)\n", (unsigned)total_rom_size, file_size);
		free(buf);
		return -1;
	}
	for (int i = 120; i < 128; i++) {
		if (buf[i] != 0) {
			fprintf(stderr, "nxemu: reserved header bytes 120-127 must be zero\n");
			free(buf);
			return -1;
		}
	}
	uint32_t header_crc_stored = read_u32(buf + 116);
	uint8_t header_verify[120];
	memcpy(header_verify, buf, 120);
	memset(header_verify + 112, 0, 8);  /* checksum and header_checksum fields not included */
	uint32_t header_crc = crc32(header_verify, 120);
	if (header_crc != header_crc_stored) {
		fprintf(stderr, "nxemu: header checksum mismatch\n");
		free(buf);
		return -1;
	}
	uint32_t rom_crc_stored = read_u32(buf + 112);
	uint32_t rom_crc = crc32_update(0, buf, 112);
	rom_crc = crc32_update(rom_crc, buf + 120, file_size - 120);
	if (rom_crc != rom_crc_stored) {
		fprintf(stderr, "nxemu: ROM checksum mismatch\n");
		free(buf);
		return -1;
	}

	uint32_t entry_point = read_u32(buf + 8);
	uint32_t code_offset = read_u32(buf + 12);
	uint32_t code_size = read_u32(buf + 16);
	uint32_t data_offset = read_u32(buf + 20);
	uint32_t data_size = read_u32(buf + 24);
	uint32_t cycle_budget = read_u32(buf + 40);
	uint16_t screen_width = (uint16_t)buf[44] | ((uint16_t)buf[45] << 8);
	uint16_t screen_height = (uint16_t)buf[46] | ((uint16_t)buf[47] << 8);

	if (entry_point >= main_ram_size || code_size > main_ram_size - (entry_point - 0)) {
		fprintf(stderr, "nxemu: code segment does not fit in Main RAM\n");
		free(buf);
		return -1;
	}
	if (code_offset + code_size > file_size || data_offset + data_size > file_size) {
		fprintf(stderr, "nxemu: segment offsets/sizes out of file bounds\n");
		free(buf);
		return -1;
	}

	uint8_t *ram = (uint8_t *)main_ram_base;
	memcpy(ram + entry_point, buf + code_offset, code_size);
	if (data_size > 0)
		memcpy(ram + entry_point + code_size, buf + data_offset, data_size);

	if (meta) {
		meta->entry_point = entry_point;
		meta->cycle_budget = cycle_budget ? cycle_budget : CYCLE_BUDGET_DEFAULT;
		meta->screen_width = screen_width;
		meta->screen_height = screen_height;
		memcpy(meta->title, buf + 48, 32);
		meta->title[31] = '\0';
		memcpy(meta->author, buf + 80, 32);
		meta->author[31] = '\0';
	}
	free(buf);
	return 0;
}
