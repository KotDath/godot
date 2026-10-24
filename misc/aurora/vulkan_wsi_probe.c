/**************************************************************************/
/*  vulkan_wsi_probe.c                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

/* Standalone WSI diagnostic, not Godot renderer/hardware acceptance.
 * Generate xdg-shell-client.h/xdg-shell-code.c from the bundled stable protocol.
 * Build with bundled Vulkan headers, generated protocol code, wayland-client
 * and libdl. No link-time Vulkan loader, shader compiler or Qt is required. */
#define _POSIX_C_SOURCE 200809L
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WAYLAND_KHR
#include "xdg-shell-client.h"

#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vulkan/vulkan.h>
#include <wayland-client.h>

#define INSTANCE_FUNCTIONS(X) \
	X(DestroyInstance) \
	X(EnumeratePhysicalDevices) \
	X(GetPhysicalDeviceProperties) \
	X(GetPhysicalDeviceMemoryProperties) \
	X(GetPhysicalDeviceQueueFamilyProperties) \
	X(EnumerateDeviceExtensionProperties) \
	X(CreateWaylandSurfaceKHR) \
	X(DestroySurfaceKHR) \
	X(GetPhysicalDeviceSurfaceSupportKHR) \
	X(GetPhysicalDeviceSurfaceCapabilitiesKHR) \
	X(GetPhysicalDeviceSurfaceFormatsKHR) \
	X(CreateDevice) \
	X(GetDeviceProcAddr)
#define DEVICE_FUNCTIONS(X) \
	X(DestroyDevice) \
	X(GetDeviceQueue) \
	X(DeviceWaitIdle) \
	X(CreateSwapchainKHR) \
	X(DestroySwapchainKHR) \
	X(GetSwapchainImagesKHR) \
	X(AcquireNextImageKHR) \
	X(QueuePresentKHR) \
	X(CreateImageView) \
	X(DestroyImageView) \
	X(CreateRenderPass) \
	X(DestroyRenderPass) \
	X(CreateFramebuffer) \
	X(DestroyFramebuffer) \
	X(CreateCommandPool) \
	X(DestroyCommandPool) \
	X(AllocateCommandBuffers) \
	X(FreeCommandBuffers) \
	X(BeginCommandBuffer) \
	X(EndCommandBuffer) \
	X(CmdBeginRenderPass) \
	X(CmdClearAttachments) \
	X(CmdEndRenderPass) \
	X(CmdPipelineBarrier) \
	X(CmdCopyImageToBuffer) \
	X(CreateBuffer) \
	X(DestroyBuffer) \
	X(GetBufferMemoryRequirements) \
	X(AllocateMemory) \
	X(FreeMemory) \
	X(BindBufferMemory) \
	X(MapMemory) \
	X(UnmapMemory) \
	X(InvalidateMappedMemoryRanges) \
	X(CreateSemaphore) \
	X(DestroySemaphore) \
	X(CreateFence) \
	X(DestroyFence) \
	X(WaitForFences) \
	X(ResetFences) \
	X(QueueSubmit)
#define DECLARE(name) static PFN_vk##name vk##name;
INSTANCE_FUNCTIONS(DECLARE)
DEVICE_FUNCTIONS(DECLARE)
#undef DECLARE
static PFN_vkGetInstanceProcAddr get_instance_proc;
static PFN_vkGetPhysicalDeviceFeatures2KHR get_features2;
static PFN_vkDestroyDebugUtilsMessengerEXT destroy_debug;
static volatile sig_atomic_t interrupted;
static atomic_uint validation_errors;

