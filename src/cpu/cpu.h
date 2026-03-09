#ifndef NEXUS32_CPU_H
#define NEXUS32_CPU_H

#include <stddef.h>
#include <stdint.h>

typedef struct nexus32_mem nexus32_mem_t;

typedef struct {
	uint32_t r[32];
	uint32_t pc;
	uint32_t sr, epc, cause;
	uint32_t v[16][4];  /* 16 x 128-bit (4 floats) */
	uint16_t vfcc;
} cpu_state_t;

void cpu_init(cpu_state_t *cpu);
int cpu_run(cpu_state_t *cpu, nexus32_mem_t *mem, uint32_t cycle_limit, uint32_t *cycles_done);

void disasm_instruction(uint32_t insn, uint32_t pc, char *buf, size_t bufsize);

#endif /* NEXUS32_CPU_H */
