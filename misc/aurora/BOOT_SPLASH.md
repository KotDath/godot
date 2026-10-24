# GLES boot-splash orientation

Aurora keeps the EGL framebuffer in native dimensions while the application sees
logical, possibly swapped window dimensions. The regular GLES final blit already
accounts for this. The boot image has a separate direct draw path and previously
used logical dimensions for its viewport without applying the content rotation.

The Aurora-only boot path now reads one coherent size/transform snapshot. It
fits the image using the logical size, draws into the native framebuffer, and
passes the quarter-turn to the existing copy shader. This preserves all six
stretch modes, filtering, Y inversion and alpha blending. Before a valid legacy
snapshot exists it uses the ordinary size and zero rotation. Other platforms'
boot code is unchanged by the preprocessor guards. No shared Wayland layer or
render-thread DisplayServer callback is added.

This implements the **main-window GLES software path**. It does not implement
per-window/per-output orientation, external-screen policy, XR eye transforms or
Vulkan swapchain transforms. Real Aurora startup/splash presentation remains
unaccepted while the ARM64 tablet is unavailable.

## GPU component regression

After building a host editor (which provides generated core headers), compile:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Wno-unused-parameter \
  -ffunction-sections -fdata-sections -I. -Iplatform/linuxbsd \
  misc/aurora/test_boot_splash_gpu.cpp core/math/vector2.cpp core/math/vector2i.cpp \
  -Wl,--gc-sections -lEGL -lGLESv2 -o /tmp/aurora-boot-gpu

EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 /tmp/aurora-boot-gpu \
  drivers/gles3/shaders/effects/copy.glsl /tmp/aurora-boot-software
```

The unused-parameter exception matches the stub methods in upstream core
headers. EGL/GLES3 development headers/libraries are required. The test owns a
surfaceless RGBA8 pbuffer/context; it neither creates a desktop window nor changes
the compositor session. A 30-second process alarm bounds the run.

The test compiles the actual production copy shader and uses the production
presentation and stretch helpers. A non-square RGBA image with four colors, an
asymmetric origin marker and a translucent quadrant is rendered in both nearest
and linear modes. For each of three framebuffer sizes, six stretch modes and
four transforms, the rotated GPU readback is compared with an unrotated GPU
reference in logical dimensions, using an independent integer pixel permutation.
Three additional cases verify the unavailable/zero-sized snapshot fallback.

Flat interior samples are required to match exactly; boundary samples are
accounted separately to expose rasterization/interpolation differences. Each
case must retain more than half its pixels as interior coverage. On both tested
drivers boundary differences were also **zero**.

On 2026-10-24, **llvmpipe LLVM 22.1.5 and AMD Radeon 8060S/radeonsi**, Mesa 26.0.8,
each pass 147 cases and 30133826 interior-pixel comparisons, with zero boundary
differences and no GLES error. Representative readbacks are saved as PPM files.
Local exact source/binary/readback hashes and logs:
`.aurora-tools/boot-splash-evidence/`.

Two negative controls must exit nonzero without the success marker:

```sh
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 /tmp/aurora-boot-gpu \
  drivers/gles3/shaders/effects/copy.glsl /tmp/aurora-boot-bad-rotation wrong-rotation
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 /tmp/aurora-boot-gpu \
  drivers/gles3/shaders/effects/copy.glsl /tmp/aurora-boot-bad-viewport wrong-viewport
```

Both controls fail at transform 1, detecting a missing rotation and a logical-size
viewport respectively. The flags exist only in the standalone test executable.

This is a real GPU component test of the layout/shader combination. It is not a
capture of Godot's actual boot sequence, a Wayland protocol/compositor check, or
physical ARM acceptance. Engine builds and the ordinary Linux Wayland regression
are recorded separately in STATUS.md.

## Engine validation

Source commit `ac904bd2ee` passes incremental Linux editor and six ARM engine
builds (editor/debug/release for both ARM architectures). Each exact ELF has its
expected architecture/ABI and embeds the full source revision. Before each ARM
architecture's compilation, the SDK stat sizes and SHA256 of orientation.h and
rasterizer_gles3.cpp match the host. The tracked source snapshot remains identical
before/after the complete run. Linux passes the 15 Aurora C++ cases / 195 assertions
and a private KWin Wayland/OpenGL 3D/native42 regression with real frame capture.
Local artifacts/manifest: `.aurora-tools/boot-splash-engine-ac904bd2/`.

These are incremental developer-checkout checks; the detached clean-source matrix
at `6573da29ad` is separate. Neither is a physical Aurora splash/startup result.