struct window {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shell *legacy;
	struct wl_shell_surface *legacy_surface;
	struct xdg_wm_base *xdg;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	struct wl_surface *surface;
	struct wl_callback *frame;
	uint32_t width, height, callbacks;
	bool configured, closed, resized;
};
struct chain {
	VkSwapchainKHR handle;
	VkExtent2D extent;
	VkFormat format;
	uint32_t count;
	VkImage *images;
	VkImageView *views;
	VkFramebuffer *framebuffers;
	VkCommandBuffer *commands;
	VkSemaphore *complete;
	VkRenderPass pass;
	VkBuffer readback;
	VkDeviceMemory memory;
	void *mapped;
	bool coherent;
};
struct probe {
	void *loader;
	VkInstance instance;
	VkDebugUtilsMessengerEXT debug;
	VkPhysicalDevice physical;
	VkDevice device;
	VkSurfaceKHR surface;
	VkQueue queue;
	uint32_t family;
	VkCommandPool pool;
	VkSemaphore acquired;
	VkFence rendered, presented;
	bool maintenance, present_pending;
	struct window window;
	struct chain chain;
	uint32_t generations, submitted, checked;
};
#define CHECK(call) \
	do { \
		VkResult check_result = (call); \
		if (check_result != VK_SUCCESS) { \
			fprintf(stderr, "WSI_ERROR:%s:%d\n", #call, check_result); \
			return false; \
		} \
	} while (0)

static void watchdog(int signal_number) {
	const char message[] = "WSI_TIMEOUT\n";
	(void)signal_number;
	(void)write(STDERR_FILENO, message, sizeof(message) - 1);
	_exit(124);
}
static void interrupt(int signal_number) {
	(void)signal_number;
	interrupted = 1;
}
static VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT *data, void *user) {
	(void)type;
	(void)user;
	if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		atomic_fetch_add(&validation_errors, 1);
	}
	fprintf(stderr, "WSI_VALIDATION:%u:%s\n", severity, data->pMessage);
	return VK_FALSE;
}
static bool has_extension(const VkExtensionProperties *values, uint32_t count, const char *name) {
	for (uint32_t i = 0; i < count; i++) {
		if (strcmp(values[i].extensionName, name) == 0) {
			return true;
		}
	}
	return false;
}
static void xdg_ping(void *data, struct xdg_wm_base *shell, uint32_t serial) {
	(void)data;
	xdg_wm_base_pong(shell, serial);
}
static const struct xdg_wm_base_listener xdg_listener = { .ping = xdg_ping };
static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
	struct window *w = data;
	if (strcmp(interface, "wl_compositor") == 0) {
		w->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version < 3 ? version : 3);
	} else if (strcmp(interface, "wl_shell") == 0) {
		w->legacy = wl_registry_bind(registry, name, &wl_shell_interface, 1);
	} else if (strcmp(interface, "xdg_wm_base") == 0) {
		w->xdg = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(w->xdg, &xdg_listener, w);
	}
}
static void removed(void *data, struct wl_registry *registry, uint32_t name) {
	(void)data;
	(void)registry;
	(void)name;
}
static const struct wl_registry_listener registry_listener = { global, removed };
static void configure_size(struct window *w, int32_t width, int32_t height) {
	if (width > 0 && height > 0 && ((uint32_t)width != w->width || (uint32_t)height != w->height)) {
		w->width = width;
		w->height = height;
		w->resized = true;
		printf("WSI_CONFIGURE:%u:%u\n", w->width, w->height);
	}
}
static void xdg_configure(void *data, struct xdg_surface *surface, uint32_t serial) {
	struct window *w = data;
	xdg_surface_ack_configure(surface, serial);
	w->configured = true;
}
static const struct xdg_surface_listener surface_listener = { .configure = xdg_configure };
static void toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height, struct wl_array *states) {
	(void)toplevel;
	(void)states;
	configure_size(data, width, height);
}
static void toplevel_close(void *data, struct xdg_toplevel *toplevel) {
	(void)toplevel;
	((struct window *)data)->closed = true;
}
static const struct xdg_toplevel_listener toplevel_listener = { .configure = toplevel_configure, .close = toplevel_close };
static void legacy_ping(void *data, struct wl_shell_surface *surface, uint32_t serial) {
	(void)data;
	wl_shell_surface_pong(surface, serial);
}
static void legacy_configure(void *data, struct wl_shell_surface *surface, uint32_t edges, int32_t width, int32_t height) {
	(void)surface;
	(void)edges;
	configure_size(data, width, height);
}
static void popup_done(void *data, struct wl_shell_surface *surface) {
	(void)data;
	(void)surface;
}
static const struct wl_shell_surface_listener legacy_listener = { legacy_ping, legacy_configure, popup_done };
static void frame_done(void *data, struct wl_callback *callback, uint32_t time) {
	struct window *w = data;
	(void)time;
	wl_callback_destroy(callback);
	w->frame = NULL;
	w->callbacks++;
}
static const struct wl_callback_listener frame_listener = { frame_done };

