@tool
extends EditorPlugin

var started := false
var busy := false
var finished := false
var original := {}
var results: Array = []
var dialog_frames := 0
var dialog_answered := false
var cancel_clicked := false
var fields_masked := false
var retry_pending := false
var preset: EditorExportPreset
var export_dialog: Window
var mode := ""

func _enter_tree() -> void:
	mode = OS.get_environment("GODOT_AURORA_SIGNING_TEST")
	# ProgressDialog disables the editor window while pumping nested frames.
	# The test driver must still be able to click its Cancel button.
	process_mode = Node.PROCESS_MODE_ALWAYS
	set_process(not mode.is_empty())
	if not mode.is_empty():
		get_tree().process_frame.connect(_drive_modal)

func _drive_modal() -> void:
	# Progress UI can disable processing on editor windows. The frame signal
	# still runs during its nested event loop, like actual user input does.
	if busy and not finished:
		_drive_dialog()
		_drive_cancel()

func _exit_tree() -> void:
	_restore_settings()

func _restore_settings() -> void:
	var settings = EditorInterface.get_editor_settings()
	for key in original:
		settings.set_setting(key, original[key])
	original.clear()

func _find(node: Node, type: String) -> Node:
	if node.is_class(type):
		return node
	for child in node.get_children(true):
		var found = _find(child, type)
		if found:
			return found
	return null

func _drive_dialog() -> void:
	var dialog = get_tree().root.find_child("AuroraSigningPassphrase", true, false)
	if not dialog or not dialog.visible or dialog_answered:
		return
	var field: LineEdit = _find(dialog, "LineEdit")
	if not field:
		return
	fields_masked = field.secret
	assert(fields_masked)
	dialog_frames += 1
	if dialog_frames == 1:
		if mode != "cancel_prompt":
			field.text = OS.get_environment("AURORA_TEST_PASSPHRASE")
		return
	if dialog_frames < 3:
		return
	dialog_answered = true
	if mode == "cancel_prompt":
		dialog.get_cancel_button().pressed.emit()
	else:
		var screenshot = OS.get_environment("AURORA_TEST_SCREENSHOT")
		if not screenshot.is_empty():
			dialog.get_texture().get_image().save_png(screenshot)
		dialog.get_ok_button().pressed.emit()

func _drive_cancel() -> void:
	if cancel_clicked or mode != "cancel_retry":
		return
	if not FileAccess.file_exists(OS.get_environment("AURORA_TEST_JOB_MARKER")):
		return
	var button := _find_progress_cancel(get_tree().root)
	if button and button.is_visible_in_tree():
		cancel_clicked = true
		button.pressed.emit()

func _find_progress_cancel(node: Node) -> Button:
	# ProgressDialog is a private, unregistered editor class. Its full-screen
	# CenterContainer has the maximum canvas Z index and owns the Cancel button.
	if node is CenterContainer and node.z_index == RenderingServer.CANVAS_ITEM_Z_MAX:
		return _find_button(node, "Cancel")
	for child in node.get_children(true):
		var found := _find_progress_cancel(child)
		if found:
			return found
	return null

func _find_button(node: Node, label: String) -> Button:
	if node is Button and node.text == label:
		return node
	for child in node.get_children(true):
		var found := _find_button(child, label)
		if found:
			return found
	return null

func _open_export(node: Node) -> bool:
	if node is PopupMenu:
		for index in node.item_count:
			if node.get_item_text(index) == "Export...":
				node.id_pressed.emit(node.get_item_id(index))
				return true
	for child in node.get_children(true):
		if _open_export(child):
			return true
	return false

func _result_text(node: Node) -> String:
	var output := ""
	if node is RichTextLabel:
		output += node.get_parsed_text()
	for child in node.get_children(true):
		output += _result_text(child)
	return output

func _process(_delta: float) -> void:
	if finished:
		return
	if busy:
		return
	if EditorInterface.get_resource_filesystem().is_scanning():
		return
	if not started:
		started = true
		var settings = EditorInterface.get_editor_settings()
		for item in [["export/auroraos/signing_key", "AURORA_TEST_SIGNING_KEY"], ["export/auroraos/signing_certificate", "AURORA_TEST_SIGNING_CERTIFICATE"]]:
			original[item[0]] = settings.get_setting(item[0])
			settings.set_setting(item[0], OS.get_environment(item[1]))
		export_dialog = _find(get_tree().root, "ProjectExportDialog")
		assert(export_dialog)
		# The Project menu also populates the preset list before opening it.
		assert(_open_export(get_tree().root))
		var list: ItemList = _find(export_dialog, "ItemList")
		list.select(0)
		list.item_selected.emit(0)
		preset = export_dialog.get_current_preset()
		assert(preset)
		preset.set("signing/enabled", true)
		if mode == "cancel_retry":
			preset.set("sdk/patchelf", OS.get_environment("AURORA_TEST_SLOW_PATCHELF"))
		return
	if retry_pending:
		retry_pending = false
		preset.set("sdk/patchelf", OS.get_environment("AURORA_TEST_REAL_PATCHELF"))
		mode = "accept"
		dialog_frames = 0
		dialog_answered = false
	busy = true
	var button := _find_button(export_dialog, "Export Project...")
	assert(button and not button.disabled)
	button.pressed.emit()
	var picker: EditorFileDialog
	for child in export_dialog.get_children(true):
		if child is EditorFileDialog and child.visible:
			picker = child
	assert(picker)
	picker.hide()
	# Use the actual export-dialog signal path; no test-only exporter API.
	picker.file_selected.emit(OS.get_environment("AURORA_TEST_RPM"))
	busy = false
	var text := _result_text(export_dialog)
	var signed_file := FileAccess.file_exists(OS.get_environment("AURORA_TEST_RPM")) and FileAccess.get_file_as_bytes(OS.get_environment("AURORA_TEST_RPM")).size() > 1024
	var previous_intact := FileAccess.get_file_as_bytes(OS.get_environment("AURORA_TEST_RPM")).get_string_from_utf8() == "previous-output-must-survive-failure" if not signed_file else false
	results.append({"mode": mode, "artifact": signed_file, "previous_intact": previous_intact, "masked": fields_masked, "answered": dialog_answered, "protected_key_failure": text.contains("Protected-key signing failed")})
	if mode == "cancel_retry" and cancel_clicked and not signed_file:
		retry_pending = true
		return
	finished = true
	_restore_settings()
	var matched := signed_file if mode not in ["cancel_prompt", "wrong"] else not signed_file
	if mode == "cancel_prompt":
		matched = matched and dialog_answered
	if mode == "wrong":
		matched = matched and text.contains("Protected-key signing failed")
	var file := FileAccess.open(OS.get_environment("AURORA_TEST_RESULT"), FileAccess.WRITE)
	file.store_string(JSON.stringify({"results": results, "matched": matched, "cancel_clicked": cancel_clicked}))
	file.close()
	get_tree().quit(0 if matched else 1)
