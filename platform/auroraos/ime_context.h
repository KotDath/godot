/**************************************************************************/
/*  ime_context.h                                                         */
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

#include "core/variant/dictionary.h"

// Protocol conversion is isolated here; text controls only see Unicode character offsets.
namespace AuroraIME {
inline int character_at_byte(const String &p_text, int64_t p_byte) {
	if (p_byte < 0) {
		return -1;
	}
	int64_t bytes = 0;
	for (int i = 0; i < p_text.length(); i++) {
		if (bytes == p_byte) {
			return i;
		}
		const char32_t c = p_text[i];
		bytes += c < 0x80 ? 1 : c < 0x800 ? 2
				: c < 0x10000			  ? 3
										  : 4;
		if (bytes > p_byte) {
			return -1; // A range must never split a UTF-8 sequence.
		}
	}
	return bytes == p_byte ? p_text.length() : -1;
}

inline int bytes_at_character(const String &p_text, int p_character) {
	return p_text.substr(0, p_character).utf8().length();
}

inline bool operation(const Dictionary &p_context, const String &p_commit, uint32_t p_before, uint32_t p_after, bool p_move, int32_t p_cursor, int32_t p_anchor, Dictionary &r_operation) {
	r_operation.clear();
	const String committed = bool(p_context.get("multiline", true)) ? p_commit : p_commit.strip_escapes();
	r_operation["text"] = committed;
	if (p_before == 0 && p_after == 0 && !p_move) {
		return true;
	}
	if (p_context.is_empty() || !bool(p_context.get("available", true)) || bool(p_context.get("secret", false))) {
		return false;
	}
	const String surrounding = p_context.get("text", String());
	const int cursor = p_context.get("cursor", 0);
	const int anchor = p_context.get("anchor", 0);
	if (cursor < 0 || anchor < 0 || cursor > surrounding.length() || anchor > surrounding.length()) {
		return false;
	}
	const int low = MIN(cursor, anchor);
	const int high = MAX(cursor, anchor);
	const int from = character_at_byte(surrounding, int64_t(bytes_at_character(surrounding, low)) - p_before);
	const int to = character_at_byte(surrounding, int64_t(bytes_at_character(surrounding, high)) + p_after);
	if (from < 0 || to < 0) {
		return false;
	}
	r_operation["context"] = p_context.duplicate();
	String result = surrounding;
	int after_commit = cursor;
	if (!committed.is_empty() || p_before || p_after) {
		r_operation["range"] = Vector2i(from, to);
		result = surrounding.left(from) + committed + surrounding.substr(to);
		after_commit = from + committed.length();
	}
	if (p_move) {
		const int64_t base = bytes_at_character(result, after_commit);
		const int moved_cursor = character_at_byte(result, base + p_cursor);
		const int moved_anchor = character_at_byte(result, base + p_anchor);
		if (moved_cursor < 0 || moved_anchor < 0) {
			return false;
		}
		r_operation["selection"] = Vector2i(moved_cursor, moved_anchor);
	}
	return true;
}
} // namespace AuroraIME
