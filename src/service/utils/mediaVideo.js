// SPDX-License-Identifier: GPL-2.0-or-later
export const PREVIEW_WIDTH = 640;
export const PREVIEW_HEIGHT = 360;
export const PREVIEW_BYTES = PREVIEW_WIDTH * PREVIEW_HEIGHT * 3;

// Gst writes a byte stream: stdout read boundaries are not RGB frame boundaries.
export class VideoFrames {
    constructor(callback) {
        this.callback = callback;
        this.frame = new Uint8Array(PREVIEW_BYTES);
        this.offset = 0;
    }

    push(bytes) {
        let offset = 0;
        while (offset < bytes.length) {
            const count = Math.min(PREVIEW_BYTES - this.offset, bytes.length - offset);
            this.frame.set(bytes.subarray(offset, offset + count), this.offset);
            this.offset += count;
            offset += count;
            if (this.offset === PREVIEW_BYTES) {
                this.callback(this.frame);
                this.frame = new Uint8Array(PREVIEW_BYTES);
                this.offset = 0;
            }
        }
    }
}

export function cameraPipeline(format, rotation, id, virtualPath = null) {
    const h264 = format === 'h264';
    const flip = {0: 'none', 90: 'clockwise', 180: 'rotate-180', 270: 'counterclockwise'}[rotation];
    if (!flip)
        throw new Error('Invalid camera rotation');
    const args = ['fdsrc', 'fd=0', 'do-timestamp=true', '!'];
    if (h264)
        args.push('h264parse', '!', 'video/x-h264,width=(int)[1,1280],height=(int)[1,720]', '!', 'openh264dec', '!', 'capssetter', 'caps=video/x-raw,framerate=30/1', '!');
    else
        args.push('image/jpeg,framerate=5/1', '!', 'jpegparse', '!', 'jpegdec', '!');
    args.push('videoconvert', '!', 'videoflip', `method=${flip}`, '!', 'videoscale', 'add-borders=true', '!',
        `video/x-raw,format=I420,width=1280,height=720,framerate=${h264 ? 30 : 5}/1`, '!', 'tee', 'name=camera');
    if (virtualPath) {
        args.push('camera.', '!', 'queue', 'max-size-buffers=2', 'leaky=downstream', '!',
            'videoconvert', '!', 'video/x-raw,format=YUY2', '!', 'v4l2sink', `device=${virtualPath}`, 'sync=false', 'async=false');
    }
    // Snapshot/portals use PipeWire. The V4L2 monitor can cache exclusive_caps as output-only at boot.
    args.push('camera.', '!', 'queue', 'max-size-buffers=2', 'leaky=downstream', '!',
        'pipewiresink', 'mode=provide', 'client-name=ZenLink-Camera', 'use-bufferpool=false',
        `stream-properties="props,media.class=Video/Source,media.type=Video,media.category=Capture,media.role=Camera,node.name=zenlink_camera_${id},node.description=\\"ZenLink Phone\\",node.virtual=true"`,
        'sync=false', 'async=false');
    args.push('camera.', '!', 'queue', 'max-size-buffers=2', 'leaky=downstream', '!',
        'videoconvert', '!', 'videoscale', '!',
        `video/x-raw,format=RGB,width=${PREVIEW_WIDTH},height=${PREVIEW_HEIGHT}`, '!', 'fdsink', 'fd=1', 'sync=false', 'async=false');
    return args;
}
