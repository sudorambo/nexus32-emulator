#ifndef NEXUS32_APU_H
#define NEXUS32_APU_H

#include <stdint.h>

typedef struct nexus32_mem nexus32_mem_t;

void apu_init(void);
void apu_shutdown(void);
void apu_mix(nexus32_mem_t *mem, uint32_t cycles);

#endif /* NEXUS32_APU_H */
