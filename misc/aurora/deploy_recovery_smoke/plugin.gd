@tool
extends EditorPlugin

var mode := ""
var phase := 0
var busy := false
var elapsed := 0.0
var restored_stop := false
const JOURNAL := "res://.godot/editor/aurora-remote-session.json"

func _enter_tree() -> void:
	mode = OS.get_environment("GODOT_AURORA_RECOVERY_TEST")
	set_process(not mode.is_empty())
	if mode.is_empty():
		return
	var settings = EditorInterface.get_editor_settings()
	settings.set_setting("export/auroraos/device", "Fixture device")
	settings.set_setting("export/auroraos/signing_key", OS.get_environment("AURORA_TEST_SIGNING_KEY"))
	settings.set_setting("export/auroraos/signing_certificate", OS.get_environment("AURORA_TEST_SIGNING_CERTIFICATE"))

func _find_item(node: Node, label: String) -> Array:
	if node is PopupMenu:
		for index in node.item_count:
			if node.get_item_text(index) == label:
				return [node, node.get_item_id(index)]
	for child in node.get_children(true):
		var found := _find_item(child, label)
		if not found.is_empty():
			return found
	return []

func _journal() -> Dictionary:
	if not FileAccess.file_exists(JOURNAL):
		return {}
	return JSON.parse_string(FileAccess.get_file_as_string(JOURNAL))

func _finish(matched: bool, detail: String) -> void:
	phase = 99
	var file := FileAccess.open(OS.get_environment("AURORA_TEST_RESULT"), FileAccess.WRITE)
	file.store_string(JSON.stringify({"mode": mode, "matched": matched, "detail": detail, "restored_stop": restored_stop, "journal": _journal()}))
	file.close()
	get_tree().quit(0 if matched else 1)

func _process(delta: float) -> void:
	if mode.is_empty() or busy or phase == 99:
		return
	elapsed += delta
	if elapsed < 2.0 or EditorInterface.get_resource_filesystem().is_scanning():
		return
	if phase == 0:
		if mode == "journal_block":
			DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(JOURNAL))
		restored_stop = not _find_item(get_tree().root, "Stop Aurora project").is_empty()
		if mode == "restore_stop":
			if not restored_stop:
				_finish(false, "saved Stop entry missing before Run")
				return
			phase = 2
			return
		if mode == "recover_run" and not restored_stop:
			_finish(false, "saved Stop entry missing before recovery Run")
			return
		var item := _find_item(get_tree().root, "Run on Aurora SDK device")
		if item.is_empty():
			_finish(false, "Run entry missing")
			return
		busy = true
		item[0].id_pressed.emit(item[1])
		busy = false
		phase = 1
		elapsed = 0.0
		return
	if phase == 1:
		var saved := _journal()
		if mode == "gate":
			_finish(saved.is_empty(), "unsupported template preflight")
		elif mode == "journal_block":
			_finish(saved.is_empty(), "journal checkpoint could not be replaced")
		elif mode == "launch_loss":
			_finish(saved.get("phase") == "launching", "unconfirmed launch survived editor exit")
		elif mode == "success":
			_finish(saved.get("phase") == "running", "confirmed launch survived editor exit")
		elif mode == "recover_run":
			if saved.get("phase") != "running":
				_finish(false, "recovery did not adopt running session")
				return
			phase = 2
		elif mode == "install_loss":
			if saved.get("phase") != "installing" or not saved.get("install_unconfirmed", false):
				_finish(false, "installation uncertainty not recorded")
				return
			# Run must not blindly submit another installation while it is unknown.
			var item := _find_item(get_tree().root, "Run on Aurora SDK device")
			busy = true
			item[0].id_pressed.emit(item[1])
			busy = false
			phase = 2
		return
	if phase == 2:
		var item := _find_item(get_tree().root, "Stop Aurora project")
		if item.is_empty():
			_finish(false, "Stop entry missing")
			return
		busy = true
		item[0].id_pressed.emit(item[1])
		busy = false
		_finish(_journal().is_empty(), "Stop reconciled saved session")
