/**************************************************************************/
/*  window_activation.cpp                                                 */
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

#include "platform/linuxbsd/wayland/display_server_wayland.h"

#ifdef TOOLS_ENABLED
#include "core/object/callable_mp.h"
#include "editor/run/editor_run_bar.h"
#include "editor/settings/editor_settings.h"
#endif

namespace {
using ActivationRequest = WaylandThread::WindowState::AuroraActivationRequest;

bool accepts_activation(uint32_t p_flags) {
	return !(p_flags & ((1 << DisplayServerEnums::WINDOW_FLAG_NO_FOCUS) | (1 << DisplayServerEnums::WINDOW_FLAG_POPUP_WM_HINT)));
}
} // namespace

void AuroraLegacyShell::activate_pending(WaylandThread::WindowState &p_window) {
	// Called under the Wayland mutex, including from its event thread. A
	// concrete Qt visibility notification acknowledges the mapped window;
	// Lipstick ignores a raise before mapping. No frame callback is needed.
	if (!p_window.aurora_visibility_received || p_window.aurora_activation_request == ActivationRequest::NONE || !p_window.qt_extended_surface || !p_window.legacy_shell_surface) {
		return;
	}
	bool initial = p_window.aurora_activation_request == ActivationRequest::INITIAL;
	p_window.aurora_activation_request = ActivationRequest::NONE;
	qt_extended_surface_raise(p_window.qt_extended_surface);
	wl_display_flush(p_window.wayland_thread->get_wl_display());
	print_verbose(vformat("Aurora activation: window=%d initial=%s protocol=qt", p_window.id, initial));
}

void DisplayServerWayland::_aurora_update_activation_flags(DisplayServerEnums::WindowID p_window) {
	const WindowData *window = windows.getptr(p_window);
	WaylandThread::WindowState *state = wayland_thread.window_get_state(p_window);
	if (window && state && !accepts_activation(window->flags)) {
		state->aurora_activation_request = ActivationRequest::NONE;
	}
}

void DisplayServerWayland::_aurora_request_activation(DisplayServerEnums::WindowID p_window, bool p_initial) {
	// The caller holds wayland_thread.mutex. Queued requests are collapsed to
	// one and can be cancelled by a subsequent NO_FOCUS/popup flag change.
	const WindowData *window = windows.getptr(p_window);
	if (!window || !window->created || !window->visible || !accepts_activation(window->flags)) {
		return;
	}
	WaylandThread::WindowState *state = wayland_thread.window_get_state(p_window);
	if (!state) {
		return;
	}
	if (state->legacy_shell_surface) {
		if (state->qt_extended_surface) {
			if (!p_initial || state->aurora_activation_request != ActivationRequest::EXPLICIT) {
				state->aurora_activation_request = p_initial ? ActivationRequest::INITIAL : ActivationRequest::EXPLICIT;
			}
			AuroraLegacyShell::activate_pending(*state);
		}
	} else if (!p_initial) {
		// Leave initial XDG mapping to its normal compositor policy. Explicit
		// grab_focus/debugger requests can use the existing activation backend.
		wayland_thread.window_request_attention(p_window);
	}
}

bool WaylandThread::aurora_window_is_focused(DisplayServerEnums::WindowID p_window) const {
	if (!window_exists(p_window)) {
		return false;
	}
	for (struct wl_seat *seat : registry.wl_seats) {
		const SeatState *state = wl_seat_get_seat_state(seat);
		if (state && state->wl_keyboard && state->focused_id == p_window) {
			return true;
		}
	}
	return false;
}

#ifdef TOOLS_ENABLED
void DisplayServerWayland::aurora_setup_editor() {
	EditorRunBar *bar = EditorRunBar::get_singleton();
	if (!bar || !EditorSettings::get_singleton()) {
		return; // A game or Project Manager has no local play-session toolbar.
	}
	EDITOR_DEF("run/auroraos/restore_editor_on_stop", true);
	Callable stopped = callable_mp(this, &DisplayServerWayland::_aurora_editor_play_stopped);
	if (!bar->is_connected(SNAME("stop_pressed"), stopped)) {
		bar->connect(SNAME("stop_pressed"), stopped);
	}
}

void DisplayServerWayland::_aurora_editor_play_stopped() {
	if (EditorSettings::get_singleton() && bool(EDITOR_GET("run/auroraos/restore_editor_on_stop"))) {
		window_move_to_foreground(DisplayServerEnums::MAIN_WINDOW_ID);
	}
}
#endif
