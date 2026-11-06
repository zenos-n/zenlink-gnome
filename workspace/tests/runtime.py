#!/usr/bin/env python3
"""PC-only TLS/socket-activation probe on a private D-Bus; no desktop/Quest input."""
import base64, hashlib, json, os, pathlib, socket, ssl, struct, subprocess, sys, tempfile
binary=pathlib.Path(sys.argv[1])/'bin'
with tempfile.TemporaryDirectory(prefix='zenlink-workspace-test-') as temp:
    root=pathlib.Path(temp);env=dict(os.environ,XDG_CONFIG_HOME=str(root/'config'))
    env.pop('QUEST_WORKSPACE_CONFIG',None)
    env['ZENLINK_PC_ENVIRONMENT_COMMAND']='/fixture/environment with spaces'
    def command(*args,check=True):return subprocess.run([str(binary/'zenlink-workspace-configure'),*args],env=env,check=check,capture_output=True,text=True,timeout=15)
    command('--prepare');directory=root/'config/quest-workspace';config=directory/'server.json';pair=directory/'connection.json'
    original=config.read_bytes();paired=pair.read_bytes();state=json.loads(original)
    assert len(state['token'])==64 and config.stat().st_mode&0o777==0o600
    assert pathlib.Path(state['private_key']).stat().st_mode&0o777==0o600
    command('--prepare');assert original==config.read_bytes() and paired==pair.read_bytes()
    command('--wivrn','environment');assert json.loads((directory/'wivrn-session.json').read_text())['application']==['/fixture/environment with spaces']
    assert command('--wivrn','bad',check=False).returncode!=0
    listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(8);port=listener.getsockname()[1]
    state['port']=port;config.write_text(json.dumps(state));assert command('--port').stdout.strip()==str(port)
    host=binary/'quest-workspace-host'
    launcher='import os,sys;fd=int(sys.argv[1]);os.dup2(fd,3);os.set_inheritable(3,True);os.environ["LISTEN_PID"]=str(os.getpid());os.environ["LISTEN_FDS"]="1";os.execv(sys.argv[2],[sys.argv[2],"--config",sys.argv[3],"--socket-activated"])'
    process=subprocess.Popen([sys.executable,'-c',launcher,str(listener.fileno()),str(host),str(config)],env=env,pass_fds=(listener.fileno(),),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    try:
        context=ssl.create_default_context(cafile=state['certificate'])
        with context.wrap_socket(socket.create_connection(('127.0.0.1',port),timeout=10),server_hostname='quest-workspace') as connection:
            connection.settimeout(10);nonce=base64.b64encode(os.urandom(16)).decode()
            connection.sendall(f'GET /workspace HTTP/1.1\r\nHost: quest-workspace:{port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: {nonce}\r\n\r\n'.encode())
            response=b''
            while not response.endswith(b'\r\n\r\n'):response+=connection.recv(1)
            assert response.startswith(b'HTTP/1.1 101'),response
            def send(opcode,data):
                mask=os.urandom(4);header=bytes([0x80|opcode]);n=len(data)
                header+=bytes([0x80|n]) if n<126 else bytes([0x80|126])+struct.pack('!H',n)
                connection.sendall(header+mask+bytes(value^mask[i%4] for i,value in enumerate(data)))
            def exact(n):
                data=b''
                while len(data)<n:
                    chunk=connection.recv(n-len(data));assert chunk;data+=chunk
                return data
            def receive():
                first,second=exact(2);n=second&127
                if n==126:n=struct.unpack('!H',exact(2))[0]
                if n==127:n=struct.unpack('!Q',exact(8))[0]
                return first&15,exact(n)
            send(1,json.dumps({'type':'hello','version':1,'token':state['token'],'hevc':True}).encode())
            opcode,payload=receive();welcome=json.loads(payload);assert opcode==1 and welcome['type']=='welcome' and welcome['transport']=='hevc-hardware-jpeg-still',welcome
            send(8,struct.pack('!H',1000));receive()
        output=process.communicate(timeout=10)[0];assert process.returncode==0,output
        assert 'receiver authenticated' in output and 'capture started' not in output,output
    finally:
        listener.close()
        if process.poll() is None:process.terminate();process.wait(timeout=5)
    invalid=subprocess.run([str(host),'--config',str(config),'--socket-activated'],env=env,capture_output=True,text=True,timeout=10)
    assert invalid.returncode!=0 and 'Expected one systemd activation socket' in invalid.stderr,invalid.stderr
    print('ZENLINK_WORKSPACE_RUNTIME_PASS TLS certificate authenticated websocket inherited listener idle exit private setup preservation no capture')
