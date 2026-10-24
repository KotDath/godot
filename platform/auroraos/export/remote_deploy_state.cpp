/**************************************************************************/
/*  remote_deploy_state.cpp                                               */
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

#include "remote_deploy_state.h"

#include "apm_reconcile.h"

#include "core/io/dir_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/variant/array.h"

namespace AuroraDeploy {
bool valid_token(const String &p_token) {
	if (p_token.length() != 32) {
		return false;
	}
	for (int index = 0; index < p_token.length(); index++) {
		char32_t value = p_token[index];
		if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) {
			return false;
		}
	}
	return true;
}

bool valid_record(const Dictionary &p_record) {
	Variant schema = p_record.get("schema", Variant());
	// JSON parses numeric tokens as FLOAT. Variant's C++ equality is a strict
	// hash comparison, so comparing the parsed value to Variant(1) rejects our
	// own on-disk journal even though its JSON version is exactly 1.
	if ((schema.get_type() != Variant::INT && schema.get_type() != Variant::FLOAT) || (double(schema) != 1.0 && double(schema) != 2.0) || !valid_token(p_record.get("token", ""))) {
		return false;
	}
	for (const String &key : { String("sfdk"), String("device"), String("package"), String("phase"), String("rpm"), String("staging") }) {
		if (p_record.get(key, Variant()).get_type() != Variant::STRING || String(p_record[key]).is_empty()) {
			return false;
		}
	}
	if (p_record.get("install_unconfirmed", Variant()).get_type() != Variant::BOOL) {
		return false;
	}
	String phase = p_record["phase"];
	if (phase != "installing" && phase != "launching" && phase != "running" && phase != "stopping" && !(double(schema) == 2 && phase == "ready")) {
		return false;
	}
	PackedStringArray parts = String(p_record["package"]).split(".");
	if (parts.size() < 3) {
		return false;
	}
	for (const String &part : parts) {
		if (!part.is_valid_ascii_identifier()) {
			return false;
		}
	}
	if (double(schema) == 2) {
		String package = p_record["package"];
		if (p_record.get("runtime_verified", Variant()).get_type() != Variant::BOOL || (bool(p_record["install_unconfirmed"]) && bool(p_record["runtime_verified"])) || ((phase == "running" || phase == "launching") && !bool(p_record["runtime_verified"]))) {
			return false;
		}
		if (p_record.get("receipt", Variant()).get_type() != Variant::DICTIONARY || !AuroraAPM::valid_receipt(p_record["receipt"], package) || p_record.get("launch_args", Variant()).get_type() != Variant::ARRAY) {
			return false;
		}
		Array args = p_record["launch_args"];
		if (args.is_empty() || args.size() > 2048 || args[0] != Variant("/usr/bin/" + package)) {
			return false;
		}
		for (int index = 1; index < args.size(); index++) {
			if (args[index].get_type() != Variant::STRING) {
				return false;
			}
			String flag = args[index];
			if (flag == "--debug-collisions" || flag == "--debug-navigation") {
				continue;
			}
			if (flag != "--remote-debug" && flag != "--breakpoints") {
				return false;
			}
			if (++index >= args.size() || args[index].get_type() != Variant::STRING || String(args[index]).is_empty()) {
				return false;
			}
		}
	}
	return true;
}

Error parse_status(const String &p_output, const String &p_token, Dictionary &r_status) {
	r_status.clear();
	if (!valid_token(p_token)) {
		return ERR_INVALID_PARAMETER;
	}
	bool found = false;
	for (const String &line : p_output.split("\n")) {
		if (!line.begins_with("GODOT_AURORA_SESSION:")) {
			continue;
		}
		PackedStringArray fields = line.strip_edges().split(":");
		if (found || fields.size() != 6 || fields[1] != p_token || fields[3].is_empty() || !fields[4].is_valid_int() || fields[4].to_int() < 0 || !fields[5].is_valid_int() || fields[5].to_int() < 0) {
			r_status.clear();
			return ERR_INVALID_DATA;
		}
		String state = fields[2];
		if (state != "missing" && state != "preparing" && state != "running" && state != "stopping" && state != "finalizing" && state != "stopped" && state != "exited" && state != "failed" && state != "interrupted") {
			return ERR_INVALID_DATA;
		}
		if ((state == "running" || state == "stopping") && (fields[4].to_int() <= 1 || fields[5].to_int() <= 0)) {
			return ERR_INVALID_DATA;
		}
		r_status["state"] = state;
		r_status["boot"] = fields[3];
		r_status["pid"] = fields[4];
		r_status["start"] = fields[5];
		found = true;
	}
	return found ? OK : ERR_INVALID_DATA;
}

