/*
 * NEXUS-32 CPU — fetch, decode, execute.
 * Minimal integer subset per spec §2; cycle counting; interrupt delivery.
 */
#include "cpu.h"
#include "../mem/memory.h"
#include <string.h>

#define OP_R    0x00u
#define OP_J    0x02u
#define OP_JAL  0x03u
#define OP_BEQ  0x04u
#define OP_BNE  0x05u
#define OP_ADDI 0x08u
#define OP_ADDIU 0x09u
#define OP_SLTI 0x0Au
#define OP_SLTIU 0x0Bu
#define OP_ANDI 0x0Cu
#define OP_ORI  0x0Du
#define OP_XORI 0x0Eu
#define OP_LUI  0x0Fu
#define OP_BLT  0x14u
#define OP_BGT  0x15u
#define OP_BLE  0x16u
#define OP_BGE  0x17u
#define OP_LB   0x20u
#define OP_LH   0x21u
#define OP_LW   0x23u
#define OP_LBU  0x24u
#define OP_LHU  0x25u
#define OP_SB   0x28u
#define OP_SH   0x29u
#define OP_SW   0x2Bu
#define OP_V    0x3Eu
#define OP_S    0x3Fu

#define FUNC_JR   0x08u
#define FUNC_JALR 0x09u
#define FUNC_SLL  0x00u
#define FUNC_SRL  0x02u
#define FUNC_SRA  0x03u
#define FUNC_SLLV 0x04u
#define FUNC_SRLV 0x06u
#define FUNC_SRAV 0x07u
#define FUNC_ADD  0x20u
#define FUNC_ADDU 0x21u
#define FUNC_SUB  0x22u
#define FUNC_SUBU 0x23u
#define FUNC_AND  0x24u
#define FUNC_OR   0x25u
#define FUNC_XOR  0x26u
#define FUNC_NOR  0x27u
#define FUNC_SLT  0x2Au
#define FUNC_SLTU 0x2Bu

#define FUNC_SYSCALL 0x00u
#define FUNC_BREAK   0x01u
#define FUNC_NOP_S  0x02u
#define FUNC_HALT   0x03u
#define FUNC_ERET   0x04u

#define SR_Z   (1u << 0)
#define SR_N   (1u << 1)
#define SR_IE  (1u << 4)
#define SR_IM  (0xFFu << 5)

#define IVT_BASE 0x00000000u
#define IRQ_VBLANK 6

static inline uint32_t sign_extend16(uint32_t x)
{
	return (uint32_t)(int32_t)(int16_t)(x & 0xFFFFu);
}

static void set_zn(cpu_state_t *cpu, uint32_t result)
{
	cpu->sr &= ~(SR_Z | SR_N);
	if (result == 0) cpu->sr |= SR_Z;
	if ((int32_t)result < 0) cpu->sr |= SR_N;
}

static int take_irq(cpu_state_t *cpu, nexus32_mem_t *mem, uint8_t irq_mask, uint32_t *out_cycles)
{
	(void)irq_mask;
	uint8_t pending = mem_get_irq_pending(mem);
	if (!pending) return 0;
	uint8_t im = (uint8_t)((cpu->sr & SR_IM) >> 5);
	if (!(cpu->sr & SR_IE)) return 0;
	for (int i = 7; i >= 0; i--) {
		if (!((pending >> i) & 1)) continue;
		if ((im >> i) & 1) continue;
		cpu->epc = cpu->pc;
		cpu->cause = (uint32_t)i;
		cpu->sr &= ~SR_IE;
		uint32_t handler = mem_read32(mem, IVT_BASE + (uint32_t)(i * 4));
		cpu->pc = handler;
		if (out_cycles) *out_cycles = 4;
		return 1;
	}
	return 0;
}

void cpu_init(cpu_state_t *cpu)
{
	memset(cpu, 0, sizeof(*cpu));
	cpu->r[0] = 0;
}

