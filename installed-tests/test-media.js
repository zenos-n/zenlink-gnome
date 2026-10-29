// SPDX-License-Identifier: GPL-2.0-or-later
import GLib from 'gi://GLib';
import {AUDIO, VIDEO, CONTROL, decodeFrame, jpegDimensions, settingsFromControl} from '../src/service/utils/mediaProtocol.js';

function check(condition, message) {
    if (!condition)
        throw new Error(message);
}

const packet = {type: AUDIO, body: {version: 1, session: 'consent', format: 's16le', rate: 48000,
    channels: 1, data: GLib.base64_encode(new Uint8Array(1920))}};
check(decodeFrame(packet, 'consent').length === 1920, 'Valid audio failed');
check(decodeFrame(packet, 'stale-session') === null, 'Accepted a stale consent session');
for (const changes of [{version: 2}, {rate: 44100}, {channels: 2}, {data: 'AAAA'},
    {data: '#'.repeat(2560)}, {data: 'A'.repeat(2564)}, {data: 'AAAA\n'}, {data: '===='}]) {
    check(decodeFrame({...packet, body: {...packet.body, ...changes}}, 'consent') === null,
        `Accepted invalid audio: ${JSON.stringify(changes).slice(0, 80)}`);
}
// Minimal SOF header; the actual JPEG decoder separately validates image data.
const header = new Uint8Array([0xff, 0xd8, 0xff, 0xc0, 0, 8, 8, 0, 240, 1, 64, 1]);
check(JSON.stringify(jpegDimensions(header)) === '[320,240]', 'JPEG dimensions were parsed incorrectly');
header[9] = 8;
const video = {type: VIDEO, body: {version: 1, session: 'consent', format: 'jpeg', data: GLib.base64_encode(header)}};
check(decodeFrame(video, 'consent') === null, 'Accepted an oversized image');
for (const bytes of [[], [0xff, 0xd8], [0xff, 0xd8, 0xff, 0xc0, 0, 1], [0xff, 0xd8, 0xff, 0xc0, 255, 255]])
    check(jpegDimensions(new Uint8Array(bytes)) === null, 'Accepted a malformed JPEG header');
print('Media protocol: consent, format, base64, audio length and image bounds passed');

const configuration = {type: CONTROL, body: {version: 1, action: 'configure', session: 'phone-session',
    desktopAudio: true, desktopVideo: false, phoneAudio: true, phoneVideo: true}};
check(settingsFromControl(configuration, null) === null, 'Accepted configuration before readiness');
check(settingsFromControl(configuration, 'stale') === null, 'Accepted a stale configuration');
check(settingsFromControl({...configuration, body: {...configuration.body, desktopVideo: 'true'}}, 'phone-session') === null,
    'Accepted a non-boolean camera request');
const settings = settingsFromControl(configuration, 'phone-session');
check(settings.sendAudio && !settings.sendVideo && settings.receiveAudio && settings.receiveVideo && settings.virtualMic && settings.virtualCamera,
    'Default phone configuration did not produce the requested routes');
print('Phone control: session gating, field types and default routes passed');
