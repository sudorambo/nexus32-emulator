/*
 * Minimal Vulkan + SDL2: window, swapchain, clear-to-color render.
 * Requires SDL2 and Vulkan. If NXEMU_HEADLESS is defined, stubs only.
 */
#include "vulkan_init.h"
#include <stdlib.h>
#include <string.h>

#ifdef NXEMU_HEADLESS

int vulkan_init(int width, int height, const char *title) { (void)width; (void)height; (void)title; return 0; }
void vulkan_shutdown(void) {}
int vulkan_begin_frame(void) { return 1; }
void vulkan_end_frame(void) {}
void vulkan_clear_screen(float r, float g, float b, float a) { (void)r;(void)g;(void)b;(void)a; }

#else

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

static SDL_Window *s_window;
static VkInstance s_instance;
static VkPhysicalDevice s_phys;
static VkDevice s_device;
static VkQueue s_queue;
static VkSurfaceKHR s_surface;
static VkSwapchainKHR s_swapchain;
static VkRenderPass s_render_pass;
static VkCommandPool s_cmd_pool;
static VkCommandBuffer s_cmd_buf;
static VkPipeline s_pipeline;
static VkPipelineLayout s_pipeline_layout;
static VkFramebuffer *s_framebuffers;
static VkImageView *s_swap_views;
static VkImage *s_swap_images;
static uint32_t s_swap_count;
static VkSemaphore s_image_available, s_render_finished;
static uint32_t s_current_image;
static VkExtent2D s_extent;
static int s_ready;

int vulkan_init(int width, int height, const char *title)
{
	if (SDL_Init(SDL_INIT_VIDEO) != 0) return -1;
	s_window = SDL_CreateWindow(title ? title : "NEXUS-32",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
		SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
	if (!s_window) { SDL_Quit(); return -1; }

	unsigned count;
	if (!SDL_Vulkan_GetInstanceExtensions(s_window, &count, NULL)) { SDL_DestroyWindow(s_window); SDL_Quit(); return -1; }
	const char **exts = malloc(sizeof(char*) * (count + 1));
	if (!exts) { SDL_DestroyWindow(s_window); SDL_Quit(); return -1; }
	SDL_Vulkan_GetInstanceExtensions(s_window, &count, exts);
	exts[count] = NULL;

	VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "nxemu", 1, NULL, 0, VK_MAKE_VERSION(1,0,0) };
	VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app, 0, NULL, (uint32_t)count, exts };
	free(exts);
	if (vkCreateInstance(&ici, NULL, &s_instance) != VK_SUCCESS) { SDL_DestroyWindow(s_window); SDL_Quit(); return -1; }

	if (!SDL_Vulkan_CreateSurface(s_window, s_instance, &s_surface)) { vkDestroyInstance(s_instance, NULL); SDL_DestroyWindow(s_window); SDL_Quit(); return -1; }

	uint32_t pd_count = 0;
	vkEnumeratePhysicalDevices(s_instance, &pd_count, NULL);
	if (pd_count == 0) { vkDestroySurfaceKHR(s_instance, s_surface, NULL); vkDestroyInstance(s_instance, NULL); SDL_DestroyWindow(s_window); SDL_Quit(); return -1; }
	VkPhysicalDevice *pds = malloc(pd_count * sizeof(VkPhysicalDevice));
	vkEnumeratePhysicalDevices(s_instance, &pd_count, pds);
	s_phys = pds[0];
	free(pds);

	float q_prio = 1.0f;
	VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &q_prio };
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &qci, 0, NULL, 0, NULL };
	if (vkCreateDevice(s_phys, &dci, NULL, &s_device) != VK_SUCCESS) { vkDestroySurfaceKHR(s_instance, s_surface, NULL); vkDestroyInstance(s_instance, NULL); SDL_DestroyWindow(s_window); SDL_Quit(); return -1; }
	vkGetDeviceQueue(s_device, 0, 0, &s_queue);

	VkSurfaceCapabilitiesKHR caps;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(s_phys, s_surface, &caps);
	uint32_t fmt_count;
	vkGetPhysicalDeviceSurfaceFormatsKHR(s_phys, s_surface, &fmt_count, NULL);
	VkSurfaceFormatKHR *fmts = fmt_count ? malloc(fmt_count * sizeof(VkSurfaceFormatKHR)) : NULL;
	if (fmts) vkGetPhysicalDeviceSurfaceFormatsKHR(s_phys, s_surface, &fmt_count, fmts);
	VkSurfaceFormatKHR fmt = fmts && fmt_count ? fmts[0] : (VkSurfaceFormatKHR){ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
	if (fmts) free(fmts);

	s_extent = (VkExtent2D){ (uint32_t)width, (uint32_t)height };
	if (caps.currentExtent.width != 0xFFFFFFFF) s_extent = caps.currentExtent;
	VkExtent2D extent = s_extent;
	uint32_t min_img = caps.minImageCount + 1;
	if (min_img > caps.maxImageCount && caps.maxImageCount > 0) min_img = caps.maxImageCount;

	VkSwapchainCreateInfoKHR sci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, NULL, 0, s_surface, min_img, fmt.format, fmt.colorSpace, extent, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, caps.currentTransform, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_PRESENT_MODE_FIFO_KHR, VK_FALSE, VK_NULL_HANDLE };
	if (vkCreateSwapchainKHR(s_device, &sci, NULL, &s_swapchain) != VK_SUCCESS) { vkDestroyDevice(s_device, NULL); vkDestroySurfaceKHR(s_instance, s_surface, NULL); vkDestroyInstance(s_instance, NULL); SDL_DestroyWindow(s_window); SDL_Quit(); return -1; }

	vkGetSwapchainImagesKHR(s_device, s_swapchain, &s_swap_count, NULL);
	s_swap_images = malloc(s_swap_count * sizeof(VkImage));
	s_swap_views = malloc(s_swap_count * sizeof(VkImageView));
	s_framebuffers = malloc(s_swap_count * sizeof(VkFramebuffer));
	if (!s_swap_images || !s_swap_views || !s_framebuffers) { vulkan_shutdown(); return -1; }
	vkGetSwapchainImagesKHR(s_device, s_swapchain, &s_swap_count, s_swap_images);

	VkAttachmentDescription att = { 0, fmt.format, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
	VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkSubpassDescription sub = { 0, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, NULL, 1, &ref, NULL, NULL, 0, NULL };
	VkRenderPassCreateInfo rpci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, NULL, 0, 1, &att, 1, &sub, 0, NULL };
	if (vkCreateRenderPass(s_device, &rpci, NULL, &s_render_pass) != VK_SUCCESS) { vulkan_shutdown(); return -1; }

	for (uint32_t i = 0; i < s_swap_count; i++) {
		VkImageViewCreateInfo ivci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, s_swap_images[i], VK_IMAGE_VIEW_TYPE_2D, fmt.format, (VkComponentMapping){VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY}, (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
		if (vkCreateImageView(s_device, &ivci, NULL, &s_swap_views[i]) != VK_SUCCESS) { vulkan_shutdown(); return -1; }
		VkFramebufferCreateInfo fbci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, NULL, 0, s_render_pass, 1, &s_swap_views[i], extent.width, extent.height, 1 };
		if (vkCreateFramebuffer(s_device, &fbci, NULL, &s_framebuffers[i]) != VK_SUCCESS) { vulkan_shutdown(); return -1; }
	}

	VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 0, NULL, 0, NULL };
	vkCreatePipelineLayout(s_device, &plci, NULL, &s_pipeline_layout);

	VkPipelineShaderStageCreateInfo stages[2] = { { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, VK_NULL_HANDLE, "main", NULL }, { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE, "main", NULL } };
	VkGraphicsPipelineCreateInfo gpci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, NULL, 0, 2, stages, NULL, NULL, NULL, NULL, NULL, VK_NULL_HANDLE, s_pipeline_layout, s_render_pass, 0, VK_NULL_HANDLE, -1 };
	s_pipeline = VK_NULL_HANDLE;

	VkCommandPoolCreateInfo cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, 0, 0 };
	vkCreateCommandPool(s_device, &cpci, NULL, &s_cmd_pool);
	VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, s_cmd_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
	vkAllocateCommandBuffers(s_device, &cbai, &s_cmd_buf);

	VkSemaphoreCreateInfo sci_sem = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0 };
	vkCreateSemaphore(s_device, &sci_sem, NULL, &s_image_available);
	vkCreateSemaphore(s_device, &sci_sem, NULL, &s_render_finished);

	s_ready = 1;
	return 0;
}

