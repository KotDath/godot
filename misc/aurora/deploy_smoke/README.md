# SDK deployment integration fixture

Use only in a dedicated test project: this plugin really installs, launches and
stops the project's debug RPM. It drives the editor Remote Deploy menu, so the
same C++ path as the user interface is exercised.

1. Copy these plugin files into `addons/aurora_deploy_smoke/` of a disposable
   project with a main scene and a runnable Aurora export preset.
2. Configure the SDK target/template/patchelf, `signing/enabled=true` and
   `remote_deploy/enabled=true` in that preset.
3. Configure the SDK device and Regular signing paths in Editor Settings.
   Credentials and device identity are intentionally absent from this fixture.
4. Enable the plugin and start the editor with `GODOT_AURORA_DEPLOY_TEST=1`.
   Without this explicit environment variable the plugin does nothing.
5. Accept the SDK installation dialog on the device if it appears.
6. Observe `DEPLOY_TEST:run_menu_found`, `run_returned`, `stop_menu_found`,
   `stopped`, then check that the device log contains the game's own startup marker
   and that the launched PID is no longer running. Menu markers alone do not
   prove rendering or successful debugger connection.

The fixture waits 30 seconds between Run and Stop and then closes its editor.
Repeat with the same package to test reinstall/update. For failed export or
installation, the absence of a Stop entry is a failing result. For debug checks,
use a reachable PC debug address and inspect the debug session separately.

## Recorded result — 2026-10-23

The menu-driven test passed on KVADRA ARM64 with the dependency-chain project:
Regular validation/signing → sfdk installation/update → launch → debugger
connection → Stop. GLES rendered the cube and text; the native extension returned
42 and a touch was logged. Two deployments retained a test `user://` counter
(1 → 2). Device process termination was checked separately after Stop.
The SDK installation confirmation was accepted on the device; installation was
not silently forced. The certificate was the SDK public Regular development
certificate, without root-CA trust verification.
