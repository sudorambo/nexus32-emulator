#ifndef NEXUS32_GPU_H
#define NEXUS32_GPU_H

#include <stdint.h>

typedef struct nexus32_mem nexus32_mem_t;

/* Frame state filled by gpu_process_command_buffer for the platform to render. */
typedef struct {
	float clear_r, clear_g, clear_b, clear_a;
	int   clear_color;   /* CMD_CLEAR flags bit 0 */
	int   clear_depth;   /* CMD_CLEAR flags bit 1 */
	float clear_depth_val;
	int   present;       /* CMD_PRESENT was seen */
} gpu_frame_state_t;

void gpu_init(void);
void gpu_shutdown(void);

/* Parse command buffer at GPU_CB_BASE; fill frame_state. Platform uses frame_state for clear/present. */
void gpu_process_command_buffer(nexus32_mem_t *mem, uint32_t cb_addr, uint32_t cb_size, gpu_frame_state_t *frame_state);

#endif /* NEXUS32_GPU_H */