void vulkan_shutdown(void)
{
	if (!s_ready) return;
	vkDeviceWaitIdle(s_device);
	vkDestroySemaphore(s_device, s_image_available, NULL);
	vkDestroySemaphore(s_device, s_render_finished, NULL);
	vkDestroyCommandPool(s_device, s_cmd_pool, NULL);
	if (s_pipeline) vkDestroyPipeline(s_device, s_pipeline, NULL);
	vkDestroyPipelineLayout(s_device, s_pipeline_layout, NULL);
	for (uint32_t i = 0; i < s_swap_count; i++) {
		vkDestroyFramebuffer(s_device, s_framebuffers[i], NULL);
		vkDestroyImageView(s_device, s_swap_views[i], NULL);
	}
	free(s_framebuffers);
	free(s_swap_views);
	free(s_swap_images);
	vkDestroyRenderPass(s_device, s_render_pass, NULL);
	vkDestroySwapchainKHR(s_device, s_swapchain, NULL);
	vkDestroyDevice(s_device, NULL);
	vkDestroySurfaceKHR(s_instance, s_surface, NULL);
	vkDestroyInstance(s_instance, NULL);
	SDL_DestroyWindow(s_window);
	SDL_Quit();
	s_ready = 0;
}

int vulkan_begin_frame(void)
{
	if (!s_ready) return 0;
	VkResult r = vkAcquireNextImageKHR(s_device, s_swapchain, UINT64_MAX, s_image_available, VK_NULL_HANDLE, &s_current_image);
	return (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) ? 1 : 0;
}

void vulkan_end_frame(void)
{
	if (!s_ready) return;
	VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, NULL, 1, &s_render_finished, 1, &s_swapchain, &s_current_image, NULL };
	vkQueuePresentKHR(s_queue, &pi);
}

void vulkan_clear_screen(float r, float g, float b, float a)
{
	if (!s_ready || !s_cmd_buf) return;
	VkClearValue clear = { .color = { { r, g, b, a } } };
	VkRenderPassBeginInfo rpbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, NULL, s_render_pass, s_framebuffers[s_current_image], (VkRect2D){ {0,0}, s_extent }, 1, &clear };
	VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, 0, NULL };
	vkBeginCommandBuffer(s_cmd_buf, &cbbi);
	vkCmdBeginRenderPass(s_cmd_buf, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdEndRenderPass(s_cmd_buf);
	vkEndCommandBuffer(s_cmd_buf);
	VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 1, &s_image_available, &wait, 1, &s_cmd_buf, 1, &s_render_finished };
	vkQueueSubmit(s_queue, 1, &si, VK_NULL_HANDLE);
}

#endif /* !NXEMU_HEADLESS */
