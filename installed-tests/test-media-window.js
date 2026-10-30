// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the production sender and ACK handler with delayed acknowledgements.
import GLib from 'gi://GLib';
const source = new TextDecoder().decode(GLib.file_get_contents(ARGV[0])[1]);
// Importing the plugin starts desktop UI infrastructure; extract just these
// actual methods so this deterministic transport test needs no session/devices.
function method(name, next) {
    const start = source.indexOf(`    ${name}(`);
    const end = source.indexOf(`\n    ${next}(`, start);
    if (start < 0 || end < 0) throw new Error(`Missing production method ${name}`);
    return source.slice(start, end).trim();
}
const Media = {AUDIO: 'zenlink.media.audio', VIDEO: 'zenlink.media.video', CONTROL: 'zenlink.media.control', RATE: 48000};
const factory = new Function('GLib', 'Media', `return ({${method('_send', 'handlePacket')},${method('handlePacket', '_updateStatus')}});`);
const p = factory(GLib, Media);
const packets = [];
p._session = 'desktop';
p._remote = {session:'phone', audio:true, video:true, seen:GLib.get_monotonic_time()/1000};
p._pending = new Map();p._sequence = 0;p.device = {sendPacket: packet => packets.push(packet)};
const bytes = new Uint8Array(1920);
for (let i=0;i<4;i++) p._send(Media.AUDIO, bytes);
if (packets.length !== 4) throw new Error('Audio burst lost frames while awaiting ACK');
p._send(Media.AUDIO, bytes);
if (packets.length !== 4) throw new Error('Audio in-flight window is unbounded');
function ack(sequence, session='phone') { p.handlePacket({type:Media.CONTROL, body:{version:1, action:'ack', session, kind:Media.AUDIO, sequence}}); }
ack(2, 'wrong');p._send(Media.AUDIO, bytes);
if (packets.length !== 4) throw new Error('Wrong session released audio credit');
ack(2);p._send(Media.AUDIO, bytes);
if (packets.length !== 5 || p._pending.get(Media.AUDIO).size !== 4) throw new Error('Out-of-order ACK did not release exactly one credit');
ack(2);p._send(Media.AUDIO, bytes);
if (packets.length !== 5) throw new Error('Duplicate ACK released credit twice');
p._send(Media.VIDEO, new Uint8Array(10));p._send(Media.VIDEO, new Uint8Array(10));
if (packets.length !== 6) throw new Error('Video stop-and-wait changed');
for (const seq of p._pending.get(Media.AUDIO).keys()) p._pending.get(Media.AUDIO).set(seq, 0);
p._send(Media.AUDIO, bytes);
if (packets.length !== 7) throw new Error('Expired window never recovers');
print('Audio burst/limit/ACK/session/timeout and video flow-control tests passed');
