@tool
extends EditorPlugin

var debugger: EditorDebuggerPlugin

func _enter_tree():
	debugger = preload("debugger.gd").new()
	add_debugger_plugin(debugger)
	print("AURORA_DEBUG:plugin_ready")

func _exit_tree():
	remove_debugger_plugin(debugger)
	debugger = null
