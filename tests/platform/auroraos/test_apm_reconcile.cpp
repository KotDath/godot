/**************************************************************************/
/*  test_apm_reconcile.cpp                                                */
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

TEST_FORCE_LINK(test_apm_reconcile)

#ifdef TOOLS_ENABLED
#include "core/io/json.h"

#include "platform/auroraos/export/apm_reconcile.h"
#include "platform/auroraos/export/remote_deploy_state.h"

namespace TestAuroraAPM {
Dictionary receipt() {
	Dictionary files;
	files["/usr/bin/org.example.game"] = String("a").repeat(64);
	files["/usr/share/org.example.game/lib/plugin dir/ёж.so"] = String("b").repeat(64);
	Dictionary value;
	value["schema"] = 1;
	value["package"] = "org.example.game";
	value["version"] = "1.0.0";
	value["release"] = "1.godot.1234";
	value["arch"] = "arm64";
	value["files"] = files;
	return value;
}

TEST_CASE("[AuroraAPM] Full GVariant package response, not a regex match") {
	Dictionary fields;
	// Shape observed from the real emulator's ordinary-user APM.GetPackage.
	String actual_shape = "({'certificate.id': '1234', 'certificate.subgroup': 'regular', 'general.appname': 'Game', 'general.id': 'org.example.game', 'general.orgname': 'org.example', 'general.version': '1.0.0-4'},)\n";
	CHECK(AuroraAPM::parse_package(actual_shape, fields) == OK);
	CHECK(String(fields["general.version"]) == "1.0.0-4");
	CHECK(AuroraAPM::parse_package(" ( @a{ss} {\"general.id\": \"org.example.game\", 'general.version': '1-2', 'label': 'User\\'s \\u0451 \\U0001f600 \\\\ path'}, ) ", fields) == OK);
	CHECK(String(fields["label"]) == U"User's ё 😀 \\ path");
	for (const String &bad : {
				 String("garbage ") + actual_shape,
				 actual_shape + "unparsed tail",
				 String("({'general.id':'org.example.game', 'general.version':'1', 'general.id':'other'},)"),
				 String("({'general.id': <'org.example.game'>, 'general.version':'1'},)"),
				 String("({'general.id':'org.example.game', 'general.version':'1'"),
				 String("({'general.id':'org.example.game'},)"),
				 String("({'general.id':'org.example.game', 'general.version':'1\\u0000'},)"),
				 String("({'general.id':'org.example.game', 'general.version':'1\\Uffffffff'},)") }) {
		CHECK(AuroraAPM::parse_package(bad, fields) == ERR_INVALID_DATA);
		CHECK(fields.is_empty());
	}
	bool pending = false;
	CHECK(AuroraAPM::parse_pending("(true,)\n", pending) == OK);
	CHECK(pending);
	CHECK(AuroraAPM::parse_pending(" (false, ) ", pending) == OK);
	CHECK_FALSE(pending);
	CHECK(AuroraAPM::parse_pending("(false, true)", pending) == ERR_INVALID_DATA);
	CHECK(AuroraAPM::parse_pending("false", pending) == ERR_INVALID_DATA);
}

TEST_CASE("[AuroraAPM] Receipt is confined to the exported native runtime") {
	CHECK(AuroraAPM::valid_receipt(receipt(), "org.example.game"));
	JSON json;
	REQUIRE(json.parse(JSON::stringify(receipt())) == OK);
	CHECK(AuroraAPM::valid_receipt(json.get_data(), "org.example.game"));
	CHECK_FALSE(AuroraAPM::valid_receipt(receipt(), "org.other.game"));
	for (const String &path : { String("/etc/shadow"), String("/usr/share/org.example.game/lib/../../other.so"), String("/usr/share/org.example.game/lib/a\nb.so") }) {
		Dictionary invalid = receipt().duplicate(true);
		Dictionary files = invalid["files"];
		files[path] = String("a").repeat(64);
		CHECK_FALSE(AuroraAPM::valid_receipt(invalid, "org.example.game"));
	}
	Dictionary invalid = receipt().duplicate(true);
	Dictionary files = invalid["files"];
	files.erase("/usr/bin/org.example.game");
	CHECK_FALSE(AuroraAPM::valid_receipt(invalid, "org.example.game"));
	invalid = receipt().duplicate(true);
	files = invalid["files"];
	files["/usr/bin/org.example.game"] = "not a hash";
	CHECK_FALSE(AuroraAPM::valid_receipt(invalid, "org.example.game"));
}

TEST_CASE("[AuroraAPM] Every installed checksum must match its exact path") {
	Dictionary expected = receipt()["files"];
	String main = String("a").repeat(64) + "  /usr/bin/org.example.game\n";
	String library = String("b").repeat(64) + "  /usr/share/org.example.game/lib/plugin dir/ёж.so\n";
	CHECK(AuroraAPM::check_hashes(main + library, expected) == OK);
	CHECK(AuroraAPM::check_hashes(String("c").repeat(64) + main.substr(64) + library, expected) == ERR_FILE_CORRUPT);
	CHECK(AuroraAPM::check_hashes(main, expected) == ERR_INVALID_DATA);
	CHECK(AuroraAPM::check_hashes(main + main + library, expected) == ERR_INVALID_DATA);
	CHECK(AuroraAPM::check_hashes(main + library + main.replace("org.example.game", "other"), expected) == ERR_INVALID_DATA);
}

TEST_CASE("[AuroraAPM] Version-two recovery keeps only supported non-secret argv") {
	Dictionary record;
	record["schema"] = 2;
	record["token"] = "0123456789abcdef0123456789abcdef";
	record["sfdk"] = "/sdk/sfdk";
	record["device"] = "device";
	record["package"] = "org.example.game";
	record["phase"] = "installing";
	record["rpm"] = "game.rpm";
	record["staging"] = "/project/.godot/aurora-deploy/1-2";
	record["install_unconfirmed"] = true;
	record["runtime_verified"] = false;
	record["receipt"] = receipt();
	Array args{ "/usr/bin/org.example.game", "--remote-debug", "tcp://host:6007", "--debug-collisions" };
	record["launch_args"] = args;
	CHECK(AuroraDeploy::valid_record(record));
	JSON json;
	REQUIRE(json.parse(JSON::stringify(record)) == OK);
	CHECK(AuroraDeploy::valid_record(json.get_data()));
	args.push_back("--remote-fs-password");
	args.push_back("not-for-journals");
	CHECK_FALSE(AuroraDeploy::valid_record(record));
	args.resize(4);
	args[0] = "/bin/sh";
	CHECK_FALSE(AuroraDeploy::valid_record(record));
	args[0] = "/usr/bin/org.example.game";
	record["phase"] = "running";
	CHECK_FALSE(AuroraDeploy::valid_record(record));
	record["install_unconfirmed"] = false;
	record["runtime_verified"] = true;
	CHECK(AuroraDeploy::valid_record(record));
}
} // namespace TestAuroraAPM
#endif
