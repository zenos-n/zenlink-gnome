// SPDX-License-Identifier: GPL-2.0-or-later
import GdkPixbuf from 'gi://GdkPixbuf';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import Gtk from 'gi://Gtk';
import Plugin from '../plugin.js';
import {MediaPipe} from '../utils/mediaPipe.js';
import * as Media from '../utils/mediaProtocol.js';

export const Metadata = {
    label: _('Audio and camera'),
    description: _('Stream sound, microphone, and camera in both directions'),
    id: 'org.zenos.ZenLink.Plugin.Media',
    incomingCapabilities: [Media.CONTROL, Media.AUDIO, Media.VIDEO],
    outgoingCapabilities: [Media.CONTROL, Media.AUDIO, Media.VIDEO],
    actions: {
        media: {
            label: _('Audio and camera'),
            icon_name: 'camera-video-symbolic',
            parameter_type: null,
            incoming: [Media.CONTROL],
            outgoing: [Media.CONTROL],
        },
    },
};

const MediaPlugin = GObject.registerClass({GTypeName: 'ZenLinkMediaPlugin'}, class MediaPlugin extends Plugin {
    _init(device) {
        super._init(device, 'media');
        this._pipes = [];
        this._modules = [];
        this._pending = new Map();
        this._remote = null;
        this._sequence = 0;
        this._session = null;
    }

    media() {
        if (this._window) {
            this._window.present();
            return;
        }
        this._window = new Gtk.Window({title: _('ZenLink · Streaming status'), default_width: 600, default_height: 480});
        const box = new Gtk.Box({orientation: Gtk.Orientation.VERTICAL, spacing: 12, margin: 20});
        this._window.add(box);
        box.add(new Gtk.Label({label: this.device.name, xalign: 0}));
        box.add(new Gtk.Label({label: _('Start and configure streaming on your phone. You can change cameras and microphones there while streaming.'), wrap: true, max_width_chars: 56, xalign: 0}));
        this._status = new Gtk.Label({label: _('Not streaming'), wrap: true, xalign: 0, selectable: true});
        box.add(this._status);
        this._preview = new Gtk.Image();
        this._preview.get_accessible().set_name(_('Phone camera preview'));
        box.add(this._preview);
        this._stopButton = new Gtk.Button({label: _('Stop streaming'), sensitive: !!this._session});
        this._stopButton.connect('clicked', () => this._stop());
        box.add(this._stopButton);
        this._window.connect('destroy', () => {
            this._window = null;
            this._preview = null;
            this._status = null;
            this._stopButton = null;
        });
        if (this._session)
            this._updateStatus();
        this._window.show_all();
        this._window.present();
    }

    _pactl(args) {
        const binary = GLib.find_program_in_path('pactl');
        if (!binary)
            throw new Error(_('Install pactl (PulseAudio utilities) to use system sound or a virtual microphone'));
        const process = Gio.Subprocess.new([binary, ...args], Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_PIPE);
        const [, output, error] = process.communicate_utf8(null, null);
        if (!process.get_successful())
            throw new Error(error.trim() || _('The audio server rejected the request'));
        return output.trim();
    }

    _pipe(elements, output = null) {
        const binary = GLib.find_program_in_path('gst-launch-1.0');
        if (!binary)
            throw new Error(_('Install GStreamer and its base/good plugins to stream media'));
        const pipe = new MediaPipe([binary, '-q', ...elements], message => this._stop(message), output);
        this._pipes.push(pipe);
        return pipe;
    }

    _computerCamera() {
        const entries = Gio.File.new_for_path('/dev').enumerate_children('standard::name', Gio.FileQueryInfoFlags.NONE, null);
        const devices = [];
        try {
            let entry;
            while ((entry = entries.next_file(null))) {
                const name = entry.get_name();
                if (/^video[0-9]+$/.test(name) && name !== 'video10')
                    devices.push(`/dev/${name}`);
            }
        } finally {
            entries.close(null);
        }
        // Never feed the phone's virtual camera back into itself.
        return devices.sort()[0] || null;
    }

    _prepareAudio(settings) {
        if (!settings.sendAudio && !settings.receiveAudio)
            return;
        // Snapshot before creating virtual nodes: policy may select a new sink.
        const sinks = JSON.parse(this._pactl(['--format=json', 'list', 'sinks']));
        const previousSink = this._pactl(['get-default-sink']);
        const previousIndex = sinks.find(sink => sink.name === previousSink)?.index;
        this._routing = {sink: previousSink, source: this._pactl(['get-default-source']),
            inputs: JSON.parse(this._pactl(['--format=json', 'list', 'sink-inputs']))
                .filter(input => input.sink === previousIndex).map(input => input.index)};
        const id = this.device.id.replace(/[^a-zA-Z0-9_]/g, '_');
        this._playbackSink = `zenlink_output_${id}`;
        this._microphoneFeed = `zenlink_mic_feed_${id}`;
        this._microphoneSource = `zenlink_microphone_${id}`;
        if (settings.sendAudio) {
            this._modules.push(this._pactl(['load-module', 'module-null-sink', `sink_name=${this._playbackSink}`,
                `sink_properties='device.description="ZenLink Speakers" device.icon_name=audio-headphones'`]));
        }
        if (settings.receiveAudio) {
            this._modules.push(this._pactl(['load-module', 'module-null-sink', `sink_name=${this._microphoneFeed}`,
                `sink_properties='device.description="ZenLink Microphone Feed" node.hidden=true priority.session=0 device.class=filter'`]));
            this._modules.push(this._pactl(['load-module', 'module-remap-source', `master=${this._microphoneFeed}.monitor`,
                `source_name=${this._microphoneSource}`,
                `source_properties='device.description="ZenLink Microphone" device.icon_name=audio-input-microphone'`]));
            this._pactl(['set-default-source', this._microphoneSource]);
        }
        if (settings.sendAudio) {
            this._pactl(['set-default-sink', this._playbackSink]);
            for (const index of this._routing.inputs) {
                try { this._pactl(['move-sink-input', `${index}`, this._playbackSink]); } catch (e) { debug(e); }
            }
        }
    }

    _restoreAudio() {
        if (!this._routing)
            return;
        try {
            const sinks = JSON.parse(this._pactl(['--format=json', 'list', 'sinks']));
            const outputIndex = sinks.find(sink => sink.name === this._playbackSink)?.index;
            const inputs = JSON.parse(this._pactl(['--format=json', 'list', 'sink-inputs']));
            for (const index of this._routing.inputs) {
                if (inputs.some(input => input.index === index && input.sink === outputIndex)) {
                    try { this._pactl(['move-sink-input', `${index}`, this._routing.sink]); } catch (e) { debug(e); }
                }
            }
            if (this._pactl(['get-default-sink']) === this._playbackSink)
                this._pactl(['set-default-sink', this._routing.sink]);
            if (this._pactl(['get-default-source']) === this._microphoneSource)
                this._pactl(['set-default-source', this._routing.source]);
        } catch (e) {
            debug(e, 'ZenLink audio route restore');
        }
        this._routing = null;
    }

    _start(settings) {
        if (this._session)
            return;
        try {
            this._session = GLib.uuid_string_random();
            this._receiveAudioEnabled = settings.receiveAudio;
            this._receiveVideoEnabled = settings.receiveVideo;
            this._receivedAudio = 0;
            this._receivedVideo = 0;
            this._warning = null;
            this._pending.clear();
            this._prepareAudio(settings);
            if (this._stopButton)
                this._stopButton.sensitive = true;
            if (this._receiveAudioEnabled) {
                const sink = [`device=${this._microphoneFeed}`];
                const properties = `stream-properties=props,application.name=ZenLink-Microphone,module-stream-restore.id=${this._microphoneFeed},node.dont-fallback=true,node.dont-reconnect=true,node.linger=true`;
                this._audioOutput = this._pipe(['fdsrc', 'fd=0', '!', `audio/x-raw,format=S16LE,rate=${Media.RATE},channels=1,layout=interleaved`, '!', 'audioconvert', '!', 'audioresample', '!', 'pulsesink', 'client-name=ZenLink-Microphone', properties, 'sync=false', ...sink]);
            }
            if (this._receiveVideoEnabled && settings.virtualCamera) {
                const path = '/dev/video10';
                if (!/^\/dev\/video[0-9]+$/.test(path))
                    throw new Error(_('Choose a virtual webcam device such as /dev/video10'));
                if (GLib.file_test(path, GLib.FileTest.EXISTS)) {
                    this._videoOutput = this._pipe(['fdsrc', 'fd=0', 'do-timestamp=true', '!', 'jpegparse', '!', 'jpegdec', '!', 'videoconvert', '!', 'videoscale', '!', 'video/x-raw,format=YUY2,width=640,height=480', '!', 'v4l2sink', `device=${path}`, 'sync=false']);
                } else {
                    this._warning = _('Camera preview is active. The virtual webcam needs v4l2loopback on this computer.');
                }
            }
            if (settings.sendAudio) {
                // Capture only the playback node, never a default or microphone monitor.
                const source = [`device=${this._playbackSink}.monitor`];
                let buffer = new Uint8Array();
                const properties = `stream-properties=props,application.name=ZenLink-Playback,module-stream-restore.id=${this._playbackSink},node.dont-fallback=true,node.dont-reconnect=true,node.linger=true`;
                this._pipe(['pulsesrc', 'client-name=ZenLink-Playback', properties, 'buffer-time=40000', 'latency-time=10000', ...source, '!', 'audioconvert', '!', 'audioresample', '!', `audio/x-raw,format=S16LE,rate=${Media.RATE},channels=1,layout=interleaved`, '!', 'fdsink', 'fd=1'], bytes => {
                    const combined = new Uint8Array(buffer.length + bytes.length);
                    combined.set(buffer); combined.set(bytes, buffer.length);
                    let offset = 0;
                    while (offset + Media.AUDIO_BYTES <= combined.length) {
                        this._send(Media.AUDIO, combined.slice(offset, offset + Media.AUDIO_BYTES));
                        offset += Media.AUDIO_BYTES;
                    }
                    buffer = combined.slice(offset);
                });
            }
            const cameraDevice = settings.sendVideo ? this._computerCamera() : null;
            if (settings.sendVideo && !cameraDevice && !settings.testVideo)
                this._warning = _('No computer webcam is connected. Other streams are active.');
            if (settings.sendVideo && (cameraDevice || settings.testVideo)) {
                let buffer = new Uint8Array();
                const source = settings.testVideo === true
                    ? ['videotestsrc', 'is-live=true', 'pattern=ball'] : ['v4l2src', `device=${cameraDevice}`];
                // shortcut: JPEG at 320×240/5 fps shares the paired TLS transport; use a separate media transport for HD.
                this._pipe([...source, '!', 'videoconvert', '!', 'videoscale', '!', 'videorate', '!', 'video/x-raw,width=320,height=240,framerate=5/1', '!', 'jpegenc', 'quality=60', '!', 'fdsink', 'fd=1'], bytes => {
                    const combined = new Uint8Array(buffer.length + bytes.length);
                    combined.set(buffer); combined.set(bytes, buffer.length);
                    let start = 0;
                    for (let index = 1; index < combined.length; index++) {
                        if (combined[index - 1] === 0xff && combined[index] === 0xd9) {
                            const frame = combined.slice(start, index + 1);
                            if (frame.length <= Media.MAX_JPEG)
                                this._send(Media.VIDEO, frame);
                            start = index + 1;
                        }
                    }
                    buffer = combined.slice(start);
                    if (buffer.length > Media.MAX_JPEG * 2)
                        throw new Error(_('Camera produced an oversized JPEG frame'));
                });
            }
            this._announce();
            this._timer = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 2, () => {
                this._announce();
                return GLib.SOURCE_CONTINUE;
            });
            this._updateStatus();
        } catch (e) {
            this._stop(e.message);
        }
    }

    _announce() {
        if (this._session)
            this.device.sendPacket(Media.control('ready', this._session,
                {audio: this._receiveAudioEnabled, video: this._receiveVideoEnabled, message: this._warning || ''}));
    }

    _send(kind, bytes) {
        const now = GLib.get_monotonic_time() / 1000;
        if (!this._session || !this._remote || now - this._remote.seen > 6000 ||
            !this._remote[kind === Media.AUDIO ? 'audio' : 'video'])
            return;
        const pending = this._pending.get(kind);
        if (pending && now - pending.time < 1000)
            return;
        const sequence = ++this._sequence;
        this._pending.set(kind, {sequence, time: now});
        this.device.sendPacket({type: kind, body: {version: 1, session: this._remote.session,
            sequence, format: kind === Media.AUDIO ? 's16le' : 'jpeg', rate: Media.RATE,
            channels: 1, data: GLib.base64_encode(bytes)}});
    }

    handlePacket(packet) {
        const body = packet.body;
        if (body.version !== 1)
            return;
        if (packet.type === Media.CONTROL) {
            if (body.action === 'ready' && typeof body.session === 'string' && /^[a-fA-F0-9-]{36}$/.test(body.session)) {
                const changed = this._remote?.session !== body.session;
                if (changed)
                    this._pending.clear();
                this._remote = {session: body.session, audio: body.audio === true,
                    video: body.video === true, seen: GLib.get_monotonic_time() / 1000};
                if (changed)
                    this._announce();
            } else if (body.action === 'configure') {
                const settings = Media.settingsFromControl(packet, this._remote?.session);
                if (settings && body.session !== this._configuredSession) {
                    this._stop(null, false);
                    this._configuredSession = body.session;
                    this._start(settings);
                }
            } else if (body.action === 'stop' && body.session === this._remote?.session) {
                if (this._session)
                    this._stop(null, false);
                this._remote = null;
                this._configuredSession = null;
                this._pending.clear();
            } else if (body.action === 'ack' && body.session === this._remote?.session &&
                this._pending.get(body.kind)?.sequence === body.sequence) {
                this._pending.delete(body.kind);
            }
            return;
        }
        if (!this._session)
            return;
        const bytes = Media.decodeFrame(packet, this._session);
        if (!bytes)
            return;
        if (packet.type === Media.AUDIO && this._receiveAudioEnabled) {
            this._audioOutput?.write(bytes);
            this._receivedAudio++;
        } else if (packet.type === Media.VIDEO && this._receiveVideoEnabled) {
            const loader = GdkPixbuf.PixbufLoader.new_with_type('jpeg');
            try {
                loader.write(bytes);
                loader.close();
                this._preview?.set_from_pixbuf(loader.get_pixbuf());
                this._videoOutput?.write(bytes);
                this._receivedVideo++;
            } catch (e) {
                debug(e, 'ZenLink media frame');
                return;
            }
        } else {
            return;
        }
        this.device.sendPacket(Media.control('ack', this._session,
            {kind: packet.type, sequence: body.sequence}));
        this._updateStatus();
    }

    _updateStatus() {
        if (this._status)
            this._status.label = _('Streaming · received %d audio packets, %d camera frames').format(this._receivedAudio, this._receivedVideo) + (this._warning ? `\n${this._warning}` : '');
    }

    _stop(error = null, notifyPeer = true) {
        if (this._session && notifyPeer)
            this.device.sendPacket(Media.control('stop', this._session, {reason: (error || '').slice(0, 240)}));
        this._session = null;
        if (this._timer) {
            GLib.source_remove(this._timer);
            this._timer = null;
        }
        this._pipes.forEach(pipe => pipe.close());
        this._pipes = [];
        this._audioOutput = null;
        this._videoOutput = null;
        this._pending.clear();
        this._restoreAudio();
        for (const id of this._modules.reverse()) {
            try { this._pactl(['unload-module', id]); } catch (e) { debug(e); }
        }
        this._modules = [];
        if (this._window) {
            this._stopButton.sensitive = false;
            this._status.label = error || _('Not streaming');
            this._preview.clear();
        }
    }

    disconnected() { this._stop(_('Device disconnected')); this._remote = null; this._configuredSession = null; super.disconnected(); }
    destroy() { this._stop(); this._window?.destroy(); super.destroy(); }
});

export default MediaPlugin;
