// SPDX-License-Identifier: GPL-2.0-or-later
import GLib from 'gi://GLib';

export const CONTROL = 'zenlink.media.control';
export const AUDIO = 'zenlink.media.audio';
export const VIDEO = 'zenlink.media.video';
export const RATE = 48000;
export const AUDIO_BYTES = 1920;
export const MAX_JPEG = 49152;
export const MAX_H264 = 262144;

export function jpegDimensions(bytes) {
    if (bytes.length < 4 || bytes[0] !== 0xff || bytes[1] !== 0xd8)
        return null;
    let offset = 2;
    while (offset + 4 <= bytes.length) {
        if (bytes[offset++] !== 0xff)
            return null;
        while (bytes[offset] === 0xff)
            offset++;
        const marker = bytes[offset++];
        if (marker === 0xda || marker === 0xd9)
            return null;
        if (marker === 0x01 || (marker >= 0xd0 && marker <= 0xd7))
            continue;
        const length = (bytes[offset] << 8) | bytes[offset + 1];
        if (length < 2 || offset + length > bytes.length)
            return null;
        if ([0xc0, 0xc1, 0xc2, 0xc3, 0xc5, 0xc6, 0xc7, 0xc9, 0xca, 0xcb, 0xcd, 0xce, 0xcf].includes(marker)) {
            if (length < 8)
                return null;
            return [(bytes[offset + 5] << 8) | bytes[offset + 6],
                (bytes[offset + 3] << 8) | bytes[offset + 4]];
        }
        offset += length;
    }
    return null;
}

export function decodeFrame(packet, session) {
    const body = packet.body;
    if (!body || body.version !== 1 || body.session !== session)
        return null;
    let limit;
    if (packet.type === AUDIO) {
        if (body.rate !== RATE || body.channels !== 1 || body.format !== 's16le')
            return null;
        limit = AUDIO_BYTES;
    } else if (packet.type === VIDEO && body.format === 'h264') {
        if (!Number.isInteger(body.width) || body.width < 1 || body.width > 1280 ||
            !Number.isInteger(body.height) || body.height < 1 || body.height > 720 ||
            ![0, 90, 180, 270].includes(body.rotation) || body.fps !== 30 ||
            typeof body.keyframe !== 'boolean' || !Number.isSafeInteger(body.sequence) || body.sequence < 1)
            return null;
        limit = MAX_H264;
    } else if (packet.type === VIDEO && body.format === 'jpeg') {
        limit = MAX_JPEG;
    } else {
        return null;
    }
    if (typeof body.data !== 'string' || !body.data.length ||
        body.data.length > Math.ceil(limit / 3) * 4 || body.data.length % 4 !== 0 ||
        !/^[A-Za-z0-9+/]*={0,2}$/.test(body.data))
        return null;
    const bytes = GLib.base64_decode(body.data);
    if (bytes.length > limit || (packet.type === AUDIO && bytes.length !== AUDIO_BYTES))
        return null;
    if (packet.type === VIDEO && body.format === 'h264') {
        // Annex B access units, never an arbitrary container or unbounded file.
        if (bytes.length < 5 || bytes[0] !== 0 || bytes[1] !== 0 ||
            !((bytes[2] === 1) || (bytes[2] === 0 && bytes[3] === 1)))
            return null;
    } else if (packet.type === VIDEO) {
        const dimensions = jpegDimensions(bytes);
        if (!dimensions || dimensions.some(size => size < 1 || size > 640))
            return null;
    }
    return bytes;
}

export function control(action, session, extra = {}) {
    return {type: CONTROL, body: {version: 1, action, session, ...extra}};
}

export function settingsFromControl(packet, remoteSession) {
    const body = packet.body;
    if (packet.type !== CONTROL || !remoteSession || body?.version !== 1 ||
        body.action !== 'configure' || body.session !== remoteSession ||
        !['desktopAudio', 'desktopVideo', 'phoneAudio', 'phoneVideo'].every(key => typeof body[key] === 'boolean'))
        return null;
    if (body.phoneVideoFormat !== undefined && body.phoneVideoFormat !== 'h264')
        return null;
    return {videoFormat: body.phoneVideoFormat || 'jpeg', sendAudio: body.desktopAudio, sendVideo: body.desktopVideo,
        receiveAudio: body.phoneAudio, receiveVideo: body.phoneVideo,
        audioSource: 'system', virtualMic: true, virtualCamera: true};
}
