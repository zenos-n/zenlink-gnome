// SPDX-License-Identifier: MIT
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Config from '../config.js';
import {workspaceUnits} from './workspace-units.js';
let transitions = Promise.resolve();

function systemd(method, args = null) {
    return new Promise((resolve, reject) => {
        Gio.DBus.session.call('org.freedesktop.systemd1', '/org/freedesktop/systemd1',
            'org.freedesktop.systemd1.Manager', method, args, null,
            Gio.DBusCallFlags.NONE, 5000, null, (bus, result) => {
                try { resolve(bus.call_finish(result)); } catch (e) { reject(e); }
            });
    });
}
async function unitJob(method, name) {
    await systemd(method, new GLib.Variant('(ss)', [name, 'replace']));
    const reply = await systemd('GetUnit', new GLib.Variant('(s)', [name]));
    const [path] = reply.deepUnpack();
    const deadline = GLib.get_monotonic_time() + 10000000;
    while (GLib.get_monotonic_time() < deadline) {
        const value = await new Promise((resolve, reject) => Gio.DBus.session.call('org.freedesktop.systemd1', path,
            'org.freedesktop.DBus.Properties', 'Get', new GLib.Variant('(ss)', ['org.freedesktop.systemd1.Unit', 'ActiveState']),
            null, Gio.DBusCallFlags.NONE, 3000, null, (bus, result) => {
                try { resolve(bus.call_finish(result).deepUnpack()[0].deepUnpack()); } catch (e) { reject(e); }
            }));
        if (value === (method === 'StopUnit' ? 'inactive' : 'active')) return;
        if (value === 'failed') throw new Error(`ZenLink workspace unit failed: ${name}`);
        await new Promise(resolve => GLib.timeout_add(GLib.PRIORITY_DEFAULT, 50, () => {resolve(); return GLib.SOURCE_REMOVE;}));
    }
    throw new Error(`Timed out waiting for ${name}`);
}
function copyTree(source, target) {
    if (source.query_file_type(Gio.FileQueryInfoFlags.NOFOLLOW_SYMLINKS, null) !== Gio.FileType.DIRECTORY) {
        source.copy(target, Gio.FileCopyFlags.NOFOLLOW_SYMLINKS, null, null); return;
    }
    target.make_directory(null);
    const children = source.enumerate_children('standard::name', Gio.FileQueryInfoFlags.NOFOLLOW_SYMLINKS, null);
    try { let child; while ((child = children.next_file(null))) copyTree(source.get_child(child.get_name()), target.get_child(child.get_name())); }
    finally { children.close(null); }
}
async function uninstallPrototype() {
    const reply = await new Promise((resolve, reject) => Gio.DBus.session.call('org.gnome.Shell.Extensions', '/org/gnome/Shell/Extensions',
        'org.gnome.Shell.Extensions', 'UninstallExtension', new GLib.Variant('(s)', ['quest-workspace@doromiert.dev']),
        null, Gio.DBusCallFlags.NONE, 5000, null, (bus, result) => {
            try { resolve(bus.call_finish(result)); } catch (e) { reject(e); }
        }));
    if (!reply.deepUnpack()[0]) throw new Error('Could not retire the standalone workspace extension');
}
async function migratePrototype() {
    const legacy = 'quest-workspace-host.service';
    if (Gio.File.new_for_path(`${GLib.get_user_config_dir()}/systemd/user/${legacy}`).query_exists(null)) {
        await unitJob('StopUnit', legacy);
        await systemd('DisableUnitFiles', new GLib.Variant('(asb)', [[legacy], false]));
    }
    const settings = new Gio.Settings({schema_id: 'org.gnome.shell'});
    const enabled = settings.get_strv('enabled-extensions');
    if (enabled.includes('quest-workspace@doromiert.dev'))
        settings.set_strv('enabled-extensions', enabled.filter(uuid => uuid !== 'quest-workspace@doromiert.dev'));
    await new Promise(resolve => GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, () => {resolve(); return GLib.SOURCE_REMOVE;}));
    const extension = Gio.File.new_for_path(`${GLib.get_user_data_dir()}/gnome-shell/extensions/quest-workspace@doromiert.dev`);
    const unit = Gio.File.new_for_path(`${GLib.get_user_config_dir()}/systemd/user/${legacy}`);
    const desktop = Gio.File.new_for_path(`${GLib.get_user_data_dir()}/applications/dev.doromiert.QuestWorkspace.PCVR.desktop`);
    const archive = `${GLib.get_user_state_dir()}/zenlink/legacy-workspace/${GLib.get_real_time()}`;
    if (extension.query_exists(null)) {
        GLib.mkdir_with_parents(archive, 0o700);
        copyTree(extension, Gio.File.new_for_path(`${archive}/${extension.get_basename()}`));
        await uninstallPrototype();
    }
    for (const file of [unit, desktop]) {
        if (!file.query_exists(null)) continue;
        GLib.mkdir_with_parents(archive, 0o700);
        file.move(Gio.File.new_for_path(`${archive}/${file.get_basename()}`), Gio.FileCopyFlags.NOFOLLOW_SYMLINKS, null, null);
    }
    await systemd('Reload');
}
async function configure() {
    const child = Gio.Subprocess.new([`${Config.WORKSPACE_BINDIR}/zenlink-workspace-configure`, '--prepare'], Gio.SubprocessFlags.NONE);
    await new Promise((resolve, reject) => child.wait_check_async(null, (proc, result) => {
        try { proc.wait_check_finish(result); resolve(); } catch (e) { reject(e); }
    }));
    const port = Gio.Subprocess.new([`${Config.WORKSPACE_BINDIR}/zenlink-workspace-configure`, '--port'], Gio.SubprocessFlags.STDOUT_PIPE);
    const output = await new Promise((resolve, reject) => port.communicate_utf8_async(null, null, (proc, result) => {
        try { const [, text] = proc.communicate_utf8_finish(result); if (!proc.get_successful()) throw new Error('Workspace configuration failed'); resolve(text); } catch (e) { reject(e); }
    }));
    return Number(output.trim());
}
export function openPreferences() {
    if (!Config.WORKSPACE_BINDIR)
        throw new Error('ZenLink was built without desktop streaming support');
    Gio.Subprocess.new([`${Config.WORKSPACE_BINDIR}/quest-workspace-settings`], Gio.SubprocessFlags.NONE);
}
export class WorkspaceService {
    constructor(bridge) {
        this.bridge = bridge;
        this.active = false;
        this.closed = false;
        this.queue = Promise.resolve();
        this.settings = new Gio.Settings({settings_schema: Config.GSCHEMA.lookup('org.zenos.ZenLink', null), path: '/org/zenos/zenlink/'});
        this.changed = this.settings.connect('changed::enabled', () => this.sync());
        this.sync();
    }
    sync() {
        transitions = transitions.then(async () => {
            const enabled = !this.closed && this.settings.get_boolean('enabled');
            if (!enabled || !Config.WORKSPACE_BINDIR || !Config.WORKSPACE_SUPPORTED) {
                await this.stop(); return;
            }
            if (this.active) return;
            const port = await configure();
            if (this.closed || !this.settings.get_boolean('enabled')) return;
            const directory = `${GLib.get_user_config_dir()}/systemd/user`;
            GLib.mkdir_with_parents(directory, 0o700);
            const units = workspaceUnits(Config.WORKSPACE_BINDIR, GLib.get_user_config_dir(), Config.WORKSPACE_MUTTER_LIBRARY, port);
            for (const [name, contents] of Object.entries(units)) {
                const file = Gio.File.new_for_path(`${directory}/${name}`);
                if (file.query_file_type(Gio.FileQueryInfoFlags.NOFOLLOW_SYMLINKS, null) === Gio.FileType.SYMBOLIC_LINK)
                    continue;
                if (file.query_exists(null)) {
                    const [, previous] = file.load_contents(null);
                    if (!new TextDecoder().decode(previous).includes('Description=ZenLink workspace'))
                        throw new Error(`Preserving an existing custom unit: ${name}`);
                }
                file.replace_contents(contents, null, false, Gio.FileCreateFlags.PRIVATE, null);
            }
            await systemd('Reload');
            if (this.closed || !this.settings.get_boolean('enabled')) return;
            await migratePrototype();
            if (this.closed || !this.settings.get_boolean('enabled')) return;
            this.bridge.enable();
            try {
                await unitJob('StartUnit', 'zenlink-workspace.socket');
                this.active = true;
            } catch (e) { this.bridge.disable(); throw e; }
        }).catch(e => logError(e, 'ZenLink workspace'));
        this.queue = transitions;
    }
    async stop() {
        if (this.active) {
            this.bridge.disable();
            this.active = false;
            for (const unit of ['zenlink-workspace.socket', 'zenlink-workspace.service']) {
                try { await unitJob('StopUnit', unit); }
                catch (e) { if (!e.message.includes('not loaded')) logError(e, 'ZenLink workspace'); }
            }
        }
    }
    destroy() {
        this.closed = true;
        this.settings.disconnect(this.changed);
        transitions = transitions.then(() => this.stop()).catch(e => logError(e, 'ZenLink workspace'));
        this.queue = transitions;
        return transitions.finally(() => this.settings.run_dispose());
    }
}
