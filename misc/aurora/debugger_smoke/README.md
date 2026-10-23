# Native Godot remote debugger smoke test

The runtime debugger uses Godot's own TCP protocol. Neither the engine nor the
export platform depends on audb. Installation/process launch may use SDK tools or
another development workflow. Debug templates are required; C++ debugging with
GDB is a separate, untested path.

## Manual use

Open the matching project in the desktop editor and enable its remote debug
server. The device must reach the PC address and TCP port. A command-line editor
can be started with (replace PC_IP and project path):

```sh
godot --headless --editor --path /path/to/project --debug-server tcp://PC_IP:6007
```

Launch the installed debug binary on the device with:

```sh
/usr/bin/APP_ID --remote-debug tcp://PC_IP:6007
```

An optional startup breakpoint is `--breakpoints res://main.gd:47` (line numbers
are one-based; select a real executable line in the matching script).
For a configured Aurora SDK device, `sfdk device exec DEVICE -- COMMAND ARGS...`
is an available transport; it is independent of the debugger protocol.
Do not put fixed PC addresses into production RPMs. The desktop Aurora exporter also provides SDK Remote Deploy (see ../EXPORT.md).
Standalone export flags requesting launch remain rejected; the Run path exports
an embedded project and passes debug flags separately to the device process.

## Automated fixture

These editor plugin files are **only for a dedicated test project**: the plugin
automatically steps over the first breakpoint and continues at the next stop.
Do not enable it in a normal development project.

1. Make a temporary copy of `misc/aurora/occlusion_smoke` for the host editor.
2. Copy plugin.cfg, plugin.gd and debugger.gd from this directory into that copy's
   `addons/aurora_debug_smoke/` directory.
3. Enable `res://addons/aurora_debug_smoke/plugin.cfg` in the copied project's
   `[editor_plugins] enabled=PackedStringArray(...)` setting.
4. Start the host editor debug server on the PC interface reachable by the device.
5. Launch the installed occlusion debug RPM with the matching address and the
   breakpoint on the `var passed := ...` line of main.gd (47 at this revision).
6. Bring its existing window to the foreground so rendered-frame waits can finish.
7. Check the host markers below and the device's successful occlusion result.
8. Stop the device process and host debugger after testing.

Expected host markers:

```text
AURORA_DEBUG:connected
AURORA_DEBUG:break:1:can_debug=true
AURORA_DEBUG:continued
AURORA_DEBUG:break:2:can_debug=true
AURORA_DEBUG:continued
```

The plugin requests the servers profiler, but this alone is not a verification
of profiler contents. Stack-variable UI, Remote Scene Tree, live editing and
native GDB sessions still require separate acceptance checks.

## Recorded result — 2026-10-23

KVADRA 192.168.2.44 connected to desktop 192.168.2.5:6007 using the installed
`ru.kotdath.godotocclusion-4.7.2-1` debug binary. Both the initial run and a restarted
process connected, stopped at the breakpoint, stepped over and continued.
Both finished `AURORA_OCCLUSION:PASS:off=102:on=2:restored=102`.

This was a developer-shell launch through audb, not a test of the desktop
application launcher security context. The launcher showed its first-use
permission dialog during window selection; it was canceled, and the already
running development window was selected instead. No new runtime permissions were
granted. Normal launcher/sandbox deployment is still part of item 2 of the roadmap.
No firewall rule or system graphics configuration was changed. The temporary
host listener and debug process were stopped after testing.
