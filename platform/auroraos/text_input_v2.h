/**************************************************************************/
/*  text_input_v2.h                                                       */
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

#pragma once
#include "ime_context.h"

#include "platform/linuxbsd/wayland/wayland_thread.h"

struct AuroraTextInputState {
	struct zwp_text_input_v2 *input = nullptr;
	WaylandThread::SeatState *seat = nullptr;
	DisplayServerEnums::WindowID window = DisplayServerEnums::INVALID_WINDOW_ID;
	uint32_t serial = 0;
	bool active = false;
	struct wl_callback *reset_callback = nullptr;
	bool entered = false;
	int preedit_cursor = INT32_MAX;
	uint32_t delete_before = 0;
	uint32_t delete_after = 0;
	bool move_cursor = false;
	int32_t commit_cursor = 0;
	int32_t commit_anchor = 0;
	Vector<AuroraIME::PreeditStyle> preedit_styles;
	Rect2i panel_rect;
	uint32_t purpose = 0;
	uint32_t hints = 0;
};
namespace AuroraTextInput {
bool bind(WaylandThread::RegistryState &registry, wl_registry *wl_registry, uint32_t name, const char *interface);
bool remove(WaylandThread::RegistryState &registry, uint32_t name);
void attach(WaylandThread::SeatState *seat);
void detach(WaylandThread::SeatState *seat);
bool activate(WaylandThread::SeatState *seat, DisplayServerEnums::WindowID window, bool active);
bool cursor(WaylandThread::SeatState *seat, const Point2i &position, DisplayServerEnums::WindowID window);
bool cursor_rect(WaylandThread::SeatState *seat, const Rect2 &rect, DisplayServerEnums::WindowID window);
} //namespace AuroraTextInput
