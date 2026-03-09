/*
 * NEXUS-32 disassembler — all integer, system, and branch instructions per spec §2.
 */
#include "cpu.h"
#include <stddef.h>
#include <stdio.h>

void disasm_instruction(uint32_t insn, uint32_t pc, char *buf, size_t bufsize)
{
	if (!buf || bufsize == 0) return;
	uint32_t op = insn >> 26;
	uint32_t rs = (insn >> 21) & 31u;
	uint32_t rt = (insn >> 16) & 31u;
	uint32_t rd = (insn >> 11) & 31u;
	uint32_t shamt = (insn >> 6) & 31u;
	uint32_t func = insn & 63u;
	uint32_t imm = insn & 0xFFFFu;
	int32_t simm = (int32_t)(int16_t)(uint16_t)imm;

	if (insn == 0) { (void)snprintf(buf, bufsize, "nop"); return; }

	if (op == 0x00u) {
		switch (func) {
		case 0x00: (void)snprintf(buf, bufsize, "sll r%u, r%u, %u", rd, rt, shamt); return;
		case 0x02: (void)snprintf(buf, bufsize, "srl r%u, r%u, %u", rd, rt, shamt); return;
		case 0x03: (void)snprintf(buf, bufsize, "sra r%u, r%u, %u", rd, rt, shamt); return;
		case 0x04: (void)snprintf(buf, bufsize, "sllv r%u, r%u, r%u", rd, rt, rs); return;
		case 0x06: (void)snprintf(buf, bufsize, "srlv r%u, r%u, r%u", rd, rt, rs); return;
		case 0x07: (void)snprintf(buf, bufsize, "srav r%u, r%u, r%u", rd, rt, rs); return;
		case 0x08: (void)snprintf(buf, bufsize, "jr r%u", rs); return;
		case 0x09: (void)snprintf(buf, bufsize, "jalr r%u, r%u", rd, rs); return;
		case 0x18: (void)snprintf(buf, bufsize, "mul r%u, r%u, r%u", rd, rs, rt); return;
		case 0x19: (void)snprintf(buf, bufsize, "mulh r%u, r%u, r%u", rd, rs, rt); return;
		case 0x1a: (void)snprintf(buf, bufsize, "div r%u, r%u, r%u", rd, rs, rt); return;
		case 0x1b: (void)snprintf(buf, bufsize, "divu r%u, r%u, r%u", rd, rs, rt); return;
		case 0x1c: (void)snprintf(buf, bufsize, "mod r%u, r%u, r%u", rd, rs, rt); return;
		case 0x20: (void)snprintf(buf, bufsize, "add r%u, r%u, r%u", rd, rs, rt); return;
		case 0x21: (void)snprintf(buf, bufsize, "addu r%u, r%u, r%u", rd, rs, rt); return;
		case 0x22: (void)snprintf(buf, bufsize, "sub r%u, r%u, r%u", rd, rs, rt); return;
		case 0x23: (void)snprintf(buf, bufsize, "subu r%u, r%u, r%u", rd, rs, rt); return;
		case 0x24: (void)snprintf(buf, bufsize, "and r%u, r%u, r%u", rd, rs, rt); return;
		case 0x25: (void)snprintf(buf, bufsize, "or r%u, r%u, r%u", rd, rs, rt); return;
		case 0x26: (void)snprintf(buf, bufsize, "xor r%u, r%u, r%u", rd, rs, rt); return;
		case 0x27: (void)snprintf(buf, bufsize, "nor r%u, r%u, r%u", rd, rs, rt); return;
		case 0x2a: (void)snprintf(buf, bufsize, "slt r%u, r%u, r%u", rd, rs, rt); return;
		case 0x2b: (void)snprintf(buf, bufsize, "sltu r%u, r%u, r%u", rd, rs, rt); return;
		default: break;
		}
	}

	switch (op) {
	case 0x02u: (void)snprintf(buf, bufsize, "j 0x%08X", (pc & 0xF0000000u) | ((insn & 0x3FFFFFFu) << 2)); return;
	case 0x03u: (void)snprintf(buf, bufsize, "jal 0x%08X", (pc & 0xF0000000u) | ((insn & 0x3FFFFFFu) << 2)); return;
	case 0x04u: (void)snprintf(buf, bufsize, "beq r%u, r%u, 0x%08X", rs, rt, pc + 4 + (uint32_t)(simm << 2)); return;
	case 0x05u: (void)snprintf(buf, bufsize, "bne r%u, r%u, 0x%08X", rs, rt, pc + 4 + (uint32_t)(simm << 2)); return;
	case 0x08u: (void)snprintf(buf, bufsize, "addi r%u, r%u, %d", rt, rs, (int)simm); return;
	case 0x09u: (void)snprintf(buf, bufsize, "addiu r%u, r%u, %d", rt, rs, (int)simm); return;
	case 0x0au: (void)snprintf(buf, bufsize, "slti r%u, r%u, %d", rt, rs, (int)simm); return;
	case 0x0bu: (void)snprintf(buf, bufsize, "sltiu r%u, r%u, %d", rt, rs, (int)simm); return;
	case 0x0cu: (void)snprintf(buf, bufsize, "andi r%u, r%u, 0x%04x", rt, rs, imm); return;
	case 0x0du: (void)snprintf(buf, bufsize, "ori r%u, r%u, 0x%04x", rt, rs, imm); return;
	case 0x0eu: (void)snprintf(buf, bufsize, "xori r%u, r%u, 0x%04x", rt, rs, imm); return;
	case 0x0fu: (void)snprintf(buf, bufsize, "lui r%u, 0x%04x", rt, imm); return;
	case 0x14u: (void)snprintf(buf, bufsize, "blt r%u, r%u, 0x%08X", rs, rt, pc + 4 + (uint32_t)(simm << 2)); return;
	case 0x15u: (void)snprintf(buf, bufsize, "bgt r%u, r%u, 0x%08X", rs, rt, pc + 4 + (uint32_t)(simm << 2)); return;
	case 0x16u: (void)snprintf(buf, bufsize, "ble r%u, r%u, 0x%08X", rs, rt, pc + 4 + (uint32_t)(simm << 2)); return;
	case 0x17u: (void)snprintf(buf, bufsize, "bge r%u, r%u, 0x%08X", rs, rt, pc + 4 + (uint32_t)(simm << 2)); return;
	case 0x20u: (void)snprintf(buf, bufsize, "lb r%u, %d(r%u)", rt, (int)simm, rs); return;
	case 0x21u: (void)snprintf(buf, bufsize, "lh r%u, %d(r%u)", rt, (int)simm, rs); return;
	case 0x23u: (void)snprintf(buf, bufsize, "lw r%u, %d(r%u)", rt, (int)simm, rs); return;
	case 0x24u: (void)snprintf(buf, bufsize, "lbu r%u, %d(r%u)", rt, (int)simm, rs); return;
	case 0x25u: (void)snprintf(buf, bufsize, "lhu r%u, %d(r%u)", rt, (int)simm, rs); return;
	case 0x28u: (void)snprintf(buf, bufsize, "sb r%u, %d(r%u)", rt, (int)simm, rs); return;
	case 0x29u: (void)snprintf(buf, bufsize, "sh r%u, %d(r%u)", rt, (int)simm, rs); return;
	case 0x2bu: (void)snprintf(buf, bufsize, "sw r%u, %d(r%u)", rt, (int)simm, rs); return;
	case 0x3eu: (void)snprintf(buf, bufsize, "v.%02x v%u, v%u, v%u", func, rd, rs, rt); return;
	case 0x3fu:
		switch (func) {
		case 0x00: (void)snprintf(buf, bufsize, "syscall"); return;
		case 0x01: (void)snprintf(buf, bufsize, "break"); return;
		case 0x02: (void)snprintf(buf, bufsize, "nop"); return;
		case 0x03: (void)snprintf(buf, bufsize, "halt"); return;
		case 0x04: (void)snprintf(buf, bufsize, "eret"); return;
		case 0x10: (void)snprintf(buf, bufsize, "mfc0 r%u, r%u", rt, rd); return;
		case 0x11: (void)snprintf(buf, bufsize, "mtc0 r%u, r%u", rt, rd); return;
		default: break;
		}
		break;
	default: break;
	}

	(void)snprintf(buf, bufsize, ".word 0x%08X", insn);
}
