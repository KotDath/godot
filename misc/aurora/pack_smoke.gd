# Development fixture only: pack the unimported, text-only smoke project.
# Production exports must use EditorExportPlatform and its dependency scanner.
extends SceneTree

func _init():
	var args := OS.get_cmdline_user_args()
	if args.size() != 2:
		push_error("Expected -- <project-directory> <output.pck>")
		quit(1)
		return
	var packer := PCKPacker.new()
	var error := packer.pck_start(args[1])
	if error != OK:
		quit(error)
		return
	for name in ["project.godot", "probe.gd", "probe.gdextension", "main.gd", "main.tscn"]:
		error = packer.add_file("res://" + name, args[0].path_join(name))
		if error != OK:
			quit(error)
			return
	error = packer.flush()
	print("AURORA_PACK_RESULT:", error)
	quit(error)
