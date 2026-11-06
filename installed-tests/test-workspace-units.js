// SPDX-License-Identifier: MIT
import {workspaceUnits} from '../src/utils/workspace-units.js';
function check(value) { if (!value) throw new Error('workspace units check failed'); }
const units = workspaceUnits('/nix/store/native/bin', '/private/config', '/nix/store/fixed/lib/libmutter.so', 9843);
check(units['zenlink-workspace.socket'].includes('ListenStream=9843'));
check(units['zenlink-workspace.service'].includes('--socket-activated'));
check(units['zenlink-workspace.service'].includes('ExecStartPre='));
check(!units['zenlink-workspace.service'].includes('PrivateTmp='));
check(!units['zenlink-workspace.service'].includes('Restart=always'));
check(units['zenlink-workspace.service'].includes('NoNewPrivileges=yes'));
const quoted = workspaceUnits('/native/bin', '/private/config 20%', '', 9843);
check(quoted['zenlink-workspace.service'].includes('"/private/config 20%%/quest-workspace/server.json"'));
for (const port of [80, -1, NaN, 65536]) { let failed = false; try {workspaceUnits('/bin', '/config', '', port);} catch {failed = true;} check(failed); }
let failed = false; try {workspaceUnits('/bin', '/config\nExecStart=bad', '', 9843);} catch {failed = true;} check(failed);
print('ZENLINK_WORKSPACE_UNITS_PASS socket activation native safety paths escaping idle lifecycle');
