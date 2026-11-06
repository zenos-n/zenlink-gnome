// SPDX-License-Identifier: MIT
export function workspaceUnits(binDir, configDir, mutterLibrary, port) {
    if (!Number.isInteger(port) || port < 1024 || port > 65535)
        throw new Error('Invalid workspace listener port');
    const quote = value => {
        if (!value.startsWith('/') || /[\n\r\0]/.test(value))
            throw new Error('Workspace paths must be absolute');
        return `"${value.replace(/%/g, '%%').replace(/[\\"]/g, '\\$&')}"`;
    };
    const guard = mutterLibrary
        ? `ExecStartPre=${quote(`${binDir}/quest-workspace-compositor-ready`)} ${quote(mutterLibrary)}\n` : '';
    return {
        'zenlink-workspace.socket': `[Unit]\nDescription=ZenLink workspace receiver socket\nPartOf=graphical-session.target\n\n[Socket]\nListenStream=${port}\nNoDelay=true\nService=zenlink-workspace.service\n`,
        'zenlink-workspace.service': `[Unit]\nDescription=ZenLink workspace native streaming backend\nPartOf=graphical-session.target\nRequires=zenlink-workspace.socket\nAfter=graphical-session.target\n\n[Service]\n${guard}ExecStart=${quote(`${binDir}/quest-workspace-host`)} --config ${quote(`${configDir}/quest-workspace/server.json`)} --socket-activated\nNoNewPrivileges=yes\nUMask=0077\nTimeoutStopSec=10\n`,
    };
}
