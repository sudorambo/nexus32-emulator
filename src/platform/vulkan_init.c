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
void vulkan_end_render_pass(void) {}
void vulkan_draw_overlay(const void *rgba, int width, int height) { (void)rgba;(void)width;(void)height; }
void vulkan_raise_window(void) {}

#else

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdio.h>
#include <clearui_overlay_spv.h>

#define OVERLAY_MAX_SIZE 512

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
static VkFormat s_swap_fmt;
static int s_ready;
static int s_cmd_buf_begun;
static VkRenderPass s_render_pass_load;
static int s_render_pass_begun;

/* Overlay: ClearUI framebuffer composited over the clear (fullscreen triangle, no vertex buffer) */
static VkImage s_overlay_image;
static VkDeviceMemory s_overlay_mem;
static VkImageView s_overlay_view;
static VkSampler s_overlay_sampler;
static VkBuffer s_overlay_staging_buf;
static VkDeviceMemory s_overlay_staging_mem;
static VkDescriptorSetLayout s_overlay_dsl;
static VkPipelineLayout s_overlay_pl;
static VkPipeline s_overlay_pipeline;
static VkDescriptorPool s_overlay_pool;
static VkDescriptorSet s_overlay_set;
static int s_overlay_ok;
static int s_overlay_swizzle_rb;

