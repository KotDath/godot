/**************************************************************************/
/*  wayland_probe.c                                                       */
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

/* Read-only registry probe; build with the target compiler and -lwayland-client. */
#include <stdio.h>
#include <wayland-client.h>

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
	printf("%s %u\n", interface, version);
}

static void removed(void *data, struct wl_registry *registry, uint32_t name) {}

int main(void) {
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		perror("wl_display_connect");
		return 1;
	}
	struct wl_registry *registry = wl_display_get_registry(display);
	const struct wl_registry_listener listener = { global, removed };
	wl_registry_add_listener(registry, &listener, NULL);
	int result = wl_display_roundtrip(display);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
	return result < 0 ? 1 : 0;
}
