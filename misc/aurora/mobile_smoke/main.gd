extends Control

var status: Label
var last_command := ""
var touch_count := 0
var names := ["Landscape", "Portrait", "Reverse landscape", "Reverse portrait", "Sensor landscape", "Sensor portrait", "Sensor"]

func _ready():
	RenderingServer.set_default_clear_color(Color("142936"))
	var column := VBoxContainer.new()
	column.position = Vector2(60, 180)
	column.add_theme_constant_override("separation", 20)
	add_child(column)
	status = Label.new()
	status.add_theme_font_size_override("font_size", 32)
	column.add_child(status)
	for mode in range(names.size()):
		var button := Button.new()
		button.text = names[mode]
		button.custom_minimum_size = Vector2(480, 75)
		button.pressed.connect(func(): set_orientation(mode))
		column.add_child(button)
	var edit := LineEdit.new()
	edit.placeholder_text = "Keyboard / IME test"
	edit.custom_minimum_size = Vector2(480, 80)
	edit.text_changed.connect(func(text): print("MOBILE_TEXT:", text))
	column.add_child(edit)
	for corner in range(4):
		var button := Button.new()
		button.text = "Corner " + str(corner)
		button.set_anchors_and_offsets_preset([PRESET_TOP_LEFT, PRESET_TOP_RIGHT, PRESET_BOTTOM_LEFT, PRESET_BOTTOM_RIGHT][corner])
		button.grow_horizontal = GROW_DIRECTION_BEGIN if corner % 2 else GROW_DIRECTION_END
		button.grow_vertical = GROW_DIRECTION_BEGIN if corner >= 2 else GROW_DIRECTION_END
		button.custom_minimum_size = Vector2(180, 100)
		button.pressed.connect(func():
			touch_count += 1
			print("MOBILE_CORNER:", corner, ":", touch_count)
		)
		add_child(button)
	get_viewport().size_changed.connect(func(): print("MOBILE_SIZE:", get_viewport_rect().size))
	print("MOBILE_READY:", OS.get_user_data_dir())

func set_orientation(mode: int):
	DisplayServer.screen_set_orientation(mode)
	print("MOBILE_ORIENTATION:", mode, ":", DisplayServer.screen_get_orientation())

func _process(_delta):
	status.text = "%s — %s\nCorner hits: %d" % [names[DisplayServer.screen_get_orientation()], str(get_viewport_rect().size), touch_count]
	# Device test control channel, kept in this test project only.
	if FileAccess.file_exists("user://orientation.txt"):
		var command := FileAccess.get_file_as_string("user://orientation.txt").strip_edges()
		if command != last_command and command.is_valid_int():
			last_command = command
			var mode := command.to_int()
			if mode >= 0 and mode < names.size():
				set_orientation(mode)

func _input(event):
	if event is InputEventScreenTouch:
		print("MOBILE_TOUCH:", event.index, ":", event.position, ":", event.pressed, ":cancel=", event.canceled)

func _notification(what):
	if what in [NOTIFICATION_APPLICATION_FOCUS_IN, NOTIFICATION_APPLICATION_FOCUS_OUT, NOTIFICATION_APPLICATION_PAUSED, NOTIFICATION_APPLICATION_RESUMED]:
		print("MOBILE_LIFECYCLE:", what)