int vulkan_init(int width, int height, const char *title)
{
	if (SDL_Init(SDL_INIT_VIDEO) != 0) return -1;
	s_window = SDL_CreateWindow(title ? title : "NEXUS-32",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
		SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
	if (!s_window) { SDL_Quit(); return -1; }
	SDL_RaiseWindow(s_window);

	unsigned count;
	if (!SDL_Vulkan_GetInstanceExtensions(s_window, &count, NULL)) { SDL_DestroyWindow(s_window); SDL_Quit(); return -1; }
	const char **exts = malloc(sizeof(char*) * (count + 1));
	if (!exts) { SDL_DestroyWindow(s_window); SDL_Quit(); return -1; }
	SDL_Vulkan_GetInstanceExtensions(s_window, &count, exts);
	exts[count] = NULL;

	VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "nxemu", 1, NULL, 0, VK_MAKE_VERSION(1,0,0) };
	VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app, 0, NULL, (uint32_t)count, exts };
	if (vkCreateInstance(&ici, NULL, &s_instance) != VK_SUCCESS) { free(exts); SDL_DestroyWindow(s_window); SDL_Quit(); return -1; }
	free(exts);

	if (!SDL_Vulkan_CreateSurface(s_window, s_instance, &s_surface)) { vkDestroyInstance(s_instance, NULL); SDL_DestroyWindow(s_window); SDL_Quit(); return -1; }

	uint32_t pd_count = 0;
	vkEnumeratePhysicalDevices(s_instance, &pd_count, NULL);
	if (pd_count == 0) { vkDestroySurfaceKHR(s_instance, s_surface, NULL); vkDestroyInstance(s_instance, NULL); SDL_DestroyWindow(s_window); SDL_Quit(); return -1; }
	VkPhysicalDevice *pds = malloc(pd_count * sizeof(VkPhysicalDevice));
	vkEnumeratePhysicalDevices(s_instance, &pd_count, pds);
	s_phys = pds[0];
	free(pds);

	static const char *const swapchain_ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	float q_prio = 1.0f;
	VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &q_prio };
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &qci, 0, NULL, 1, &swapchain_ext, NULL };
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
	s_swap_fmt = fmt.format;

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
	att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	att.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	if (vkCreateRenderPass(s_device, &rpci, NULL, &s_render_pass_load) != VK_SUCCESS) { vulkan_shutdown(); return -1; }

	for (uint32_t i = 0; i < s_swap_count; i++) {
		VkImageViewCreateInfo ivci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, s_swap_images[i], VK_IMAGE_VIEW_TYPE_2D, fmt.format, (VkComponentMapping){VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY}, (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
		if (vkCreateImageView(s_device, &ivci, NULL, &s_swap_views[i]) != VK_SUCCESS) { vulkan_shutdown(); return -1; }
		VkFramebufferCreateInfo fbci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, NULL, 0, s_render_pass, 1, &s_swap_views[i], extent.width, extent.height, 1 };
		if (vkCreateFramebuffer(s_device, &fbci, NULL, &s_framebuffers[i]) != VK_SUCCESS) { vulkan_shutdown(); return -1; }
	}

	VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 0, NULL, 0, NULL };
	vkCreatePipelineLayout(s_device, &plci, NULL, &s_pipeline_layout);

	VkPipelineShaderStageCreateInfo stages[2] = { { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, VK_NULL_HANDLE, "main", NULL }, { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE, "main", NULL } };
	VkGraphicsPipelineCreateInfo gpci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, NULL, 0, 2, stages, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, s_pipeline_layout, s_render_pass, 0, VK_NULL_HANDLE, -1 };
	(void)gpci;
	s_pipeline = VK_NULL_HANDLE;

	VkCommandPoolCreateInfo cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, 0, 0 };
	vkCreateCommandPool(s_device, &cpci, NULL, &s_cmd_pool);
	VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, s_cmd_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
	vkAllocateCommandBuffers(s_device, &cbai, &s_cmd_buf);

	VkSemaphoreCreateInfo sci_sem = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0 };
	vkCreateSemaphore(s_device, &sci_sem, NULL, &s_image_available);
	vkCreateSemaphore(s_device, &sci_sem, NULL, &s_render_finished);

	s_overlay_ok = 0;
	s_overlay_swizzle_rb = (s_swap_fmt == VK_FORMAT_B8G8R8A8_UNORM || s_swap_fmt == VK_FORMAT_B8G8R8A8_SRGB) ? 1 : 0;
	s_cmd_buf_begun = 0;
	s_render_pass_begun = 0;

	/* Overlay pipeline: uses ClearUI's embedded SPIR-V (fullscreen triangle, push-constant swizzle) */
	VkShaderModule vert_mod = VK_NULL_HANDLE, frag_mod = VK_NULL_HANDLE;
	VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0,
		sizeof(clearui_overlay_vert_spv), (const uint32_t *)clearui_overlay_vert_spv };
	VkResult vr = vkCreateShaderModule(s_device, &smci, NULL, &vert_mod);
	if (vr != VK_SUCCESS) fprintf(stderr, "nxemu: overlay vert shader failed: %d\n", vr);
	if (vr == VK_SUCCESS) {
		smci.codeSize = sizeof(clearui_overlay_frag_spv);
		smci.pCode = (const uint32_t *)clearui_overlay_frag_spv;
		vr = vkCreateShaderModule(s_device, &smci, NULL, &frag_mod);
		if (vr != VK_SUCCESS) fprintf(stderr, "nxemu: overlay frag shader failed: %d\n", vr);
		if (vr == VK_SUCCESS) {
			VkPhysicalDeviceMemoryProperties mem_props;
			vkGetPhysicalDeviceMemoryProperties(s_phys, &mem_props);
			uint32_t mem_type_index = UINT32_MAX;
			for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
				if (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) { mem_type_index = i; break; }
			uint32_t staging_type = UINT32_MAX;
			for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
				if ((mem_props.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { staging_type = i; break; }
			if (mem_type_index == UINT32_MAX || staging_type == UINT32_MAX) fprintf(stderr, "nxemu: overlay mem types not found (dev=%u stg=%u)\n", mem_type_index, staging_type);
			if (mem_type_index != UINT32_MAX && staging_type != UINT32_MAX) {
				VkImageCreateInfo ici = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
					{ OVERLAY_MAX_SIZE, OVERLAY_MAX_SIZE, 1 }, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED };
				if (vkCreateImage(s_device, &ici, NULL, &s_overlay_image) == VK_SUCCESS) {
					VkMemoryRequirements mr;
					vkGetImageMemoryRequirements(s_device, s_overlay_image, &mr);
					VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, mem_type_index };
					if (vkAllocateMemory(s_device, &mai, NULL, &s_overlay_mem) == VK_SUCCESS &&
					    vkBindImageMemory(s_device, s_overlay_image, s_overlay_mem, 0) == VK_SUCCESS) {
						VkImageViewCreateInfo ivci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, s_overlay_image, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
							(VkComponentMapping){ VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY },
							(VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
						if (vkCreateImageView(s_device, &ivci, NULL, &s_overlay_view) == VK_SUCCESS) {
							VkSamplerCreateInfo sci_samp = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, NULL, 0, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0, VK_FALSE, 0, VK_FALSE, VK_COMPARE_OP_NEVER, 0, 0, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, VK_FALSE };
							if (vkCreateSampler(s_device, &sci_samp, NULL, &s_overlay_sampler) == VK_SUCCESS) {
								VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, (VkDeviceSize)(OVERLAY_MAX_SIZE * OVERLAY_MAX_SIZE * 4), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL };
								if (vkCreateBuffer(s_device, &bci, NULL, &s_overlay_staging_buf) == VK_SUCCESS) {
									vkGetBufferMemoryRequirements(s_device, s_overlay_staging_buf, &mr);
									mai.memoryTypeIndex = staging_type;
									mai.allocationSize = mr.size;
									if (vkAllocateMemory(s_device, &mai, NULL, &s_overlay_staging_mem) == VK_SUCCESS &&
									    vkBindBufferMemory(s_device, s_overlay_staging_buf, s_overlay_staging_mem, 0) == VK_SUCCESS) {
										VkDescriptorSetLayoutBinding binding = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &s_overlay_sampler };
										VkDescriptorSetLayoutCreateInfo dslci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 1, &binding };
										if (vkCreateDescriptorSetLayout(s_device, &dslci, NULL, &s_overlay_dsl) == VK_SUCCESS) {
											VkPushConstantRange pcr = { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int32_t) };
											VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &s_overlay_dsl, 1, &pcr };
											if (vkCreatePipelineLayout(s_device, &plci, NULL, &s_overlay_pl) == VK_SUCCESS) {
												VkPipelineShaderStageCreateInfo stages[2] = {
													{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vert_mod, "main", NULL },
													{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag_mod, "main", NULL }
												};
												VkPipelineVertexInputStateCreateInfo visci = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, NULL, 0, 0, NULL, 0, NULL };
												VkPipelineInputAssemblyStateCreateInfo iasci = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
												VkPipelineViewportStateCreateInfo vsci = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1, NULL };
												VkPipelineRasterizationStateCreateInfo rsci = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, NULL, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FALSE, 0, 0, 0, 1 };
												VkPipelineMultisampleStateCreateInfo msci = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0, VK_SAMPLE_COUNT_1_BIT, VK_FALSE, 0, NULL, VK_FALSE, VK_FALSE };
												VkPipelineColorBlendAttachmentState blend_att = { VK_TRUE, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
												VkPipelineColorBlendStateCreateInfo cbsci = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, VK_FALSE, VK_LOGIC_OP_CLEAR, 1, &blend_att, { 0,0,0,0 } };
												VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
												VkPipelineDynamicStateCreateInfo dsci = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 2, dyn };
												VkGraphicsPipelineCreateInfo gpci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, NULL, 0, 2, stages, &visci, &iasci, NULL, &vsci, &rsci, &msci, NULL, &cbsci, &dsci, s_overlay_pl, s_render_pass, 0, VK_NULL_HANDLE, -1 };
												vr = vkCreateGraphicsPipelines(s_device, VK_NULL_HANDLE, 1, &gpci, NULL, &s_overlay_pipeline);
												if (vr != VK_SUCCESS) fprintf(stderr, "nxemu: overlay pipeline failed: %d\n", vr);
												if (vr == VK_SUCCESS) {
													VkDescriptorPoolSize dps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
													VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, 1, &dps };
													if (vkCreateDescriptorPool(s_device, &dpci, NULL, &s_overlay_pool) == VK_SUCCESS) {
														VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, s_overlay_pool, 1, &s_overlay_dsl };
														if (vkAllocateDescriptorSets(s_device, &dsai, &s_overlay_set) == VK_SUCCESS)
															s_overlay_ok = 1;
													}
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
			vkDestroyShaderModule(s_device, frag_mod, NULL);
		}
		vkDestroyShaderModule(s_device, vert_mod, NULL);
	}

	fprintf(stderr, "nxemu: overlay pipeline %s\n", s_overlay_ok ? "OK" : "FAILED");
	s_ready = 1;
	return 0;
}

