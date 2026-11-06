# ZenLink workspace backend

Canonical native desktop/PCVR host implementation, integrated with ZenLink's
existing GNOME extension. Imported from Quest Workspace commit4412adc under MIT;
see LICENSE. The Quest receiver and wire/pairing identities remain compatible.
The old standalone extension is superseded by src/shell/workspace.js.

ZenLink owns a systemd user socket on the configured receiver port. A connection
starts the native C++ TLS/WebSocket backend, which exits after five seconds
without a client. No capture, encoder, native host process or VR runtime runs
while idle. The shell bridge closes capture when locked and restores Forge
floating state on owner loss. Native PCVR settings are opened from ZenLink.

Settings include **Connect USB** (verified USB transport only) and reconnection
of a Quest already present in wireless debugging. Pairing details go over stdin
to the receiver sandbox, never into APKs or logs. The helper exits after setup.
USB debugging authorization is required; there is no background USB polling loop.

Streams are capped at120fps. The quality↔responsiveness control changes hardware
HEVC bitrate and the bounded ACK queue, from either PC or Quest. Unchanged pixels
are not encoded repeatedly. Physical monitor streams follow actual source cadence;
Mutter50.4 window capture itself advertises60fps.

Explicit virtual monitors negotiate120Hz and resize the same output through
PipeWire (`framerate=0/1,max-framerate=120/1`). No fixed modes or automatic oversized
output is used for ordinary window resizing. Virtual monitors pause without being
removed on focus loss, and end on close/disconnect.

Alignment uses a separate bounded GTK marker helper, without new Shell APIs or
Mutter modifications. The receiver supplies marker cells and measured screen
widths. Markers disappear on completion, Escape, cancel, timeout or disconnect.
Capture pauses during alignment and resumes after removal. Camera data stays on
Quest; the host receives only marker patterns and monitor selection.

WiVRn EXTX/OpenVR overlay composition shares the existing immersive video stream.
PC-rendered scenery is a separate immersive Godot environment with no desktop
stream inside it. Installed launchers use packaged executables; no runtime Nix
builds, repository paths or Python are required.

2026-11-06 native configuration/CTest, private-bus TLS/socket activation and
idle shutdown, controller lifecycle/migration/archive and independent PC
scene tests pass. Packaged settings entry and libadwaita preferences construction pass offscreen UI checks.
Five native CTests, private-bus TLS/socket/idle tests, fake-ADB USB transfer and
offscreen preferences checks pass. Real virtual monitor creation and four resizes
at120Hz pass in a disposable headless GNOME50.4 compositor, retaining the same
session/stream and verifying removal. Physical HDMI modes were not changed.
Hardware HEVC encoding passes. Marker detection and known-distance pose recovery
passed synthetic tests on Quest. A physical USB cable and on-head monitor alignment
remain unverified, as does WiVRn/SteamVR acceptance. Do not require headset wearing
for testing; coordinate updates and never log out without authorization.
