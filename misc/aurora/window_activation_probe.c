/**************************************************************************/
/*  window_activation_probe.c                                             */
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

/* Standalone compositor-contract probe; it does not run or test the engine. */
#define _GNU_SOURCE
#include "surface_extension.h"

#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

struct probe {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shell *shell;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_keyboard *keyboard;
	struct qt_surface_extension *extension;
	struct wl_surface *surface;
	struct qt_extended_surface *extended;
	struct wl_callback *frame;
	const char *role;
	const char *mode;
	int raised;
};
static volatile sig_atomic_t stopped;

static uint64_t now_ms(void) {
	struct timespec value;
	clock_gettime(CLOCK_MONOTONIC, &value);
	return (uint64_t)value.tv_sec * 1000 + value.tv_nsec / 1000000;
}
static void event(struct probe *p, const char *name, int value) {
	printf("{\"role\":\"%s\",\"event\":\"%s\",\"value\":%d,\"ms\":%" PRIu64 "}\n", p->role, name, value, now_ms());
}
static void stop(int number) {
	stopped = 1;
}
static void activate(struct probe *p) {
	if (!p->raised) {
		qt_extended_surface_raise(p->extended);
		p->raised = 1;
		event(p, "raise", 1);
	}
}
static void frame_done(void *data, struct wl_callback *callback, uint32_t time) {
	struct probe *p = data;
	wl_callback_destroy(callback);
	p->frame = NULL;
	event(p, "frame", 1);
	if (strcmp(p->mode, "frame") == 0) {
		activate(p);
	}
}
static const struct wl_callback_listener frame_listener = { frame_done };
static void keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int fd, uint32_t size) {
	close(fd);
}
static void enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
	struct probe *p = data;
	if (surface == p->surface) {
		event(p, "focus", 1);
	}
}
static void leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface) {
	struct probe *p = data;
	if (surface == p->surface) {
		event(p, "focus", 0);
	}
}
static void key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t code, uint32_t state) {}
static void modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {}
static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keymap,
	.enter = enter,
	.leave = leave,
	.key = key,
	.modifiers = modifiers
};
static void capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
	struct probe *p = data;
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !p->keyboard) {
		p->keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(p->keyboard, &keyboard_listener, p);
	}
}
static const struct wl_seat_listener seat_listener = { .capabilities = capabilities };
static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
	struct probe *p = data;
	if (strcmp(interface, "wl_compositor") == 0) {
		p->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version < 3 ? version : 3);
	} else if (strcmp(interface, "wl_shell") == 0) {
		p->shell = wl_registry_bind(registry, name, &wl_shell_interface, 1);
	} else if (strcmp(interface, "wl_shm") == 0) {
		p->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, "qt_surface_extension") == 0) {
		p->extension = wl_registry_bind(registry, name, &qt_surface_extension_interface, 1);
	} else if (strcmp(interface, "wl_seat") == 0 && !p->seat) {
		p->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
		wl_seat_add_listener(p->seat, &seat_listener, p);
	}
}
static void removed(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = { global, removed };
static void ping(void *data, struct wl_shell_surface *surface, uint32_t serial) {
	wl_shell_surface_pong(surface, serial);
}
static void configure(void *data, struct wl_shell_surface *surface, uint32_t edges, int32_t width, int32_t height) {
	event(data, "configure", width);
}
static void popup_done(void *data, struct wl_shell_surface *surface) {}
static const struct wl_shell_surface_listener shell_listener = { ping, configure, popup_done };
static void visibility(void *data, struct qt_extended_surface *surface, int32_t value) {
	event(data, "visibility", value);
	struct probe *p = data;
	if (strcmp(p->mode, "visibility") == 0 && value >= 0 && value <= 5 && value != 1) {
		activate(p);
	}
}
static void property(void *data, struct qt_extended_surface *surface, const char *name, struct wl_array *value) {}
static void closed(void *data, struct qt_extended_surface *surface) {
	event(data, "close", 1);
	stopped = 1;
}
static const struct qt_extended_surface_listener extended_listener = { visibility, property, closed };

int main(int argc, char **argv) {
	if ((argc != 5 && argc != 6) || (strcmp(argv[1], "parent") && strcmp(argv[1], "child")) || (strcmp(argv[2], "none") && strcmp(argv[2], "before") && strcmp(argv[2], "visibility") && strcmp(argv[2], "frame"))) {
		fprintf(stderr, "Usage: probe parent|child none|before|visibility|frame seconds app-id [raise-request-file]\n");
		return 64;
	}
	int seconds = atoi(argv[3]);
	if (seconds < 1 || seconds > 60) {
		return 64;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	signal(SIGTERM, stop);
	signal(SIGINT, stop);
	struct probe p = { .role = argv[1], .mode = argv[2] };
	p.display = wl_display_connect(NULL);
	if (!p.display) {
		perror("wl_display_connect");
		return 1;
	}
	struct wl_registry *registry = wl_display_get_registry(p.display);
	wl_registry_add_listener(registry, &registry_listener, &p);
	if (wl_display_roundtrip(p.display) < 0 || wl_display_roundtrip(p.display) < 0 || !p.compositor || !p.shell || !p.shm || !p.extension || !p.keyboard) {
		fprintf(stderr, "Required wl_shell/Qt extension/SHM/keyboard unavailable\n");
		wl_display_disconnect(p.display);
		return 2;
	}
	p.surface = wl_compositor_create_surface(p.compositor);
	struct wl_shell_surface *shell_surface = wl_shell_get_shell_surface(p.shell, p.surface);
	wl_shell_surface_add_listener(shell_surface, &shell_listener, &p);
	p.extended = qt_surface_extension_get_extended_surface(p.extension, p.surface);
	qt_extended_surface_add_listener(p.extended, &extended_listener, &p);
	wl_shell_surface_set_maximized(shell_surface, NULL);
	p.frame = wl_surface_frame(p.surface);
	wl_callback_add_listener(p.frame, &frame_listener, &p);
	wl_surface_commit(p.surface); // Same initial buffer-less commit as the engine.
	wl_shell_surface_set_class(shell_surface, argv[4]);
	wl_shell_surface_set_title(shell_surface, p.role);
	if (strcmp(p.mode, "before") == 0) {
		activate(&p);
	}
	const int width = 480, height = 320, stride = width * 4;
	const size_t bytes = stride * height;
	char temporary[] = "/tmp/godot-activation-XXXXXX";
	int fd = mkstemp(temporary);
	if (fd < 0) {
		perror("mkstemp");
		return 1;
	}
	unlink(temporary);
	if (ftruncate(fd, bytes) < 0) {
		perror("ftruncate");
		return 1;
	}
	uint32_t *pixels = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (pixels == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	for (int i = 0; i < width * height; i++) {
		pixels[i] = strcmp(p.role, "parent") == 0 ? 0xff1d4f91 : 0xff139948;
	}
	struct wl_shm_pool *pool = wl_shm_create_pool(p.shm, fd, bytes);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	wl_surface_attach(p.surface, buffer, 0, 0);
	wl_surface_damage(p.surface, 0, 0, width, height);
	wl_surface_commit(p.surface);
	event(&p, "attached", 1);
	uint64_t deadline = now_ms() + seconds * 1000;
	int result = 0;
	while (!stopped && now_ms() < deadline) {
		if (argc == 6 && access(argv[5], F_OK) == 0 && unlink(argv[5]) == 0) {
			p.raised = 0;
			event(&p, "explicit_raise", 1);
			activate(&p);
		}
		while (wl_display_prepare_read(p.display) != 0) {
			if (wl_display_dispatch_pending(p.display) < 0) {
				result = 1;
				goto done;
			}
		}
		wl_display_flush(p.display);
		struct pollfd input = { .fd = wl_display_get_fd(p.display), .events = POLLIN };
		int ready = poll(&input, 1, 100);
		if (ready > 0 && (input.revents & POLLIN)) {
			if (wl_display_read_events(p.display) < 0 || wl_display_dispatch_pending(p.display) < 0) {
				result = 1;
				break;
			}
		} else {
			wl_display_cancel_read(p.display);
			if ((ready < 0 && errno != EINTR) || (input.revents & (POLLHUP | POLLERR | POLLNVAL))) {
				result = 1;
				break;
			}
		}
	}
done:
	if (p.frame) {
		wl_callback_destroy(p.frame);
	}
	wl_buffer_destroy(buffer);
	munmap(pixels, bytes);
	qt_extended_surface_destroy(p.extended);
	wl_shell_surface_destroy(shell_surface);
	wl_surface_destroy(p.surface);
	wl_display_flush(p.display);
	wl_display_disconnect(p.display);
	event(&p, "finished", result);
	return result;
}
