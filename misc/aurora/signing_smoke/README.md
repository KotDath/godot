# Protected signing and SDK cancellation acceptance

These opt-in tests use a disposable project and the public **Regular development
credentials shipped with the SDK**. They do not install anything on a device.
The existing SDK key/certificate are read only. An encrypted copy is created
temporarily beside them, outside the project, with mode 0600 and a random
in-memory passphrase. The runner removes that copy even on test failure.

Prerequisites: a desktop Linux editor built from this branch, working `sfdk`,
the SDK's public `package-signing/regular_{key,cert}.pem`, and an SDK x86_64
target with OpenSSL/GOST. Prepare a disposable project as described in
[native_layout_smoke](../native_layout_smoke/README.md), with its first Aurora
preset configured for ARM64, debug/release templates and SDK Python/patchelf.
The project and tools must be visible in the SDK shared workspace. The runner
copies the project, keeps only that preset and uses isolated XDG settings.
GUI cases require a working Wayland desktop.

```sh
python3 misc/aurora/signing_smoke/run.py \
  --editor bin/godot.linuxbsd.editor.x86_64 \
  --sfdk /path/to/AuroraOS/bin/sfdk \
  --project /path/to/prepared-native-layout-project \
  --output /path/to/new-test-results \
  --tool-target AuroraOS-5.2.1.200-x86_64
```

The target name is an example, not an OS-version requirement. Select the
installed x86_64 SDK tool target with a working GOST engine. `--modes` can select
individual cases: `headless wrong cancel_prompt accept cancel_retry`.

The plugin drives the actual Project → Export dialog and file-selection signal;
no test-only exporter API is introduced. GUI input uses the secret LineEdit,
OK/Cancel buttons and the SDK progress Cancel button. Acceptance checks cover:

- Correct headless passphrase and GUI confirmation produce a Regular-signed RPM.
- Wrong passphrase and prompt cancellation preserve a pre-existing output file.
- Cancellation during packaging terminates a SIGTERM-ignoring SDK child; the
  next export in the **same editor process** succeeds with the real patchelf.
- Staging directories are cleaned. The synthetic passphrase is absent from the
  editor log and saved preset. GUI results record masking/confirmation; screenshots
  contain only the masked dialog.

The final host RPM bytes are streamed to a private SDK temporary file for an
independent signature dump. This avoids a stale size/inode in the SDK shared
mount after overwriting the test's pre-existing sentinel output.
The runner saves public signature details, logs and `summary.json`; it does not
perform a root-CA trust check. These development credentials establish neither
production signing nor device-install acceptance.

The process supervisor can also be tested without credentials or a GUI:

```sh
python3 -m unittest misc/aurora/test_sdk_job.py -v
python3 misc/aurora/sdk_job_smoke.py --sfdk /path/to/AuroraOS/bin/sfdk
```

The seven unit cases check exact exit status/Unicode, secret suppression, large
output, explicit cancellation, EOF/lease loss, transport signals and heartbeat
renewal. The real SDK smoke additionally verifies that cancellation leaves an
unrelated SDK job running and checks SIGHUP/SIGTERM cleanup. Both check the outer
SDK/interpreter exit, including Python shutdown diagnostics. This matters on the
SDK's Python 3.8: the protocol reader must be
stopped and joined before interpreter shutdown.

These checks cover Linux `engine exec` supervision. Device deployment uses a
separate transport path: killing its local sfdk client does not establish the
outcome of an already submitted install or launch. Connection-loss recovery,
Windows/macOS hosts and real ARM32 hardware have separate acceptance requirements.

## Recorded result — 2026-10-24

All five modes pass through the desktop Linux editor and the actual installed
SDK. The cancellation case preserves the old output, kills the ignoring child
and then creates a signed RPM in the same editor session. The real SDK supervisor
smoke and all 29 Python regression cases pass. The public SDK development
signature is inspected from final host RPM bytes; root-CA trust is unverified.
Hardware was not involved in these checks.
