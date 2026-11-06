#!/usr/bin/env python3
"""Private session-bus fixture: no XR server, headset, desktop capture or input."""
import json, os, pathlib, signal, subprocess, sys, tempfile
from gi.repository import Gio, GLib
root=pathlib.Path(__file__).resolve().parents[1]
if '--fixture' in sys.argv:
    bus=Gio.bus_get_sync(Gio.BusType.SESSION,None); loop=GLib.MainLoop()
    def emit(value):
        bus.emit_signal(None,'/io/github/wivrn/Server','org.freedesktop.DBus.Properties','PropertiesChanged',GLib.Variant('(sa{sv}as)',('io.github.wivrn.Server',{'HeadsetConnected':GLib.Variant('b',value)},[])))
        return False
    def acquired(*_):
        GLib.timeout_add(200,emit,True); GLib.timeout_add(350,emit,True); GLib.timeout_add(1200,emit,False)
    def stop(*_): print('MOCK_STOPPED',flush=True);loop.quit();return False
    Gio.bus_own_name_on_connection(bus,'io.github.wivrn.Server',Gio.BusNameOwnerFlags.NONE,acquired,None)
    GLib.unix_signal_add(GLib.PRIORITY_DEFAULT,signal.SIGTERM,stop);loop.run();sys.exit()
if '--child' in sys.argv:
    path=pathlib.Path(sys.argv[sys.argv.index('--child')+1]); path.open('a').write(json.dumps({'runtime':os.environ['XR_RUNTIME_JSON'],'config':os.environ['XDG_CONFIG_HOME']})+'\n')
    sys.exit()
with tempfile.TemporaryDirectory(prefix='workspace-pcvr-') as temp:
    temp=pathlib.Path(temp); config=temp/'config'; state=config/'quest-workspace';state.mkdir(parents=True)
    manifest=temp/'runtime.json';manifest.write_text('{}')
    fixture=temp/'server';fixture.write_text('#!'+sys.executable+'\nimport runpy,sys\nsys.argv.append("--fixture")\nrunpy.run_path('+repr(str(pathlib.Path(__file__).resolve()))+',run_name="__main__")\n');fixture.chmod(0o700)
    marker=temp/'started';wrong=temp/'wrong'; overlay=temp/'overlay';overlay.write_text('#!'+sys.executable+'\nfrom pathlib import Path\nPath('+repr(str(temp/'overlay-started'))+').open("a").write("once\\n")\n');overlay.chmod(0o700)
    command=[sys.executable,str(pathlib.Path(__file__).resolve()),'--child',str(marker)]
    state.joinpath('pcvr.json').write_text(json.dumps({'overlay':True,'applications':[{'name':'test','runtime':'wivrn','argv':command},{'name':'wrong','runtime':'steamvr','argv':command[:-1]+[str(wrong)]}]}))
    env=dict(os.environ,QUEST_WORKSPACE_CONFIG=str(config),XDG_CONFIG_HOME=str(temp/'private-wivrn'))
    result=subprocess.run([str(pathlib.Path(os.environ.get('WORKSPACE_TEST_BIN_DIR',root/'build/native'))/'quest-workspace-pcvr'),'--runtime','wivrn','--runtime-json',str(manifest),'--server',str(fixture),'--server-config',str(manifest),'--overlay',str(overlay)],env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=10)
    assert result.returncode==0,result.stdout
    records=[json.loads(line) for line in marker.read_text().splitlines()]
    assert records==[{'runtime':str(manifest),'config':str(config)}],records
    assert not wrong.exists()
    assert (temp/'overlay-started').read_text()=='once\n'
    assert not state.joinpath('pcvr-session.json').exists()
    assert result.stdout.count('PCVR_ENTER')==1,result.stdout
    assert 'MOCK_STOPPED' in result.stdout,result.stdout
    print('PCVR_LIFECYCLE_PASS once-only runtime filtering scoped config child cleanup state cleanup')
