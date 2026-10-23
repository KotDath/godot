@tool
extends EditorPlugin

func _enter_tree():
	add_tool_menu_item("Run Aurora editor smoke", _check)
	if OS.get_environment("GODOT_AURORA_EDITOR_SMOKE") == "1":
		_check.call_deferred()

func _exit_tree():
	remove_tool_menu_item("Run Aurora editor smoke")

func _check():
	await get_tree().create_timer(3).timeout
	var root := get_tree().root
	var actual := DisplayServer.window_get_size()
	assert(root.size == actual, "Editor root differs from the compositor window")
	# Regression: an editor minimum-size update must not shrink a shell-owned window.
	root.size = Vector2i(1024, 600)
	await get_tree().process_frame
	assert(root.size == actual, "Rejected resize left a stale root viewport")
	assert(Vector2i(root.get_visible_rect().size) == actual)
	print("AURORA_EDITOR_GEOMETRY_OK ", actual)
	if ResourceLoader.exists("res://probe.gdextension"):
		var extension = load("res://probe.gdextension")
		assert(extension != null)
		assert(ClassDB.class_exists("AuroraDependencyProbe"))
		var probe = ClassDB.instantiate("AuroraDependencyProbe")
		assert(probe.value() == 42)
		print("AURORA_EDITOR_NATIVE_CHAIN_OK 42")
	var evidence := FileAccess.open("user://aurora_editor_smoke.txt", FileAccess.WRITE)
	assert(evidence != null, "Cannot persist editor smoke evidence")
	evidence.store_string("Geometry PASS: %s\nNative probe requested: %s\n" % [actual, ResourceLoader.exists("res://probe.gdextension")])
