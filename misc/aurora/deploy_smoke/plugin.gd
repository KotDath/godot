@tool
extends EditorPlugin

func _enter_tree():
	if OS.get_environment("GODOT_AURORA_DEPLOY_TEST") != "1":
		return
	run_test.call_deferred()

func find_item(node: Node, label: String) -> Array:
	if node is MenuButton:
		var popup = node.get_popup()
		for i in range(popup.item_count):
			if popup.get_item_text(i) == label:
				return [popup, popup.get_item_id(i)]
	for child in node.get_children():
		var found = find_item(child, label)
		if not found.is_empty():
			return found
	return []

func run_test():
	await get_tree().create_timer(8).timeout
	var item = find_item(get_tree().root, "Run on Aurora SDK device")
	if item.is_empty():
		push_error("DEPLOY_TEST:run_menu_missing")
		get_tree().quit(1)
		return
	print("DEPLOY_TEST:run_menu_found")
	item[0].id_pressed.emit(item[1])
	print("DEPLOY_TEST:run_returned")
	await get_tree().create_timer(30).timeout
	item = find_item(get_tree().root, "Stop Aurora project")
	if item.is_empty():
		push_error("DEPLOY_TEST:stop_menu_missing")
		get_tree().quit(2)
		return
	print("DEPLOY_TEST:stop_menu_found")
	item[0].id_pressed.emit(item[1])
	await get_tree().create_timer(2).timeout
	if not find_item(get_tree().root, "Stop Aurora project").is_empty():
		push_error("DEPLOY_TEST:stop_failed")
		get_tree().quit(3)
		return
	print("DEPLOY_TEST:stopped")
	get_tree().quit()
