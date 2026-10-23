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
#include "protocol/text_input_v2.gen.h"

#include "core/input/input_event.h"

#include "platform/linuxbsd/wayland/display_server_wayland.h"
#include "platform/linuxbsd/wayland/key_mapping_xkb.h"

namespace AuroraTextInput {
namespace {
void clear_preedit(AuroraTextInputState *s);
void reset_pending(AuroraTextInputState *s) {
	s->delete_before = 0;
	s->delete_after = 0;
	s->move_cursor = false;
	s->commit_cursor = 0;
	s->commit_anchor = 0;
	s->preedit_cursor = INT32_MAX;
	s->preedit_highlight = Vector2i(-1, 0);
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
	zwp_text_input_v2_enable(s->input, ws->wl_surface);
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
		zwp_text_input_v2_set_content_type(s->input, s->hints, s->purpose);
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
void clear_preedit(AuroraTextInputState *s) {
	if (s->window == DisplayServerEnums::INVALID_WINDOW_ID) {
		return;
	}
	Ref<WaylandThread::IMEUpdateEventMessage> message;
	message.instantiate();
	message->id = s->window;
	auto *ws = s->seat->wayland_thread->window_get_state(s->window);
	message->scoped = true;
	message->generation = ws ? ws->ime_generation : 0;
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
	s->panel_height = 0;
	s->entered = false;
	reset_pending(s);
	auto *ws = s->seat->wayland_thread->window_get_state(s->window);
	if (ws) {
		ws->ime_generation++;
	}
	clear_preedit(s);
}
void panel(void *data, zwp_text_input_v2 *, uint32_t state, int32_t, int32_t, int32_t, int32_t height) {
	auto *s = static_cast<AuroraTextInputState *>(data);
	s->panel_height = state ? MAX(0, height) : 0;
}
void commit(void *data, zwp_text_input_v2 *, const char *text);
void preedit(void *data, zwp_text_input_v2 *input, const char *text, const char *) {
	auto *s = static_cast<AuroraTextInputState *>(data);
	if (!s->active || !s->entered || s->reset_callback) {
		reset_pending(s);
		return;
	}
	const int pending_cursor = s->preedit_cursor;
	const Vector2i pending_highlight = s->preedit_highlight;
	if (s->delete_before || s->delete_after) {
		s->move_cursor = false;
		commit(data, input, "");
	}
	Ref<WaylandThread::IMEUpdateEventMessage> message;
	message.instantiate();
	message->id = s->window;
	message->text = String::utf8(text);
	const int cursor = pending_cursor == INT32_MAX ? message->text.length() : AuroraIME::character_at_byte(message->text, pending_cursor);
	message->selection = Vector2i(MAX(0, cursor), 0);
	if (pending_highlight.x >= 0) {
		const int from = AuroraIME::character_at_byte(message->text, pending_highlight.x);
		const int to = AuroraIME::character_at_byte(message->text, int64_t(pending_highlight.x) + pending_highlight.y);
		if (from >= 0 && to >= from) {
			message->selection = Vector2i(from, to - from);
		}
	}
	auto *ws = s->seat->wayland_thread->window_get_state(s->window);
	message->scoped = true;
	message->generation = ws ? ws->ime_generation : 0;
	s->seat->wayland_thread->push_message(message);
	reset_pending(s);
}
void styling(void *data, zwp_text_input_v2 *, uint32_t index, uint32_t length, uint32_t style) {
	if ((style == ZWP_TEXT_INPUT_V2_PREEDIT_STYLE_HIGHLIGHT || style == ZWP_TEXT_INPUT_V2_PREEDIT_STYLE_SELECTION) && index <= INT32_MAX && length <= INT32_MAX) {
		static_cast<AuroraTextInputState *>(data)->preedit_highlight = Vector2i(index, length);
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
		update(s, ZWP_TEXT_INPUT_V2_UPDATE_STATE_ENTER);
	} else {
		ws->ime_commit_callback = Callable();
		ws->ime_context_callback = Callable();
		ws->ime_context.clear();
		ws->ime_generation++;
		reset_pending(s);
		zwp_text_input_v2_hide_input_panel(s->input);
		zwp_text_input_v2_disable(s->input, ws->wl_surface);
		s->panel_height = 0;
		clear_preedit(s);
	}
	return true;
}
bool cursor(WaylandThread::SeatState *seat, const Point2i &position, DisplayServerEnums::WindowID window) {
	if (!seat || !seat->aurora_text_input || seat->wp_text_input) {
		return false;
	}
	auto *ws = seat->wayland_thread->window_get_state(window);
	double scale = ws ? WaylandThread::window_state_get_scale_factor(ws) : 1.0;
	seat->ime_rect = Rect2i(Vector2(position) / scale, Size2i(1, 10));
	update(seat->aurora_text_input, ZWP_TEXT_INPUT_V2_UPDATE_STATE_CHANGE);
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
			s->hints = ZWP_TEXT_INPUT_V2_CONTENT_HINT_MULTILINE;
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
	}
	AuroraTextInput::update(s, ZWP_TEXT_INPUT_V2_UPDATE_STATE_CHANGE);
	zwp_text_input_v2_show_input_panel(s->input);
}
int WaylandThread::aurora_keyboard_height() const {
	auto *seat = wl_seat_get_seat_state(wl_seat_current);
	return seat && seat->aurora_text_input ? seat->aurora_text_input->panel_height : 0;
}
void DisplayServerWayland::virtual_keyboard_show(const String &, const Rect2 &, DisplayServerEnums::VirtualKeyboardType type, int, int, int) {
	MutexLock lock(wayland_thread.mutex);
	wayland_thread.aurora_show_keyboard(type);
}
void DisplayServerWayland::virtual_keyboard_hide() {
	MutexLock lock(wayland_thread.mutex);
	wayland_thread.window_set_ime_active(false, DisplayServerEnums::MAIN_WINDOW_ID);
}
int DisplayServerWayland::virtual_keyboard_get_height() const {
	MutexLock lock(wayland_thread.mutex);
	return wayland_thread.aurora_keyboard_height();
}
