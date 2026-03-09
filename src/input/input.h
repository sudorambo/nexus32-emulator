#ifndef NEXUS32_INPUT_H
#define NEXUS32_INPUT_H

#include <stdint.h>

typedef struct nexus32_mem nexus32_mem_t;

void input_poll(nexus32_mem_t *mem);
void input_init(void);
void input_shutdown(void);

#endif /* NEXUS32_INPUT_H */
