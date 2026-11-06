#!/usr/bin/env python3
"""Exercise real virtual outputs in an isolated GNOME compositor, never the user's."""
import json,os,pathlib,shutil,subprocess,sys,tempfile,time
root=pathlib.Path(__file__).resolve().parents[2]
if not os.environ.get('ZENLINK_DISPOSABLE_COMPOSITOR'):
    with tempfile.TemporaryDirectory(prefix='zenlink-headless-') as temporary:
        # Only transport the disposable compositor's own video node through the
        # existing PipeWire service; no hardware/display/audio nodes are opened.
        path=pathlib.Path(temporary);env=dict(os.environ,ZENLINK_DISPOSABLE_COMPOSITOR='1',GSETTINGS_BACKEND='keyfile',PIPEWIRE_RUNTIME_DIR=os.environ['XDG_RUNTIME_DIR'],
            XDG_CONFIG_HOME=str(path/'config'),XDG_DATA_HOME=str(path/'data'),XDG_CACHE_HOME=str(path/'cache'),XDG_STATE_HOME=str(path/'state'),
            XDG_RUNTIME_DIR=str(path/'runtime'),LIBGL_ALWAYS_SOFTWARE='1',GALLIUM_DRIVER='llvmpipe',GNOME_SHELL_SESSION_MODE='user')
        (path/'runtime').mkdir(mode=0o700)
        for key in ('DISPLAY','WAYLAND_DISPLAY','SESSION_MANAGER','GNOME_SETUP_DISPLAY'):env.pop(key,None)
        extension=path/'data/gnome-shell/extensions/zenlink-test@local';extension.mkdir(parents=True)
        (extension/'metadata.json').write_text(json.dumps({'uuid':'zenlink-test@local','name':'Disposable workspace test','description':'Isolated test fixture','shell-version':['50']}))
        (extension/'extension.js').write_text("""import Gio from 'gi://Gio';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import Bridge from './workspace.js';
export default class Test extends Extension {
 enable(){this.bridge=new Bridge();this.bridge.enable();this.test=Gio.DBusExportedObject.wrapJSObject('<node><interface name="org.zenos.ZenLink.Test"><method name="Paint"/></interface></node>',{Paint(){global.stage.queue_redraw();}});this.test.export(Gio.DBus.session,'/org/zenos/ZenLink/Test');}
 disable(){this.test.unexport();this.bridge.disable();}
}
""")
        shutil.copy2(root/'src/shell/workspace.js',extension/'workspace.js')
        result=subprocess.run(['dbus-run-session','--',sys.executable,__file__,sys.argv[1]],env=env,timeout=90)
        raise SystemExit(result.returncode)
subprocess.run(['gsettings','set','org.gnome.shell','enabled-extensions',"['zenlink-test@local']"],check=True)
log=pathlib.Path(os.environ['XDG_STATE_HOME']);log.mkdir(parents=True,exist_ok=True)
with (log/'shell.log').open('w') as output:
    shell=subprocess.Popen(['gnome-shell','--headless','--wayland','--no-x11','--virtual-monitor','1280x720@60'],stdout=output,stderr=subprocess.STDOUT)
    try:
        deadline=time.monotonic()+25
        while time.monotonic()<deadline:
            if shell.poll() is not None:raise RuntimeError('Disposable Shell exited')
            bridge=subprocess.run(['gdbus','introspect','--session','--dest','org.gnome.Shell','--object-path','/dev/doromiert/QuestWorkspace'],capture_output=True,text=True,timeout=3)
            if 'TouchDevice' in bridge.stdout:break
            time.sleep(.2)
        else:raise RuntimeError('Disposable workspace bridge did not load')
        subprocess.run([sys.argv[1]],check=True,timeout=60)
    except Exception:
        print((log/'shell.log').read_text()[-10000:]);raise
    finally:
        shell.terminate()
        try:shell.wait(timeout=8)
        except subprocess.TimeoutExpired:shell.kill();shell.wait()
