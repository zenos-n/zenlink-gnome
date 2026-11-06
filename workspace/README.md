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

WiVRn EXTX/OpenVR overlay composition shares the existing immersive video stream.
PC-rendered scenery is a separate immersive Godot environment with no desktop
stream inside it. Installed launchers use packaged executables; no runtime Nix
builds, repository paths or Python are required.

2026-11-06 integration in progress. User requests PC/offscreen verification only;
no headset wearing/tests, Quest app launches, phone modifications, GNOME logout
or OS activation unless needed and coordinated. Preserve pairing files.
