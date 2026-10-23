extends Control

var multiline := CodeEdit.new()
var single := LineEdit.new()
var status := Label.new()
var evidence: FileAccess
var native_evidence: FileAccess
var last_geometry := Vector3i(-1, -1, -1)
var last_preedit := ""
var last_preedit_selection := Vector2i(-1, -1)

func _process(_delta: float) -> void:
	if not DisplayServer.has_feature(DisplayServer.FEATURE_IME):
		return
	var window_size := DisplayServer.window_get_size()
	var height := DisplayServer.virtual_keyboard_get_height() if DisplayServer.has_feature(DisplayServer.FEATURE_VIRTUAL_KEYBOARD) else 0
	var geometry := Vector3i(window_size.x, window_size.y, height)
	if geometry != last_geometry:
		last_geometry = geometry
		print("AURORA_IME_GEOMETRY ", JSON.stringify({"width": window_size.x, "height": window_size.y, "keyboard_height": height}))
		_record_native("geometry", {"width": window_size.x, "height": window_size.y, "keyboard_height": height})
	var preedit := DisplayServer.ime_get_text()
	var selection := DisplayServer.ime_get_selection()
	if preedit != last_preedit or selection != last_preedit_selection:
		last_preedit = preedit
		last_preedit_selection = selection
		print("AURORA_IME_PREEDIT ", JSON.stringify({"text": preedit, "cursor": selection.x, "selection_length": selection.y}))
		_record_native("preedit", {"text": preedit, "cursor": selection.x, "selection_length": selection.y})

func _record_native(kind: String, data: Dictionary) -> void:
	if native_evidence:
		native_evidence.store_line(JSON.stringify({"kind": kind, "data": data}))
		native_evidence.flush()

func _ready() -> void:
	evidence = FileAccess.open("user://ime-results.jsonl", FileAccess.WRITE)
	native_evidence = FileAccess.open("user://ime-native.jsonl", FileAccess.WRITE)
	var title := Label.new()
	title.text = "Aurora IME: Unicode, multiline, selection, undo"
	title.position = Vector2(30, 15)
	title.add_theme_font_size_override("font_size", 32)
	add_child(title)
	multiline.position = Vector2(30, 80)
	multiline.size = Vector2(1850, 290)
	multiline.add_theme_font_size_override("font_size", 30)
	add_child(multiline)
	single.position = Vector2(30, 390)
	single.size = Vector2(1850, 65)
	single.add_theme_font_size_override("font_size", 30)
	add_child(single)
	var actions := ["Reset", "Select multiline", "Undo multiline", "Select single", "Undo single", "Read only", "Password", "Move caret"]
	for index in actions.size():
		var button := Button.new()
		button.text = actions[index]
		button.position = Vector2(30 + (index % 6) * 300, 480 + floori(index / 6.0) * 80)
		button.size = Vector2(285, 65)
		button.focus_mode = Control.FOCUS_NONE
		button.pressed.connect(_action.bind(index))
		add_child(button)
	status.position = Vector2(650, 575)
	status.size = Vector2(1850, 60)
	add_child(status)
	multiline.text_changed.connect(_record.bind("multiline"))
	single.text_changed.connect(func(_text: String): _record("single"))
	_action(0)

func _action(index: int) -> void:
	match index:
		0:
			multiline.editable = true
			single.editable = true
			single.secret = false
			multiline.text = ""
			single.text = ""
			multiline.clear_undo_history()
		1:
			multiline.grab_focus()
			multiline.select_all()
		2:
			multiline.undo()
		3:
			single.grab_focus()
			single.edit()
			single.select_all()
		4:
			single.menu_option(LineEdit.MENU_UNDO)
		5:
			multiline.editable = false
			single.editable = false
		6:
			single.secret = not single.secret
		7:
			single.caret_column = 1
	_record("action_%d" % index)

func _record(event: String) -> void:
	var data := {"event": event, "multiline": multiline.text, "single": single.text, "secret": single.secret}
	var line := JSON.stringify(data)
	status.text = line
	print("AURORA_IME ", line)
	_record_native("state", data)
	if evidence:
		evidence.store_line(line)
		evidence.flush()