static bool pump(struct window *w) {
	if (interrupted || w->closed || wl_display_get_error(w->display)) {
		return false;
	}
	while (wl_display_prepare_read(w->display) != 0) {
		if (wl_display_dispatch_pending(w->display) < 0) {
			return false;
		}
	}
	int flushed = wl_display_flush(w->display);
	struct pollfd fd = { .fd = wl_display_get_fd(w->display), .events = POLLIN };
	if (flushed < 0) {
		if (errno != EAGAIN) {
			wl_display_cancel_read(w->display);
			return false;
		}
		fd.events |= POLLOUT;
	}
	int ready = poll(&fd, 1, 2);
	if (ready > 0 && (fd.revents & POLLIN)) {
		if (wl_display_read_events(w->display) < 0 || wl_display_dispatch_pending(w->display) < 0) {
			return false;
		}
	} else {
		wl_display_cancel_read(w->display);
		if ((ready < 0 && errno != EINTR) || (fd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
			return false;
		}
	}
	return !interrupted && !w->closed;
}
static bool create_window(struct window *w, const char *shell) {
	w->width = 640;
	w->height = 480;
	w->display = wl_display_connect(NULL);
	if (!w->display) {
		perror("wl_display_connect");
		return false;
	}
	w->registry = wl_display_get_registry(w->display);
	wl_registry_add_listener(w->registry, &registry_listener, w);
	if (wl_display_roundtrip(w->display) < 0 || !w->compositor) {
		return false;
	}
	w->surface = wl_compositor_create_surface(w->compositor);
	bool legacy = strcmp(shell, "legacy") == 0 || (strcmp(shell, "auto") == 0 && w->legacy);
	if (legacy && w->legacy) {
		w->legacy_surface = wl_shell_get_shell_surface(w->legacy, w->surface);
		wl_shell_surface_add_listener(w->legacy_surface, &legacy_listener, w);
		wl_shell_surface_set_class(w->legacy_surface, "org.godotengine.aurora_wsi_probe");
		wl_shell_surface_set_title(w->legacy_surface, "Aurora Vulkan WSI probe");
		wl_shell_surface_set_maximized(w->legacy_surface, NULL);
		w->configured = true; // wl_shell has no initial configure/ack handshake.
		puts("WSI_SHELL:legacy");
	} else if (!legacy && w->xdg) {
		w->xdg_surface = xdg_wm_base_get_xdg_surface(w->xdg, w->surface);
		xdg_surface_add_listener(w->xdg_surface, &surface_listener, w);
		w->toplevel = xdg_surface_get_toplevel(w->xdg_surface);
		xdg_toplevel_add_listener(w->toplevel, &toplevel_listener, w);
		xdg_toplevel_set_app_id(w->toplevel, "org.godotengine.aurora_wsi_probe");
		xdg_toplevel_set_title(w->toplevel, "Aurora Vulkan WSI probe");
		puts("WSI_SHELL:xdg");
	} else {
		fprintf(stderr, "WSI_SHELL_UNAVAILABLE:%s\n", shell);
		return false;
	}
	wl_surface_commit(w->surface);
	while (!w->configured) {
		if (!pump(w)) {
			return false;
		}
	}
	return true;
}

static bool create_instance(struct probe *p, const char *loader_name, bool validate, bool *surface_maintenance) {
	const char *names[] = { "libvulkan.so.1", "libvulkan.so" };
	for (unsigned i = 0; i < (loader_name ? 1u : 2u); i++) {
		const char *name = loader_name ? loader_name : names[i];
		p->loader = dlopen(name, RTLD_NOW | RTLD_LOCAL);
		if (p->loader) {
			printf("LOADER_OK:%s\n", name);
			break;
		}
		fprintf(stderr, "LOADER_MISSING:%s:%s\n", name, dlerror());
	}
	if (!p->loader) {
		return false;
	}
	get_instance_proc = (PFN_vkGetInstanceProcAddr)dlsym(p->loader, "vkGetInstanceProcAddr");
	if (!get_instance_proc) {
		return false;
	}
	PFN_vkEnumerateInstanceExtensionProperties extensions = (PFN_vkEnumerateInstanceExtensionProperties)get_instance_proc(NULL, "vkEnumerateInstanceExtensionProperties");
	PFN_vkCreateInstance create = (PFN_vkCreateInstance)get_instance_proc(NULL, "vkCreateInstance");
	if (!extensions || !create) {
		return false;
	}
	uint32_t count = 0;
	CHECK(extensions(NULL, &count, NULL));
	VkExtensionProperties *values = calloc(count, sizeof(*values));
	if (!values) {
		return false;
	}
	VkResult result = extensions(NULL, &count, values);
	const char *enabled[6] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME };
	uint32_t enabled_count = 2;
	bool properties2 = has_extension(values, count, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
	*surface_maintenance = properties2 &&
			has_extension(values, count, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME) &&
			has_extension(values, count, VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
	if (*surface_maintenance) {
		enabled[enabled_count++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
		enabled[enabled_count++] = VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME;
		enabled[enabled_count++] = VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME;
	}
	bool supported = result == VK_SUCCESS;
	for (uint32_t i = 0; i < enabled_count; i++) {
		if (!has_extension(values, count, enabled[i])) {
			fprintf(stderr, "WSI_INSTANCE_EXTENSION_MISSING:%s\n", enabled[i]);
			supported = false;
		}
	}
	if (validate) {
		supported = supported && has_extension(values, count, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		enabled[enabled_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	}
	free(values);
	if (!supported) {
		return false;
	}
	const char *layer = "VK_LAYER_KHRONOS_validation";
	VkDebugUtilsMessengerCreateInfoEXT debug_info = {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = validation,
	};
	VkApplicationInfo application = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "Aurora WSI diagnostic", .apiVersion = VK_API_VERSION_1_0 };
	VkInstanceCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pNext = validate ? &debug_info : NULL,
		.pApplicationInfo = &application,
		.enabledExtensionCount = enabled_count,
		.ppEnabledExtensionNames = enabled,
		.enabledLayerCount = validate ? 1 : 0,
		.ppEnabledLayerNames = validate ? &layer : NULL,
	};
	CHECK(create(&info, NULL, &p->instance));
#define LOAD(name) \
	vk##name = (PFN_vk##name)get_instance_proc(p->instance, "vk" #name); \
	if (!vk##name) { \
		fprintf(stderr, "WSI_FUNCTION_MISSING:vk%s\n", #name); \
		return false; \
	}
	INSTANCE_FUNCTIONS(LOAD)
#undef LOAD
	if (*surface_maintenance) {
		get_features2 = (PFN_vkGetPhysicalDeviceFeatures2KHR)get_instance_proc(p->instance, "vkGetPhysicalDeviceFeatures2KHR");
	}
	if (validate) {
		PFN_vkCreateDebugUtilsMessengerEXT create_debug = (PFN_vkCreateDebugUtilsMessengerEXT)get_instance_proc(p->instance, "vkCreateDebugUtilsMessengerEXT");
		destroy_debug = (PFN_vkDestroyDebugUtilsMessengerEXT)get_instance_proc(p->instance, "vkDestroyDebugUtilsMessengerEXT");
		if (!create_debug || !destroy_debug) {
			return false;
		}
		CHECK(create_debug(p->instance, &debug_info, NULL, &p->debug));
	}
	return true;
}

static bool create_device(struct probe *p, uint32_t index, bool surface_maintenance, bool require_fence) {
	VkWaylandSurfaceCreateInfoKHR surface_info = { .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR, .display = p->window.display, .surface = p->window.surface };
	CHECK(vkCreateWaylandSurfaceKHR(p->instance, &surface_info, NULL, &p->surface));
	uint32_t count = 0;
	CHECK(vkEnumeratePhysicalDevices(p->instance, &count, NULL));
	if (index >= count) {
		fprintf(stderr, "WSI_DEVICE_INDEX:%u:count=%u\n", index, count);
		return false;
	}
	VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
	if (!devices) {
		return false;
	}
	VkResult result = vkEnumeratePhysicalDevices(p->instance, &count, devices);
	if (result == VK_SUCCESS && index < count) {
		p->physical = devices[index];
	}
	free(devices);
	if (!p->physical) {
		return false;
	}
	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(p->physical, &properties);
	printf("WSI_DEVICE:%u:%s:type=%u:api=%u.%u.%u:driver=%u\n", index, properties.deviceName, properties.deviceType, VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion), VK_VERSION_PATCH(properties.apiVersion), properties.driverVersion);
	vkGetPhysicalDeviceQueueFamilyProperties(p->physical, &count, NULL);
	VkQueueFamilyProperties *families = calloc(count, sizeof(*families));
	if (!families) {
		return false;
	}
	vkGetPhysicalDeviceQueueFamilyProperties(p->physical, &count, families);
	p->family = UINT32_MAX;
	for (uint32_t i = 0; i < count; i++) {
		VkBool32 present = VK_FALSE;
		result = vkGetPhysicalDeviceSurfaceSupportKHR(p->physical, i, p->surface, &present);
		if (result == VK_SUCCESS && present && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
			p->family = i;
			break;
		}
	}
	free(families);
	if (p->family == UINT32_MAX) {
		puts("WSI_NO_COMBINED_GRAPHICS_PRESENT_QUEUE");
		return false;
	}
	CHECK(vkEnumerateDeviceExtensionProperties(p->physical, NULL, &count, NULL));
	VkExtensionProperties *values = calloc(count, sizeof(*values));
	if (!values) {
		return false;
	}
	result = vkEnumerateDeviceExtensionProperties(p->physical, NULL, &count, values);
	bool swapchain = result == VK_SUCCESS && has_extension(values, count, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	bool maintenance = surface_maintenance && get_features2 && has_extension(values, count, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
	free(values);
	if (!swapchain) {
		puts("WSI_SWAPCHAIN_EXTENSION_MISSING");
		return false;
	}
	VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT feature = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT };
	if (maintenance) {
		VkPhysicalDeviceFeatures2KHR features = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR, .pNext = &feature };
		get_features2(p->physical, &features);
		p->maintenance = feature.swapchainMaintenance1;
	}
	printf("WSI_PRESENT_FENCE:%s\n", p->maintenance ? "extension" : "legacy-wait-idle-limitation");
	if (require_fence && !p->maintenance) {
		return false;
	}
	const char *enabled[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME };
	float priority = 1.0f;
	VkDeviceQueueCreateInfo queue = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = p->family, .queueCount = 1, .pQueuePriorities = &priority };
	VkDeviceCreateInfo info = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = p->maintenance ? &feature : NULL, .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue, .enabledExtensionCount = p->maintenance ? 2 : 1, .ppEnabledExtensionNames = enabled };
	CHECK(vkCreateDevice(p->physical, &info, NULL, &p->device));
#define LOAD(name) \
	vk##name = (PFN_vk##name)vkGetDeviceProcAddr(p->device, "vk" #name); \
	if (!vk##name) { \
		fprintf(stderr, "WSI_FUNCTION_MISSING:vk%s\n", #name); \
		return false; \
	}
	DEVICE_FUNCTIONS(LOAD)
#undef LOAD
	vkGetDeviceQueue(p->device, p->family, 0, &p->queue);
	VkCommandPoolCreateInfo pool = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = p->family };
	CHECK(vkCreateCommandPool(p->device, &pool, NULL, &p->pool));
	VkSemaphoreCreateInfo semaphore = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	CHECK(vkCreateSemaphore(p->device, &semaphore, NULL, &p->acquired));
	VkFenceCreateInfo fence = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
	CHECK(vkCreateFence(p->device, &fence, NULL, &p->rendered));
	fence.flags = 0;
	if (p->maintenance) {
		CHECK(vkCreateFence(p->device, &fence, NULL, &p->presented));
	}
	return true;
}

static bool wait_fence(struct probe *p, VkFence fence) {
	for (;;) {
		VkResult result = vkWaitForFences(p->device, 1, &fence, VK_TRUE, 1000000);
		if (result == VK_SUCCESS) {
			return true;
		}
		if (result != VK_TIMEOUT || !pump(&p->window)) {
			fprintf(stderr, "WSI_FENCE_FAILED:%d\n", result);
			return false;
		}
	}
}
static void destroy_chain(struct probe *p) {
	struct chain *c = &p->chain;
	if (c->mapped) {
		vkUnmapMemory(p->device, c->memory);
	}
	if (c->readback) {
		vkDestroyBuffer(p->device, c->readback, NULL);
	}
	if (c->memory) {
		vkFreeMemory(p->device, c->memory, NULL);
	}
	for (uint32_t i = 0; i < c->count; i++) {
		if (c->commands && c->commands[i]) {
			vkFreeCommandBuffers(p->device, p->pool, 1, &c->commands[i]);
		}
		if (c->framebuffers && c->framebuffers[i]) {
			vkDestroyFramebuffer(p->device, c->framebuffers[i], NULL);
		}
		if (c->views && c->views[i]) {
			vkDestroyImageView(p->device, c->views[i], NULL);
		}
		if (c->complete && c->complete[i]) {
			vkDestroySemaphore(p->device, c->complete[i], NULL);
		}
	}
	if (c->pass) {
		vkDestroyRenderPass(p->device, c->pass, NULL);
	}
	if (c->handle) {
		vkDestroySwapchainKHR(p->device, c->handle, NULL);
	}
	free(c->images);
	free(c->views);
	free(c->framebuffers);
	free(c->commands);
	free(c->complete);
	memset(c, 0, sizeof(*c));
}
static bool create_readback(struct probe *p) {
	struct chain *c = &p->chain;
	VkDeviceSize bytes = (VkDeviceSize)c->extent.width * c->extent.height * 4;
	VkBufferCreateInfo info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = bytes, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
	CHECK(vkCreateBuffer(p->device, &info, NULL, &c->readback));
	VkMemoryRequirements requirements;
	vkGetBufferMemoryRequirements(p->device, c->readback, &requirements);
	VkPhysicalDeviceMemoryProperties memory;
	vkGetPhysicalDeviceMemoryProperties(p->physical, &memory);
	uint32_t type = UINT32_MAX;
	for (uint32_t i = 0; i < memory.memoryTypeCount; i++) {
		if ((requirements.memoryTypeBits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
			type = i;
			if (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
				break;
			}
		}
	}
	if (type == UINT32_MAX || requirements.size > SIZE_MAX) {
		return false;
	}
	c->coherent = memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	VkMemoryAllocateInfo allocation = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = requirements.size, .memoryTypeIndex = type };
	CHECK(vkAllocateMemory(p->device, &allocation, NULL, &c->memory));
	CHECK(vkBindBufferMemory(p->device, c->readback, c->memory, 0));
	CHECK(vkMapMemory(p->device, c->memory, 0, VK_WHOLE_SIZE, 0, &c->mapped));
	return true;
}
static bool record(struct probe *p, uint32_t index) {
	struct chain *c = &p->chain;
	VkCommandBuffer command = c->commands[index];
	VkCommandBufferBeginInfo begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	CHECK(vkBeginCommandBuffer(command, &begin));
	VkClearValue black = { .color.float32 = { 0, 0, 0, 1 } };
	VkRenderPassBeginInfo pass = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = c->pass, .framebuffer = c->framebuffers[index], .renderArea.extent = c->extent, .clearValueCount = 1, .pClearValues = &black };
	vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
	const float colors[4][4] = { { 1, 0, 0, 1 }, { 0, 1, 0, 1 }, { 0, 0, 1, 1 }, { 1, 1, 0, 1 } };
	for (unsigned i = 0; i < 4; i++) {
		uint32_t x = (i % 2) * (c->extent.width / 2);
		uint32_t y = (i / 2) * (c->extent.height / 2);
		VkClearAttachment clear = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .colorAttachment = 0 };
		memcpy(clear.clearValue.color.float32, colors[i], sizeof(colors[i]));
		VkClearRect rect = { .rect = { .offset = { x, y }, .extent = { i % 2 ? c->extent.width - x : c->extent.width / 2, i / 2 ? c->extent.height - y : c->extent.height / 2 } }, .layerCount = 1 };
		vkCmdClearAttachments(command, 1, &clear, 1, &rect);
	}
	vkCmdEndRenderPass(command);
	if (c->readback) {
		VkBufferImageCopy copy = { .imageSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1 }, .imageExtent = { c->extent.width, c->extent.height, 1 } };
		vkCmdCopyImageToBuffer(command, c->images[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, c->readback, 1, &copy);
		VkImageMemoryBarrier image = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = c->images[index],
			.subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 },
		};
		VkBufferMemoryBarrier buffer = { .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .buffer = c->readback, .size = VK_WHOLE_SIZE };
		vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 1, &buffer, 1, &image);
	}
	CHECK(vkEndCommandBuffer(command));
	return true;
}
static bool create_chain(struct probe *p, bool require_readback) {
	struct chain *c = &p->chain;
	VkSurfaceCapabilitiesKHR caps;
	CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(p->physical, p->surface, &caps));
	if (!(caps.supportedTransforms & caps.currentTransform)) {
		return false;
	}
	VkExtent2D extent = caps.currentExtent;
	if (extent.width == UINT32_MAX) {
		extent = (VkExtent2D){ p->window.width, p->window.height };
		if (extent.width < caps.minImageExtent.width) {
			extent.width = caps.minImageExtent.width;
		}
		if (extent.width > caps.maxImageExtent.width) {
			extent.width = caps.maxImageExtent.width;
		}
		if (extent.height < caps.minImageExtent.height) {
			extent.height = caps.minImageExtent.height;
		}
		if (extent.height > caps.maxImageExtent.height) {
			extent.height = caps.maxImageExtent.height;
		}
	}
	if (extent.width < 2 || extent.height < 2 || extent.width > 8192 || extent.height > 8192) {
		fprintf(stderr, "WSI_EXTENT_UNAVAILABLE:%u:%u\n", extent.width, extent.height);
		return false;
	}
	uint32_t count = 0;
	CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(p->physical, p->surface, &count, NULL));
	VkSurfaceFormatKHR *formats = calloc(count, sizeof(*formats));
	if (!formats) {
		return false;
	}
	VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(p->physical, p->surface, &count, formats);
	VkSurfaceFormatKHR selected = { .format = VK_FORMAT_UNDEFINED };
	for (uint32_t i = 0; result == VK_SUCCESS && i < count; i++) {
		if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM || formats[i].format == VK_FORMAT_R8G8B8A8_UNORM || formats[i].format == VK_FORMAT_B8G8R8A8_SRGB || formats[i].format == VK_FORMAT_R8G8B8A8_SRGB) {
			selected = formats[i];
			break;
		}
	}
	if (result == VK_SUCCESS && count == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
		selected = formats[0];
		selected.format = VK_FORMAT_B8G8R8A8_UNORM;
	}
	free(formats);
	if (selected.format == VK_FORMAT_UNDEFINED) {
		return false;
	}
	bool readback = caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	if (require_readback && !readback) {
		puts("WSI_READBACK_UNSUPPORTED");
		return false;
	}
	VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	while (!(caps.supportedCompositeAlpha & alpha) && alpha < VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
		alpha <<= 1;
	}
	uint32_t images = caps.minImageCount + 1;
	if (caps.maxImageCount && images > caps.maxImageCount) {
		images = caps.maxImageCount;
	}
	VkSwapchainKHR old = c->handle;
	VkSwapchainKHR next = VK_NULL_HANDLE;
	VkSwapchainCreateInfoKHR info = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = p->surface,
		.minImageCount = images,
		.imageFormat = selected.format,
		.imageColorSpace = selected.colorSpace,
		.imageExtent = extent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (readback ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0),
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.preTransform = caps.currentTransform,
		.compositeAlpha = alpha,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR,
		.clipped = VK_TRUE,
		.oldSwapchain = old,
	};
	CHECK(vkCreateSwapchainKHR(p->device, &info, NULL, &next));
	destroy_chain(p); // Caller has waited for GPU/present fences before replacement.
	if (p->window.frame) {
		wl_callback_destroy(p->window.frame);
		p->window.frame = NULL;
	}
	c->handle = next;
	c->extent = extent;
	c->format = selected.format;
	CHECK(vkGetSwapchainImagesKHR(p->device, c->handle, &c->count, NULL));
	if (!c->count) {
		return false;
	}
	c->images = calloc(c->count, sizeof(*c->images));
	c->views = calloc(c->count, sizeof(*c->views));
	c->framebuffers = calloc(c->count, sizeof(*c->framebuffers));
	c->commands = calloc(c->count, sizeof(*c->commands));
	c->complete = calloc(c->count, sizeof(*c->complete));
	if (!c->images || !c->views || !c->framebuffers || !c->commands || !c->complete) {
		return false;
	}
	uint32_t actual = c->count;
	CHECK(vkGetSwapchainImagesKHR(p->device, c->handle, &actual, c->images));
	if (actual != c->count) {
		return false;
	}
	if (readback && !create_readback(p)) {
		return false;
	}
	VkAttachmentDescription attachment = { .format = c->format, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = readback ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
	VkAttachmentReference color = { .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkSubpassDescription subpass = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &color };
	VkSubpassDependency dependencies[2] = {
		{ .srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0, .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT },
		{ .srcSubpass = 0, .dstSubpass = VK_SUBPASS_EXTERNAL, .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .dstStageMask = readback ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, .dstAccessMask = readback ? VK_ACCESS_TRANSFER_READ_BIT : 0 },
	};
	VkRenderPassCreateInfo render_pass = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &attachment, .subpassCount = 1, .pSubpasses = &subpass, .dependencyCount = 2, .pDependencies = dependencies };
	CHECK(vkCreateRenderPass(p->device, &render_pass, NULL, &c->pass));
	VkCommandBufferAllocateInfo commands = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = p->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = c->count };
	CHECK(vkAllocateCommandBuffers(p->device, &commands, c->commands));
	for (uint32_t i = 0; i < c->count; i++) {
		VkImageViewCreateInfo view = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = c->images[i], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = c->format, .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 } };
		CHECK(vkCreateImageView(p->device, &view, NULL, &c->views[i]));
		VkFramebufferCreateInfo framebuffer = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = c->pass, .attachmentCount = 1, .pAttachments = &c->views[i], .width = extent.width, .height = extent.height, .layers = 1 };
		CHECK(vkCreateFramebuffer(p->device, &framebuffer, NULL, &c->framebuffers[i]));
		VkSemaphoreCreateInfo semaphore = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		CHECK(vkCreateSemaphore(p->device, &semaphore, NULL, &c->complete[i]));
		if (!record(p, i)) {
			return false;
		}
	}
	p->generations++;
	p->window.resized = false;
	printf("WSI_SWAPCHAIN:%u:size=%ux%u:images=%u:format=%u:current=%u:supported=%u:pre=%u:readback=%u\n", p->generations, extent.width, extent.height, c->count, c->format, caps.currentTransform, caps.supportedTransforms, info.preTransform, readback);
	return true;
}
static bool check_pixels(struct probe *p) {
	struct chain *c = &p->chain;
	if (!c->readback) {
		return true;
	}
	if (!c->coherent) {
		VkMappedMemoryRange range = { .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, .memory = c->memory, .size = VK_WHOLE_SIZE };
		CHECK(vkInvalidateMappedMemoryRanges(p->device, 1, &range));
	}
	const uint8_t expected[4][4] = { { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 255, 255 }, { 255, 255, 0, 255 } };
	bool bgra = c->format == VK_FORMAT_B8G8R8A8_UNORM || c->format == VK_FORMAT_B8G8R8A8_SRGB;
	for (unsigned i = 0; i < 4; i++) {
		uint32_t x = (i % 2) * (c->extent.width / 2) + c->extent.width / 4;
		uint32_t y = (i / 2) * (c->extent.height / 2) + c->extent.height / 4;
		const uint8_t *pixel = (const uint8_t *)c->mapped + ((size_t)y * c->extent.width + x) * 4;
		uint8_t rgba[] = { pixel[bgra ? 2 : 0], pixel[1], pixel[bgra ? 0 : 2], pixel[3] };
		if (memcmp(rgba, expected[i], 4)) {
			fprintf(stderr, "WSI_PIXEL_MISMATCH:%u:%u,%u,%u,%u\n", i, rgba[0], rgba[1], rgba[2], rgba[3]);
			return false;
		}
	}
	p->checked++;
	return true;
}
static bool quiesce(struct probe *p) {
	CHECK(vkDeviceWaitIdle(p->device));
	if (p->present_pending) {
		// Cleanup must complete even after a close/SIGTERM. The process alarm
		// bounds a broken driver; an interrupt must not authorize early free.
		CHECK(vkWaitForFences(p->device, 1, &p->presented, VK_TRUE, UINT64_MAX));
		p->present_pending = false;
	}
	return true;
}
static void cleanup(struct probe *p) {
	if (p->device && vkDeviceWaitIdle) {
		(void)quiesce(p); // Watchdog also bounds driver cleanup.
		destroy_chain(p);
		if (p->rendered) {
			vkDestroyFence(p->device, p->rendered, NULL);
		}
		if (p->presented) {
			vkDestroyFence(p->device, p->presented, NULL);
		}
		if (p->acquired) {
			vkDestroySemaphore(p->device, p->acquired, NULL);
		}
		if (p->pool) {
			vkDestroyCommandPool(p->device, p->pool, NULL);
		}
		vkDestroyDevice(p->device, NULL);
	}
	if (p->surface && vkDestroySurfaceKHR) {
		vkDestroySurfaceKHR(p->instance, p->surface, NULL);
	}
	if (p->debug && destroy_debug) {
		destroy_debug(p->instance, p->debug, NULL);
	}
	if (p->instance && vkDestroyInstance) {
		vkDestroyInstance(p->instance, NULL);
	}
	struct window *w = &p->window;
	if (w->frame) {
		wl_callback_destroy(w->frame);
	}
	if (w->toplevel) {
		xdg_toplevel_destroy(w->toplevel);
	}
	if (w->xdg_surface) {
		xdg_surface_destroy(w->xdg_surface);
	}
	if (w->legacy_surface) {
		wl_shell_surface_destroy(w->legacy_surface);
	}
	if (w->surface) {
		wl_surface_destroy(w->surface);
	}
	if (w->xdg) {
		xdg_wm_base_destroy(w->xdg);
	}
	if (w->legacy) {
		wl_shell_destroy(w->legacy);
	}
	if (w->compositor) {
		wl_compositor_destroy(w->compositor);
	}
	if (w->registry) {
		wl_registry_destroy(w->registry);
	}
	if (w->display) {
		wl_display_disconnect(w->display);
	}
	if (p->loader) {
		dlclose(p->loader);
	}
}
static bool number(const char *value, uint32_t min, uint32_t max, uint32_t *destination) {
	char *end;
	errno = 0;
	unsigned long parsed = strtoul(value, &end, 10);
	if (errno || !*value || *end || parsed < min || parsed > max) {
		return false;
	}
	*destination = parsed;
	return true;
}
int main(int argc, char **argv) {
	uint32_t frames = 60, timeout = 30, device_index = 0;
	const char *shell = "auto", *loader = NULL;
	bool validate = false, require_readback = false, require_fence = false;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--validation")) {
			validate = true;
		} else if (!strcmp(argv[i], "--require-readback")) {
			require_readback = true;
		} else if (!strcmp(argv[i], "--require-present-fence")) {
			require_fence = true;
		} else if (!strcmp(argv[i], "--frames") && i + 1 < argc && number(argv[i + 1], 4, 10000, &frames)) {
			i++;
		} else if (!strcmp(argv[i], "--timeout") && i + 1 < argc && number(argv[i + 1], 1, 120, &timeout)) {
			i++;
		} else if (!strcmp(argv[i], "--device") && i + 1 < argc && number(argv[i + 1], 0, 255, &device_index)) {
			i++;
		} else if (!strcmp(argv[i], "--loader") && i + 1 < argc) {
			loader = argv[++i];
		} else if (!strcmp(argv[i], "--shell") && i + 1 < argc) {
			shell = argv[++i];
		} else {
			fprintf(stderr, "Usage: probe [--frames 4..10000] [--timeout 1..120] [--device N] [--shell auto|legacy|xdg] [--loader PATH] [--validation] [--require-readback] [--require-present-fence]\n");
			return 64;
		}
	}
	if (strcmp(shell, "auto") && strcmp(shell, "legacy") && strcmp(shell, "xdg")) {
		return 64;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	signal(SIGINT, interrupt);
	signal(SIGTERM, interrupt);
	signal(SIGALRM, watchdog);
	alarm(timeout);
	struct probe p = { 0 };
	bool surface_maintenance = false;
	int status = 3;
	if (!create_instance(&p, loader, validate, &surface_maintenance)) {
		if (!p.loader) {
			status = 2;
		}
		goto done;
	}
	status = 6;
	if (!create_window(&p.window, shell)) {
		goto done;
	}
	status = 4;
	if (!create_device(&p, device_index, surface_maintenance, require_fence)) {
		goto done;
	}
	status = 5;
	if (!create_chain(&p, require_readback)) {
		goto done;
	}
	bool forced = false, recreate = false;
	while (p.submitted < frames) {
		if (!pump(&p.window)) {
			goto done;
		}
		if (recreate || p.window.resized || (!forced && p.submitted >= frames / 2)) {
			if (!forced && p.submitted >= frames / 2) {
				// Exercise replacement even when the shell keeps a maximized size.
				forced = true;
				puts("WSI_RECREATE:forced-not-a-resize-claim");
			}
			if (!quiesce(&p) || !create_chain(&p, require_readback)) {
				goto done;
			}
			recreate = false;
		}
		if (!wait_fence(&p, p.rendered)) {
			goto done;
		}
		uint32_t index;
		VkResult acquired = vkAcquireNextImageKHR(p.device, p.chain.handle, 1000000, p.acquired, VK_NULL_HANDLE, &index);
		if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
			recreate = true;
			continue;
		}
		if (acquired == VK_TIMEOUT || acquired == VK_NOT_READY) {
			continue;
		}
		if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
			fprintf(stderr, "WSI_ACQUIRE_ERROR:%d\n", acquired);
			goto done;
		}
		if (index >= p.chain.count || vkResetFences(p.device, 1, &p.rendered) != VK_SUCCESS) {
			goto done;
		}
		VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1, .pWaitSemaphores = &p.acquired, .pWaitDstStageMask = &stage, .commandBufferCount = 1, .pCommandBuffers = &p.chain.commands[index], .signalSemaphoreCount = 1, .pSignalSemaphores = &p.chain.complete[index] };
		if (vkQueueSubmit(p.queue, 1, &submit, p.rendered) != VK_SUCCESS) {
			goto done;
		}
		if (!p.window.frame) {
			p.window.frame = wl_surface_frame(p.window.surface);
			wl_callback_add_listener(p.window.frame, &frame_listener, &p.window);
		}
		VkSwapchainPresentFenceInfoEXT present_fence = { .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT, .swapchainCount = 1, .pFences = &p.presented };
		if (p.maintenance && vkResetFences(p.device, 1, &p.presented) != VK_SUCCESS) {
			goto done;
		}
		VkPresentInfoKHR present = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .pNext = p.maintenance ? &present_fence : NULL, .waitSemaphoreCount = 1, .pWaitSemaphores = &p.chain.complete[index], .swapchainCount = 1, .pSwapchains = &p.chain.handle, .pImageIndices = &index };
		VkResult presented = vkQueuePresentKHR(p.queue, &present);
		p.present_pending = p.maintenance && (presented == VK_SUCCESS || presented == VK_SUBOPTIMAL_KHR || presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_ERROR_SURFACE_LOST_KHR);
		if (!wait_fence(&p, p.rendered) || !check_pixels(&p)) {
			goto done;
		}
		if (p.present_pending) {
			if (!wait_fence(&p, p.presented)) {
				goto done;
			}
			p.present_pending = false;
		}
		if (presented != VK_SUCCESS && presented != VK_SUBOPTIMAL_KHR && presented != VK_ERROR_OUT_OF_DATE_KHR) {
			fprintf(stderr, "WSI_PRESENT_ERROR:%d\n", presented);
			goto done;
		}
		if (presented == VK_ERROR_OUT_OF_DATE_KHR) {
			recreate = true;
			continue;
		}
		p.submitted++;
		// A successful QueuePresent alone does not establish compositor progress.
		while (p.window.frame) {
			if (!pump(&p.window)) {
				goto done;
			}
		}
		recreate = presented == VK_SUBOPTIMAL_KHR || acquired == VK_SUBOPTIMAL_KHR;
	}
	if (!quiesce(&p) || !forced || p.generations < 2 || p.window.callbacks < p.submitted) {
		goto done;
	}
	status = 0;
done:
	cleanup(&p); // Count validation errors from destruction as well.
	unsigned errors = atomic_load(&validation_errors);
	if (errors && !status) {
		status = 5;
	}
	alarm(0);
	printf("WSI_RESULT:%s:exit=%d:presented=%u:generations=%u:readbacks=%u:callbacks=%u:validation_errors=%u\n", status ? "FAIL" : "PASS", status, p.submitted, p.generations, p.checked, p.window.callbacks, errors);
	return status;
}
