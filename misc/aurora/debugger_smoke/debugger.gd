@tool
extends EditorDebuggerPlugin

func _setup_session(index):
	var session := get_session(index)
	var state := {"breaks": 0}
	# A callback owned by the session must not retain that same session.
	var session_ref := weakref(session)
	session.started.connect(func():
		state.breaks = 0
		print("AURORA_DEBUG:connected")
		session_ref.get_ref().toggle_profiler("servers", true)
	)
	session.breaked.connect(func(can_debug):
		state.breaks += 1
		print("AURORA_DEBUG:break:", state.breaks, ":can_debug=", can_debug)
		if can_debug:
			# This fixture deliberately exercises the main-thread debugger only.
			session_ref.get_ref().send_message("next" if state.breaks == 1 else "continue")
	)
	session.continued.connect(func(): print("AURORA_DEBUG:continued"))
	session.stopped.connect(func(): print("AURORA_DEBUG:stopped:breaks=", state.breaks))
