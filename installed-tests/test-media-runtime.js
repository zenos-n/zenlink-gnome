// SPDX-License-Identifier: GPL-2.0-or-later
// PC-only integration test: creates isolated audio nodes and sends silence.
// Arguments: installed extension directory, test-audio-routing.py path.
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import 'gi://Gtk?version=3.0';
import 'gi://Gdk?version=3.0';

globalThis._ = text => text;
globalThis.debug = () => {};
if (ARGV.length !== 2)
    throw new Error('Expected installed extension directory and routing test path');
const root = Gio.File.new_for_path(ARGV[0]);
const Setup = await import(root.get_child('utils/setup.js').get_uri());
Setup.setup(root.get_path());
Setup.setupGettext();
// Match the service's import order around the shared plugin registry.
const {default: plugins} = await import(root.get_child('service/plugins/index.js').get_uri());
const MediaPlugin = plugins.media.default;
const prototype = MediaPlugin.prototype;
const fixture = Object.create(prototype);
fixture._device = {id: 'routing_fixture', sendPacket() {}};
fixture._pipes = [];
fixture._modules = [];
fixture._pending = new Map();
fixture._sequence = 0;
fixture._session = null;
fixture._remote = null;
fixture._pactl = function (args) {
    // Leave the user's existing playback streams and defaults in place.
    if (args[0].startsWith('set-default-') || args[0] === 'move-sink-input')
        return '';
    return prototype._pactl.call(this, args);
};

const loop = new GLib.MainLoop(null, false);
let failure = null;
let writes = null;
try {
    fixture._start({sendAudio: true, sendVideo: false, receiveAudio: true,
        receiveVideo: false, virtualMic: true, virtualCamera: false});
    if (!fixture._session)
        throw new Error('The media pipeline failed to start');
    writes = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 20, () => {
        fixture._audioOutput?.write(new Uint8Array(1920));
        return GLib.SOURCE_CONTINUE;
    });
    GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 3, () => {
        try {
            const process = Gio.Subprocess.new(['python3', ARGV[1], 'routing_fixture'],
                Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_PIPE);
            const [, output, error] = process.communicate_utf8(null, null);
            if (!process.get_successful())
                throw new Error(error || 'Live routing checks failed');
            print(output.trim());
        } catch (e) {
            failure = e;
        } finally {
            loop.quit();
        }
        return GLib.SOURCE_REMOVE;
    });
    loop.run();
} finally {
    if (writes)
        GLib.source_remove(writes);
    fixture._stop(null, false);
}
if (failure)
    throw failure;
print('PC-only runtime test passed; no phone connection or capture was used');
