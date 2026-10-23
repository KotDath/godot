/**************************************************************************/
/*  vulkan_probe.c                                                        */
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

/* Read-only diagnostic: no dependency on a system Vulkan SDK or loader at link time.
 * Build: cc -Ithirdparty/vulkan/include misc/aurora/vulkan_probe.c -ldl -o probe
 * Exit codes: 0 physical device found, 2 loader absent, 3 loader/instance failed,
 * 4 no usable device, 5 allocation/enumeration failed. WSI is not tested here. */
#define VK_NO_PROTOTYPES
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>

int main(void) {
	const char *names[] = { "libvulkan.so.1", "libvulkan.so" };
	void *loader = NULL;
	for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		loader = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
		if (loader) {
			printf("LOADER_OK:%s\n", names[i]);
			break;
		}
		printf("LOADER_MISSING:%s:%s\n", names[i], dlerror());
	}
	if (!loader) {
		/* Report exported entry points only; never substitute a driver for the
		 * loader or change the device's graphics configuration. */
		void *mali = dlopen("libmali.so.1", RTLD_LAZY | RTLD_LOCAL);
		if (mali) {
			printf("MALI_VK_ENTRY:%d\n", dlsym(mali, "vkGetInstanceProcAddr") != NULL);
			printf("MALI_ICD_ENTRY:%d\n", dlsym(mali, "vk_icdGetInstanceProcAddr") != NULL);
			dlclose(mali);
		} else {
			printf("MALI_LOAD_ERROR:%s\n", dlerror());
		}
		return 2;
	}
	PFN_vkGetInstanceProcAddr get_proc = (PFN_vkGetInstanceProcAddr)dlsym(loader, "vkGetInstanceProcAddr");
	if (!get_proc) {
		puts("LOADER_ENTRY_MISSING");
		dlclose(loader);
		return 3;
	}
	PFN_vkEnumerateInstanceExtensionProperties enumerate_extensions = (PFN_vkEnumerateInstanceExtensionProperties)get_proc(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
	PFN_vkCreateInstance create = (PFN_vkCreateInstance)get_proc(VK_NULL_HANDLE, "vkCreateInstance");
	if (!enumerate_extensions || !create) {
		dlclose(loader);
		return 3;
	}
	uint32_t count = 0;
	VkResult result = enumerate_extensions(NULL, &count, NULL);
	if (result != VK_SUCCESS) {
		printf("INSTANCE_EXTENSIONS_ERROR:%d\n", result);
		dlclose(loader);
		return 3;
	}
	VkExtensionProperties *extensions = count ? calloc(count, sizeof(*extensions)) : NULL;
	if (count && !extensions) {
		dlclose(loader);
		return 5;
	}
	if (count) {
		result = enumerate_extensions(NULL, &count, extensions);
	}
	if (extensions && (result == VK_SUCCESS || result == VK_INCOMPLETE)) {
		for (uint32_t i = 0; i < count; i++) {
			printf("INSTANCE_EXTENSION:%s\n", extensions[i].extensionName);
		}
	}
	free(extensions);
	VkApplicationInfo application = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "Godot Aurora Vulkan probe", .apiVersion = VK_API_VERSION_1_0 };
	VkInstanceCreateInfo info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &application };
	VkInstance instance = VK_NULL_HANDLE;
	result = create(&info, NULL, &instance);
	if (result != VK_SUCCESS) {
		printf("INSTANCE_ERROR:%d\n", result);
		dlclose(loader);
		return 3;
	}
	PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)get_proc(instance, "vkDestroyInstance");
	PFN_vkEnumeratePhysicalDevices enumerate_devices = (PFN_vkEnumeratePhysicalDevices)get_proc(instance, "vkEnumeratePhysicalDevices");
	PFN_vkGetPhysicalDeviceProperties properties = (PFN_vkGetPhysicalDeviceProperties)get_proc(instance, "vkGetPhysicalDeviceProperties");
	if (!destroy || !enumerate_devices || !properties) {
		if (destroy) {
			destroy(instance, NULL);
		}
		dlclose(loader);
		return 3;
	}
	count = 0;
	result = enumerate_devices(instance, &count, NULL);
	printf("PHYSICAL_DEVICES:%d:%u\n", result, count);
	int status = 4;
	if (result == VK_SUCCESS && count) {
		VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
		if (devices) {
			result = enumerate_devices(instance, &count, devices);
			if (result == VK_SUCCESS || result == VK_INCOMPLETE) {
				for (uint32_t i = 0; i < count; i++) {
					VkPhysicalDeviceProperties p;
					properties(devices[i], &p);
					printf("DEVICE:%s:api=%u.%u.%u:type=%u\n", p.deviceName, VK_VERSION_MAJOR(p.apiVersion), VK_VERSION_MINOR(p.apiVersion), VK_VERSION_PATCH(p.apiVersion), p.deviceType);
				}
				status = count ? 0 : 4;
			} else {
				status = 5;
			}
			free(devices);
		} else {
			status = 5;
		}
	}
	destroy(instance, NULL);
	dlclose(loader);
	return status;
}
