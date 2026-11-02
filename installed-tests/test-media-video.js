// SPDX-License-Identifier: GPL-2.0-or-later
import {cameraPipeline, VideoFrames, PREVIEW_BYTES} from '../src/service/utils/mediaVideo.js';
const frames = [];
const reader = new VideoFrames(frame => frames.push(frame));
const bytes = new Uint8Array(PREVIEW_BYTES * 3);
for (let i = 0; i < bytes.length; i++) bytes[i] = Math.floor(i / PREVIEW_BYTES) + 1;
for (let offset = 0; offset < bytes.length; offset += 16381)
    reader.push(bytes.subarray(offset, offset + 16381));
if (frames.length !== 3 || frames.some((f, i) => f.some(byte => byte !== i + 1)))
    throw new Error('Raw preview frames were corrupted by pipe read boundaries');
const args = cameraPipeline('h264', 90, 'fixture', '/dev/video10');
if (!args.includes('openh264dec') || !args.includes('mode=provide') || !args.includes('method=clockwise') ||
    !args.includes('device=/dev/video10') || !args.some(x => x.includes('framerate=30/1')))
    throw new Error('Camera pipeline lost AVC/30fps/rotation/PipeWire/V4L2 support');
let rejected = false;
try { cameraPipeline('h264', 1, 'fixture'); } catch (e) { rejected = true; }
if (!rejected) throw new Error('Invalid rotation was accepted');
print('Video preview framing and camera pipeline checks passed');
