/**************************************************************************/
/*  text_input_v2.cpp                                                     */
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

#include "text_input_v2.h"

#include "ime_context.h"
#include "orientation.h"
#include "protocol/text_input_v2.gen.h"

#include "core/input/input_event.h"

#include "platform/linuxbsd/wayland/display_server_wayland.h"
#include "platform/linuxbsd/wayland/key_mapping_xkb.h"

namespace AuroraTextInput {
namespace {
void clear_preedit(AuroraTextInputState *s, bool finish = false);
void reset_pending(AuroraTextInputState *s) {
	s->delete_before = 0;
	s->delete_after = 0;
	s->move_cursor = false;
	s->commit_cursor = 0;
	s->commit_anchor = 0;
	s->preedit_cursor = INT32_MAX;
	s->preedit_styles.clear();
}
void synchronized(void *data, wl_callback *callback, uint32_t) {
	auto *s = static_cast<AuroraTextInputState *>(data);
	wl_callback_destroy(callback);
	s->reset_callback = nullptr;
}
const wl_callback_listener sync_listener = { synchronized };
void update(AuroraTextInputState *s, uint32_t reason) {
	if (!s->active) {
		return;
	}
	auto *ws = s->seat->wayland_thread->window_get_state(s->window);
	if (!ws) {
		return;
	}
	const Dictionary &context = ws->ime_context;
	const String text = context.get("text", String());
	const int cursor = context.get("cursor", 0);
	const int anchor = context.get("anchor", 0);
	const bool secret = context.get("secret", false);
	if (!secret && text.utf8().length() <= 4000 && cursor >= 0 && anchor >= 0 && cursor <= text.length() && anchor <= text.length()) {
		zwp_text_input_v2_set_surrounding_text(s->input, text.utf8().get_data(), AuroraIME::bytes_at_character(text, cursor), AuroraIME::bytes_at_character(text, anchor));
	} else {
		zwp_text_input_v2_set_surrounding_text(s->input, "", 0, 0);
	}
	if (secret) {
		zwp_text_input_v2_set_content_type(s->input, ZWP_TEXT_INPUT_V2_CONTENT_HINT_HIDDEN_TEXT | ZWP_TEXT_INPUT_V2_CONTENT_HINT_SENSITIVE_DATA, ZWP_TEXT_INPUT_V2_CONTENT_PURPOSE_PASSWORD);
	} else {
		uint32_t hints = s->hints;
		if (!bool(context.get("completion", false)) || !bool(context.get("available", false))) {
			hints &= ~(ZWP_TEXT_INPUT_V2_CONTENT_HINT_AUTO_COMPLETION | ZWP_TEXT_INPUT_V2_CONTENT_HINT_AUTO_CORRECTION);
		}
		zwp_text_input_v2_set_content_type(s->input, hints, s->purpose);
	}
	const Rect2i &rect = s->seat->ime_rect;
	zwp_text_input_v2_set_cursor_rectangle(s->input, rect.position.x, rect.position.y, rect.size.x, rect.size.y);
	zwp_text_input_v2_update_state(s->input, s->serial, reason);
	if (reason != ZWP_TEXT_INPUT_V2_UPDATE_STATE_CHANGE) {
		reset_pending(s);
		if (s->reset_callback) {
			wl_callback_destroy(s->reset_callback);
		}
		s->reset_callback = wl_display_sync(s->seat->wayland_thread->get_wl_display());
		wl_callback_add_listener(s->reset_callback, &sync_listener, s);
	}
}
void clear_preedit(AuroraTextInputState *s, bool finish) {
	if (s->window == DisplayServerEnums::INVALID_WINDOW_ID) {
		return;
	}
	Ref<WaylandThread::IMEUpdateEventMessage> message;
	message.instantiate();
	message->id = s->window;
	auto *ws = s->seat->wayland_thread->window_get_state(s->window);
	message->scoped = true;
	message->generation = ws ? ws->ime_generation : 0;
	if (finish && ws) {
		message->finish_callback = ws->ime_finish_callback;
	}
	s->seat->wayland_thread->push_message(message);
}
void enter(void *data, zwp_text_input_v2 *, uint32_t serial, wl_surface *surface) {
	auto *s = static_cast<AuroraTextInputState *>(data);
	s->serial = serial;
	s->entered = true;
	if (s->active) {
		update(s, ZWP_TEXT_INPUT_V2_UPDATE_STATE_FULL);
	}
}
void leave(void *data, zwp_text_input_v2 *, uint32_t serial, wl_surface *) {
	auto *s = static_cast<AuroraTextInputState *>(data);
	s->serial = serial;
	s->panel_rect = Rect2i();
	s->entered = false;
	reset_pending(s);
	auto *ws = s->seat->wayland_thread->window_get_state(s->window);
	if (ws) {
		ws->ime_generation++;
	}
	clear_preedit(s, true);
}
void panel(void *data, zwp_text_input_v2 *, uint32_t state, int32_t x, int32_t y, int32_t width, int32_t height) {
	auto *s = static_cast<AuroraTextInputState *>(data);
	s->panel_rect = state ? Rect2i(x, y, MAX(0, width), MAX(0, height)) : Rect2i();
}
void commit(void *data, zwp_text_input_v2 *, const char *text);
void preedit(void *data, zwp_text_input_v2 *input, const char *text, const char *commit_text) {
	auto *s = static_cast<AuroraTextInputState *>(data);
	if (!s->active || !s->entered || s->reset_callback) {
		reset_pending(s);
		return;
	}
	const int pending_cursor = s->preedit_cursor;
	const Vector<AuroraIME::PreeditStyle> pending_styles = s->preedit_styles;
	if (s->delete_before || s->delete_after) {
		s->move_cursor = false;
		commit(data, input, "");
	}
	Ref<WaylandThread::IMEUpdateEventMessage> message;
	message.instantiate();
	message->id = s->window;
	message->text = String::utf8(text);
	message->metadata = AuroraIME::preedit_metadata(message->text, String::utf8(commit_text), pending_cursor, pending_styles, message->selection);
	auto *ws = s->seat->wayland_thread->window_get_state(s->window);
	message->scoped = true;
	message->generation = ws ? ws->ime_generation : 0;
	s->seat->wayland_thread->push_message(message);
	reset_pending(s);
}
void styling(void *data, zwp_text_input_v2 *, uint32_t index, uint32_t length, uint32_t style) {
	auto *s = static_cast<AuroraTextInputState *>(data);
	if (s->preedit_styles.size() < 1024) {
		s->preedit_styles.push_back({ index, length, style });
	}
}
void preedit_cursor(void *data, zwp_text_input_v2 *, int32_t index) {
	static_cast<AuroraTextInputState *>(data)->preedit_cursor = index;
}
void commit(void *data, zwp_text_input_v2 *, const char *text) {
	auto *s = static_cast<AuroraTextInputState *>(data);
	if (!s->active || !s->entered || s->reset_callback) {
		reset_pending(s);
		return;
	}
	Ref<WaylandThread::IMECommitEventMessage> message;
	message.instantiate();
	message->id = s->window;
	message->text = String::utf8(text);
	auto *ws = s->seat->wayland_thread->window_get_state(s->window);
	if (ws) {
		message->callback = ws->ime_commit_callback;
		message->generation = ws->ime_generation;
		message->atomic = !message->callback.is_null();
		if (message->atomic && !AuroraIME::operation(ws->ime_context, message->text, s->delete_before, s->delete_after, s->move_cursor, s->commit_cursor, s->commit_anchor, message->operation)) {
			update(s, ZWP_TEXT_INPUT_V2_UPDATE_STATE_RESET);
			clear_preedit(s);
			return;
		}
	}
	s->seat->wayland_thread->push_message(message);
	reset_pending(s);
}
void cursor_position(void *data, zwp_text_input_v2 *, int32_t cursor, int32_t anchor) {
	auto *s = static_cast<AuroraTextInputState *>(data);
	s->move_cursor = true;
	s->commit_cursor = cursor;
	s->commit_anchor = anchor;
}
void delete_surrounding(void *data, zwp_text_input_v2 *, uint32_t before, uint32_t after) {
	auto *s = static_cast<AuroraTextInputState *>(data);
	s->delete_before = before;
	s->delete_after = after;
}
void modifiers(void *, zwp_text_input_v2 *, wl_array *) {}
void keysym(void *data, zwp_text_input_v2 *, uint32_t, uint32_t sym, uint32_t state, uint32_t) {
	auto *s = static_cast<AuroraTextInputState *>(data);
	if (!s->active || !s->entered || s->reset_callback) {
		return;
	}
	Ref<InputEventKey> event;
	event.instantiate();
	event->set_window_id(s->window);
	event->set_pressed(state == WL_KEYBOARD_KEY_STATE_PRESSED);
	event->set_keycode(KeyMappingXKB::get_keycode(sym));
	event->set_unicode(xkb_keysym_to_utf32(sym));
	Ref<WaylandThread::InputEventMessage> message;
	message.instantiate();
	message->event = event;
	s->seat->wayland_thread->push_message(message);
}
void language(void *, zwp_text_input_v2 *, const char *) {}
void direction(void *, zwp_text_input_v2 *, uint32_t) {}
void surrounding(void *, zwp_text_input_v2 *, int32_t, int32_t) {}
void changed(void *data, zwp_text_input_v2 *, uint32_t serial, uint32_t) {
	auto *s = static_cast<AuroraTextInputState *>(data);
	s->serial = serial;
	update(s, ZWP_TEXT_INPUT_V2_UPDATE_STATE_FULL);
}
const zwp_text_input_v2_listener listener = { enter, leave, panel, preedit, styling, preedit_cursor, commit, cursor_position, delete_surrounding, modifiers, keysym, language, direction, surrounding, changed };
} //namespace
void attach(WaylandThread::SeatState *seat) {
	if (!seat->registry->aurora_text_manager || seat->aurora_text_input) {
		return;
	}
	auto *s = memnew(AuroraTextInputState);
	s->seat = seat;
	s->input = zwp_text_input_manager_v2_get_text_input(seat->registry->aurora_text_manager, seat->wl_seat);
	seat->aurora_text_input = s;
	zwp_text_input_v2_add_listener(s->input, &listener, s);
}
void detach(WaylandThread::SeatState *seat) {
	if (!seat->aurora_text_input) {
		return;
	}
	if (seat->aurora_text_input->reset_callback) {
		wl_callback_destroy(seat->aurora_text_input->reset_callback);
	}
	zwp_text_input_v2_destroy(seat->aurora_text_input->input);
	memdelete(seat->aurora_text_input);
	seat->aurora_text_input = nullptr;
}
bool bind(WaylandThread::RegistryState &registry, wl_registry *reg, uint32_t name, const char *interface) {
	if (strcmp(interface, zwp_text_input_manager_v2_interface.name) != 0) {
		return false;
	}
	registry.aurora_text_manager = static_cast<zwp_text_input_manager_v2 *>(wl_registry_bind(reg, name, &zwp_text_input_manager_v2_interface, 1));
	registry.aurora_text_manager_name = name;
	for (auto *seat : registry.wl_seats) {
		attach(WaylandThread::wl_seat_get_seat_state(seat));
	}
	return true;
}
bool remove(WaylandThread::RegistryState &registry, uint32_t name) {
	if (registry.aurora_text_manager_name != name) {
		return false;
	}
	for (auto *seat : registry.wl_seats) {
		detach(WaylandThread::wl_seat_get_seat_state(seat));
	}
	zwp_text_input_manager_v2_destroy(registry.aurora_text_manager);
	registry.aurora_text_manager = nullptr;
	registry.aurora_text_manager_name = 0;
	return true;
}
bool activate(WaylandThread::SeatState *seat, DisplayServerEnums::WindowID window, bool active) {
	if (!seat || !seat->aurora_text_input || seat->wp_text_input) {
		return false;
	}
	auto *s = seat->aurora_text_input;
	auto *ws = seat->wayland_thread->window_get_state(window);
	if (!ws) {
		return true;
	}
	if (s->active == active && s->window == window) {
		return true;
	}
	s->window = window;
	s->active = active;
	if (active) {
		zwp_text_input_v2_enable(s->input, ws->wl_surface);
		update(s, ZWP_TEXT_INPUT_V2_UPDATE_STATE_ENTER);
	} else {
		ws->ime_commit_callback = Callable();
		ws->ime_context_callback = Callable();
		ws->ime_finish_callback = Callable();
		ws->ime_context.clear();
		ws->ime_generation++;
		reset_pending(s);
		zwp_text_input_v2_hide_input_panel(s->input);
		zwp_text_input_v2_disable(s->input, ws->wl_surface);
		s->panel_rect = Rect2i();
		clear_preedit(s);
	}
	return true;
}
bool cursor(WaylandThread::SeatState *seat, const Point2i &position, DisplayServerEnums::WindowID window) {
	return cursor_rect(seat, Rect2(position, Size2(1, 10)), window);
}
bool cursor_rect(WaylandThread::SeatState *seat, const Rect2 &rect, DisplayServerEnums::WindowID window) {
	if (!seat || !seat->aurora_text_input || seat->wp_text_input) {
		return false;
	}
	auto *ws = seat->wayland_thread->window_get_state(window);
	double scale = ws ? WaylandThread::window_state_get_scale_factor(ws) : 1.0;
	const Point2i from = Vector2(rect.position / scale).floor();
	const Point2i to = Vector2(rect.get_end() / scale).ceil();
	Rect2i logical_rect(from, to - from);
	logical_rect.size.x = MAX(1, logical_rect.size.x);
	logical_rect.size.y = MAX(1, logical_rect.size.y);
	if (seat->ime_rect != logical_rect) {
		seat->ime_rect = logical_rect;
		update(seat->aurora_text_input, ZWP_TEXT_INPUT_V2_UPDATE_STATE_CHANGE);
	}
	return true;
}
} //namespace AuroraTextInput

