/**************************************************************************/
/*  orientation_wayland.cpp                                               */
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

#include "orientation_input.h"

#include "core/input/input_event.h"
#include "core/os/main_loop.h"
#include "core/os/os.h"

#include "platform/linuxbsd/wayland/display_server_wayland.h"
void DisplayServerWayland::screen_set_orientation(DisplayServerEnums::ScreenOrientation p_orientation, int p_screen) {
	ERR_FAIL_COND(p_orientation < DisplayServerEnums::SCREEN_LANDSCAPE || p_orientation > DisplayServerEnums::SCREEN_SENSOR);
	MutexLock lock(wayland_thread.mutex);
	aurora_orientation = p_orientation;
	_aurora_update_orientation();
}
DisplayServerEnums::ScreenOrientation DisplayServerWayland::screen_get_orientation(int p_screen) const {
	return aurora_orientation;
}
void DisplayServerWayland::_aurora_update_orientation() {
	auto *ws = wayland_thread.window_get_state(DisplayServerEnums::MAIN_WINDOW_ID);
	if (!ws || !ws->legacy_shell_surface || !windows.has(ws->id) || wayland_thread.get_screen_count() == 0) {
		return;
	}
	const auto screen = wayland_thread.screen_get_data(0);
	if (screen.size.x <= 0 || screen.size.y <= 0) {
		return;
	}
	int next = AuroraOrientation::select_transform(aurora_orientation, screen.transform, screen.size.x > screen.size.y, aurora_transform);
	if (next == aurora_transform) {
		return;
	}
	// Cancel using the previous coordinate system before publishing the new one.
	wayland_thread.aurora_cancel_touches();
	aurora_transform = next;
	wl_surface_set_buffer_transform(ws->wl_surface, next);
	Rect2i rect = ws->rect;
	rect.size *= wayland_thread.window_state_get_scale_factor(ws);
	_update_window_rect(rect, ws->id);
	print_verbose(vformat("Aurora orientation: transform=%d, buffer=%s", next, windows[ws->id].rect.size));
}
void AuroraOrientation::transform_input(const Ref<InputEvent> &p_event, Vector2 size, int transform) {
	Ref<InputEventScreenTouch> touch = p_event;
	if (touch.is_valid()) {
		touch->set_position(AuroraOrientation::to_buffer(touch->get_position(), size, transform));
	}
	Ref<InputEventScreenDrag> drag = p_event;
	if (drag.is_valid()) {
		drag->set_position(AuroraOrientation::to_buffer(drag->get_position(), size, transform));
		drag->set_relative(AuroraOrientation::to_buffer(drag->get_relative(), size, transform, true));
		drag->set_relative_screen_position(AuroraOrientation::to_buffer(drag->get_relative_screen_position(), size, transform, true));
		drag->set_velocity(AuroraOrientation::to_buffer(drag->get_velocity(), size, transform, true));
		drag->set_screen_velocity(AuroraOrientation::to_buffer(drag->get_screen_velocity(), size, transform, true));
	}
	Ref<InputEventMouse> mouse = p_event;
	if (mouse.is_valid()) {
		mouse->set_position(AuroraOrientation::to_buffer(mouse->get_position(), size, transform));
		mouse->set_global_position(mouse->get_position());
	}
	Ref<InputEventMouseMotion> motion = p_event;
	if (motion.is_valid()) {
		motion->set_relative(AuroraOrientation::to_buffer(motion->get_relative(), size, transform, true));
		motion->set_relative_screen_position(AuroraOrientation::to_buffer(motion->get_relative_screen_position(), size, transform, true));
		motion->set_velocity(AuroraOrientation::to_buffer(motion->get_velocity(), size, transform, true));
		motion->set_screen_velocity(AuroraOrientation::to_buffer(motion->get_screen_velocity(), size, transform, true));
	}
}

void WaylandThread::aurora_cancel_touches() {
	for (auto *seat : registry.wl_seats) {
		auto *state = wl_seat_get_seat_state(seat);
		if (state && !state->touch_points_buffer.is_empty()) {
			_wl_touch_on_cancel(state, state->wl_touch);
		}
	}
}
void DisplayServerWayland::_aurora_update_lifecycle() {
	auto *ws = wayland_thread.window_get_state(DisplayServerEnums::MAIN_WINDOW_ID);
	if (!ws || !ws->legacy_shell_surface || aurora_paused == ws->suspended) {
		return;
	}
	aurora_paused = ws->suspended;
	if (aurora_paused) {
		wayland_thread.aurora_cancel_touches();
		wayland_thread.window_set_ime_active(false, ws->id);
	}
	if (OS::get_singleton()->get_main_loop()) {
		OS::get_singleton()->get_main_loop()->notification(aurora_paused ? MainLoop::NOTIFICATION_APPLICATION_PAUSED : MainLoop::NOTIFICATION_APPLICATION_RESUMED);
	}
}
