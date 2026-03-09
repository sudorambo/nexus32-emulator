/*
 * Input: poll host input and write 128-byte state to 0x0B004000.
 */
#include "input.h"
#include "../mem/memory.h"
#include <string.h>

#ifdef NXEMU_HEADLESS

void input_init(void) {}
void input_shutdown(void) {}

void input_poll(nexus32_mem_t *mem)
{
	if (!mem) return;
	for (int i = 0; i < 128; i += 4)
		mem_write32(mem, INPUT_BASE + (uint32_t)i, 0);
}

#else

#include <SDL2/SDL.h>

void input_init(void)
{
	SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
}

void input_shutdown(void)
{
	SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

/* Pack controller 0 state: bytes 0-3 = button bitmask, 4-7 = axes (L stick X/Y, R stick X/Y). */
static uint32_t get_controller0_buttons(void)
{
	uint32_t b = 0;
	const Uint8 *key = SDL_GetKeyboardState(NULL);
	if (!key) return 0;
	if (key[SDL_SCANCODE_RIGHT])  b |= 0x01;
	if (key[SDL_SCANCODE_LEFT])   b |= 0x02;
	if (key[SDL_SCANCODE_DOWN])   b |= 0x04;
	if (key[SDL_SCANCODE_UP])     b |= 0x08;
	if (key[SDL_SCANCODE_Z])      b |= 0x10;   /* A */
	if (key[SDL_SCANCODE_X])      b |= 0x20;   /* B */
	if (key[SDL_SCANCODE_A])      b |= 0x40;   /* X */
	if (key[SDL_SCANCODE_S])      b |= 0x80;   /* Y */
	if (key[SDL_SCANCODE_Q])      b |= 0x100;  /* L */
	if (key[SDL_SCANCODE_W])      b |= 0x200;  /* R */
	if (key[SDL_SCANCODE_RETURN]) b |= 0x400;  /* Start */
	if (key[SDL_SCANCODE_BACKSPACE]) b |= 0x800; /* Select */
	return b;
}

void input_poll(nexus32_mem_t *mem)
{
	if (!mem) return;
	for (int i = 0; i < 128; i += 4)
		mem_write32(mem, INPUT_BASE + (uint32_t)i, 0);
	mem_write32(mem, INPUT_BASE, get_controller0_buttons());
}

#endif
