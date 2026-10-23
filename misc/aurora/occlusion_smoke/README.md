# Aurora Embree / occlusion smoke test

This project measures draw submission, not merely whether Embree linked.
It creates 100 boxes behind an opaque wall and two visible meshes (the wall and
a green box). It samples rendered-object counts after 60 rendered frames in each
state: culling disabled, culling enabled, occluder hidden again.

Expected: at least 100 objects initially, at least 90 culled with both visible
meshes preserved, and at least 100 objects restored after disabling the occluder.
It prints `AURORA_OCCLUSION:PASS/FAIL:off=...:on=...:restored=...`.
Pass `-- --quit` to exit with status 0 on success, 1 on failure. The test waits for
actual rendered frames; a minimized app must be brought to the foreground.

The host negative control (`module_raycast_enabled=no`, GLES/Xvfb) reports:

```text
WARNING: Occlusion culling is disabled at build-time.
AURORA_OCCLUSION:FAIL:off=102:on=102:restored=102
```

For Aurora ARM64 use `module_raycast_enabled=yes` with the existing build runner.
The Embree SIMD adaptation needs `-flax-vector-conversions` with SDK GCC; the flag
is scoped to Embree's third-party objects and does not change Linux configuration.
ARM32 is excluded by upstream modules/raycast/config.py. Do not remove that guard
or claim ARM32 support without implementing and validating an appropriate backend.
Physical RayCast3D queries are separate from this module.

## Hardware result — 2026-10-23

KVADRA ARM64, SDK GCC 12.3.1, GLES/Mali, debug template built with
`vulkan=yes extra_suffix=vulkan module_raycast_enabled=yes`, installed test RPM
`ru.kotdath.godotocclusion-4.7.2-1`:

```text
AURORA_OCCLUSION:PASS:off=102:on=2:restored=102
```

The same result was obtained in two subsequent remote-debugger runs. This verifies
basic culling on ARM64; large-scene performance/memory, baking and editor workflows
remain untested. The default Aurora profile still disables the module until those
checks and the release artifact matrix are updated.
