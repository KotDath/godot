/**************************************************************************/
/*  native_library_map.h                                                  */
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

namespace AuroraNativeLibrary {
inline bool valid_relative_path(const String &p_path) {
	if (p_path.is_empty() || p_path.is_absolute_path()) {
		return false;
	}
	for (int i = 0; i < p_path.length(); i++) {
		const char32_t ch = p_path[i];
		if (ch < 32 || ch == 127 || ch == '\\' || ch == ':' || ch == '%' || ch == '$') {
			return false;
		}
	}
	for (const String &part : p_path.split("/")) {
		if (part.is_empty() || part == "." || part == "..") {
			return false;
		}
	}
	return true;
}

inline String mapped_path(const String &p_project_path, const Dictionary &p_paths) {
	if (!p_project_path.begins_with("res://") || !valid_relative_path(p_project_path.substr(6))) {
		return String();
	}
	const Variant value = p_paths.get(p_project_path, Variant());
	if (value.get_type() != Variant::STRING) {
		return String();
	}
	const String path = value;
	return path.begins_with("lib/project/") && valid_relative_path(path) ? path : String();
}
} // namespace AuroraNativeLibrary
