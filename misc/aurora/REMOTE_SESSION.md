# Recoverable SDK launches: native backend and editor integration

The native backend is implemented and tested. **The desktop exporter's Run/Stop
actions still use their previous implementation**; persistent editor-side session
recovery and installation-outcome reconciliation are not integrated yet.

## Device ownership

An Aurora executable now handles two internal arguments before creating OS,
display or editor objects:

- `--aurora-remote-session-version` prints `GODOT_AURORA_REMOTE_SESSION_V1`.
- `--aurora-remote-session TOKEN [normal Godot arguments]` creates a detached
  supervisor, then launches the same executable with the normal arguments.

The supervisor is part of the Aurora executable, including export templates.
It requires neither Python nor a separate installed helper. Normal Linux's main
function and its behavior are unchanged. Native launch/session code is confined
to `platform/auroraos`.

Tokens are 32 lowercase hexadecimal characters, generated randomly by the
controller. Records live under `~/.cache/godot-aurora/sessions/TOKEN`, with private
directories/files. Each session stores the boot ID, PID and kernel start time of
both the supervisor and game, plus the final exit status. The app's log is
`~/.cache/godot-aurora/TOKEN.log`.

The **detached supervisor** atomically reserves the directory. Concurrent/retried
requests for the same token can therefore launch only once. It forks the game
behind a pipe gate, publishes the game identity atomically, checks Stop, and
only then lets the child exec. A lost SSH reply does not destroy its state.

`remote_session.sh` supplies developer SDK `launch`, `status` and `stop` operations.
It uses only the shell and ordinary Linux utilities. Stop creates a tombstone
even if the launch request has not arrived yet. Either the worker sees that
tombstone before launching, or its published identity is already observable.

The shell **never sends a signal to a recorded PID**. Only the native supervisor
signals its own child/group. It observes exit with `waitid(WNOWAIT)` and retains
the group leader until the last TERM/KILL signal, preventing PID/group reuse from
targeting an unrelated process. This also cleans a descendant that ignores TERM.
The supervisor survives transport SIGHUP and handles TERM/INT by cleaning its child.

Status messages have the ASCII form:

```text
GODOT_AURORA_SESSION:TOKEN:STATE:BOOT_ID:PID:START_TIME
```

States include `missing`, `preparing`, `running`, `stopping`, `finalizing`,
`stopped`, `exited`, `failed` and `interrupted`. `finalizing` means the leader has
exited but the supervisor is finishing group cleanup. `interrupted` preserves
uncertainty when the supervisor disappeared without recording completion;
it must not be interpreted as permission to signal a recycled PID.

## Pending desktop integration

Before submitting an operation, persist a versioned local record under
`EditorPaths::get_project_settings_dir()`, outside the saved export preset.
Record the token, SDK executable/device, application ID, expected package version
and operation phase. Write to a sibling temporary file and rename atomically.
Failure to save must stop before submitting the remote operation.

On startup, restore this record without contacting the device automatically.
Explicit Run/Stop queries the same token and reconciles its state. Restore a Stop
entry even if the original launch reply was lost. A `missing` response is not
enough to forget a request: it might still arrive later. Fence it with Stop
before deleting the local record or submitting a replacement launch.

Check template support for the native protocol **before deployment/launch**.
Do not probe an arbitrary older installed template with the new launch flag:
an older engine could ignore that flag and start the game. The new template's
capability marker/protocol needs an export-time check; ordinary manual exports
remain independent of a connected device.

Installation outcome is a separate state from game/process ownership. Retain
the expected package identity when SDK deploy returns an uncertain result, and
query the actual installed state before retrying/launching. The correct query
must be verified against Aurora's application package manager and its namespaces;
a global RPM database query alone has not been established as sufficient.
Stop fences game launch and terminates its owned runtime, but must not claim to
roll back an already submitted installation or delete user data.

## Evidence and reproduction — 2026-10-24

```sh
python3 -m unittest misc/aurora/test_remote_session.py -v
```

Eight acceptance cases compile the **actual native source** with a host C++17
compiler. They cover lost launch replies, concurrent duplicate requests, Stop
before launch, a TERM-ignoring descendant, foreign/stale PID records, supervisor
HUP/TERM, nonzero normal exit and capability/invalid-token handling. Test state
uses a disposable child HOME, without modifying the user's environment/settings.

ARM32 and ARM64 debug templates build. Both actual ARM binaries run the capability
probe and native fork → exec → exit workflow, and reject a fenced launch inside
their SDK targets (SDK emulation, **not physical hardware**):

```sh
python3 misc/aurora/remote_session_sdk_smoke.py \
  --sfdk /path/to/AuroraOS/bin/sfdk \
  --sdk-target AuroraOS-5.2.1.200-aarch64 \
  --binary bin/godot.auroraos.template_debug.arm64 \
  --output /path/to/new-shared-test-output
```

Repeat with the matching `armv7hl` target and ARM32 binary. The target names are
examples, not OS-version requirements. The runner records the exact binary
SHA256 and explicitly records `hardware=false`.

Still required: exporter journal/Run/Stop integration and its transport-failure
tests; new editor/release-template builds; real ARM64 SDK/sandbox acceptance;
installation reconciliation. Real ARM32 hardware remains excluded from the
current goal. KVADRA is currently unreachable through both of its connections.
