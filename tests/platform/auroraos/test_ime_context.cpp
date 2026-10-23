/**************************************************************************/
/*  test_ime_context.cpp                                                  */
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

#include "platform/auroraos/ime_context.h"
#include "platform/auroraos/orientation.h"

TEST_FORCE_LINK(test_ime_context)

#ifndef ADVANCED_GUI_DISABLED
#include "scene/gui/code_edit.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/text_edit.h"
#include "scene/main/scene_tree.h"
#include "tests/display_server_mock.h"
#endif

namespace TestAuroraIME {
TEST_CASE("[AuroraIME] Preedit metadata and rotated rectangles") {
	Vector<AuroraIME::PreeditStyle> styles;
	styles.push_back({ 2, 4, 6 }); // The emoji, not its four individual bytes.
	styles.push_back({ 3, 1, 7 }); // Invalid UTF-8 boundaries must not decorate another character.
	styles.push_back({ UINT32_MAX, UINT32_MAX, 5 });
	Vector2i selection;
	const Dictionary metadata = AuroraIME::preedit_metadata(U"А😀Б", U"итог", -1, styles, selection);
	CHECK(String(metadata["commit_text"]) == String(U"итог"));
	CHECK_FALSE(bool(metadata["caret_visible"]));
	CHECK(selection == Vector2i(1, 1));
	const Array converted = metadata["styles"];
	REQUIRE(converted.size() == 1);
	CHECK(Vector2i(Dictionary(converted[0])["range"]) == Vector2i(1, 2));
	CHECK(StringName(Dictionary(converted[0])["style"]) == StringName("selection"));
	CHECK(bool(AuroraIME::preedit_metadata(U"А😀Б", "", INT32_MAX, {}, selection)["caret_visible"]));
	CHECK(selection == Vector2i(3, 0));
	CHECK_FALSE(bool(AuroraIME::preedit_metadata(U"А😀Б", "", 3, {}, selection)["caret_visible"]));
	const Rect2 native_panel(0, 0, 520, 2000);
	CHECK(AuroraOrientation::transform_rect(native_panel, Size2(1200, 2000), 3) == Rect2(0, 680, 2000, 520));
	const Rect2 caret(200, 180, 1, 40);
	for (int transform = 0; transform < 4; transform++) {
		const Size2 native_size(1200, 2000);
		const Size2 oriented_size = transform % 2 ? Size2(2000, 1200) : native_size;
		const Rect2 surface = AuroraOrientation::transform_rect(caret, oriented_size, (4 - transform) % 4);
		CHECK(AuroraOrientation::transform_rect(surface, native_size, transform) == caret);
	}
}

TEST_CASE("[AuroraIME] UTF-8 boundaries and replacement ranges") {
	const String text = U"А😀Б";
	CHECK(AuroraIME::character_at_byte(text, 0) == 0);
	CHECK(AuroraIME::character_at_byte(text, 2) == 1);
	CHECK(AuroraIME::character_at_byte(text, 6) == 2);
	CHECK(AuroraIME::character_at_byte(text, 8) == 3);
	CHECK(AuroraIME::character_at_byte(text, 3) == -1);
	CHECK(AuroraIME::character_at_byte(text, -1) == -1);
	CHECK(AuroraIME::character_at_byte(text, UINT32_MAX) == -1);
	Dictionary context;
	context["text"] = text;
	context["cursor"] = 2;
	context["anchor"] = 1;
	context["origin"] = Vector2i();
	Dictionary operation;
	REQUIRE(AuroraIME::operation(context, U"Ю", 2, 2, false, 0, 0, operation));
	CHECK(Vector2i(operation["range"]) == Vector2i(0, 3));
	CHECK(String(operation["text"]) == String(U"Ю"));
	CHECK_FALSE(AuroraIME::operation(context, "x", 1, 0, false, 0, 0, operation));
	CHECK_FALSE(AuroraIME::operation(context, "x", UINT32_MAX, 0, false, 0, 0, operation));
	context["secret"] = true;
	CHECK_FALSE(AuroraIME::operation(context, "x", 2, 0, false, 0, 0, operation));
	CHECK(AuroraIME::operation(context, U"пароль", 0, 0, false, 0, 0, operation));
	CHECK_FALSE(operation.has("context"));
}

#ifndef ADVANCED_GUI_DISABLED
struct IMEScope {
	DisplayServerMock *display = static_cast<DisplayServerMock *>(DisplayServer::get_singleton());
	IMEScope() { display->set_ime_enabled(true); }
	~IMEScope() { display->set_ime_enabled(false); }
	void commit(const String &p_text, uint32_t p_before = 0, uint32_t p_after = 0, bool p_move = false, int p_cursor = 0, int p_anchor = 0) {
		Dictionary operation;
		REQUIRE(AuroraIME::operation(display->get_ime_context(), p_text, p_before, p_after, p_move, p_cursor, p_anchor, operation));
		display->commit_ime(operation);
	}
};

TEST_CASE("[SceneTree][AuroraIME] Preedit fallback belongs to its text client") {
	IMEScope ime;
	LineEdit *line = memnew(LineEdit);
	TextEdit *edit = memnew(TextEdit);
	SceneTree::get_singleton()->get_root()->add_child(line);
	SceneTree::get_singleton()->get_root()->add_child(edit);
	line->grab_focus();
	line->edit();
	Dictionary metadata;
	metadata["commit_text"] = U"\tготово 😀\n";
	metadata["caret_visible"] = false;
	ime.display->set_ime_preedit(U"композиция", Vector2i(), metadata);
	line->notification(MainLoop::NOTIFICATION_OS_IME_UPDATE);
	CHECK(line->has_ime_text());
	CHECK(line->get_text().is_empty());
	// A different client/backend update must not overwrite the cached fallback.
	ime.display->set_ime_preedit("unrelated", Vector2i(), Dictionary());
	line->apply_ime();
	CHECK(line->get_text() == String(U"готово 😀"));
	CHECK_FALSE(line->has_ime_text());
	line->undo();
	CHECK(line->get_text().is_empty());
	line->edit();
	metadata["commit_text"] = "";
	ime.display->set_ime_preedit(U"отмена", Vector2i(), metadata);
	line->notification(MainLoop::NOTIFICATION_OS_IME_UPDATE);
	line->apply_ime();
	CHECK(line->get_text().is_empty());
	line->edit();
	ime.display->set_ime_preedit("legacy", Vector2i(), Dictionary());
	line->notification(MainLoop::NOTIFICATION_OS_IME_UPDATE);
	line->apply_ime();
	CHECK(line->get_text() == "legacy");

	edit->grab_focus();
	metadata["commit_text"] = U"первая\n\t😀";
	ime.display->set_ime_preedit("preedit", Vector2i(), metadata);
	edit->notification(MainLoop::NOTIFICATION_OS_IME_UPDATE);
	CHECK(edit->has_ime_text());
	// Same display text with a changed fallback must also notify the client.
	metadata["commit_text"] = U"вторая\n\t😀";
	ime.display->set_ime_preedit("preedit", Vector2i(), metadata);
	edit->notification(MainLoop::NOTIFICATION_OS_IME_UPDATE);
	edit->apply_ime();
	CHECK(edit->get_text() == String(U"вторая\n\t😀"));
	edit->undo();
	CHECK(edit->get_text().is_empty());
	ime.display->set_ime_preedit("discard", Vector2i(), metadata);
	edit->notification(MainLoop::NOTIFICATION_OS_IME_UPDATE);
	edit->cancel_ime();
	edit->apply_ime();
	CHECK(edit->get_text().is_empty());
	memdelete(line);
	memdelete(edit);
}

TEST_CASE("[SceneTree][AuroraIME] LineEdit context and atomic edits") {
	IMEScope ime;
	LineEdit *line = memnew(LineEdit);
	SceneTree::get_singleton()->get_root()->add_child(line);
	line->set_text(U"А😀Б");
	line->grab_focus();
	line->edit();
	line->set_caret_column(2);
	REQUIRE_FALSE(ime.display->get_ime_context().is_empty());
	ime.commit(U"ё", 4, 2);
	CHECK(line->get_text() == String(U"Аё"));
	line->undo();
	CHECK(line->get_text() == String(U"А😀Б"));

	line->select(1, 2);
	line->set_caret_column(1); // Reversed selection: delete counts exclude the selection itself.
	ime.commit(U"Ю", 2, 2);
	CHECK(line->get_text() == String(U"Ю"));
	line->undo();
	CHECK(line->get_text() == String(U"А😀Б"));

	line->deselect();
	line->set_text("ab");
	line->set_caret_column(1);
	ime.commit(U"Ю", 0, 0, true, -2, 0);
	CHECK(line->get_text() == String(U"aЮb"));
	CHECK(line->get_selected_text() == String(U"Ю"));
	CHECK(line->get_caret_column() == 1);

	Dictionary stale;
	REQUIRE(AuroraIME::operation(ime.display->get_ime_context(), "x", 1, 0, false, 0, 0, stale));
	line->set_caret_column(0);
	ime.display->commit_ime(stale);
	CHECK(line->get_text() == String(U"aЮb"));

	line->set_secret(true);
	CHECK(String(ime.display->get_ime_context()["text"]).is_empty());
	CHECK_FALSE(bool(ime.display->get_ime_context()["completion"]));
	line->set_secret(false);
	CHECK(bool(ime.display->get_ime_context()["completion"]));
	line->set_virtual_keyboard_type(LineEdit::KEYBOARD_TYPE_PASSWORD);
	CHECK(String(ime.display->get_ime_context()["text"]).is_empty());
	CHECK(bool(ime.display->get_ime_context()["secret"]));
	CHECK_FALSE(bool(ime.display->get_ime_context()["completion"]));
	CHECK_FALSE(bool(ime.display->get_ime_context()["available"]));
	line->set_virtual_keyboard_type(LineEdit::KEYBOARD_TYPE_DEFAULT);
	CHECK_FALSE(bool(ime.display->get_ime_context()["secret"]));
	CHECK(bool(ime.display->get_ime_context()["completion"]));
	line->set_max_length(4);
	line->set_text("abc");
	line->select_all();
	ime.commit(U"А😀БВГ");
	CHECK(line->get_text() == String(U"А😀БВ"));

	const Callable old_target = ime.display->get_ime_commit_callback();
	line->set_editable(false);
	Dictionary ignored;
	ignored["text"] = "ignored";
	old_target.call(ignored);
	CHECK(line->get_text() == String(U"А😀БВ"));
	memdelete(line);
}

TEST_CASE("[SceneTree][AuroraIME] Surface leave finishes native preedit once") {
	IMEScope ime;
	LineEdit *line = memnew(LineEdit);
	TextEdit *edit = memnew(TextEdit);
	SceneTree::get_singleton()->get_root()->add_child(line);
	SceneTree::get_singleton()->get_root()->add_child(edit);
	line->grab_focus();
	line->edit();
	Dictionary metadata;
	metadata["commit_text"] = U"готово 😀";
	ime.display->set_ime_preedit("pending", Vector2i(7, 0), metadata);
	line->notification(MainLoop::NOTIFICATION_OS_IME_UPDATE);
	const Callable old_finish = ime.display->get_ime_finish_callback();
	REQUIRE(old_finish.is_valid());
	old_finish.call();
	CHECK(line->get_text() == String(U"готово 😀"));
	CHECK_FALSE(line->has_ime_text());
	old_finish.call();
	CHECK(line->get_text() == String(U"готово 😀"));
	line->undo();
	CHECK(line->get_text().is_empty());

	line->set_caret_column(0);
	ime.display->set_ime_preedit("pending", Vector2i(7, 0), metadata);
	line->notification(MainLoop::NOTIFICATION_OS_IME_UPDATE);
	line->notification(Control::NOTIFICATION_WM_WINDOW_FOCUS_OUT);
	CHECK(line->get_text() == String(U"готово 😀"));
	CHECK_FALSE(line->has_ime_text());
	line->notification(Control::NOTIFICATION_WM_WINDOW_FOCUS_IN);
	line->undo();
	CHECK(line->get_text().is_empty());

	// Backends without native fallback metadata keep their existing focus policy.
	ime.display->set_ime_preedit("legacy", Vector2i(6, 0), Dictionary());
	line->notification(MainLoop::NOTIFICATION_OS_IME_UPDATE);
	line->notification(Control::NOTIFICATION_WM_WINDOW_FOCUS_OUT);
	CHECK(line->has_ime_text());
	CHECK(line->get_text().is_empty());
	line->notification(Control::NOTIFICATION_WM_WINDOW_FOCUS_IN);
	line->cancel_ime();

	edit->grab_focus();
	edit->set_caret_column(0);
	metadata["commit_text"] = U"строка\n\t😀";
	ime.display->set_ime_preedit("pending", Vector2i(7, 0), metadata);
	edit->notification(MainLoop::NOTIFICATION_OS_IME_UPDATE);
	const Callable current_commit = ime.display->get_ime_commit_callback();
	old_finish.call(); // A delayed old client's finish must not deactivate the new one.
	CHECK(ime.display->get_ime_commit_callback() == current_commit);
	CHECK(edit->has_ime_text());
	edit->notification(Control::NOTIFICATION_WM_WINDOW_FOCUS_OUT);
	CHECK(edit->get_text() == String(U"строка\n\t😀"));
	CHECK_FALSE(edit->has_ime_text());
	edit->notification(Control::NOTIFICATION_WM_WINDOW_FOCUS_IN);
	edit->undo();
	CHECK(edit->get_text().is_empty());
	memdelete(line);
	memdelete(edit);
}

TEST_CASE("[SceneTree][AuroraIME] TextEdit context across lines") {
	IMEScope ime;
	TextEdit *edit = memnew(TextEdit);
	SceneTree::get_singleton()->get_root()->add_child(edit);
	edit->set_text(U"head\nА😀Б\nend");
	edit->grab_focus();
	edit->set_caret_line(1);
	edit->set_caret_column(2);
	REQUIRE_FALSE(ime.display->get_ime_context().is_empty());
	CHECK(bool(ime.display->get_ime_context()["completion"]));
	ime.commit(U"ё", 4, 2);
	CHECK(edit->get_text() == String(U"head\nАё\nend"));
	edit->undo();
	CHECK(edit->get_text() == String(U"head\nА😀Б\nend"));
	edit->deselect();
	edit->set_caret_line(1);
	edit->set_caret_column(0);
	ime.commit("X", 1, 0); // Remove the previous line break.
	CHECK(edit->get_text() == String(U"headXА😀Б\nend"));
	edit->undo();
	CHECK(edit->get_text() == String(U"head\nА😀Б\nend"));

	edit->set_selecting_enabled(false);
	edit->set_caret_line(1);
	edit->set_caret_column(2);
	ime.commit(U"ё", 4, 2);
	CHECK(edit->get_text() == String(U"head\nАё\nend"));
	edit->undo();
	edit->set_selecting_enabled(true);

	edit->set_text("ab");
	edit->set_caret_line(0);
	edit->set_caret_column(1);
	ime.commit(U"Ю", 0, 0, true, -2, 0);
	CHECK(edit->get_selected_text() == String(U"Ю"));
	CHECK(edit->get_caret_column() == 1);

	edit->set_text(String("abcdefghij\n").repeat(10000));
	edit->set_caret_line(5000);
	const Dictionary bounded = ime.display->get_ime_context();
	CHECK(String(bounded["text"]).length() <= 1000);
	CHECK(String(bounded["text"]).utf8().length() <= 4000);
	edit->select(0, 0, 9999, 0);
	CHECK(String(ime.display->get_ime_context()["text"]).is_empty());
	memdelete(edit);
}

TEST_CASE("[SceneTree][AuroraIME] CodeEdit opts out of native word prediction") {
	IMEScope ime;
	CodeEdit *edit = memnew(CodeEdit);
	SceneTree::get_singleton()->get_root()->add_child(edit);
	edit->set_text("extends Node");
	edit->grab_focus();
	edit->set_caret_column(7);
	REQUIRE_FALSE(ime.display->get_ime_context().is_empty());
	CHECK_FALSE(bool(ime.display->get_ime_context()["completion"]));
	// Disabling prediction must not disable ordinary IME commits.
	ime.commit(U"\n# Русский 😀");
	CHECK(edit->get_text().contains(String(U"\n# Русский 😀")));
	edit->undo();
	CHECK(edit->get_text() == "extends Node");
	memdelete(edit);
}
#endif
} // namespace TestAuroraIME
