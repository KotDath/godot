# Recoverable SDK launches: native backend and editor integration

The native backend, persistent Run/Stop controller and installation-outcome
reconciliation are implemented. **Physical ARM64 acceptance of the new path
remains pending.** Software, SDK/emulator and physical results are distinct below.

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
phase and installation uncertainty. Version 2 also stores the exported-runtime
receipt and supported debug arguments, without a remote-filesystem password.
Version 1 remains readable for stopping older sessions. Writes use a sibling temporary file and
atomic rename. Failure to save stops before submitting the next remote operation.

Startup restores this record without contacting the device automatically.
Run/Stop addresses the same saved device/token. A restored Stop entry is available
before another Run, including after a lost launch reply. Retrying a `launching`
operation queries its status and adopts an already running game without another
installation/launch. A `missing` launch can be retried with the **same** token and
saved arguments after runtime verification; the native backend accepts it once.
A missing response cannot authorize forgetting a request: it might arrive later.
Stop fences it before deleting the local record or submitting a replacement
operation. Unconfirmed cleanup keeps the record available.

Before contacting the device, Run checks the selected template's native protocol
marker inside a matching ARM ELF `PT_LOAD` segment. A marker in appended PCK data
cannot authorize the protocol. Older/incorrect templates are rejected before an
SDK operation; ordinary RPM exports still accept them. No new launch flag is sent
to an arbitrary old installed engine to probe its behavior.

## Installation registration and active runtime

Installation outcome is separate from process ownership. If SDK deploy does not
confirm success, the signed RPM and staging directory are retained and no game
launch is submitted. Another Run **queries that attempt**, without replaying
installation. It calls `ru.omp.APM.GetPackage` through system-bus `gdbus`, using
`sfdk device exec` as the ordinary SDK user. The returned `general.id` and
`general.version` must match the exported ID and exact `version-release`, including
the Run-only unique development suffix. A strict parser consumes the complete
GVariant `(a{ss},)` result and rejects duplicate keys, malformed/trailing data and
missing fields. SDK stderr is captured separately from machine-readable stdout.

If available, `CheckPackageHasPendingUpgrade` must return false. A registered
version alone cannot establish which bytes the application will run: Aurora can
retain an older active runtime until another instance closes. Consequently each
PCK export contains `runtime-receipt.json`, computed after RPATH changes, stripping
and PCK embedding. It records SHA256 for the complete executable/PCK,
`native-manifest.json` and every packaged private DSO, preserving nested paths.
SDK recovery retains a matching receipt outside the RPM. RPM post-processing is
disabled for embedded-PCK ELF files so it cannot invalidate those identities.

Before launch, ordinary-user `sha256sum` must match **every exact path** in the
receipt. Queries are batched with bounded argument sizes. Missing, duplicate,
unknown or mismatching checksum entries refuse launch. The package stays retained
for a later Run after activation; an older private `.so` cannot be
mistaken for the current deployment. APM registration and active-file identities
are both required to resolve an unknown SDK installation result. This is a
snapshot check, not a lock against another client installing concurrently.
Before rechecking a missing launch, the controller durably revokes its earlier
successful snapshot. A failed/cancelled recheck therefore retains the RPM on
Stop, rather than cleaning it based on a stale `runtime_verified` flag. The
session token is unchanged, so Stop still fences a delayed original request.

Capability/errors select the backend; no OS-version check enables it. If the APM
read API is absent or denied, **only an SDK-confirmed installation** may proceed,
and exact active-file verification is still required. An unknown result stays
unresolved. Missing only the pending-upgrade method also leaves the full-file
check mandatory. Neither a global RPM query nor the device's receipt alone is
treated as proof. The exporter has no audb dependency.

Explicit Stop fences launch and archives an unverified installation/activation as
`aurora-install-uncertain-TOKEN.json`, retaining its package. It does not roll back
an installation or delete user data. Confirmed launch recovery/Stop cleans only
the named RPM and token owner marker inside its marked project staging directory;
unknown files and directories are left intact.

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

The complete Aurora C++ regression passes 15 cases / 195 assertions. This
includes 3 controller cases / 37 assertions plus 4 APM/receipt/journal cases /
47 assertions. It checks ownership/status parsing, ARM ELF capability boundaries,
GVariant parsing, exact checksum sets, receipt path confinement, supported
non-secret launch arguments and JSON compatibility for numeric journal versions
1/2. The restart integration caught strict Variant equality rejecting JSON's
floating `schema=1.0`; valid numeric versions now survive a round-trip.

Read-only checks on the installed **x86_64 SDK emulator**, as its ordinary SDK
user, confirm the APM object advertises `GetPackage(s) → a{ss}` and
`CheckPackageHasPendingUpgrade(s) → b`. An existing installed application returns
the exact ID/version fields above, `(false,)` for pending upgrade, and a stable
SHA256 of its `/usr/bin/APP_ID`. No application was installed, launched, stopped
or changed by this contract check. Example reproduction with an installed app:

```sh
sfdk device exec 'SDK device name' -- gdbus introspect --system \
  --dest ru.omp.APM --object-path /ru/omp/APM
sfdk device exec 'SDK device name' -- gdbus call --system \
  --dest ru.omp.APM --object-path /ru/omp/APM \
  --method ru.omp.APM.GetPackage org.example.game
sfdk device exec 'SDK device name' -- gdbus call --system \
  --dest ru.omp.APM --object-path /ru/omp/APM \
  --method ru.omp.APM.CheckPackageHasPendingUpgrade org.example.game
sfdk device exec 'SDK device name' -- sha256sum -- /usr/bin/org.example.game
```

This verifies the observed API/ordinary-user read contract. It does not establish
installation recovery, app-sandbox namespaces or ARM gameplay on physical hardware.

Fourteen menu-driven scenarios pass with the actual Linux editor and real SDK
packaging/validation/development signing, using a **fake device** backed by the
actual native supervisor: lost launch reply/recovery without duplication, restored
Stop before Run, uncertain install/RPM retention, unsupported-template preflight,
late-request fencing, a blocked journal write, committed-install recovery, older
private DSO/pending upgrade/version mismatch refusing launch, unsupported API
handling and same-token retry before launch dispatch. Final signed RPM payload
bytes are independently checked against the exported receipt before the fake
device uses them for actual checksum responses. All cases pass without another
installation or a duplicate game during recovery.
The added stale-recheck/Stop regression fails with the previous editor and passes
after durable snapshot revocation. ARM32 committed-install recovery also passes;
both architectures' extracted ELF/manifest/RUNPATH closure and receipt bytes match.
See [deploy_recovery_smoke](deploy_recovery_smoke/README.md).

Still required: real ARM64 SDK/sandbox acceptance of the complete recovery path.
Real ARM32 hardware remains excluded from the current goal. KVADRA is currently
unreachable through both of its connections. Final artifact provenance/matrix
validation is tracked separately in point 13.
