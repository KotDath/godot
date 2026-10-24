/**************************************************************************/
/*  test_remote_deploy_state.cpp                                          */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_remote_deploy_state)

#ifdef TOOLS_ENABLED
#include "core/io/file_access_memory.h"
#include "core/io/json.h"

#include "platform/auroraos/export/remote_deploy_state.h"

namespace TestAuroraDeploy {
const String token = "0123456789abcdef0123456789abcdef";

Dictionary record() {
	Dictionary result;
	result["schema"] = 1;
	result["token"] = token;
	result["sfdk"] = "/SDK path/sfdk";
	result["device"] = "Fixture device";
	result["package"] = "org.example.game";
	result["phase"] = "launching";
	result["rpm"] = "org.example.game-1-2.aarch64.rpm";
	result["staging"] = "/project/.godot/aurora-deploy/fixture";
	result["install_unconfirmed"] = false;
	return result;
}

TEST_CASE("[AuroraDeploy] Journal rejects invalid ownership and phases") {
	CHECK(AuroraDeploy::valid_record(record()));
	JSON json;
	REQUIRE(json.parse(JSON::stringify(record())) == OK);
	CHECK(AuroraDeploy::valid_record(json.get_data()));
	CHECK_FALSE(AuroraDeploy::valid_token("../0123456789abcdef0123456789abcde"));
	CHECK_FALSE(AuroraDeploy::valid_token("-123456789abcdef0123456789abcdef"));
	CHECK_FALSE(AuroraDeploy::valid_token(token.to_upper()));
	for (const String &key : { String("token"), String("device"), String("sfdk"), String("staging") }) {
		Dictionary invalid = record();
		invalid[key] = "";
		CHECK_FALSE(AuroraDeploy::valid_record(invalid));
	}
	Dictionary invalid = record();
	invalid["phase"] = "guessed_success";
	CHECK_FALSE(AuroraDeploy::valid_record(invalid));
	invalid = record();
	invalid["schema"] = 2;
	CHECK_FALSE(AuroraDeploy::valid_record(invalid));
	invalid["schema"] = true;
	CHECK_FALSE(AuroraDeploy::valid_record(invalid));
	invalid["schema"] = 1.5;
	CHECK_FALSE(AuroraDeploy::valid_record(invalid));
	invalid = record();
	invalid["install_unconfirmed"] = "false";
	CHECK_FALSE(AuroraDeploy::valid_record(invalid));
	invalid = record();
	invalid["package"] = "org.example/other.game";
	CHECK_FALSE(AuroraDeploy::valid_record(invalid));
}

TEST_CASE("[AuroraDeploy] Status belongs to one saved token") {
	Dictionary status;
	String reply = "GODOT_AURORA_SESSION:" + token + ":running:boot-id:123:456\n";
	CHECK(AuroraDeploy::parse_status("SDK diagnostic\n" + reply, token, status) == OK);
	CHECK(String(status["state"]) == "running");
	CHECK(String(status["pid"]) == "123");
	CHECK(AuroraDeploy::parse_status(reply, "ffffffffffffffffffffffffffffffff", status) == ERR_INVALID_DATA);
	CHECK(status.is_empty());
	CHECK(AuroraDeploy::parse_status(reply + reply, token, status) == ERR_INVALID_DATA);
	CHECK(status.is_empty());
	CHECK(AuroraDeploy::parse_status("no result", token, status) == ERR_INVALID_DATA);
	CHECK(AuroraDeploy::parse_status(reply.replace(":123:", ":1:"), token, status) == ERR_INVALID_DATA);
	CHECK(AuroraDeploy::parse_status(reply.replace(":456", ":0"), token, status) == ERR_INVALID_DATA);
	CHECK(AuroraDeploy::parse_status(reply.replace("running", "unknown"), token, status) == ERR_INVALID_DATA);
	CHECK(AuroraDeploy::parse_status(reply.replace("running", "missing").replace(":123:456", ":0:0"), token, status) == OK);
}

void put(Vector<uint8_t> &p_bytes, int p_offset, uint64_t p_value, int p_size) {
	for (int index = 0; index < p_size; index++) {
		p_bytes.write[p_offset + index] = (p_value >> (index * 8)) & 255;
	}
}

Vector<uint8_t> elf(bool p_arm64, bool p_marker_loaded) {
	Vector<uint8_t> bytes;
	bytes.resize(1024);
	bytes.fill(0);
	put(bytes, 0, 0x464c457f, 4);
	put(bytes, 4, p_arm64 ? 2 : 1, 1);
	put(bytes, 5, 1, 1);
	put(bytes, 18, p_arm64 ? 183 : 40, 2);
	put(bytes, p_arm64 ? 32 : 28, 64, p_arm64 ? 8 : 4);
	put(bytes, p_arm64 ? 54 : 42, p_arm64 ? 56 : 32, 2);
	put(bytes, p_arm64 ? 56 : 44, 1, 2);
	put(bytes, 64, 1, 4); // PT_LOAD.
	put(bytes, p_arm64 ? 72 : 68, 256, p_arm64 ? 8 : 4);
	put(bytes, p_arm64 ? 96 : 80, 256, p_arm64 ? 8 : 4);
	const char marker[] = "GODOT_AURORA_REMOTE_SESSION_V1";
	memcpy(bytes.ptrw() + (p_marker_loaded ? 300 : 700), marker, sizeof(marker));
	return bytes;
}

bool supports(const Vector<uint8_t> &p_bytes, const String &p_arch) {
	Ref<FileAccessMemory> memory;
	memory.instantiate();
	memory->open_custom(p_bytes.ptr(), p_bytes.size());
	return AuroraDeploy::supports_session(memory, p_arch);
}

TEST_CASE("[AuroraDeploy] Runtime capability must be loaded ARM code") {
	CHECK(supports(elf(true, true), "arm64"));
	CHECK(supports(elf(false, true), "arm32"));
	CHECK_FALSE(supports(elf(true, false), "arm64")); // Marker only in an appended pack.
	CHECK_FALSE(supports(elf(true, true), "arm32"));
	CHECK_FALSE(supports(elf(false, true), "arm64"));
	Vector<uint8_t> invalid = elf(true, true);
	put(invalid, 18, 62, 2); // x86_64 must not authorize an ARM deployment.
	CHECK_FALSE(supports(invalid, "arm64"));
	invalid = elf(true, true);
	put(invalid, 32, UINT64_MAX, 8);
	CHECK_FALSE(supports(invalid, "arm64"));
	invalid = elf(true, true);
	put(invalid, 96, UINT64_MAX, 8);
	CHECK_FALSE(supports(invalid, "arm64"));
	invalid = elf(true, true);
	put(invalid, 5, 2, 1);
	CHECK_FALSE(supports(invalid, "arm64"));
}
} // namespace TestAuroraDeploy
#endif
