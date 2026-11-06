#!/usr/bin/env python3
"""Exercise USB selection and private pairing with a fake ADB, no real devices."""
import json,os,pathlib,subprocess,sys,tempfile
package=pathlib.Path(sys.argv[1])
# Exercise the ELF directly, leaving the production wrapper's pinned ADB intact.
helper=next(p for p in (package/'bin').iterdir() if 'zenlink-workspace-connect' in p.name and p.read_bytes()[:4]==b'\x7fELF')
with tempfile.TemporaryDirectory(prefix='zenlink-usb-test-') as temporary:
    root=pathlib.Path(temporary);config=root/'config/quest-workspace';config.mkdir(parents=True)
    server={'port':19843,'token':'private-fixture-token','certificate':str(root/'certificate')}
    (root/'certificate').write_text('private-fixture-certificate');(config/'server.json').write_text(json.dumps(server));original=(config/'server.json').read_bytes()
    adb=root/'adb';adb.write_text('#!'+sys.executable+'\n'+'''import json,os,pathlib,sys
r=pathlib.Path(os.environ['FIXTURE']);args=sys.argv[1:]
if args==['devices','-l']:print((r/'devices').read_text());sys.exit(0)
if args[-3:]==['shell','getprop','ro.product.model']:print('Quest 3');sys.exit(0)
if 'reverse' in args:
 with (r/'calls').open('a') as f:f.write(json.dumps(args)+'\\n')
 sys.exit(0)
if 'shell' in args and 'run-as' in args[-1]:(r/'received').write_text(sys.stdin.read());sys.exit(0)
sys.exit(1)
''');adb.chmod(0o700)
    env=dict(os.environ,XDG_CONFIG_HOME=str(root/'config'),ZENLINK_ADB=str(adb),FIXTURE=str(root))
    def run(mode,success):
        result=subprocess.run([str(helper),mode],env=env,capture_output=True,text=True,timeout=10)
        assert (result.returncode==0)==success,(result.stdout,result.stderr)
        assert server['token'] not in result.stdout+result.stderr and 'private-fixture-certificate' not in result.stdout+result.stderr
        return result
    (root/'devices').write_text('List of devices attached\nQ3 device usb:1-2 product:hollywood\n192.0.2.1:1234 device product:hollywood\n')
    assert 'USB wired mode is ready' in run('--usb',True).stdout
    paired=json.loads((root/'received').read_text());assert paired['connection_mode']=='usb' and paired['token']==server['token']
    calls=[json.loads(line) for line in (root/'calls').read_text().splitlines()]
    assert calls==[['-s','Q3','reverse','tcp:9843','tcp:19843'],['-s','Q3','reverse','tcp:9757','tcp:9757']]
    assert 'Wireless debugging' in run('--adb',True).stdout
    assert json.loads((root/'received').read_text())['connection_mode']=='adb-wifi'
    (root/'devices').write_text('List of devices attached\nQ3 unauthorized usb:1-2\n');assert 'Allow USB debugging' in run('--usb',False).stderr
    (root/'devices').write_text('List of devices attached\n192.0.2.1:1234 device product:hollywood\n');assert 'No USB Quest' in run('--usb',False).stderr
    (root/'devices').write_text('List of devices attached\nA device usb:1-2\nB device usb:1-3\n');assert 'More than one Quest' in run('--usb',False).stderr
    assert original==(config/'server.json').read_bytes()
print('ZENLINK_TRANSPORT_TEST_PASS usb_selection no_wireless_fallback authorization private_pair_transfer identity_preserved')
