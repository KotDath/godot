/**************************************************************************/
/*  apm_reconcile.cpp                                                     */
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

#include "apm_reconcile.h"

namespace AuroraAPM {
namespace {
bool hex_digest(const String &p_value) {
	if (p_value.length() != 64) {
		return false;
	}
	for (int index = 0; index < p_value.length(); index++) {
		char32_t value = p_value[index];
		if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) {
			return false;
		}
	}
	return true;
}
bool rpm_component(const String &p_value) {
	if (p_value.is_empty()) {
		return false;
	}
	for (int index = 0; index < p_value.length(); index++) {
		char32_t value = p_value[index];
		bool alphanumeric = (value >= '0' && value <= '9') || (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
		if (!alphanumeric && (index == 0 || (value != '.' && value != '_' && value != '+'))) {
			return false;
		}
	}
	return true;
}

// Strictly consume gdbus's (a{ss},) result. No regex extraction of a plausible
// ID/version from malformed, duplicate or nested data, and no shell evaluation.
class PackageParser {
	const String &text;
	int cursor = 0;

public:
	PackageParser(const String &p_text) : text(p_text) {}
	void whitespace() {
		while (cursor < text.length() && (text[cursor] == ' ' || text[cursor] == '\t' || text[cursor] == '\n' || text[cursor] == '\r')) {
			cursor++;
		}
	}
	bool take(char32_t p_value) {
		whitespace();
		if (cursor >= text.length() || text[cursor] != p_value) {
			return false;
		}
		cursor++;
		return true;
	}
	bool literal(const String &p_value) {
		whitespace();
		if (text.substr(cursor, p_value.length()) != p_value) {
			return false;
		}
		cursor += p_value.length();
		return true;
	}
	bool end() {
		whitespace();
		return cursor == text.length();
	}
	bool quoted(String &r_value) {
		whitespace();
		if (cursor >= text.length() || (text[cursor] != '\'' && text[cursor] != '"')) {
			return false;
		}
		char32_t quote = text[cursor++];
		r_value = String();
		while (cursor < text.length()) {
			char32_t value = text[cursor++];
			if (value == quote) {
				return true;
			}
			if (value < 32) {
				return false;
			}
			if (value == '\\') {
				if (cursor >= text.length()) {
					return false;
				}
				value = text[cursor++];
				switch (value) {
					case '\\':
					case '\'':
					case '"':
						break;
					case 'n':
						value = '\n';
						break;
					case 'r':
						value = '\r';
						break;
					case 't':
						value = '\t';
						break;
					case 'b':
						value = '\b';
						break;
					case 'f':
						value = '\f';
						break;
					case 'a':
						value = '\a';
						break;
					case 'v':
						value = '\v';
						break;
					case 'u':
					case 'U':
					case 'x': {
						int digits = value == 'u' ? 4 : value == 'U' ? 8
																	 : 2;
						uint32_t scalar = 0;
						for (int index = 0; index < digits; index++) {
							if (cursor >= text.length()) {
								return false;
							}
							char32_t digit = text[cursor++];
							int number = digit >= '0' && digit <= '9' ? digit - '0' : digit >= 'a' && digit <= 'f' ? digit - 'a' + 10
									: digit >= 'A' && digit <= 'F'												   ? digit - 'A' + 10
																												   : -1;
							if (number < 0) {
								return false;
							}
							scalar = (scalar << 4) | number;
						}
						if (scalar == 0 || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) {
							return false;
						}
						value = scalar;
					} break;
					default:
						return false;
				}
			}
			r_value += value;
		}
		return false;
	}
};
} // namespace

bool valid_receipt(const Dictionary &p_receipt, const String &p_package) {
	Variant schema = p_receipt.get("schema", Variant());
	if ((schema.get_type() != Variant::INT && schema.get_type() != Variant::FLOAT) || double(schema) != 1 || p_receipt.get("package", "") != Variant(p_package)) {
		return false;
	}
	for (const String &key : { String("version"), String("release") }) {
		if (p_receipt.get(key, Variant()).get_type() != Variant::STRING || !rpm_component(p_receipt[key])) {
			return false;
		}
	}
	String arch = p_receipt.get("arch", "");
	if ((arch != "arm32" && arch != "arm64") || p_receipt.get("files", Variant()).get_type() != Variant::DICTIONARY) {
		return false;
	}
	Dictionary files = p_receipt["files"];
	if (!files.has("/usr/bin/" + p_package) || files.size() > 1024) {
		return false;
	}
	for (const Variant &key : files.get_key_list()) {
		if (key.get_type() != Variant::STRING || files[key].get_type() != Variant::STRING || !hex_digest(files[key])) {
			return false;
		}
		String path = key;
		if (path.length() > 2048 || path.simplify_path() != path || path.contains("//") || path.contains("\\") || path.contains("\n") || path.contains("\r")) {
			return false;
		}
		if (path != "/usr/bin/" + p_package && path != "/usr/share/" + p_package + "/native-manifest.json" && !path.begins_with("/usr/share/" + p_package + "/lib/")) {
			return false;
		}
	}
	return true;
}

Error parse_package(const String &p_stdout, Dictionary &r_fields) {
	r_fields.clear();
	if (p_stdout.length() > 1024 * 1024) {
		return ERR_INVALID_DATA;
	}
	PackageParser parser(p_stdout);
	if (!parser.take('(')) {
		return ERR_INVALID_DATA;
	}
	parser.literal("@a{ss}");
	if (!parser.take('{')) {
		return ERR_INVALID_DATA;
	}
	Dictionary fields;
	if (!parser.take('}')) {
		while (true) {
			String key;
			String value;
			if (!parser.quoted(key) || !parser.take(':') || !parser.quoted(value) || fields.has(key)) {
				return ERR_INVALID_DATA;
			}
			fields[key] = value;
			if (parser.take('}')) {
				break;
			}
			if (!parser.take(',')) {
				return ERR_INVALID_DATA;
			}
		}
	}
	if (!parser.take(',') || !parser.take(')') || !parser.end() || String(fields.get("general.id", "")).is_empty() || String(fields.get("general.version", "")).is_empty()) {
		return ERR_INVALID_DATA;
	}
	r_fields = fields;
	return OK;
}

Error parse_pending(const String &p_stdout, bool &r_pending) {
	PackageParser parser(p_stdout);
	if (!parser.take('(')) {
		return ERR_INVALID_DATA;
	}
	if (parser.literal("true")) {
		r_pending = true;
	} else if (parser.literal("false")) {
		r_pending = false;
	} else {
		return ERR_INVALID_DATA;
	}
	return parser.take(',') && parser.take(')') && parser.end() ? OK : ERR_INVALID_DATA;
}

Error check_hashes(const String &p_stdout, const Dictionary &p_expected) {
	Dictionary seen;
	for (const String &raw : p_stdout.split("\n")) {
		String line = raw.trim_suffix("\r");
		if (line.is_empty()) {
			continue;
		}
		if (line.length() < 67 || !hex_digest(line.substr(0, 64)) || (line.substr(64, 2) != "  " && line.substr(64, 2) != " *")) {
			return ERR_INVALID_DATA;
		}
		String path = line.substr(66);
		if (!p_expected.has(path) || seen.has(path)) {
			return ERR_INVALID_DATA;
		}
		if (p_expected[path] != Variant(line.substr(0, 64))) {
			return ERR_FILE_CORRUPT;
		}
		seen[path] = true;
	}
	return seen.size() == p_expected.size() && !seen.is_empty() ? OK : ERR_INVALID_DATA;
}
} // namespace AuroraAPM