void WaylandThread::aurora_set_ime_context(DisplayServerEnums::WindowID p_window, const Dictionary &p_context, bool p_reset) {
	auto *ws = window_get_state(p_window);
	if (!ws) {
		return;
	}
	ws->ime_context = p_context.duplicate();
	auto *seat = wl_seat_get_seat_state(wl_seat_current);
	if (!seat || !seat->aurora_text_input || seat->wp_text_input) {
		return;
	}
	auto *s = seat->aurora_text_input;
	if (!s->active || s->window != p_window) {
		return;
	}
	if (p_reset) {
		ws->ime_generation++;
		AuroraTextInput::clear_preedit(s);
	}
	AuroraTextInput::update(s, p_reset ? ZWP_TEXT_INPUT_V2_UPDATE_STATE_RESET : ZWP_TEXT_INPUT_V2_UPDATE_STATE_CHANGE);
}

void WaylandThread::aurora_show_keyboard(DisplayServerEnums::VirtualKeyboardType type) {
	auto *seat = wl_seat_get_seat_state(wl_seat_current);
	if (!seat || !seat->aurora_text_input || seat->wp_text_input) {
		return;
	}
	auto *s = seat->aurora_text_input;
	using namespace DisplayServerEnums;
	s->hints = 0;
	switch (type) {
		case KEYBOARD_TYPE_MULTILINE:
			s->purpose = ZWP_TEXT_INPUT_V2_CONTENT_PURPOSE_NORMAL;
			s->hints = ZWP_TEXT_INPUT_V2_CONTENT_HINT_MULTILINE | ZWP_TEXT_INPUT_V2_CONTENT_HINT_AUTO_COMPLETION | ZWP_TEXT_INPUT_V2_CONTENT_HINT_AUTO_CORRECTION;
			break;
		case KEYBOARD_TYPE_NUMBER:
			s->purpose = ZWP_TEXT_INPUT_V2_CONTENT_PURPOSE_NUMBER;
			break;
		case KEYBOARD_TYPE_NUMBER_DECIMAL:
			s->purpose = ZWP_TEXT_INPUT_V2_CONTENT_PURPOSE_NUMBER;
			break;
		case KEYBOARD_TYPE_PHONE:
			s->purpose = ZWP_TEXT_INPUT_V2_CONTENT_PURPOSE_PHONE;
			break;
		case KEYBOARD_TYPE_EMAIL_ADDRESS:
			s->purpose = ZWP_TEXT_INPUT_V2_CONTENT_PURPOSE_EMAIL;
			break;
		case KEYBOARD_TYPE_PASSWORD:
			s->purpose = ZWP_TEXT_INPUT_V2_CONTENT_PURPOSE_PASSWORD;
			s->hints = ZWP_TEXT_INPUT_V2_CONTENT_HINT_HIDDEN_TEXT | ZWP_TEXT_INPUT_V2_CONTENT_HINT_SENSITIVE_DATA;
			break;
		case KEYBOARD_TYPE_URL:
			s->purpose = ZWP_TEXT_INPUT_V2_CONTENT_PURPOSE_URL;
			break;
		default:
			s->purpose = ZWP_TEXT_INPUT_V2_CONTENT_PURPOSE_NORMAL;
			s->hints = ZWP_TEXT_INPUT_V2_CONTENT_HINT_AUTO_COMPLETION | ZWP_TEXT_INPUT_V2_CONTENT_HINT_AUTO_CORRECTION;
	}
	AuroraTextInput::update(s, ZWP_TEXT_INPUT_V2_UPDATE_STATE_CHANGE);
	zwp_text_input_v2_show_input_panel(s->input);
}
void WaylandThread::aurora_hide_keyboard() {
	auto *seat = wl_seat_get_seat_state(wl_seat_current);
	if (seat && seat->aurora_text_input) {
		AuroraTextInput::activate(seat, seat->aurora_text_input->window, false);
	}
}

