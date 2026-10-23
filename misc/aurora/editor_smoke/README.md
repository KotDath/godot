# Aurora device editor smoke

Use an installed ARM64 editor RPM on the device. Package the editor with
`--permissions 'Audio;UserDirs;Internet'`: the sandbox needs `Internet` even for
the loopback debugger and GDScript language-server sockets. Game exports retain
the permissions explicitly configured in their preset. Keep projects in writable user
storage; a project symlinked into `/usr/share` does not test saving. Use a fresh
test project and GLES Compatibility. Installed editors default to
`~/.local/share/<application-id>/projects`; settings and caches also live within
the corresponding application ID directories. Existing development projects
remain at their original paths and can be imported explicitly. ARM32 hardware coverage is separate.

1. Create the project through Project Manager, including the initial icon import.
2. Create a 2D scene, save it as `main.tscn`, and attach an empty `main.gd`.
3. Enter the following using the editor (Enter between lines):

   ```gdscript
   extends Node2D
   func _ready(): print("AURORA_EDITOR_RUN_OK Привет")
   func _draw(): draw_circle(Vector2(400, 300), 120, Color.CORNFLOWER_BLUE)
   ```

4. Run the current scene (F6). Verify the log marker, a separate game process,
   the editor's loopback debugger connection, and the blue circle. If the shell
   does not foreground the child, select its card in the app switcher.
5. Return to the editor and Stop (F8). Verify the child process has exited.
6. Reopen the project and check the saved scene and script, then run again.

For an additional regression check, copy `addons/aurora_editor_smoke` into the
project, enable the plugin, and launch the editor with
`GODOT_AURORA_EDITOR_SMOKE=1`. It checks that the root viewport agrees with the
compositor even after a rejected client resize. Alternatively invoke Project → Tools → Run Aurora editor smoke. It makes no
changes until explicitly invoked or enabled by the environment flag.
An optional `res://probe.gdextension` can reference the installed
`aurora_dependency_chain` example from godot-cpp; the plugin then calls its native
method in editor/tool context and expects 42. A successful check writes `user://aurora_editor_smoke.txt`,
which also checks persistent user-data access. Keep the private SO chain in the
RPM, with `$ORIGIN` RUNPATH for each DSO. A descriptor may reference
`/usr/share/<application-id>/lib/libaurora_probe.so`; no executable library copy
into a noexec project directory is required.

Track developer launches separately from normal application-launcher/sandbox
runs. This procedure does not establish native compilation, RPM packaging,
hot reload, or full IME support on the device. Multiline IME commits and atomic undo are covered separately by
`misc/aurora/ime_smoke`; composing input and keyboard layout need their own checks.