bool supports_session(const Ref<FileAccess> &p_file, const String &p_arch) {
	if (p_file.is_null() || p_file->get_length() < 64) {
		return false;
	}
	p_file->set_big_endian(false);
	p_file->seek(0);
	if (p_file->get_32() != 0x464c457f) {
		return false;
	}
	const uint8_t elf_class = p_file->get_8();
	if ((p_arch != "arm32" && p_arch != "arm64") || elf_class != (p_arch == "arm32" ? 1 : 2) || p_file->get_8() != 1) {
		return false;
	}
	p_file->seek(18);
	if (p_file->get_16() != (p_arch == "arm32" ? 40 : 183)) {
		return false;
	}
	p_file->seek(elf_class == 1 ? 28 : 32);
	const uint64_t phoff = elf_class == 1 ? p_file->get_32() : p_file->get_64();
	p_file->seek(elf_class == 1 ? 42 : 54);
	const uint16_t phsize = p_file->get_16();
	const uint16_t phcount = p_file->get_16();
	const uint64_t size = p_file->get_length();
	if (phsize < (elf_class == 1 ? 32 : 56) || phcount > 1024 || phoff > size || uint64_t(phsize) * phcount > size - phoff) {
		return false;
	}
	const char marker[] = "GODOT_AURORA_REMOTE_SESSION_V1";
	constexpr int marker_size = sizeof(marker); // Include the terminating NUL.
	uint8_t bytes[65536 + marker_size];
	for (int entry = 0; entry < phcount; entry++) {
		p_file->seek(phoff + uint64_t(entry) * phsize);
		if (p_file->get_32() != 1) { // PT_LOAD only.
			continue;
		}
		if (elf_class == 2) {
			p_file->get_32(); // Flags.
		}
		uint64_t offset = elf_class == 1 ? p_file->get_32() : p_file->get_64();
		p_file->seek(p_file->get_position() + (elf_class == 1 ? 8 : 16));
		uint64_t length = elf_class == 1 ? p_file->get_32() : p_file->get_64();
		if (offset > size || length > size - offset) {
			return false;
		}
		p_file->seek(offset);
		int overlap = 0;
		while (length) {
			uint64_t count = MIN(length, uint64_t(65536));
			if (p_file->get_buffer(bytes + overlap, count) != count) {
				return false;
			}
			int total = overlap + count;
			for (int index = 0; index + marker_size <= total; index++) {
				if (memcmp(bytes + index, marker, marker_size) == 0) {
					return true;
				}
			}
			length -= count;
			overlap = MIN(total, marker_size - 1);
			memmove(bytes, bytes + total - overlap, overlap);
		}
	}
	return false;
}

Error load_record(const String &p_path, Dictionary &r_record) {
	r_record.clear();
	if (!FileAccess::exists(p_path)) {
		return OK;
	}
	Error error;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ, &error);
	if (error != OK) {
		return error;
	}
	if (file->get_length() > 1024 * 1024) {
		return ERR_INVALID_DATA;
	}
	JSON json;
	if (json.parse(file->get_as_text()) != OK || json.get_data().get_type() != Variant::DICTIONARY || !valid_record(json.get_data())) {
		return ERR_INVALID_DATA;
	}
	r_record = json.get_data();
	return OK;
}

Error save_record(const String &p_path, const Dictionary &p_record) {
	if (!valid_record(p_record)) {
		return ERR_INVALID_DATA;
	}
	String encoded = JSON::stringify(p_record);
	if (encoded.utf8().length() > 1024 * 1024) {
		return ERR_INVALID_DATA;
	}
	Error error = DirAccess::make_dir_recursive_absolute(p_path.get_base_dir());
	if (error != OK) {
		return error;
	}
	const String temporary = p_path + ".tmp." + itos(OS::get_singleton()->get_process_id());
	Ref<FileAccess> file = FileAccess::open(temporary, FileAccess::WRITE, &error);
	if (error != OK) {
		return error;
	}
	file->store_string(encoded);
	file->flush();
	error = file->get_error();
	file.unref();
	if (error == OK) {
		error = DirAccess::rename_absolute(temporary, p_path);
	}
	if (error != OK) {
		DirAccess::remove_absolute(temporary);
	}
	return error;
}
} // namespace AuroraDeploy
