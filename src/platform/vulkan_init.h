#ifndef NEXUS32_VULKAN_INIT_H
#define NEXUS32_VULKAN_INIT_H

int vulkan_init(int width, int height, const char *title);
void vulkan_shutdown(void);
int vulkan_begin_frame(void);
void vulkan_end_frame(void);
void vulkan_clear_screen(float r, float g, float b, float a);

#endif /* NEXUS32_VULKAN_INIT_H */
