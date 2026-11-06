// Ported from Quest Workspace, copyright2026doromiert, MIT.
// SPDX-License-Identifier: MIT
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Shell from 'gi://Shell';
import Meta from 'gi://Meta';
import Clutter from 'gi://Clutter';
import Graphene from 'gi://Graphene';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';


const XML = `<node><interface name="dev.doromiert.QuestWorkspace">
<method name="List"><arg type="s" direction="out"/></method>
<method name="Focus"><arg type="u" direction="in"/><arg type="b" direction="out"/></method>
<method name="Resize"><arg type="u" direction="in"/><arg type="u" direction="in"/><arg type="u" direction="in"/><arg type="b" direction="out"/></method>
<method name="Restore"><arg type="u" direction="in"/><arg type="b" direction="out"/></method>
<method name="Float"><arg type="u" direction="in"/><arg type="b" direction="out"/></method>
<method name="Canvas"><arg type="u" direction="in"/><arg type="b" direction="out"/></method>
<method name="Home"><arg type="u" direction="in"/><arg type="b" direction="out"/></method>
<method name="TouchDevice"><arg type="b" direction="out"/></method>
</interface></node>`;

export default class WorkspaceBridge {
    enable() {
        this._owner = null;
        this._touchDevice = null;
        this._floated = new Map();
        this._forgeHooks = new Map();
        this._barriers = [];
        this._previewChildren = new Map();
        this._mapHook = global.window_manager.connect('map', (_wm, actor) => {
            GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, () => {
                this._keepWindowVisible(actor);
                return GLib.SOURCE_REMOVE;
            });
        });
        this._watch = Gio.bus_watch_name(Gio.BusType.SESSION,
            'dev.doromiert.QuestWorkspace.Host', Gio.BusNameWatcherFlags.NONE,
            (_bus, _name, owner) => { this._owner = owner; },
            () => { this._owner = null; this._restoreAll(); });
        this._object = Gio.DBusExportedObject.wrapJSObject(XML, this);
        this._object.export(Gio.DBus.session, '/dev/doromiert/QuestWorkspace');
    }

    _windows() {
        if (Main.sessionMode.isLocked || Main.sessionMode.isGreeter)
            return [];
        return global.get_window_actors().map(actor => actor.meta_window)
            .filter(window => !window.is_override_redirect() && !window.skip_taskbar);
    }

    ListAsync(_args, invocation) {
        if (!this._authorized(invocation)) return;
        const tracker = Shell.WindowTracker.get_default();
        const windows = this._windows().map(window => {
            const rect = window.get_frame_rect();
            const buffer = window.get_buffer_rect();
            const monitor = global.display.get_monitor_geometry(Math.max(0,window.get_monitor()));
            const scale = Math.max(1, Math.ceil(window.get_compositor_private().get_resource_scale()));
            const preview = this._floated.get(window.get_stable_sequence());
            const actor = window.get_compositor_private().get_first_child() ?? window.get_compositor_private();
            const [px,py] = actor.get_transformed_position();
            const [pw,ph] = actor.get_transformed_size();
            const home = global.display.get_monitor_geometry(preview?.previewMonitor ?? Math.max(0,window.get_monitor()));
            return {id: window.get_stable_sequence(), native_id: String(window.get_id()),
                title: window.get_title() ?? '', app: tracker.get_window_app(window)?.get_name() ?? '',
                width: rect.width, height: rect.height, minimized: window.minimized,
                monitor_width: monitor.width, monitor_height: monitor.height,
                desktop_preview_scale: preview?.previewScale ?? 1,
                desktop_preview_visible: px>=home.x-1 && py>=home.y-1 && px+pw<=home.x+home.width+1 && py+ph<=home.y+home.height+1,
                scale, inset_x: Math.max(0, rect.x-buffer.x)*scale,
                inset_y: Math.max(0, rect.y-buffer.y)*scale};
        });
        invocation.return_value(new GLib.Variant('(s)', [JSON.stringify(windows)]));
    }

    FocusAsync([id], invocation) {
        if (!this._authorized(invocation)) return;
        const window = this._windows().find(item => item.get_stable_sequence() === id);
        if (window) { window.unminimize(); Main.activateWindow(window); }
        invocation.return_value(new GLib.Variant('(b)', [Boolean(window)]));
    }

    ResizeAsync([id, width, height], invocation) {
        if (!this._authorized(invocation)) return;
        const window = this._windows().find(item => item.get_stable_sequence() === id);
        const valid = window && window.allows_resize() && width >= 320 && width <= 4096
            && height >= 240 && height <= 4096;
        if (valid) {
            this._float(window);
            window.set_unmaximize_flags(Meta.MaximizeFlags.BOTH);
            window.unmaximize();
            if (window.fullscreen) window.unmake_fullscreen();
            const rect = window.get_frame_rect();
            window.move_resize_frame(true, rect.x, rect.y, width, height);
        }
        invocation.return_value(new GLib.Variant('(b)', [Boolean(valid)]));
    }

    RestoreAsync([id], invocation) {
        if (!this._authorized(invocation)) return;
        invocation.return_value(new GLib.Variant('(b)', [this._restore(id)]));
    }

    FloatAsync([id], invocation) {
        if (!this._authorized(invocation)) return;
        const window = this._windows().find(item => item.get_stable_sequence() === id);
        if (window) this._float(window);
        invocation.return_value(new GLib.Variant('(b)', [Boolean(window)]));
    }

    CanvasAsync([id], invocation) {
        if (!this._authorized(invocation)) return;
        const window = this._windows().find(item => item.get_stable_sequence() === id);
        let index = -1, matches = 0;
        for (let i=0; i<global.display.get_n_monitors(); i++) {
            const rect = global.display.get_monitor_geometry(i);
            if (rect.width === 4352 && rect.height === 4352) { index=i; matches++; }
        }
        if (!window || matches !== 1) {
            invocation.return_value(new GLib.Variant('(b)',[false]));
            return;
        }
        this._float(window);
        this._floated.get(id).canvas = true;
        this._syncBarriers();
        window.set_unmaximize_flags(Meta.MaximizeFlags.BOTH);
        window.unmaximize();
        window.move_to_monitor(index);
        const rect = global.display.get_monitor_geometry(index);
        window.move_frame(true,rect.x+64,rect.y+64);
        const deadline = GLib.get_monotonic_time()+1500000;
        GLib.timeout_add(GLib.PRIORITY_DEFAULT,16,() => {
            const frame = window.get_frame_rect();
            const ready = this._floated.has(id) && window.get_monitor() === index
                && frame.x >= rect.x && frame.y >= rect.y;
            if (ready || !this._floated.has(id) || GLib.get_monotonic_time()>deadline) {
                invocation.return_value(new GLib.Variant('(b)',[ready]));
                return GLib.SOURCE_REMOVE;
            }
            return GLib.SOURCE_CONTINUE;
        });
    }

    TouchDeviceAsync(_args, invocation) {
        if (!this._authorized(invocation)) return;
        // Advertise the seat capability before the first contact, without
        // generating a dummy tap. Wayland clients need time to bind wl_touch.
        try {
            this._touchDevice ??= global.stage.context.get_backend().get_default_seat()
                .create_virtual_device(Clutter.InputDeviceType.TOUCHSCREEN_DEVICE);
            invocation.return_value(new GLib.Variant('(b)', [Boolean(this._touchDevice)]));
        } catch (error) {
            invocation.return_dbus_error('org.freedesktop.DBus.Error.Failed', String(error));
        }
    }

    HomeAsync([id], invocation) {
        if (!this._authorized(invocation)) return;
        const entry = this._floated.get(id);
        if (!entry?.window.get_compositor_private()) {
            invocation.return_value(new GLib.Variant('(b)', [false]));
            return;
        }
        const original = entry.rect;
        const x = original.x + original.width/2, y = original.y + original.height/2;
        let index = -1;
        for (let i=0; i<global.display.get_n_monitors(); i++) {
            const rect = global.display.get_monitor_geometry(i);
            if (rect.width === 4352 && rect.height === 4352) continue;
            if (index < 0) index = i;
            if (x>=rect.x && x<rect.x+rect.width && y>=rect.y && y<rect.y+rect.height) { index=i; break; }
        }
        if (index >= 0) {
            entry.previewMonitor = index;
            if (!entry.fitSignal)
                entry.fitSignal = entry.window.connect('size-changed', () => this._fitDesktop(entry));
            if (!entry.positionSignal)
                entry.positionSignal = entry.window.connect('position-changed', () => this._fitDesktop(entry));
            this._fitDesktop(entry);
        }
        invocation.return_value(new GLib.Variant('(b)', [index >= 0]));
    }

    _fitDesktop(entry) {
        const actor = entry.window.get_compositor_private();
        if (!actor || entry.previewMonitor === undefined) return;
        const work = entry.window.get_workspace().get_work_area_for_monitor(entry.previewMonitor);
        const frame = entry.window.get_frame_rect();
        const buffer = entry.window.get_buffer_rect();
        const scale = Math.min(1,(work.width-32)/Math.max(1,buffer.width),(work.height-32)/Math.max(1,buffer.height));
        entry.previewScale = scale;
        const transform = new Graphene.Matrix();
        transform.init_from_matrix(entry.childTransform);
        transform.scale(scale,scale,1);
        const x = Math.max(work.x+16,Math.min(entry.rect.x,work.x+work.width-buffer.width*scale-16));
        const y = Math.max(work.y+16,Math.min(entry.rect.y,work.y+work.height-buffer.height*scale-16));
        entry.previewX = x;
        entry.previewY = y;
        transform.translate(new Graphene.Point3D({x:x-buffer.x,y:y-buffer.y,z:0}));
        actor.set_child_transform(transform);
        for (const [child,preview] of this._previewChildren)
            if (preview.parent===entry) this._keepWindowVisible(child);
    }

    _syncBarriers() {
        for (const barrier of this._barriers) barrier.destroy();
        this._barriers = [];
        if (![...this._floated.values()].some(entry => entry.canvas)) return;
        const physical = [];
        for (let i=0;i<global.display.get_n_monitors();i++) {
            const rect=global.display.get_monitor_geometry(i);
            if (rect.width!==4352 || rect.height!==4352) physical.push(rect);
        }
        for (const rect of physical) {
            for (const [vertical,coordinate,start,end,direction] of [
                [true,rect.x,rect.y,rect.y+rect.height,Meta.BarrierDirection.POSITIVE_X],
                [true,rect.x+rect.width,rect.y,rect.y+rect.height,Meta.BarrierDirection.NEGATIVE_X],
                [false,rect.y,rect.x,rect.x+rect.width,Meta.BarrierDirection.POSITIVE_Y],
                [false,rect.y+rect.height,rect.x,rect.x+rect.width,Meta.BarrierDirection.NEGATIVE_Y]]) {
                let segments=[[start,end]];
                for (const other of physical) {
                    if (other===rect) continue;
                    const adjacent=vertical ? coordinate===other.x || coordinate===other.x+other.width
                        : coordinate===other.y || coordinate===other.y+other.height;
                    if (!adjacent) continue;
                    const a=vertical?other.y:other.x, b=a+(vertical?other.height:other.width);
                    segments=segments.flatMap(([lo,hi]) => {
                        if (b<=lo || a>=hi) return [[lo,hi]];
                        const result=[];
                        if (a>lo) result.push([lo,a]);
                        if (b<hi) result.push([b,hi]);
                        return result;
                    });
                }
                for (const [lo,hi] of segments) this._barriers.push(new Meta.Barrier({backend:global.backend,
                    x1:vertical?coordinate:lo,y1:vertical?lo:coordinate,
                    x2:vertical?coordinate:hi,y2:vertical?hi:coordinate,directions:direction}));
            }
        }
        const [x,y]=global.get_pointer();
        if (physical.length && !physical.some(r=>x>=r.x && x<r.x+r.width && y>=r.y && y<r.y+r.height)) {
            const r=physical[0];
            global.stage.context.get_backend().get_default_seat().warp_pointer(r.x+r.width/2,r.y+r.height/2);
        }
    }

    _keepWindowVisible(actor) {
        if (!actor?.meta_window || ![...this._floated.values()].some(entry=>entry.canvas)) return;
        const window=actor.meta_window;
        if (this._floated.get(window.get_stable_sequence())?.canvas) return;
        let parent=window.get_transient_for();
        let entry=null;
        while (parent && !entry) {
            entry=this._floated.get(parent.get_stable_sequence());
            if (!entry?.canvas) { entry=null; parent=parent.get_transient_for(); }
        }
        if (entry?.previewMonitor !== undefined && entry) {
            const buffer=window.get_buffer_rect(), origin=entry.window.get_buffer_rect();
            const previous=this._previewChildren.get(actor);
            const old=previous?.transform ?? actor.get_child_transform();
            const transform=new Graphene.Matrix(); transform.init_from_matrix(old);
            transform.scale(entry.previewScale,entry.previewScale,1);
            transform.translate(new Graphene.Point3D({x:entry.previewX+(buffer.x-origin.x)*entry.previewScale-buffer.x,
                y:entry.previewY+(buffer.y-origin.y)*entry.previewScale-buffer.y,z:0}));
            actor.set_child_transform(transform);
            if (!previous) {
                const destroy=actor.connect('destroy',()=>this._previewChildren.delete(actor));
                this._previewChildren.set(actor,{transform:old,parent:entry,destroy});
            }
            return;
        }
        const monitor=global.display.get_monitor_geometry(Math.max(0,window.get_monitor()));
        if (monitor.width!==4352 || monitor.height!==4352) return;
        for (let i=0;i<global.display.get_n_monitors();i++) {
            const rect=global.display.get_monitor_geometry(i);
            if (rect.width===4352 && rect.height===4352) continue;
            window.move_to_monitor(i);
            const frame=window.get_frame_rect();
            window.move_resize_frame(true,rect.x+32,rect.y+48,
                Math.min(frame.width,rect.width-64),Math.min(frame.height,rect.height-96));
            break;
        }
    }

    _float(window) {
        const id = window.get_stable_sequence();
        if (this._floated.has(id)) { window.unminimize(); return; }
        const wm = Main.extensionManager.lookup('forge@jmmaranan.com')?.stateObj?.extWm;
        const node = wm?.findNodeWindow(window);
        if (node && !this._forgeHooks.has(wm)) {
            const original = wm.isFloatingExempt;
            const owner = this;
            const wrapper = function(candidate) {
                return owner._floated.has(candidate?.get_stable_sequence()) || original.call(this, candidate);
            };
            this._forgeHooks.set(wm, {original, wrapper});
            wm.isFloatingExempt = wrapper;
        }
        this._floated.set(id, {wm, node, window, mode: node?.mode, above: window.is_above(), minimized: window.minimized,
            rect: window.get_frame_rect(), monitor: window.get_monitor(), canvas: false,
            actorScale: window.get_compositor_private().get_scale(),
            childTransform: window.get_compositor_private().get_child_transform()});
        window.unminimize();
        if (node) {
            node.float = true;
            wm.renderTree('quest-workspace-float');
        }
    }

    _restore(id) {
        const entry = this._floated.get(id);
        if (!entry) return false;
        this._floated.delete(id);
        if (entry.fitSignal) entry.window.disconnect(entry.fitSignal);
        if (entry.positionSignal) entry.window.disconnect(entry.positionSignal);
        for (const [child,preview] of this._previewChildren) {
            if (preview.parent!==entry) continue;
            child.disconnect(preview.destroy);
            child.set_child_transform(preview.transform);
            this._previewChildren.delete(child);
        }
        const actor = entry.window.get_compositor_private();
        if (actor && entry.childTransform) actor.set_child_transform(entry.childTransform);
        if (actor && entry.actorScale) actor.set_scale(...entry.actorScale);
        if (entry.canvas && entry.window.get_compositor_private()) {
            const original = entry.rect;
            for (let i=0; i<global.display.get_n_monitors(); i++) {
                const rect = global.display.get_monitor_geometry(i);
                const x = original.x+original.width/2, y = original.y+original.height/2;
                if (x>=rect.x && x<rect.x+rect.width && y>=rect.y && y<rect.y+rect.height) {
                    entry.window.move_to_monitor(i);
                    break;
                }
            }
            entry.window.move_resize_frame(true,original.x,original.y,original.width,original.height);
        }
        if (![...this._floated.values()].some(other => other.wm === entry.wm)) {
            const hook = this._forgeHooks.get(entry.wm);
            if (hook && entry.wm.isFloatingExempt === hook.wrapper) entry.wm.isFloatingExempt = hook.original;
            this._forgeHooks.delete(entry.wm);
        }
        if (entry.wm && Main.extensionManager.lookup('forge@jmmaranan.com')?.stateObj?.extWm === entry.wm && entry.wm.findNodeWindow(entry.window) === entry.node) {
            entry.node.mode = entry.mode;
            if (!entry.above && entry.window.is_above()) entry.window.unmake_above();
            entry.wm.renderTree('quest-workspace-restore');
        }
        if (entry.minimized && entry.window.get_compositor_private()) entry.window.minimize();
        this._syncBarriers();
        return true;
    }

    _restoreAll() {
        for (const id of this._floated.keys()) this._restore(id);
        this._touchDevice = null;
    }

    _authorized(invocation) {
        if (this._owner && invocation.get_sender() === this._owner) return true;
        invocation.return_dbus_error('org.freedesktop.DBus.Error.AccessDenied', 'Only the workspace daemon may call this bridge');
        return false;
    }

    disable() {
        this._restoreAll();
        this._object?.unexport();
        if (this._watch) Gio.bus_unwatch_name(this._watch);
        if (this._mapHook) global.window_manager.disconnect(this._mapHook);
        this._object = null;
        this._watch = 0;
        this._owner = null;
    }
}
