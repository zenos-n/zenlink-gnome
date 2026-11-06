// SPDX-License-Identifier: MIT
// Private Xvfb/session-bus construction test; never opens a desktop window.
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import system from 'system';
const prefix=system.programArgs[0];
await import(`file://${prefix}/__nix-prepend-search-paths.js`);
const {default:Gtk}=await import('gi://Gtk?version=3.0');
await import(`file://${prefix}/preferences/init.js`);
const {Window}=await import(`file://${prefix}/preferences/service.js`);
const app=new Gtk.Application({application_id:'org.zenos.ZenLink.Workspace.UITest',flags:Gio.ApplicationFlags.NON_UNIQUE});
let failed=null;
app.connect('activate',()=>{
    try {
        const window=new Window({application:app});window.show_all();
        const button=window.headerbar.get_children().find(widget=>widget instanceof Gtk.Button&&widget.get_label()==='VR & Desktop');
        if(!button||!button.get_visible())throw new Error('ZenLink workspace settings entry missing');
        print('ZENLINK_WORKSPACE_UI_PASS integrated preferences entry');
        GLib.timeout_add(GLib.PRIORITY_DEFAULT,250,()=>{window.destroy();app.quit();return GLib.SOURCE_REMOVE;});
    }catch(e){failed=e;app.quit();}
});
app.run([]);if(failed)throw failed;
