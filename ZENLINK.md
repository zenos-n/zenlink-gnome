# ZenLink desktop extension

Fork of https://github.com/GSConnect/gnome-shell-extension-gsconnect, based on
upstream tag `v72`. Full upstream Git history has been fetched. The imported
snapshot is preserved on branch `snapshot-import`; development is on `zenlink`.
Hosted at https://github.com/zenos-n/zenlink-gnome.
Original licenses and attribution are retained.

Identity: `zenlink@zenos-n.github.io`, application/D-Bus ID `org.zenos.ZenLink`,
and separate `zenlink` configuration/cache/runtime directories.

The media plugin receives its configuration from the paired Android app. Its
local action shows status/preview and Stop. Phone mic audio goes to a dedicated
virtual input; PC playback uses a separate output and monitor. Internal audio
streams cannot fall back or be restored to unrelated devices. Phone video can
feed `/dev/video10`, provided the host has v4l2loopback. The Nix host module sets
up the module, permissions, dependencies, and firewall rules.

Build with `nix build` and check with `nix flake check`. `nix/host.nix` exports the
reusable host integration through the flake's `nixosModules.default`.

PC-only runtime check, from the workspace desktop development shell:

```sh
gjs -m installed-tests/test-media-runtime.js /path/to/installed/extension installed-tests/test-audio-routing.py
```

This uses isolated audio nodes and silence, without a phone connection. The
separate `test-audio-routing.py` checks an already active default streaming session
without recording media. `test-media.js` verifies protocol validation and defaults.

Initial video is JPEG at up to 320 pixels on the long edge / 5 fps, and audio is
48 kHz mono PCM. See the workspace README for device-test limitations.