void vulkan_shutdown(void)
{
	if (!s_ready) return;
	vkDeviceWaitIdle(s_device);
	if (s_overlay_ok) {
		vkDestroyDescriptorPool(s_device, s_overlay_pool, NULL);
		vkDestroyPipeline(s_device, s_overlay_pipeline, NULL);
		vkDestroyPipelineLayout(s_device, s_overlay_pl, NULL);
		vkDestroyDescriptorSetLayout(s_device, s_overlay_dsl, NULL);
		vkDestroyBuffer(s_device, s_overlay_staging_buf, NULL);
		vkFreeMemory(s_device, s_overlay_staging_mem, NULL);
		vkDestroySampler(s_device, s_overlay_sampler, NULL);
		vkDestroyImageView(s_device, s_overlay_view, NULL);
		vkDestroyImage(s_device, s_overlay_image, NULL);
		vkFreeMemory(s_device, s_overlay_mem, NULL);
	}
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
	vkDestroyRenderPass(s_device, s_render_pass_load, NULL);
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
	if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return 0;
	VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, 0, NULL };
	vkBeginCommandBuffer(s_cmd_buf, &cbbi);
	s_cmd_buf_begun = 1;
	return 1;
}

void vulkan_end_frame(void)
{
	if (!s_ready) return;
	if (s_cmd_buf_begun) {
		vkEndCommandBuffer(s_cmd_buf);
		s_cmd_buf_begun = 0;
		VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 1, &s_image_available, &wait, 1, &s_cmd_buf, 1, &s_render_finished };
		vkQueueSubmit(s_queue, 1, &si, VK_NULL_HANDLE);
		VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, NULL, 1, &s_render_finished, 1, &s_swapchain, &s_current_image, NULL };
		vkQueuePresentKHR(s_queue, &pi);
	}
}

