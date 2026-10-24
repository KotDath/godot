# Vulkan Wayland presentation diagnostic

`vulkan_wsi_probe.c` is standalone. It does not run Godot or establish Mobile,
Forward+, XR or Aurora hardware support. The older `vulkan_probe.c` remains
useful for loader/device discovery without Wayland.

## Contract

The probe dynamically opens a Vulkan loader, creates a real Wayland surface and
selects the explicitly numbered device. It requires a combined graphics/present
queue, RGBA/BGRA 8-bit format, FIFO and color attachment usage. Unsupported
configurations fail; another GPU/renderer is not silently substituted. Both
Aurora's `wl_shell` and an XDG role for native Linux testing are supported.

A shader-free render pass clears four buffer-space quadrants: red/green above,
blue/yellow below. With supported transfer-source usage, each frame is copied
to host-visible memory; four pixel samples must match exactly. Noncoherent
memory is invalidated before reading. `--require-readback` makes this mandatory.
Each successful present also waits for its Wayland frame callback.

Halfway through the requested frames, the swapchain is replaced via
`oldSwapchain`, with new views/framebuffers/commands/readback.
`WSI_RECREATE:forced-not-a-resize-claim` distinguishes this from real resize.
Configure changes and out-of-date/suboptimal results also request replacement.
Zero/unsupported extents fail instead of counting as success.

`currentTransform`, `supportedTransforms` and `preTransform=currentTransform`
are reported, after checking support. Godot's GLES transform is not applied;
Wayland buffer orientation is not changed. The pattern is in buffer coordinates:
it does not establish correct physical rotation, shader/compute/material or 3D
renderer behavior.

## Synchronization and execution limits

One graphics submission is in flight. Acquisition is consumed before semaphore
reuse. Present-wait semaphores belong to swapchain images, following the
[Khronos semaphore guide](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html).
When available, `VK_EXT_swapchain_maintenance1` and its instance dependencies
are enabled by capability. Present fences are waited before replacement/free;
`--require-present-fence` makes this mandatory in the strict host smoke.

Without that extension the diagnostic reports `legacy-wait-idle-limitation`.
Conventional device-idle cleanup has no formal presentation-completion guarantee
in unextended Vulkan (see Khronos guide). That mode cannot establish safe
presentation-resource teardown. No Aurora version is used to infer capabilities.

Per-frame fence waits pump Wayland events. A process alarm bounds initialization, rendering
and cleanup, including a hanging driver call. SIGINT/TERM or window close does
not count as success. Timeout exits 124 and can omit the final result record.
Other exits: 2 missing loader; 3 Vulkan instance/setup; 4 device/queue/required
feature; 5 render/readback/validation/interruption; 6 Wayland/shell; 64 arguments.
Only a completed run produces `WSI_RESULT:PASS` and exit 0. Optional readback or
validation must not be inferred when their flags were not supplied.

## Build and reproduction

Use the repository root, Linux Python, native `wayland-scanner`, a C11 compiler
and `wayland-devel`. Vulkan headers are bundled. No link-time Vulkan loader,
Qt or shader compiler is needed. Generated bindings/binaries remain in a new
output directory.

```sh
python3 misc/aurora/vulkan_wsi_smoke.py --build-only \
  --output /path/to/new/native-build

python3 misc/aurora/vulkan_wsi_smoke.py \
  --sfdk /path/to/AuroraOS/bin/sfdk \
  --sdk-target YOUR_EXACT_AARCH64_TARGET \
  --output /path/to/new/shared-arm64-build
```

Select an armv7hl target for ARM32. SDK mode only compiles, with no deployment or
rendering claim. `build.json` records command, source/protocol/binary SHA256 and
ELF header/dependencies. This is diagnostic identity, not the full point-13
clean-source/SDK provenance manifest. Do not run SDK refresh/builds concurrently
with another engine build in the same checkout.

Native Linux testing requires Khronos validation and KWin with virtual backend:

```sh
python3 misc/aurora/vulkan_wsi_smoke.py \
  --output /path/to/new/host-test \
  --software-icd /path/to/software-icd.json
```

The optional software case selects that ICD only for its child process; the
default case records the device actually chosen. `--layer-dir`/`--library-dir`
can supply an unpacked validation layer. Core validation is enabled and
`VK_VALIDATION_VALIDATE_SYNC=1` requests synchronization validation, as documented
by [LunarG](https://vulkan.lunarg.com/doc/view/latest/linux/layer_configuration.html).

The runner owns a 0700 runtime directory, D-Bus session and KWin virtual socket.
It stops its compositor in `finally`; a session timeout kills its still-owned
process group. It does not unlock or replace the existing desktop. Paths with
spaces work. Strict checks require readback/present fences, 60 presents/pixel
checks/callbacks, at least two swapchains and zero validation errors. Negative
checks cover a missing loader, invalid device, missing compositor and watchdog.
A second native binary changes only expected red from 255 to 254 and must fail
the real GPU readback comparison with exit 5 and no validation errors.

On Aurora package/install through the usual signed-RPM development workflow,
using the intended application security context and actual loader/ICD:

```sh
/usr/bin/YOUR_PROBE_ID --shell legacy --frames 60 --timeout 30 --require-readback
```

A hidden surface can lack frame callbacks and time out. Record that state;
successful queue submission alone does not establish display acceptance.
audb is not a dependency.

## Recorded results — 2026-10-24

- Native Linux, private KWin: AMD Radeon 8060S/RADV and explicit llvmpipe each
  pass 60 presents, 60 exact readbacks, 60 callbacks, two swapchains and zero
  validation errors. Khronos layer 1.4.341 comes from a signature-verified Fedora
  RPM unpacked locally. The default loader warns about an unrelated unavailable
  DZN ICD; the selected device is RADV.
- Setup failures, pixel negative control and watchdog pass expected failure
  checks. Local final evidence: `.aurora-tools/vulkan wsi verified/`.
- ARM64/ARM32 compile with SDK GCC and `-Wall -Wextra -Werror`: ELF64/AArch64 and
  ELF32/ARM respectively. Both actual target executables return the expected
  missing-loader error inside SDK target execution/emulation. Target presentation
  is not claimed. Evidence: `.aurora-tools/vulkan-wsi-arm64-final/` and `-arm32-final/`.
- KVADRA previously lacked loader/ICD and Mali Vulkan entry points; it is currently
  unreachable. A compatible vendor stack or another Vulkan-capable Aurora device
  is required. No device graphics stack has been replaced.

Aurora legacy-shell WSI, real resize/rotation/minimize/lock/unlock, device loss and
sustained presentation remain unaccepted. See [VULKAN.md](VULKAN.md) and
[STATUS.md](STATUS.md).
