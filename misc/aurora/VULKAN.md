# Experimental Aurora Vulkan path

This is an opt-in build, not validated Vulkan rendering on Aurora hardware.
Compatibility/GLES remains the default. No shared Wayland directory is introduced
and the original Linux build configuration is unchanged.

## Build

Use the existing build runner with trailing SCons options:

```sh
python3 misc/aurora/build.py --sfdk /home/kotdath/AuroraOS/bin/sfdk \
  --sdk-target AuroraOS-5.2.1.200-aarch64 --arch arm64 --target template_debug \
  --host-python .aurora-tools/sdk-x86_64/bin/python3.12 \
  -- vulkan=yes extra_suffix=vulkan
```

For ARM32 select the armv7hl SDK target and `--arch arm32`. The suffix preserves
the previously validated GLES-only binary. Vulkan uses bundled headers/glslang
and Volk to load a runtime loader dynamically. `use_volk=no` instead requires
Vulkan development libraries in the target sysroot. Wayland must be enabled.

Both ARM64 and ARM32 debug templates compile with this opt-in profile. On the
current KVADRA, explicit Vulkan startup falls back to GLES and the mobile fixture
runs (test RPM 4.7.2-8). Vulkan presentation is still untested.

## Runtime prerequisites and probe

`misc/aurora/vulkan_probe.c` links only libc/libdl and uses the bundled Vulkan
headers. Build it with the target compiler:

```sh
gcc -std=c11 -Wall -Wextra -Ithirdparty/vulkan/include \
  misc/aurora/vulkan_probe.c -ldl -o vulkan-probe
```

Install through a signed RPM on Aurora, as with engine probes. Do not execute
binaries from noexec home/tmp paths or copy arbitrary host GPU drivers.
The probe reports loader availability, instance extensions and physical devices.
If no loader exists it checks Mali exported entry points without using the driver
as a substitute loader. Exit 0 means a physical device was found, **not** successful
Wayland presentation. Exit 2 means the runtime loader could not be opened.

## KVADRA ARM64 findings — 2026-10-23

Installed read-only probe RPM: `ru.kotdath.godotvulkanprobe-4.7.2-1.aarch64`.

```text
LOADER_MISSING:libvulkan.so.1:...No such file or directory
LOADER_MISSING:libvulkan.so:...No such file or directory
MALI_VK_ENTRY:0
MALI_ICD_ENTRY:0
PROBE_EXIT:2
```

Installed Mali package: `libmali-0.0.6-1.2.22.omp.aarch64`, backing library
`libmali-valhall-g610-g13p0-wayland-gbm.so`. No standard Vulkan ICD directories,
loader registration or Vulkan packages were found. The same probe on the Linux
host finds its loader, RADV GPU and software device. Both ARM probe builds pass.

A compatible vendor Vulkan loader/ICD stack or another Aurora device is required
for hardware WSI testing. Adding only a loader does not create a hardware driver.
No system graphics libraries or device graphics settings were replaced.

## Still required

- The new [WSI diagnostic](VULKAN_WSI.md) creates a surface, presents/readbacks
  frames and replaces its swapchain. Linux RADV/llvmpipe pass; both ARM probes
  compile. Execute it on a Vulkan-capable Aurora stack; advertised
  `VK_KHR_wayland_surface` alone is insufficient evidence of presentation.
- Mobile renderer, then Forward+, feature/format/queue checks on both ARM ABIs.
- Coordinate/rotation validation against currentTransform/preTransform. Existing
  GLES client-side rotation must not be assumed correct for Vulkan.
- Resize, minimization, lock/unlock, device loss, long-run memory/performance.
- XR presentation remains a separate path.

See IMPLEMENTATION_ROADMAP.md for the full priority order and STATUS.md for actual
build/runtime results. Do not publish this experimental profile as Vulkan-ready.