void vulkan_clear_screen(float r, float g, float b, float a)
{
	if (!s_ready || !s_cmd_buf_begun || s_render_pass_begun || s_current_image >= s_swap_count) return;
	VkClearValue clear = { .color = { { r, g, b, a } } };
	VkRenderPassBeginInfo rpbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, NULL, s_render_pass, s_framebuffers[s_current_image], (VkRect2D){ {0,0}, s_extent }, 1, &clear };
	vkCmdBeginRenderPass(s_cmd_buf, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
	s_render_pass_begun = 1;
}

void vulkan_end_render_pass(void)
{
	if (!s_ready || !s_render_pass_begun) return;
	vkCmdEndRenderPass(s_cmd_buf);
	s_render_pass_begun = 0;
}

static int s_draw_diag_once;
void vulkan_draw_overlay(const void *rgba, int width, int height)
{
	if (!s_ready || !s_cmd_buf_begun || !s_render_pass_begun || !rgba || width <= 0 || height <= 0) {
		if (!s_draw_diag_once) { fprintf(stderr, "nxemu: draw_overlay early exit (ready=%d cmdbuf=%d rp=%d rgba=%p w=%d h=%d)\n", s_ready, s_cmd_buf_begun, s_render_pass_begun, rgba, width, height); s_draw_diag_once = 1; }
		return;
	}
	if (!s_overlay_ok) {
		if (!s_draw_diag_once) { fprintf(stderr, "nxemu: draw_overlay: pipeline not ok\n"); s_draw_diag_once = 1; }
		vulkan_end_render_pass();
		return;
	}
	if (!s_draw_diag_once) { fprintf(stderr, "nxemu: draw_overlay: drawing %dx%d\n", width, height); s_draw_diag_once = 1; }
	int w = width > OVERLAY_MAX_SIZE ? OVERLAY_MAX_SIZE : width;
	int h = height > OVERLAY_MAX_SIZE ? OVERLAY_MAX_SIZE : height;
	size_t src_stride = (size_t)width * 4u;
	size_t dst_stride = (size_t)w * 4u;
	size_t copy_size = (size_t)(w * h) * 4u;
	void *ptr;
	if (vkMapMemory(s_device, s_overlay_staging_mem, 0, copy_size, 0, &ptr) != VK_SUCCESS) {
		vulkan_end_render_pass();
		return;
	}
	const uint8_t *src = (const uint8_t *)rgba;
	uint8_t *dst = (uint8_t *)ptr;
	for (int y = 0; y < h; y++) {
		memcpy(dst + y * dst_stride, src + y * src_stride, dst_stride);
	}
	vkUnmapMemory(s_device, s_overlay_staging_mem);

	/* Transfer ops must happen outside a render pass */
	vkCmdEndRenderPass(s_cmd_buf);
	s_render_pass_begun = 0;

	VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, s_overlay_image, (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
	vkCmdPipelineBarrier(s_cmd_buf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
	VkBufferImageCopy region = { 0, 0, 0, (VkImageSubresourceLayers){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { (uint32_t)w, (uint32_t)h, 1 } };
	vkCmdCopyBufferToImage(s_cmd_buf, s_overlay_staging_buf, s_overlay_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	vkCmdPipelineBarrier(s_cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

	/* Restart render pass with LOAD_OP_LOAD to preserve the cleared background */
	VkRenderPassBeginInfo rpbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, NULL, s_render_pass_load, s_framebuffers[s_current_image], (VkRect2D){ {0,0}, s_extent }, 0, NULL };
	vkCmdBeginRenderPass(s_cmd_buf, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
	s_render_pass_begun = 1;

	VkDescriptorImageInfo dii = { s_overlay_sampler, s_overlay_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkWriteDescriptorSet wds = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, s_overlay_set, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &dii, NULL, NULL };
	vkUpdateDescriptorSets(s_device, 1, &wds, 0, NULL);

	vkCmdBindPipeline(s_cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, s_overlay_pipeline);
	vkCmdBindDescriptorSets(s_cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, s_overlay_pl, 0, 1, &s_overlay_set, 0, NULL);
	int32_t swizzle = s_overlay_swizzle_rb;
	vkCmdPushConstants(s_cmd_buf, s_overlay_pl, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(swizzle), &swizzle);
	VkViewport vp = { 0, 0, (float)w, (float)h, 0, 1 };
	VkRect2D scissor = { { 0, 0 }, { (uint32_t)w, (uint32_t)h } };
	vkCmdSetViewport(s_cmd_buf, 0, 1, &vp);
	vkCmdSetScissor(s_cmd_buf, 0, 1, &scissor);
	vkCmdDraw(s_cmd_buf, 3, 1, 0, 0);

	vkCmdEndRenderPass(s_cmd_buf);
	s_render_pass_begun = 0;
}

void vulkan_raise_window(void)
{
	if (s_ready && s_window)
		SDL_RaiseWindow(s_window);
}

#endif /* !NXEMU_HEADLESS */
