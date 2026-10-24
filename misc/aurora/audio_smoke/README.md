# Aurora PulseAudio policy diagnostic

This standalone native client checks the installed audio server's stream policy.
It does not link into Godot, add a runtime dependency to engine/game RPMs, or
establish audible playback, headset behavior or engine-driver acceptance.

## Build and client contract

From the engine checkout, choose an explicit SDK target. The x86_64 build was
executed on the SDK emulator; ARM builds are compilation/ELF checks only.
Both ARM executables also exercise the expected missing-server exit inside their
SDK targets. This checks target startup/ABI, without ARM playback acceptance.

```sh
sfdk engine exec sb2 -t YOUR_TARGET -m sdk-build gcc \
  -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
  -Ithirdparty/linuxbsd_headers misc/aurora/audio_smoke/pulse_probe.c \
  -l:libpulse-simple.so.0 -l:libpulse.so.0 -lm -o /shared/output/pulse-probe
```

The diagnostic uses Godot's bundled public PulseAudio headers and the target's
actual runtime libraries. It can therefore run without installing PulseAudio
development packages into the emulator target. Use a fresh output/source path or
the existing SDK source-refresh mechanism when the shared filesystem has cached
an older inode size; see the SDK mount caveat in STATUS.md.

```sh
pulse-probe unset
pulse-probe x-maemo
pulse-probe event
```

`unset` removes the role from this client; other arguments explicitly set it.
The client writes five seconds of stereo 48 kHz S16LE silence. An optional integer
second argument adds 0–20 seconds. `AURORA_AUDIO_PROBE_TONE=1` enables a 440 Hz
test signal (880 Hz for the `notification` role). Both channels contain the same
signal; this is not a stereo-channel separation test. `AURORA_AUDIO_PROBE_SINK`
selects a sink for the client, subject to server policy. An alarm bounds blocked
connect/write operations. Exit 0 and `AURORA_AUDIO_POLICY:DONE` are both required.

Use a dedicated emulator session. Transfer/start the tool through audb or the SDK
device frontend, then inspect its own `application.process.id` and
`application.name=GodotAuroraPolicyProbe` in `pactl --format=json list sink-inputs`.
No server configuration, volume setting or resource-policy override is needed.

## Measured emulator behavior — 2026-10-24

The installed server is PulseAudio 16.1+git18.1. Its actual loaded modules include
`module-match`, `module-policy-enforcement` and `module-role-ducking` with
`trigger_roles=alarm,notification,warning ducking_roles=x-maemo volume=-200dB`.
These are observed capabilities, not an Aurora minimum-version condition.

| Requested role | Observed role | Policy group | Restore identity |
| --- | --- | --- | --- |
| unset | x-maemo | othermedia | sink-input-by-media-role:x-maemo |
| x-maemo | x-maemo | othermedia | sink-input-by-media-role:x-maemo |
| event | event | othermedia | sink-input-by-media-role:event |

The server's `/etc/pulse/x-maemo-match.table` supplies the fallback role. Thus
copying savegame's explicit role assignment is not necessary to obtain that role
on this tested server. Other server configurations still require observation.
Ordinary introspection volume fields stay constant during role ducking and
cannot prove that the audible data is unchanged.

For the sample check, a 15-second 440 Hz client overlaps a five-second 880 Hz
notification data stream, starting three seconds later. `parec --raw --format=s16le
--rate=48000 --channels=2 --device=sink.emulator.monitor | head -c 2304000`
captures exactly twelve seconds. Inspect stream/sink and monitor-source identities
before/during/after, require only the owned playback clients, and wait for both
clients and the capture to finish. Repeat with `event` as a negative control.
The actual server routes these clients to `sink.emulator`, including an explicit
private-sink request; final measurements use that ordinary output's monitor.

The 440/880 Hz Fourier amplitudes are measured in one-second windows starting at
1, 4.5 and 9 seconds. The same game stream survives all three snapshots.

| Test | 440 Hz before | 440 Hz during notification | 440 Hz after |
| --- | ---: | ---: | ---: |
| x-maemo | 4095.357 | 0.001800 | 4095.357 |
| event control | 4095.357 | 4095.357 | 4095.357 |

The trigger's 880 Hz component is present during interruption and absent in the
before/after windows. The game attenuation ratio is below 0.0001, restoration is
within 2%, and the event control stays within 2%. Each capture contains 576000
stereo frames. Exact client/source/binary/capture hashes, stream identities and
verification results remain locally in `.aurora-tools/audio-policy-evidence/`.
After repository formatting and the license header, the published x86_64 tool's
`.text` and `.rodata` remain byte-identical to the measured client; those section
hashes and final ELF identities are in `published-identity.json`.

This proves server role classification, PCM attenuation and restoration for native
libpulse test clients on the emulator. It does not test Godot's PulseAudio mixing,
real alarm/call acquisition, physical listening, latency, headphones, long
background sessions or lock/unlock. Those remain in point 5 of the roadmap.

## Primary references

- [PulseAudio application and stream properties](https://wiki.freedesktop.org/www/Software/PulseAudio/Documentation/Developer/Clients/ApplicationProperties/).
- [savegame Aurora driver](https://github.com/savegame/godot/blob/4.4_auroraos/drivers/pulseaudio/audio_driver_pulseaudio.cpp).
- [Sailfish audio-resource API and its backend requirements](https://github.com/sailfishos/libaudioresource).

The inspected Aurora SDK inventory has libresource/resourceqt, but no
libaudioresource. Availability of a Sailfish API is not taken as Aurora support;
no Qt/GLib resource adapter or process-global role override is added on that basis.
