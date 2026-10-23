/**************************************************************************/
/*  test_editor_help_lifecycle.cpp                                        */
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

TEST_FORCE_LINK(test_editor_help_lifecycle)

#ifdef TOOLS_ENABLED
#include "core/object/callable_mp.h"
#include "core/object/message_queue.h"
#include "editor/doc/editor_help.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"

namespace TestEditorHelpLifecycle {
TEST_CASE("[DocCache] Deferred cache callbacks after editor teardown") {
	// These static methods can remain in the message queue after cleanup_doc()
	// and EditorNode destruction. No new worker may start in that state.
	REQUIRE(EditorNode::get_singleton() == nullptr);
	REQUIRE(EditorFileSystem::get_singleton() == nullptr);
	EditorHelp::regenerate_script_doc_cache();
	EditorHelp::load_script_doc_cache();
	callable_mp_static(&EditorHelp::regenerate_script_doc_cache).call_deferred();
	callable_mp_static(&EditorHelp::load_script_doc_cache).call_deferred();
	MessageQueue::get_singleton()->flush();
	CHECK(EditorHelp::get_doc_data() == nullptr);
}
} // namespace TestEditorHelpLifecycle
#endif
