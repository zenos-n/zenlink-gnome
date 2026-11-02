// SPDX-License-Identifier: GPL-2.0-or-later
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';

// Keep process I/O asynchronous so audio devices cannot block the service.
export class MediaPipe {
    constructor(args, onError, onBytes = null) {
        this.active = true;
        this.busy = false;
        this.queue = [];
        this.errorText = '';
        this.cancel = new Gio.Cancellable();
        let flags = Gio.SubprocessFlags.STDERR_PIPE | Gio.SubprocessFlags.STDIN_PIPE;
        if (onBytes)
            flags |= Gio.SubprocessFlags.STDOUT_PIPE;
        this.process = Gio.Subprocess.new(args, flags);
        this._read(this.process.get_stderr_pipe(), bytes => {
            if (this.errorText.length < 4096)
                this.errorText += new TextDecoder().decode(bytes);
        }, onError);
        if (onBytes)
            this._read(this.process.get_stdout_pipe(), onBytes, onError);
        this.process.wait_async(null, (process, result) => {
            process.wait_finish(result);
            if (this.active && !process.get_successful())
                onError(this.errorText.trim() || 'The media device could not be opened');
        });
        this.onError = onError;
    }

    _read(stream, callback, onError) {
        stream.read_bytes_async(16384, GLib.PRIORITY_DEFAULT, this.cancel, (source, result) => {
            try {
                const bytes = source.read_bytes_finish(result).toArray();
                if (!this.active || bytes.length === 0)
                    return;
                callback(bytes);
                this._read(stream, callback, onError);
            } catch (e) {
                if (this.active)
                    onError(e.message);
            }
        });
    }

    enqueue(bytes, onWritten) {
        if (!this.active || this.queue.length + (this.busy ? 1 : 0) >= 4)
            return false;
        if (this.busy)
            this.queue.push({bytes, onWritten});
        else
            this.write(bytes, onWritten);
        return true;
    }

    write(bytes, onWritten = null) {
        if (!this.active || this.busy)
            return false;
        this.busy = true;
        this.process.get_stdin_pipe().write_all_async(bytes, GLib.PRIORITY_DEFAULT,
            this.cancel, (stream, result) => {
                try {
                    stream.write_all_finish(result);
                    onWritten?.();
                } catch (e) {
                    if (this.active)
                        this.onError(e.message);
                } finally {
                    this.busy = false;
                    const next = this.queue.shift();
                    if (next && this.active)
                        this.write(next.bytes, next.onWritten);
                }
            });
        return true;
    }

    close() {
        this.active = false;
        this.queue.length = 0;
        this.cancel.cancel();
        this.process.force_exit();
    }
}
