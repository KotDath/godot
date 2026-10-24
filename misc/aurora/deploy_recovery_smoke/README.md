# Desktop Run/Stop recovery acceptance

This opt-in fixture drives the **actual Remote Deploy menu** in a built Linux
editor. RPM packaging, Regular validation and public SDK development signing use
the real SDK. Device calls use a disposable fake SDK frontend and the actual
native session supervisor compiled from this repository into a small host test
driver. It installs nothing on hardware and does not establish rendering,
debugging, sandbox permissions, APM completion or ARM hardware behavior.

Prerequisites: host C++17 compiler, `rpm2cpio`/`cpio`, a Linux editor built from this
branch, working sfdk and its public `regular_key.pem` / `regular_cert.pem`, and a
prepared [native layout project](../native_layout_smoke/README.md) with exactly
one Aurora preset for the selected architecture (`--arch arm64` by default;
`--arch arm32` selects its ARM32 preset). Rebuild its debug template from this branch so
the runtime capability gate passes. All paths must be visible in the SDK shared
workspace. Signing credentials stay in the SDK; isolated XDG directories protect
the user's editor settings. No protected-key passphrase is used by this fixture.

```sh
python3 misc/aurora/deploy_recovery_smoke/run.py \
  --editor bin/godot.linuxbsd.editor.x86_64 \
  --sfdk /path/to/AuroraOS/bin/sfdk \
  --project /path/to/prepared-native-layout-project \
  --output /path/to/new-shared-results
```

The frontend itself is in a directory containing a space. It forwards `engine
exec` unchanged to sfdk, including stdin, and logs only operation names. Its
deploy implementation extracts the **final signed RPM**, independently compares
every actual payload file to the host receipt, and checks the receipt inside the
RPM agrees. Device checksum responses hash those extracted files; they do not
echo expected hashes from the controller. APM replies deliberately write a
warning to stderr to exercise stdout/stderr separation. Its
device implementation runs the actual generated shell protocol/native supervisor
against a disposable child HOME. It substitutes a controlled process for the game.
The fixture leaves no running native test job, including after an assertion fails.

Use `--cases` to select scenarios:

- `recover_run`: lose the reply **after** the native game process started, quit
  the editor, reopen it, verify Stop is already available, retry Run, then Stop.
  Exactly one installation and launch must occur; recovery queries the saved token.
- `restore_stop`: reopen a confirmed session and Stop it without another Run.
- `install_loss`: simulate an uncertain installation reply; reject a blind second
  Run, fence launch with Stop, and retain the signed RPM and archived operation.
- `gate`: remove the runtime capability marker from a copied ARM template. No
  SDK call may occur. The template is never executed by the fixture.
- `late_launch`: delay dispatch until after the second editor has already stopped
  and cleared the saved operation. The old request must return `stopped`; no game
  may start. The queued helper is observed to completion.
- `journal_block`: put a directory at the journal filename. Saving the checkpoint
  must fail before installation/launch; the completed temporary package is cleaned.
- `install_recover`: installation commits but its reply is lost. Reopen the
  editor and recover by matching APM registration and actual file hashes, then
  launch once; installation must not be repeated.
- `runtime_stale`: registration has the expected version but a nested private
  DSO still contains older bytes. Refuse launch and retain the RPM. After the
  fixture activates the correct files, another editor recovers the same attempt.
- `pending_upgrade`: registration matches but APM reports a pending upgrade.
  Do not hash/launch until the pending state clears; then recover without deploy.
- `api_unknown`: an unknown install result cannot be resolved when the APM read
  method is unavailable. Stop fences launch and retains its archived RPM.
- `no_api_confirmed`: an SDK-confirmed install can work without the optional APM
  read API, but must still match every active-file digest before launching.
- `missing_recover`: lose the launch connection before dispatch. Retry the
  missing session using its same token/arguments and verify one game, one install.
- `version_mismatch`: an older registered version must block before checksum
  queries even if active files already match. Matching registration then allows
  recovery without another deploy.
- `missing_stale`: after losing a launch request before dispatch, change an
  active DSO. The recheck must revoke the earlier successful verification in the
  journal; explicit Stop must archive the uncertainty and retain the signed RPM
  even though SDK installation itself was confirmed. No game may start.

Results/logs/operation counts are saved under the new output directory. Expected
failure messages are part of the scenarios. The install-loss case intentionally
retains its package as evidence. The runner never invokes an installation or Stop
against an actual configured device.

Recorded result on 2026-10-24: all 14 cases pass. The stale-verification regression
first fails with the previous editor (persisted `launching`, `runtime_verified=true`),
then passes with the fix: Stop archives the uncertain activation and retains its
signed RPM. The successful missing-launch retry is also rechecked after the fix.
Separately, the controller/APM
C++ tests pass 7 cases / 84 assertions; the complete Aurora set passes 15 cases /
195 assertions:

```sh
bin/godot.linuxbsd.editor.x86_64 --headless --test --test-case='*Aurora*'
```

An additional `--arch arm32 --cases install_recover` run passes with a real
ARM32 signed RPM. Extracted ELF metadata/ARM32 hard-float flags, exact native
project mappings, per-library RUNPATH closure and receipt hashes pass for both
architectures. These are packaging/fake-device results, not ARM execution.
The packager also checks the host-selected template SHA256 before and after
copying. A separate negative check in the real SDK rejects a stale template
before any package is created; this covers the shared-mount boundary beyond the
editor's native capability check. The Python regression set now has 39 cases.

Real ARM device verification remains required; software fixtures cannot establish
APM permissions or namespace behavior on that device. A separate read-only SDK
emulator check validates the APM API shape and ordinary-user file reads:
see [REMOTE_SESSION.md](../REMOTE_SESSION.md).
