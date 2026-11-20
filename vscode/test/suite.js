// What the extension must do, checked inside a real VS Code (see run.js):
// build the project, report a mistake in the Problems view, debug on the
// simulator to a stop in main, read the stack and a register, step, and
// leave no simulator behind.
'use strict';
const vscode = require('vscode');
const assert = require('assert');
const fs = require('fs');
const path = require('path');

const log = text => {
    console.log(text);
    if (process.env.PIC16F1_TEST_LOG) fs.appendFileSync(process.env.PIC16F1_TEST_LOG, text + '\n');
};
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

function within(promise, seconds, what) {
    return Promise.race([promise, sleep(seconds * 1000).then(() => { throw new Error(`timed out: ${what}`); })]);
}

exports.run = async function () {
    try {
        await suite();
        log('extension ok');
    } catch (e) {
        log(`FAIL ${e && e.stack || e}`);
        throw e;
    }
};

async function suite() {
    const extension = vscode.extensions.getExtension('pic16f1.pic16f1-toolchain');
    assert.ok(extension, 'the extension is not there');
    const api = await extension.activate();
    const folder = vscode.workspace.workspaceFolders[0];
    const bin = await api.toolchainBin();
    log(`toolchain: ${bin}`);
    assert.ok(fs.existsSync(path.join(bin, process.platform === 'win32' ? 'clang.exe' : 'clang')), 'no clang in the toolchain');

    // The project is known for what it is.
    const info = await api.projectInfo(folder);
    assert.strictEqual(info.device, 'pic16f1937');
    assert.strictEqual(info.language, process.env.PIC16F1_TEST_LANGUAGE || 'c');

    // Build.
    assert.strictEqual(await vscode.commands.executeCommand('pic16f1.build'), true, 'the build failed');
    assert.ok(fs.statSync(info.hex).size > 0 && fs.statSync(info.elf).size > 0, 'no program after the build');
    log('ok   build');

    // The build task is offered, and the device can be changed by setting.
    const tasks = await vscode.tasks.fetchTasks({ type: 'pic16f1' });
    assert.ok(tasks.some(t => t.name === 'build') && tasks.some(t => t.name === 'clean'), 'no build and clean tasks');
    const config = vscode.workspace.getConfiguration('pic16f1', folder.uri);
    await config.update('device', 'pic16f1829', vscode.ConfigurationTarget.WorkspaceFolder);
    assert.strictEqual((await api.projectInfo(folder)).device, 'pic16f1829');
    await config.update('device', undefined, vscode.ConfigurationTarget.WorkspaceFolder);
    log('ok   tasks and settings');

    if (info.language === 'c') {
        // A mistake is a failed build and an entry in the Problems view.
        const bad = path.join(folder.uri.fsPath, 'bad.c');
        fs.writeFileSync(bad, 'int broken(void) { return undefined_name; }\n');
        assert.strictEqual(await vscode.commands.executeCommand('pic16f1.build'), false, 'a mistake built');
        let found = [];
        for (let i = 0; i < 50 && !found.length; i++) {
            await sleep(100);
            found = vscode.languages.getDiagnostics().filter(([uri, list]) => /bad\.c$/.test(uri.fsPath) && list.length);
        }
        fs.unlinkSync(bad);
        assert.ok(found.length, 'the mistake is not in the Problems view');
        assert.ok(/undefined_name/.test(found[0][1][0].message), found[0][1][0].message);
        assert.strictEqual(await vscode.commands.executeCommand('pic16f1.build'), true, 'the build failed after the mistake was removed');
        log('ok   problems');
    }

    // Debug on the simulator: the session stops in main.
    let stoppedAt;
    const stopped = () => new Promise(resolve => { stoppedAt = resolve; });
    let next = stopped();
    const tracker = vscode.debug.registerDebugAdapterTrackerFactory('pic16f1', {
        createDebugAdapterTracker() {
            return {
                onDidSendMessage(m) { if (m.type === 'event' && m.event === 'stopped') stoppedAt(m.body); },
                onError(e) { log(`adapter error: ${e}`); },
            };
        },
    });
    const started = await vscode.debug.startDebugging(folder,
        { type: 'pic16f1', request: 'launch', name: 'test', target: 'simulator' });
    assert.strictEqual(started, true, 'the debug session did not start');
    let stop = await within(next, 60, 'the stop in main');
    const session = vscode.debug.activeDebugSession;
    assert.ok(session, 'no active debug session');
    assert.strictEqual(api.servers.size, 1, 'no simulator for the session');
    let frames = (await session.customRequest('stackTrace', { threadId: stop.threadId })).stackFrames;
    assert.ok(/main/.test(frames[0].name), `stopped in ${frames[0].name}, not main`);
    assert.ok(frames[0].source && frames[0].line > 0, 'no source line for main');
    log(`ok   debug: stopped in ${frames[0].name} at ${path.basename(frames[0].source.path)}:${frames[0].line}`);

    // Registers, from the description the simulator sends.
    const scopes = (await session.customRequest('scopes', { frameId: frames[0].id })).scopes;
    const registers = scopes.find(s => /register/i.test(s.name));
    assert.ok(registers, `no registers among ${scopes.map(s => s.name)}`);
    const pc = await session.customRequest('evaluate', { expression: '`register read pc', frameId: frames[0].id, context: 'repl' });
    assert.ok(/PC = 0x/i.test(pc.result), `register read pc: ${pc.result}`);

    // A step ends in a stop on another line (or the same function).
    next = stopped();
    await session.customRequest('next', { threadId: stop.threadId });
    stop = await within(next, 30, 'the stop after a step');
    frames = (await session.customRequest('stackTrace', { threadId: stop.threadId })).stackFrames;
    log(`ok   step: now at line ${frames[0].line}`);

    // Ending the session ends the simulator.
    const ended = new Promise(resolve => vscode.debug.onDidTerminateDebugSession(resolve));
    await vscode.debug.stopDebugging(session);
    await within(ended, 30, 'the end of the session');
    await sleep(1500);
    assert.strictEqual(api.servers.size, 0, 'the simulator is still there');
    tracker.dispose();
    log('ok   session ended, simulator gone');
}
