extends Control

var status: Label
var last_command := ""
var touch_count := 0
var audio: AudioStreamPlayer
var audio_command := ""
var frame_gap_usec := 0
var last_frame_usec := 0
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
	audio = AudioStreamPlayer.new()
	audio.volume_db = -18.0
	add_child(audio)
	audio.finished.connect(func(): print("MOBILE_AUDIO:finished"))
	var tone_button := Button.new()
	tone_button.text = "Play stereo tone (4 seconds)"
	tone_button.position = Vector2(650, 180)
	tone_button.custom_minimum_size = Vector2(480, 80)
	tone_button.pressed.connect(play_tone)
	add_child(tone_button)
	get_viewport().size_changed.connect(func(): print("MOBILE_SIZE:", get_viewport_rect().size))
	print("MOBILE_READY:", OS.get_user_data_dir())

func set_orientation(mode: int):
	DisplayServer.screen_set_orientation(mode)
	print("MOBILE_ORIENTATION:", mode, ":", DisplayServer.screen_get_orientation())

func play_tone():
	# Two seconds left, then two seconds right, with a short fade at each edge.
	var rate := 44100
	var frames := rate * 4
	var pcm := PackedByteArray()
	pcm.resize(frames * 4)
	for frame in range(frames):
		var local_frame := frame % (rate * 2)
		var envelope := minf(1.0, minf(float(local_frame), float(rate * 2 - 1 - local_frame)) / (rate * 0.02))
		var sample := int(sin(TAU * 440.0 * frame / rate) * 32767.0 * envelope)
		pcm.encode_s16(frame * 4 + (0 if frame < rate * 2 else 2), sample)
	var wave := AudioStreamWAV.new()
	wave.format = AudioStreamWAV.FORMAT_16_BITS
	wave.mix_rate = rate
	wave.stereo = true
	wave.data = pcm
	audio.stream = wave
	audio.play()
	print("MOBILE_AUDIO:started:driver=", AudioServer.get_driver_name(), ":device=", AudioServer.output_device)

func _process(_delta):
	var now := Time.get_ticks_usec()
	if last_frame_usec:
		frame_gap_usec = maxi(frame_gap_usec, now - last_frame_usec)
	last_frame_usec = now
	if FileAccess.file_exists("user://audio.txt"):
		var command := FileAccess.get_file_as_string("user://audio.txt").strip_edges()
		if command != audio_command:
			audio_command = command
			if command.begins_with("play"):
				play_tone()
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
		print("MOBILE_LIFECYCLE:", what, ":ticks=", Time.get_ticks_msec(), ":max_frame_gap_usec=", frame_gap_usec)
