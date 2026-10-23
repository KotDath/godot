/**************************************************************************/
/*  legacy_shell.cpp                                                      */
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

#include "legacy_shell.h"

#include "protocol/surface_extension.gen.h"
#include "text_input_v2.h"

namespace AuroraLegacyShell {
namespace {
void ping(void *, wl_shell_surface *p_surface, uint32_t p_serial) {
	wl_shell_surface_pong(p_surface, p_serial);
}
void configure(void *p_data, wl_shell_surface *, uint32_t, int32_t p_width, int32_t p_height) {
	auto *window = static_cast<WaylandThread::WindowState *>(p_data);
	WaylandThread::window_state_update_size(window, p_width, p_height);
	window->ready = true;
}
void popup_done(void *, wl_shell_surface *) {}
void visibility(void *data, qt_extended_surface *, int32_t visible) {
	auto *window = static_cast<WaylandThread::WindowState *>(data);
	// qt_extended_surface uses QWindow::Visibility, not a boolean.
	if (visible != 1) { // Automatic does not specify a visibility state.
		window->suspended = visible == 0 || visible == 3; // Hidden or Minimized.
	}
	print_verbose(vformat("Aurora surface visibility: %d", visible));
}
void property(void *, qt_extended_surface *, const char *, wl_array *) {}
void close(void *p_data, qt_extended_surface *) {
	auto *window = static_cast<WaylandThread::WindowState *>(p_data);
	Ref<WaylandThread::WindowEventMessage> message;
	message.instantiate();
	message->id = window->id;
	message->event = DisplayServerEnums::WINDOW_EVENT_CLOSE_REQUEST;
	window->wayland_thread->push_message(message);
}
const qt_extended_surface_listener qt_listener = { visibility, property, close };
const wl_shell_surface_listener listener = { ping, configure, popup_done };
} //namespace

bool bind(WaylandThread::RegistryState &p_registry, wl_registry *p_wl_registry, uint32_t p_name, const char *p_interface) {
	if (AuroraTextInput::bind(p_registry, p_wl_registry, p_name, p_interface)) {
		return true;
	}
	if (strcmp(p_interface, qt_surface_extension_interface.name) == 0) {
		p_registry.qt_surface_extension = static_cast<qt_surface_extension *>(wl_registry_bind(p_wl_registry, p_name, &qt_surface_extension_interface, 1));
		p_registry.qt_surface_extension_name = p_name;
		return true;
	}
	if (strcmp(p_interface, wl_shell_interface.name) != 0) {
		return false;
	}
	p_registry.legacy_shell = static_cast<wl_shell *>(wl_registry_bind(p_wl_registry, p_name, &wl_shell_interface, 1));
	p_registry.legacy_shell_name = p_name;
	return true;
}
bool remove(WaylandThread::RegistryState &p_registry, uint32_t p_name) {
	if (AuroraTextInput::remove(p_registry, p_name)) {
		return true;
	}
	if (p_name == p_registry.qt_surface_extension_name) {
		qt_surface_extension_destroy(p_registry.qt_surface_extension);
		p_registry.qt_surface_extension = nullptr;
		p_registry.qt_surface_extension_name = 0;
		return true;
	}
	if (p_name != p_registry.legacy_shell_name) {
		return false;
	}
	wl_shell_destroy(p_registry.legacy_shell);
	p_registry.legacy_shell = nullptr;
	p_registry.legacy_shell_name = 0;
	return true;
}
void create(WaylandThread::WindowState &p_window) {
	p_window.legacy_shell_surface = wl_shell_get_shell_surface(p_window.registry->legacy_shell, p_window.wl_surface);
	wl_shell_surface_add_listener(p_window.legacy_shell_surface, &listener, &p_window);
	if (p_window.registry->qt_surface_extension) {
		p_window.qt_extended_surface = qt_surface_extension_get_extended_surface(p_window.registry->qt_surface_extension, p_window.wl_surface);
		qt_extended_surface_add_listener(p_window.qt_extended_surface, &qt_listener, &p_window);
	}
	// Mobile compositor controls the available area and sends configure.
	set_mode(p_window, DisplayServerEnums::WINDOW_MODE_MAXIMIZED);
	p_window.can_minimize = false;
	// wl_shell has no initial configure/ack handshake. Some compositors only
	// configure after the first attached buffer, unlike XDG shell. Let EGL
	// create that buffer at the requested size, then apply later configures.
	p_window.ready = true;
}
void destroy(WaylandThread::WindowState &p_window) {
	if (p_window.qt_extended_surface) {
		qt_extended_surface_destroy(p_window.qt_extended_surface);
		p_window.qt_extended_surface = nullptr;
	}
	if (p_window.legacy_shell_surface) {
		wl_shell_surface_destroy(p_window.legacy_shell_surface);
		p_window.legacy_shell_surface = nullptr;
	}
}
void set_mode(WaylandThread::WindowState &p_window, DisplayServerEnums::WindowMode p_mode) {
	switch (p_mode) {
		case DisplayServerEnums::WINDOW_MODE_FULLSCREEN:
		case DisplayServerEnums::WINDOW_MODE_EXCLUSIVE_FULLSCREEN:
			wl_shell_surface_set_fullscreen(p_window.legacy_shell_surface, WL_SHELL_SURFACE_FULLSCREEN_METHOD_DEFAULT, 0, nullptr);
			break;
		case DisplayServerEnums::WINDOW_MODE_MAXIMIZED:
			wl_shell_surface_set_maximized(p_window.legacy_shell_surface, nullptr);
			break;
		case DisplayServerEnums::WINDOW_MODE_WINDOWED:
			wl_shell_surface_set_toplevel(p_window.legacy_shell_surface);
			break;
		default:
			return;
	}
	p_window.mode = p_mode;
}
} //namespace AuroraLegacyShell
