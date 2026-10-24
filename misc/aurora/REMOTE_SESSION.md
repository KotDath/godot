# Recoverable SDK launches: native backend and editor integration

The native backend and the desktop exporter's persistent Run/Stop controller
are implemented and tested. **Installation-outcome reconciliation through APM
and physical ARM64 acceptance remain pending.**

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

## Desktop controller

Before submitting installation/launch/Stop, the exporter persists
`aurora-remote-session.json` under `EditorPaths::get_project_settings_dir()`
(normally `.godot/editor`), outside the saved export preset. It records the token,
SDK executable/device, application ID, RPM filename, staging path, operation
phase and installation uncertainty. Writes use a sibling temporary file and
atomic rename. Failure to save stops before submitting the next remote operation.

Startup restores this record without contacting the device automatically.
Run/Stop addresses the same saved device/token. A restored Stop entry is available
before another Run, including after a lost launch reply. Retrying a `launching`
operation queries its status and adopts an already running game without another
installation/launch. A `missing` response is not enough to forget a request:
it might still arrive later. Stop fences it before deleting the local record or
submitting a replacement launch. Unconfirmed cleanup keeps the record available.

Before contacting the device, Run checks the selected template's native protocol
marker inside a matching ARM ELF `PT_LOAD` segment. A marker in appended PCK data
cannot authorize the protocol. Older/incorrect templates are rejected before an
SDK operation; ordinary RPM exports still accept them. No new launch flag is sent
to an arbitrary old installed engine to probe its behavior.

Installation outcome is a separate state from game/process ownership. If SDK
deploy does not confirm success, the signed RPM and staging directory are retained
and no game launch is submitted. Run refuses a blind second installation while
this record is unresolved. Explicit Stop fences the launch and archives the
operation as `aurora-install-uncertain-TOKEN.json`; its package is retained.
This does **not** establish whether the installation eventually succeeded.
Querying the actual installed state is still pending. The correct query
must be verified against Aurora's application package manager and its namespaces;
a global RPM database query alone has not been established as sufficient.
Stop fences game launch and terminates its owned runtime, but must not claim to
roll back an already submitted installation or delete user data.

The documented [Market API](https://developer.auroraos.ru/doc/5.1.7/software_development/reference/data_and_contacts/market_api/market)
offers installed packages and active/completed operations through `GetAllProperties`.
Its caller identity/permissions and applicability to SDK-originated installation
need checking on the device; it is not yet used by this exporter. Presence of an
OS-version number alone will not enable a backend.

## Evidence and reproduction — 2026-10-24

```sh
python3 -m unittest misc/aurora/test_remote_session.py -v
```

Eight acceptance cases compile the **actual native source** with a host C++17
compiler. They cover lost launch replies, concurrent duplicate requests, Stop
before launch, a TERM-ignoring descendant, foreign/stale PID records, supervisor
HUP/TERM, nonzero normal exit and capability/invalid-token handling. Test state
uses a disposable child HOME, without modifying the user's environment/settings.

ARM32/ARM64 editor and debug/release template builds pass. Actual ARM debug and
release binaries run the capability probe and native fork → exec → exit workflow,
and reject a fenced launch inside their SDK targets (SDK emulation,
**not physical hardware**):

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

The desktop controller's 3 C++ cases / 37 assertions verify ownership/status
parsing, ARM ELF capability boundaries and JSON round-trip compatibility.
The restart integration caught strict Variant equality rejecting JSON's floating
`schema=1.0`; the codec now accepts numeric version 1 and rejects other types/versions.

Six menu-driven scenarios pass with the actual Linux editor and real SDK
packaging/validation/development signing, using a **fake device** backed by the
actual native supervisor: lost launch reply/recovery without duplication, restored
Stop before Run, uncertain install/RPM retention, unsupported-template preflight,
late-request fencing and a blocked journal write preventing deployment.
See [deploy_recovery_smoke](deploy_recovery_smoke/README.md).

Still required: real ARM64 SDK/sandbox acceptance and installation reconciliation.
Real ARM32 hardware remains excluded from the current goal. KVADRA is currently
unreachable through both of its connections. Final artifact provenance/matrix
validation is tracked separately in point 13.