int WaylandThread::aurora_keyboard_height() const {
	auto *seat = wl_seat_get_seat_state(wl_seat_current);
	if (!seat || !seat->aurora_text_input) {
		return 0;
	}
	auto *s = seat->aurora_text_input;
	auto *ws = window_get_state(s->window);
	if (!ws || !s->active || !s->entered) {
		return 0;
	}
	const double scale = window_state_get_scale_factor(ws);
	Rect2 rect(Vector2(s->panel_rect.position) * scale, Vector2(s->panel_rect.size) * scale);
	if (ws->legacy_shell_surface) {
		rect = AuroraOrientation::transform_rect(rect, Vector2(ws->rect.size) * scale, AuroraOrientation::get_presentation().transform);
	}
	return MAX(0, int(Math::ceil(rect.size.y)));
}

bool WaylandThread::aurora_set_ime_rect(const Rect2 &p_rect, DisplayServerEnums::WindowID p_window) {
	return AuroraTextInput::cursor_rect(wl_seat_get_seat_state(wl_seat_current), p_rect, p_window);
}

bool DisplayServerWayland::window_set_ime_rect(const Rect2 &p_rect, DisplayServerEnums::WindowID p_window_id) {
	MutexLock lock(wayland_thread.mutex);
	auto *ws = wayland_thread.window_get_state(p_window_id);
	Rect2 rect = p_rect;
	if (ws && ws->legacy_shell_surface && windows.has(p_window_id)) {
		rect = AuroraOrientation::transform_rect(rect, windows[p_window_id].rect.size, (4 - aurora_transform) % 4);
	}
	return wayland_thread.aurora_set_ime_rect(rect, p_window_id);
}
void DisplayServerWayland::virtual_keyboard_show(const String &, const Rect2 &, DisplayServerEnums::VirtualKeyboardType type, int, int, int) {
	MutexLock lock(wayland_thread.mutex);
	wayland_thread.aurora_show_keyboard(type);
}
void DisplayServerWayland::virtual_keyboard_hide() {
	MutexLock lock(wayland_thread.mutex);
	wayland_thread.aurora_hide_keyboard();
}
int DisplayServerWayland::virtual_keyboard_get_height() const {
	MutexLock lock(wayland_thread.mutex);
	return wayland_thread.aurora_keyboard_height();
}