int cpu_run(cpu_state_t *cpu, nexus32_mem_t *mem, uint32_t cycle_limit, uint32_t *cycles_done)
{
	uint32_t cycles = 0;
	while (cycles < cycle_limit) {
		if (take_irq(cpu, mem, 0, &cycles))
			continue;

		uint32_t insn = mem_read32(mem, cpu->pc);
		uint32_t op = insn >> 26;
		uint32_t rs = (insn >> 21) & 31u;
		uint32_t rt = (insn >> 16) & 31u;
		uint32_t rd = (insn >> 11) & 31u;
		uint32_t shamt = (insn >> 6) & 31u;
		uint32_t func = insn & 63u;
		uint32_t imm = insn & 0xFFFFu;
		uint32_t target = insn & 0x3FFFFFFu;
		int32_t simm = (int32_t)sign_extend16(imm);

		uint32_t pc_next = cpu->pc + 4;
		uint32_t cost = 1;

		if (op == OP_R) {
			uint32_t rs_val = cpu->r[rs];
			uint32_t rt_val = cpu->r[rt];
			uint32_t result = 0;
			int do_rd = 1;
			switch (func) {
			case FUNC_SLL:
				result = rt_val << shamt;
				break;
			case FUNC_SRL:
				result = rt_val >> shamt;
				break;
			case FUNC_SRA:
				result = (uint32_t)((int32_t)rt_val >> (int)shamt);
				break;
			case FUNC_SLLV: result = rt_val << (rs_val & 31u); break;
			case FUNC_SRLV: result = rt_val >> (rs_val & 31u); break;
			case FUNC_SRAV: result = (uint32_t)((int32_t)rt_val >> (int)(rs_val & 31u)); break;
			case FUNC_JR:
				pc_next = rs_val;
				cost = 2;
				do_rd = 0;
				break;
			case FUNC_JALR:
				cpu->r[rd] = cpu->pc + 4;
				pc_next = rs_val;
				cost = 2;
				do_rd = 0;
				break;
			case FUNC_ADD:
			case FUNC_ADDU: {
				result = rs_val + rt_val;
				set_zn(cpu, result);
				break;
			}
			case FUNC_SUB:
			case FUNC_SUBU: {
				result = rs_val - rt_val;
				set_zn(cpu, result);
				break;
			}
			case FUNC_AND: result = rs_val & rt_val; set_zn(cpu, result); break;
			case FUNC_OR:  result = rs_val | rt_val; set_zn(cpu, result); break;
			case FUNC_XOR: result = rs_val ^ rt_val; set_zn(cpu, result); break;
			case FUNC_NOR: result = ~(rs_val | rt_val); set_zn(cpu, result); break;
			case FUNC_SLT: result = ((int32_t)rs_val < (int32_t)rt_val) ? 1u : 0u; break;
			case FUNC_SLTU: result = (rs_val < rt_val) ? 1u : 0u; break;
			default:
				if (rd == 0 && rt == 0 && shamt == 0 && func == FUNC_SLL) {
					/* NOP */
					do_rd = 0;
					break;
				}
				do_rd = 0;
				cost = 1;
				break;
			}
			if (do_rd && rd != 0)
				cpu->r[rd] = result;
		}
		else if (op == OP_J) {
			pc_next = (cpu->pc & 0xF0000000u) | (target << 2);
			cost = 2;
		}
		else if (op == OP_JAL) {
			cpu->r[31] = cpu->pc + 4;
			pc_next = (cpu->pc & 0xF0000000u) | (target << 2);
			cost = 2;
		}
		else if (op == OP_ADDIU || op == OP_ADDI) {
			uint32_t result = cpu->r[rs] + (uint32_t)simm;
			if (rt != 0) cpu->r[rt] = result;
			set_zn(cpu, result);
		}
		else if (op == OP_LUI) {
			if (rt != 0) cpu->r[rt] = imm << 16;
		}
		else if (op == OP_ANDI) {
			uint32_t result = cpu->r[rs] & imm;
			if (rt != 0) cpu->r[rt] = result;
			set_zn(cpu, result);
		}
		else if (op == OP_ORI) {
			uint32_t result = cpu->r[rs] | imm;
			if (rt != 0) cpu->r[rt] = result;
			set_zn(cpu, result);
		}
		else if (op == OP_XORI) {
			uint32_t result = cpu->r[rs] ^ imm;
			if (rt != 0) cpu->r[rt] = result;
			set_zn(cpu, result);
		}
		else if (op == OP_SLTI) {
			uint32_t result = ((int32_t)cpu->r[rs] < (int32_t)simm) ? 1u : 0u;
			if (rt != 0) cpu->r[rt] = result;
		}
		else if (op == OP_SLTIU) {
			uint32_t result = (cpu->r[rs] < (uint32_t)simm) ? 1u : 0u;
			if (rt != 0) cpu->r[rt] = result;
		}
		else if (op == OP_BEQ) {
			int taken = (cpu->r[rs] == cpu->r[rt]);
			if (taken) {
				pc_next = cpu->pc + 4 + (uint32_t)(simm << 2);
				cost = 2;
			}
		}
		else if (op == OP_BNE) {
			int taken = (cpu->r[rs] != cpu->r[rt]);
			if (taken) {
				pc_next = cpu->pc + 4 + (uint32_t)(simm << 2);
				cost = 2;
			}
		}
		else if (op == OP_BLT) {
			int taken = ((int32_t)cpu->r[rs] < (int32_t)cpu->r[rt]);
			if (taken) { pc_next = cpu->pc + 4 + (uint32_t)(simm << 2); cost = 2; }
		}
		else if (op == OP_BGT) {
			int taken = ((int32_t)cpu->r[rs] > (int32_t)cpu->r[rt]);
			if (taken) { pc_next = cpu->pc + 4 + (uint32_t)(simm << 2); cost = 2; }
		}
		else if (op == OP_BLE) {
			int taken = ((int32_t)cpu->r[rs] <= (int32_t)cpu->r[rt]);
			if (taken) { pc_next = cpu->pc + 4 + (uint32_t)(simm << 2); cost = 2; }
		}
		else if (op == OP_BGE) {
			int taken = ((int32_t)cpu->r[rs] >= (int32_t)cpu->r[rt]);
			if (taken) { pc_next = cpu->pc + 4 + (uint32_t)(simm << 2); cost = 2; }
		}
		else if (op == OP_LW) {
			uint32_t addr = cpu->r[rs] + (uint32_t)simm;
			if (addr & 3) { /* unaligned: treat as no-op or trap; spec says alignment exception */
				if (rt != 0) cpu->r[rt] = 0;
			} else {
				uint32_t val = mem_read32(mem, addr);
				if (rt != 0) cpu->r[rt] = val;
			}
			cost = 3;
		}
		else if (op == OP_SW) {
			uint32_t addr = cpu->r[rs] + (uint32_t)simm;
			if ((addr & 3) == 0)
				mem_write32(mem, addr, cpu->r[rt]);
			cost = 3;
		}
		else if (op == OP_LB) {
			uint32_t addr = cpu->r[rs] + (uint32_t)simm;
			int32_t v = (int32_t)(int8_t)mem_read8(mem, addr);
			if (rt != 0) cpu->r[rt] = (uint32_t)v;
			cost = 3;
		}
		else if (op == OP_LBU) {
			uint32_t addr = cpu->r[rs] + (uint32_t)simm;
			uint32_t v = mem_read8(mem, addr);
			if (rt != 0) cpu->r[rt] = v;
			cost = 3;
		}
		else if (op == OP_LH) {
			uint32_t addr = cpu->r[rs] + (uint32_t)simm;
			if (addr & 1) {
				if (rt != 0) cpu->r[rt] = 0;
			} else {
				int32_t v = (int32_t)(int16_t)mem_read16(mem, addr);
				if (rt != 0) cpu->r[rt] = (uint32_t)v;
			}
			cost = 3;
		}
		else if (op == OP_LHU) {
			uint32_t addr = cpu->r[rs] + (uint32_t)simm;
			if (addr & 1) {
				if (rt != 0) cpu->r[rt] = 0;
			} else {
				uint32_t v = mem_read16(mem, addr);
				if (rt != 0) cpu->r[rt] = v;
			}
			cost = 3;
		}
		else if (op == OP_SB) {
			uint32_t addr = cpu->r[rs] + (uint32_t)simm;
			mem_write8(mem, addr, (uint8_t)cpu->r[rt]);
			cost = 3;
		}
		else if (op == OP_SH) {
			uint32_t addr = cpu->r[rs] + (uint32_t)simm;
			if ((addr & 1) == 0)
				mem_write16(mem, addr, (uint16_t)cpu->r[rt]);
			cost = 3;
		}
		else if (op == OP_S) {
			if (func == FUNC_ERET) {
				cpu->pc = cpu->epc;
				cpu->sr |= SR_IE;
				cost = 2;
				pc_next = cpu->epc;
			}
			else if (func == FUNC_HALT) {
				/* HALT: spin until interrupt */
				while (!take_irq(cpu, mem, 0, &cost)) {
					cycles += 1;
					if (cycles >= cycle_limit) break;
				}
				continue;
			}
			else if (func == FUNC_SYSCALL || func == FUNC_BREAK || func == FUNC_NOP_S) {
				cost = (func == FUNC_SYSCALL) ? 4 : 1;
			}
		}
		else if (op == OP_V) {
			/* Vector: stub — no-op, 1 cycle */
			(void)rd;
			(void)rs;
			(void)rt;
		}
		else {
			/* Unknown opcode: treat as NOP */
		}

		cpu->pc = pc_next;
		cycles += cost;
	}

	if (cycles_done) *cycles_done = cycles;
	return 0;
}
