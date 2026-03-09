#ifndef NEXUS32_VULKAN_INIT_H
#define NEXUS32_VULKAN_INIT_H

int vulkan_init(int width, int height, const char *title);
void vulkan_shutdown(void);
int vulkan_begin_frame(void);
void vulkan_end_frame(void);
void vulkan_clear_screen(float r, float g, float b, float a);
/** Call when no overlay is drawn this frame (closes the render pass). */
void vulkan_end_render_pass(void);
/** Composite RGBA overlay (row-major, 4 bytes per pixel) on top of current pass. Ends render pass. */
void vulkan_draw_overlay(const void *rgba, int width, int height);
/** Bring window to front so it can receive keyboard input (e.g. F12). No-op in headless. */
void vulkan_raise_window(void);

#endif /* NEXUS32_VULKAN_INIT_H */
