extends Control

var extension: GDExtension
var probe: RefCounted

func _ready() -> void:
	extension = load("res://probe.gdextension")
	var result := -1
	if extension and ClassDB.class_exists("AuroraDependencyProbe"):
		probe = ClassDB.instantiate("AuroraDependencyProbe")
		result = probe.call("value")
	var evidence := {"value": result, "executable": OS.get_executable_path()}
	var file := FileAccess.open("user://native-layout.json", FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify(evidence))
		file.close()
	print("AURORA_NATIVE_LAYOUT ", JSON.stringify(evidence))
	var label := Label.new()
	label.position = Vector2(40, 80)
	label.add_theme_font_size_override("font_size", 40)
	label.text = "Nested GDExtension chain = %d" % result
	add_child(label)
	if result != 42:
		push_error("Nested native dependency chain failed")
		get_tree().quit(1)
