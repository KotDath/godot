/**************************************************************************/
/*  test_native_library_map.cpp                                           */
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

#include "platform/auroraos/native_library_map.h"

TEST_FORCE_LINK(test_native_library_map)

namespace TestAuroraNativeLibrary {
TEST_CASE("[AuroraNative] Exact project mapping and confined installed paths") {
	Dictionary paths;
	paths["res://addons/Мой plugin/libentry.so"] = "lib/project/addons/Мой plugin/libentry.so";
	paths["res://vendor/libleaf.so"] = "lib/project/deps/libleaf.so";
	CHECK(AuroraNativeLibrary::mapped_path("res://addons/Мой plugin/libentry.so", paths) == "lib/project/addons/Мой plugin/libentry.so");
	CHECK(AuroraNativeLibrary::mapped_path("res://vendor/libleaf.so", paths) == "lib/project/deps/libleaf.so");
	CHECK(AuroraNativeLibrary::mapped_path("res://other/libentry.so", paths).is_empty());
	CHECK(AuroraNativeLibrary::mapped_path("/host/libentry.so", paths).is_empty());
	paths["res://bad.so"] = "lib/project/../../bad.so";
	CHECK(AuroraNativeLibrary::mapped_path("res://bad.so", paths).is_empty());
	paths["res://bad.so"] = "/usr/lib/bad.so";
	CHECK(AuroraNativeLibrary::mapped_path("res://bad.so", paths).is_empty());
	paths["res://bad.so"] = 42;
	CHECK(AuroraNativeLibrary::mapped_path("res://bad.so", paths).is_empty());
	CHECK_FALSE(AuroraNativeLibrary::valid_relative_path("lib/project//bad.so"));
	CHECK_FALSE(AuroraNativeLibrary::valid_relative_path("lib/project/./bad.so"));
	CHECK_FALSE(AuroraNativeLibrary::valid_relative_path("lib/project/$LIB/bad.so"));
}
} // namespace TestAuroraNativeLibrary
