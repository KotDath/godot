extends Node3D

func box(at: Vector3, size: Vector3, color: Color):
	var instance := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	instance.mesh = mesh
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = color
	instance.material_override = material
	instance.position = at
	add_child(instance)

func settle():
	# Wait for actual rendered frames, not merely process ticks while minimized.
	for frame in range(60):
		await RenderingServer.frame_post_draw

func objects_drawn() -> int:
	return get_viewport().get_render_info(Viewport.RENDER_INFO_TYPE_VISIBLE, Viewport.RENDER_INFO_OBJECTS_IN_FRAME)

func _ready():
	var camera := Camera3D.new()
	camera.position = Vector3(0, 0, 8)
	add_child(camera)
	camera.make_current()
	for x in range(10):
		for y in range(10):
			box(Vector3((x - 4.5) * 0.45, (y - 4.5) * 0.45, -3), Vector3.ONE * 0.3, Color.RED)
	box(Vector3.ZERO, Vector3(8, 8, 0.2), Color("405060"))
	box(Vector3(5, 0, 1), Vector3.ONE, Color.GREEN)
	var occluder := OccluderInstance3D.new()
	var shape := BoxOccluder3D.new()
	shape.size = Vector3(8, 8, 0.2)
	occluder.occluder = shape
	add_child(occluder)
	get_viewport().use_occlusion_culling = false
	await settle()
	var disabled := objects_drawn()
	get_viewport().use_occlusion_culling = true
	await settle()
	var enabled := objects_drawn()
	occluder.hide()
	await settle()
	var restored := objects_drawn()
	var passed := disabled >= 100 and enabled >= 2 and enabled <= disabled - 90 and restored >= 100
	print("AURORA_OCCLUSION:", "PASS" if passed else "FAIL", ":off=", disabled, ":on=", enabled, ":restored=", restored)
	if OS.get_cmdline_user_args().has("--quit"):
		get_tree().quit(0 if passed else 1)
