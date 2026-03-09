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

	if (op == 0x00u) {
		switch (func) {
		case 0x08: (void)snprintf(buf, bufsize, "jr r%u", rs); return;
		case 0x09: (void)snprintf(buf, bufsize, "jalr r%u, r%u", rd, rs); return;
		case 0x20: (void)snprintf(buf, bufsize, "add r%u, r%u, r%u", rd, rs, rt); return;
		case 0x21: (void)snprintf(buf, bufsize, "addu r%u, r%u, r%u", rd, rs, rt); return;
		case 0x22: (void)snprintf(buf, bufsize, "sub r%u, r%u, r%u", rd, rs, rt); return;
		case 0x23: (void)snprintf(buf, bufsize, "subu r%u, r%u, r%u", rd, rs, rt); return;
		case 0x24: (void)snprintf(buf, bufsize, "and r%u, r%u, r%u", rd, rs, rt); return;
		case 0x25: (void)snprintf(buf, bufsize, "or r%u, r%u, r%u", rd, rs, rt); return;
		case 0x26: (void)snprintf(buf, bufsize, "xor r%u, r%u, r%u", rd, rs, rt); return;
		case 0x00: (void)snprintf(buf, bufsize, "sll r%u, r%u, %u", rd, rt, shamt); return;
		case 0x02: (void)snprintf(buf, bufsize, "srl r%u, r%u, %u", rd, rt, shamt); return;
		case 0x03: (void)snprintf(buf, bufsize, "sra r%u, r%u, %u", rd, rt, shamt); return;
		case 0x2a: (void)snprintf(buf, bufsize, "slt r%u, r%u, r%u", rd, rs, rt); return;
		case 0x2b: (void)snprintf(buf, bufsize, "sltu r%u, r%u, r%u", rd, rs, rt); return;
		default: break;
		}
		if (rd == 0 && rt == 0 && shamt == 0 && func == 0) {
			(void)snprintf(buf, bufsize, "nop");
			return;
		}
	}
	if (op == 0x09u) { (void)snprintf(buf, bufsize, "addiu r%u, r%u, %d", rt, rs, (int)simm); return; }
	if (op == 0x0fu) { (void)snprintf(buf, bufsize, "lui r%u, 0x%04x", rt, imm); return; }
	if (op == 0x23u) { (void)snprintf(buf, bufsize, "lw r%u, %d(r%u)", rt, (int)simm, rs); return; }
	if (op == 0x2bu) { (void)snprintf(buf, bufsize, "sw r%u, %d(r%u)", rt, (int)simm, rs); return; }
	if (op == 0x04u) { (void)snprintf(buf, bufsize, "beq r%u, r%u, 0x%08X", rs, rt, pc + 4 + (uint32_t)(simm << 2)); return; }
	if (op == 0x05u) { (void)snprintf(buf, bufsize, "bne r%u, r%u, 0x%08X", rs, rt, pc + 4 + (uint32_t)(simm << 2)); return; }
	if (op == 0x02u) { (void)snprintf(buf, bufsize, "j 0x%08X", (pc & 0xF0000000u) | ((insn & 0x3FFFFFFu) << 2)); return; }
	if (op == 0x03u) { (void)snprintf(buf, bufsize, "jal 0x%08X", (pc & 0xF0000000u) | ((insn & 0x3FFFFFFu) << 2)); return; }
	if (op == 0x3Fu && (insn & 0x3F) == 0x04) { (void)snprintf(buf, bufsize, "eret"); return; }
	if (op == 0x3Fu && (insn & 0x3F) == 0x03) { (void)snprintf(buf, bufsize, "halt"); return; }
	(void)snprintf(buf, bufsize, ".word 0x%08X", insn);
}
