// SPDX-License-Identifier: MIT
// Run only on a private session bus with memory settings and private XDG dirs.
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import system from 'system';
const prefix = system.programArgs[0];
const {setup} = await import(`file://${prefix}/utils/setup.js`);
const {default: Config} = await import(`file://${prefix}/config.js`);
setup(prefix);
const {WorkspaceService} = await import(`file://${prefix}/utils/workspace.js`);
function check(value, message) { if (!value) throw new Error(message); }
check(GLib.getenv('GSETTINGS_BACKEND') === 'memory', 'use memory settings');
check(GLib.get_user_config_dir().startsWith('/tmp/'), 'use private XDG dirs');
const bus = Gio.bus_get_sync(Gio.BusType.SESSION, null);
const unitPaths = new Map();const unitStates = new Map();const calls = [];
const objects = [];
const unitXML = '<node><interface name="org.freedesktop.systemd1.Unit"><property name="ActiveState" type="s" access="read"/></interface></node>';
function unitPath(name) {
    if (!unitPaths.has(name)) {
        const path = `/fixture/unit${unitPaths.size}`;unitPaths.set(name,path);
        const object = Gio.DBusExportedObject.wrapJSObject(unitXML, {get ActiveState(){return unitStates.get(name) ?? 'inactive';}});
        object.export(bus,path);objects.push(object);
    }
    return unitPaths.get(name);
}
const managerXML = `<node><interface name="org.freedesktop.systemd1.Manager">
<method name="Reload"/><method name="GetUnit"><arg type="s" direction="in"/><arg type="o" direction="out"/></method>
<method name="StartUnit"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="o" direction="out"/></method>
<method name="StopUnit"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="o" direction="out"/></method>
<method name="DisableUnitFiles"><arg type="as" direction="in"/><arg type="b" direction="in"/><arg type="a(sss)" direction="out"/></method>
</interface></node>`;
const manager = Gio.DBusExportedObject.wrapJSObject(managerXML, {
    Reload(){calls.push(['reload']);},
    GetUnit(name){return unitPath(name);},
    StartUnit(name){calls.push(['start',name]);unitStates.set(name,'active');return '/fixture/job';},
    StopUnit(name){calls.push(['stop',name]);unitStates.set(name,'inactive');return '/fixture/job';},
    DisableUnitFiles(names){calls.push(['disable',...names]);return [];},
});manager.export(bus,'/org/freedesktop/systemd1');objects.push(manager);
let uninstalled=false;
const extensionPath=`${GLib.get_user_data_dir()}/gnome-shell/extensions/quest-workspace@doromiert.dev`;
const extensionAPI=Gio.DBusExportedObject.wrapJSObject('<node><interface name="org.gnome.Shell.Extensions"><method name="UninstallExtension"><arg type="s" direction="in"/><arg type="b" direction="out"/></method></interface></node>', {
    UninstallExtension(uuid){check(uuid==='quest-workspace@doromiert.dev','only prototype can be retired');Gio.File.new_for_path(`${extensionPath}/metadata.json`).delete(null);Gio.File.new_for_path(extensionPath).delete(null);uninstalled=true;return true;},
});extensionAPI.export(bus,'/org/gnome/Shell/Extensions');objects.push(extensionAPI);
for (const name of ['org.freedesktop.systemd1','org.gnome.Shell.Extensions'])
    bus.call_sync('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','RequestName',new GLib.Variant('(su)',[name,4]),null,Gio.DBusCallFlags.NONE,-1,null);
GLib.mkdir_with_parents(extensionPath,0o700);GLib.file_set_contents(`${extensionPath}/metadata.json`,'{"uuid":"quest-workspace@doromiert.dev"}');
GLib.mkdir_with_parents(`${GLib.get_user_config_dir()}/systemd/user`,0o700);GLib.file_set_contents(`${GLib.get_user_config_dir()}/systemd/user/quest-workspace-host.service`,'old unit');
const shellSettings=new Gio.Settings({schema_id:'org.gnome.shell'});shellSettings.set_strv('enabled-extensions',['other@example','quest-workspace@doromiert.dev']);
const settings=new Gio.Settings({settings_schema:Config.GSCHEMA.lookup('org.zenos.ZenLink',null),path:'/org/zenos/zenlink/'});
await (async()=>{
    const bridge={enabled:false,starts:0,enable(){check(!this.enabled,'bridge enabled twice');this.enabled=true;this.starts++;},disable(){this.enabled=false;}};
    const controller=new WorkspaceService(bridge);await controller.queue;
    check(controller.active&&bridge.enabled,'integration starts socket and bridge');
    check(uninstalled&&!Gio.File.new_for_path(extensionPath).query_exists(null),'prototype extension retired');
    check(shellSettings.get_strv('enabled-extensions').join(',')==='other@example','preserve other extensions');
    check(calls.some(c=>c[0]==='disable'&&c[1]==='quest-workspace-host.service'),'legacy service disabled');
    check(!calls.some(c=>c[0]==='start'&&c[1]==='zenlink-workspace.service'),'native backend stays stopped while idle');
    const [,before]=Gio.File.new_for_path(`${GLib.get_user_config_dir()}/quest-workspace/server.json`).load_contents(null);
    settings.set_boolean('enabled',false);controller.sync();await controller.queue;check(!controller.active&&!bridge.enabled,'global disable stops workspace');
    settings.set_boolean('enabled',true);controller.sync();await controller.queue;check(controller.active&&bridge.enabled,'workspace restarts');
    const [,after]=Gio.File.new_for_path(`${GLib.get_user_config_dir()}/quest-workspace/server.json`).load_contents(null);
    check(new TextDecoder().decode(before)===new TextDecoder().decode(after),'pairing preserved');
    await controller.destroy();check(!controller.active&&!bridge.enabled,'disable/destroy cleanup');
    print('ZENLINK_WORKSPACE_LIFECYCLE_PASS socket-only idle global enable disable migration archive pairing preservation cleanup');
})();
for(const object of objects)object.unexport();
