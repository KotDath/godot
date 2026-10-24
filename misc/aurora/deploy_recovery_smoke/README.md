# Desktop Run/Stop recovery acceptance

This opt-in fixture drives the **actual Remote Deploy menu** in a built Linux
editor. RPM packaging, Regular validation and public SDK development signing use
the real SDK. Device calls use a disposable fake SDK frontend and the actual
native session supervisor compiled from this repository into a small host test
driver. It installs nothing on hardware and does not establish rendering,
debugging, sandbox permissions, APM completion or ARM hardware behavior.

Prerequisites: host C++17 compiler, a Linux editor with tests built from this
branch, working sfdk and its public `regular_key.pem` / `regular_cert.pem`, and a
prepared [native layout project](../native_layout_smoke/README.md) with its first
ARM64 Aurora preset configured. Rebuild its debug template from this branch so
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

Results/logs/operation counts are saved under the new output directory. Expected
failure messages are part of the scenarios. The install-loss case intentionally
retains its package as evidence. The runner never invokes an installation or Stop
against an actual configured device.

Recorded result on 2026-10-24: all six cases pass. Separately, the 3-case /
37-assertion C++ controller tests pass:

```sh
bin/godot.linuxbsd.editor.x86_64 --headless --test --test-case='*AuroraDeploy*'
```

Real device verification and installation outcome reconciliation remain required;
see [REMOTE_SESSION.md](../REMOTE_SESSION.md).
