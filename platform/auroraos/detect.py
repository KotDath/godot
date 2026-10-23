import os
import shlex
import subprocess

from platform_methods import validate_arch


def get_name():
    return "AuroraOS"


def can_build():
    return os.name == "posix"


def get_opts():
    from SCons.Variables import BoolVariable

    return [
        ("aurora_toolchain_prefix", "Cross-tool prefix (empty when running under SB2)", ""),
        ("aurora_sysroot", "Target sysroot (empty when provided by SB2)", ""),
        ("aurora_pkg_config", "Target pkg-config executable", "pkg-config"),
        BoolVariable("wayland", "Build the Wayland display driver", True),
        BoolVariable("pulseaudio", "Build the PulseAudio driver", True),
    ]


def get_flags():
    return {
        "arch": "arm64",
        "vulkan": False,
        "opengl3": True,
        "accesskit": False,
        "sdl": False,
        "lto": "none",
        # Embree/occlusion culling is outside the validated mobile profile.
        "module_raycast_enabled": False,
    }


def configure(env):
    validate_arch(env["arch"], get_name(), ["arm32", "arm64"])
    prefix = env["aurora_toolchain_prefix"]
    if prefix:
        for variable, tool in (("CC", "gcc"), ("CXX", "g++"), ("AR", "ar"), ("RANLIB", "ranlib")):
            env[variable] = prefix + tool
    machine = subprocess.check_output(shlex.split(env["CC"]) + ["-dumpmachine"], text=True).strip()
    expected = "aarch64" if env["arch"] == "arm64" else "arm"
    if not machine.startswith(expected):
        raise RuntimeError("Aurora {} requires a target compiler, got {}".format(env["arch"], machine))
    sysroot = env["aurora_sysroot"]
    if sysroot:
        if not os.path.isdir(sysroot):
            raise RuntimeError("Missing Aurora sysroot: " + sysroot)
        env.Append(CCFLAGS=["--sysroot=" + sysroot], LINKFLAGS=["--sysroot=" + sysroot])
        env["ENV"]["PKG_CONFIG_SYSROOT_DIR"] = sysroot
        libdir = "lib64" if env["arch"] == "arm64" else "lib"
        env["ENV"]["PKG_CONFIG_LIBDIR"] = ":".join(
            os.path.join(sysroot, path)
            for path in ("usr/" + libdir + "/pkgconfig", "usr/lib/pkgconfig", "usr/share/pkgconfig")
        )
        env["ENV"]["PKG_CONFIG_PATH"] = ""

    def pkg_config(*packages):
        command = [env["aurora_pkg_config"], "--cflags", "--libs", *packages]
        output = subprocess.check_output(command, env=env["ENV"], text=True)
        env.MergeFlags(output)

    # The SDK owns the ARM floating-point ABI and CPU baseline; don't infer them
    # from the host, or override its compiler defaults with desktop flags.
    env.Append(CPPDEFINES=["UNIX_ENABLED", "AURORAOS_ENABLED", "_FILE_OFFSET_BITS=64"])
    env.Append(CPPPATH=["#platform/auroraos"])
    env.Append(LIBS=["pthread", "dl"])
    env["use_sowrap"] = False
    env["libdecor"] = False
    env["dbus"] = False
    env["speechd"] = False
    env["fontconfig"] = False
    env["alsa"] = False
    env["udev"] = False
    if env["accesskit"] or env["sdl"]:
        raise RuntimeError("Aurora AccessKit and SDL integration is not implemented yet")
    if env["wayland"]:
        pkg_config("wayland-client", "wayland-cursor", "wayland-egl", "xkbcommon")
        env.Append(CPPDEFINES=["WAYLAND_ENABLED", "XKB_ENABLED"])
        # Reuse upstream sources until the common Wayland layer is extracted.
        env.Append(CPPPATH=["#platform/linuxbsd"])
        env.Append(LIBS=["rt"])
    if env["pulseaudio"]:
        pkg_config("libpulse")
        env.Append(CPPDEFINES=["PULSEAUDIO_ENABLED", "_REENTRANT"])
    if env["opengl3"]:
        env.Append(CPPDEFINES=["GLES3_ENABLED"])
    if env["vulkan"]:
        raise RuntimeError("Aurora Vulkan support is not validated yet; use vulkan=no")
