`surface-extension.xml` is the Qt Wayland surface extension (BSD-3-Clause,
Digia 2012). Imported unchanged from savegame/godot commit
`6f208cf0a4433625a87d18f0d05cb7d13ac2bbe2`, path
`thirdparty/wayland-protocols/stable/surface-extension/surface-extension.xml`.
The Aurora adapter binds protocol version 1, including on compositors that
advertise later versions. No Qt library is linked into the engine.

`text-input-unstable-v2.xml` is imported unchanged from the local Qt 6.10.3
protocol distribution (`qt6-qtbase-gui-6.10.3-1.fc44`),
`wayland/protocols/text-input/v2/text-input-unstable-v2.xml`.
The Intel/Jan Arne Petersen X11 license is preserved in the XML and COPYRIGHT.txt.
Only protocol code is generated; no Qt dependency is introduced.
