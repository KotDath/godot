/**************************************************************************/
/*  ime_preedit.h                                                         */
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

#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"
#include "servers/rendering/rendering_server.h"
#include "servers/text/text_server.h"

// Optional native preedit decorations. Backends without metadata retain the
// controls' original rendering. Ranges are half-open Unicode character offsets.
namespace IMEPreedit {
inline bool draw(RID p_canvas, RID p_text, int p_start, int p_length, const Dictionary &p_metadata, Vector2 p_origin, float p_left, float p_right, float p_height, float p_width, Color p_color) {
	const Array styles = p_metadata.get("styles", Array());
	if (styles.is_empty() || p_length <= 0 || p_length > 4000) {
		return false;
	}
	Vector<StringName> characters;
	characters.resize(p_length);
	characters.fill(SNAME("default"));
	for (int i = 0; i < styles.size(); i++) {
		const Dictionary span = styles[i];
		const Vector2i range = span.get("range", Vector2i());
		const StringName style = span.get("style", SNAME("default"));
		for (int j = MAX(0, range.x); j < MIN(p_length, range.y); j++) {
			characters.write[j] = style;
		}
	}
	for (int from = 0; from < p_length;) {
		int to = from + 1;
		const StringName style = characters[from];
		while (to < p_length && characters[to] == style) {
			to++;
		}
		if (style != SNAME("none")) {
			Color color = p_color;
			float width = p_width;
			const bool selected = style == SNAME("selection") || style == SNAME("highlight");
			if (style == SNAME("inactive")) {
				color.a *= 0.5f;
			} else if (style == SNAME("incorrect")) {
				color = Color(1.0f, 0.25f, 0.25f, color.a);
			} else if (style == SNAME("active") || selected) {
				width *= 3;
			}
			const Vector<Vector2> selection = TS->shaped_text_get_selection(p_text, p_start + from, p_start + to);
			for (const Vector2 &part : selection) {
				const float left = MAX(p_left, part.x + p_origin.x);
				const float right = MIN(p_right, part.y + p_origin.x);
				if (right <= left) {
					continue;
				}
				if (selected) {
					Color background = color;
					background.a *= 0.2f;
					RS::get_singleton()->canvas_item_add_rect(p_canvas, Rect2(left, p_origin.y - p_height, right - left, p_height), background);
				}
				RS::get_singleton()->canvas_item_add_rect(p_canvas, Rect2(left, p_origin.y, right - left, width), color);
			}
		}
		from = to;
	}
	return true;
}
} // namespace IMEPreedit
