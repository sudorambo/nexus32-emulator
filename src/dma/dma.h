#ifndef NEXUS32_DMA_H
#define NEXUS32_DMA_H

#include <stdint.h>

typedef struct nexus32_mem nexus32_mem_t;

void dma_init(void);
void dma_step(nexus32_mem_t *mem, uint32_t cycles);

#endif /* NEXUS32_DMA_H */
