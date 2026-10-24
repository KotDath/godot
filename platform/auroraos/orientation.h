/**************************************************************************/
/*  orientation.h                                                         */
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

#include "core/math/rect2.h"
#include "core/math/vector2.h"
#include "core/math/vector2i.h"
#include "servers/display/display_server_enums.h"

#include <atomic>

// Surface coordinates stay in the compositor's coordinate system. Godot draws
// in buffer coordinates; all positions AND vectors must use the same transform.
namespace AuroraOrientation {
// Rendering can run on a separate thread. Publish one coherent atomic
// snapshot; never call back into DisplayServer while presenting a frame.
inline std::atomic<uint64_t> presentation_state{ 0 };
inline void set_presentation(Vector2i native_size, int transform) {
	presentation_state.store(uint64_t(native_size.x) | (uint64_t(native_size.y) << 16) | (uint64_t(transform) << 32));
}
struct Presentation {
	Vector2i native_size;
	int transform;

	Vector2i get_logical_size() const {
		return transform % 2 ? Vector2i(native_size.y, native_size.x) : native_size;
	}
};
inline Presentation get_presentation() {
	uint64_t state = presentation_state.load();
	return { Vector2i(state & 0xffff, (state >> 16) & 0xffff), int((state >> 32) & 3) };
}
inline Presentation get_presentation(Vector2i p_fallback_size) {
	const Presentation presentation = get_presentation();
	if (presentation.native_size.x <= 0 || presentation.native_size.y <= 0) {
		return { p_fallback_size, 0 };
	}
	return presentation;
}

inline Vector2 to_buffer(Vector2 p, Vector2 size, int transform, bool vector = false) {
	if (vector) {
		size = Vector2();
	}
	switch (transform) {
		case 1:
			return Vector2(size.y - p.y, p.x);
		case 2:
			return Vector2(size.x - p.x, size.y - p.y);
		case 3:
			return Vector2(p.y, size.x - p.x);
		default:
			return p;
	}
}
inline Rect2 transform_rect(const Rect2 &p_rect, Vector2 p_size, int p_transform) {
	Rect2 result(to_buffer(p_rect.position, p_size, p_transform), Vector2());
	result.expand_to(to_buffer(p_rect.position + Vector2(p_rect.size.x, 0), p_size, p_transform));
	result.expand_to(to_buffer(p_rect.position + Vector2(0, p_rect.size.y), p_size, p_transform));
	result.expand_to(to_buffer(p_rect.get_end(), p_size, p_transform));
	return result;
}
inline int select_transform(DisplayServerEnums::ScreenOrientation requested, int device, bool native_landscape, int previous) {
	using namespace DisplayServerEnums;
	int landscape = native_landscape ? 0 : 3;
	int portrait = native_landscape ? 1 : 0;
	switch (requested) {
		case SCREEN_LANDSCAPE:
			return landscape;
		case SCREEN_PORTRAIT:
			return portrait;
		case SCREEN_REVERSE_LANDSCAPE:
			return (landscape + 2) % 4;
		case SCREEN_REVERSE_PORTRAIT:
			return (portrait + 2) % 4;
		case SCREEN_SENSOR_LANDSCAPE:
			return (device % 2 == landscape % 2) ? device : ((previous % 2 == landscape % 2) ? previous : landscape);
		case SCREEN_SENSOR_PORTRAIT:
			return (device % 2 == portrait % 2) ? device : ((previous % 2 == portrait % 2) ? previous : portrait);
		case SCREEN_SENSOR:
			return device;
	}
	return 0;
}
} // namespace AuroraOrientation
