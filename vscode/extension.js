// PIC16F1 Toolchain for VS Code.
//
// The extension carries the toolchain for the system it was packaged for
// (toolchain/ beside this file: an archive and its VERSION) and unpacks
// it once into .pic16f1/toolchain of the home directory, a place that
// stays the same from one version of the extension to the next, so that
// what a project names (in its Makefile, its editor settings) stays valid.
// Everything it does is done by the toolchain's own programs:
// pic16f1-new-project starts a project, pic16f1-build builds one,
// pic16f1-sim is the simulator and lldb-dap the debug adapter. Plain
// JavaScript with no dependencies: nothing here needs building.
'use strict';
const vscode = require('vscode');
const cp = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const WINDOWS = process.platform === 'win32';
const EXE = WINDOWS ? '.exe' : '';
// (PIC16F1_HOME puts it somewhere else: the extension's tests do.)
const HOME = process.env.PIC16F1_HOME || path.join(os.homedir(), '.pic16f1');
const INSTALLED = path.join(HOME, 'toolchain');
const LANGUAGES = [
    { label: 'C', id: 'c', description: 'the compiler\'s first language' },
    { label: 'C++', id: 'c++', description: 'without exceptions and run-time type information' },
    { label: 'Rust', id: 'rust', description: 'needs rustup; core only' },
    { label: 'Zig', id: 'zig', description: 'needs Zig 0.15 on the PATH' },
];
const OPTIMIZATIONS = [
    { label: '-O1', description: 'small and fast, and still easy to follow in the debugger' },
    { label: '-O2', description: 'faster where that costs words' },
    { label: '-O3', description: 'small, and nothing that costs time' },
    { label: '-Os', description: 'smallest' },
    { label: '-munoptimized', description: 'as written, for stepping through every line' },
    { label: '', description: 'what the project\'s Makefile says' },
];

let context;
let output;
let deviceItem;
let sizeItem;
let installing;                 // the promise of an installation under way
const servers = new Map();      // debug session id (ours) -> the simulator or OpenOCD serving it
let serverCount = 0;

// ---------------------------------------------------------------- toolchain

function readText(file) {
    try { return fs.readFileSync(file, 'utf8'); } catch (e) { return undefined; }
}

function bundled() {
    // The archive this extension was packaged with, if any (there is none
    // when it runs from a checkout).
    const dir = path.join(context.extensionPath, 'toolchain');
    const version = readText(path.join(dir, 'VERSION'));
    if (!version) return undefined;
    const archive = fs.readdirSync(dir).find(f => /\.(tar\.gz|zip)$/.test(f));
    return archive ? { version: version.trim(), archive: path.join(dir, archive) } : undefined;
}

function run(program, args, options) {
    // Runs a program to its end. Resolves with { code, stdout, stderr };
    // code is undefined if it could not be started.
    return new Promise(resolve => {
        cp.execFile(program, args, Object.assign({ maxBuffer: 64 * 1024 * 1024, windowsHide: true }, options || {}),
            (error, stdout, stderr) => {
                if (error && typeof error.code !== 'number')
                    resolve({ code: undefined, stdout: String(stdout || ''), stderr: String(stderr || '') + error.message });
                else
                    resolve({ code: error ? error.code : 0, stdout: String(stdout), stderr: String(stderr) });
            });
    });
}

async function unpack(bundle, progress) {
    // Into a directory beside the final one, then renamed into place, so
    // that an interrupted installation leaves nothing that looks whole.
    fs.mkdirSync(HOME, { recursive: true });
    const work = path.join(HOME, 'toolchain.installing');
    fs.rmSync(work, { recursive: true, force: true });
    fs.mkdirSync(work);
    progress.report({ message: 'unpacking (once; about a minute)...' });
    // Windows has had a tar that reads zip archives since Windows 10; it is
    // named in full because another tar on the PATH (Git's) reads C: as a
    // host name.
    const tar = WINDOWS ? path.join(process.env.SystemRoot || 'C:\\Windows', 'System32', 'tar.exe') : 'tar';
    const r = await run(tar, ['-xf', bundle.archive, '-C', work]);
    if (r.code !== 0)
        throw new Error(`could not unpack ${bundle.archive}: ${r.stderr.trim() || 'tar failed'}`);
    const made = path.join(work, 'pic16f1-toolchain');
    if (!fs.existsSync(path.join(made, 'VERSION')))
        throw new Error(`${bundle.archive} is not a toolchain archive`);
    try {
        fs.rmSync(INSTALLED, { recursive: true, force: true });
        fs.renameSync(made, INSTALLED);
    } catch (e) {
        throw new Error(`could not replace ${INSTALLED} (${e.message}). If a program of the toolchain is running ` +
            '(a debug session, say), end it and reload the window.');
    }
    fs.rmSync(work, { recursive: true, force: true });
}

async function toolchainBin(quiet) {
    // The bin directory of the toolchain to use, installing it first if
    // need be. Throws with a message for the user if there is none.
    const fromEnvironment = process.env.PIC16F1_TOOLCHAIN_BIN;      // (for the extension's own tests)
    if (fromEnvironment) return fromEnvironment;
    const set = vscode.workspace.getConfiguration('pic16f1').get('toolchain');
    if (set) {
        if (!fs.existsSync(path.join(set, 'pic16f1-build')))
            throw new Error(`The setting pic16f1.toolchain names ${set}, which has no pic16f1-build. ` +
                'Clear the setting to use the toolchain that comes with this extension.');
        return set;
    }
    const bundle = bundled();
    const have = (readText(path.join(INSTALLED, 'VERSION')) || '').trim();
    if (bundle && have !== bundle.version) {
        if (!installing) {
            installing = vscode.window.withProgress(
                { location: vscode.ProgressLocation.Notification, title: `PIC16F1 toolchain ${bundle.version.split(' ')[0]}` },
                progress => unpack(bundle, progress)).finally(() => { installing = undefined; });
        }
        await installing;
        if (!quiet)
            vscode.window.showInformationMessage(`PIC16F1 toolchain ${bundle.version.split(' ')[0]} installed in ${INSTALLED}.`);
    } else if (!have) {
        throw new Error('No PIC16F1 toolchain: this copy of the extension carries none. Set pic16f1.toolchain ' +
            'to the bin directory of one.');
    }
    return path.join(INSTALLED, 'bin');
}

async function tool(name) {
    return path.join(await toolchainBin(), name + EXE);
}

function report(error) {
    output.appendLine(String(error && error.message || error));
    vscode.window.showErrorMessage(`PIC16F1: ${error && error.message || error}`);
}

// ----------------------------------------------------------------- projects

function isProject(folder) {
    // A folder the extension has something to offer: its Makefile or
    // Cargo.toml speaks of the PIC16F1, or its settings name a device.
    if (!folder) return false;
    if (vscode.workspace.getConfiguration('pic16f1', folder.uri).get('device')) return true;
    for (const f of ['Makefile', 'Cargo.toml']) {
        const text = readText(path.join(folder.uri.fsPath, f));
        if (text && /pic16f1/i.test(text)) return true;
    }
    return false;
}

function currentFolder() {
    // The folder of the file being edited, else the first that is a
    // project, else the first.
    const folders = vscode.workspace.workspaceFolders || [];
    const editor = vscode.window.activeTextEditor;
    const mine = editor && vscode.workspace.getWorkspaceFolder(editor.document.uri);
    if (mine && isProject(mine)) return mine;
    return folders.find(isProject) || mine || folders[0];
}

function needFolder() {
    const folder = currentFolder();
    if (!folder) throw new Error('Open a project folder first (PIC16F1: New Project... makes one).');
    return folder;
}

function flags(folder, more) {
    // What the settings add to the project's own choices.
    const config = vscode.workspace.getConfiguration('pic16f1', folder.uri);
    const args = [];
    const device = more && more.device || config.get('device');
    if (device) args.push('--device', device);
    if (config.get('optimization')) args.push('--opt', config.get('optimization'));
    if (more && more.debugger) args.push('--debugger');
    return args;
}

async function projectInfo(folder, more) {
    const r = await run(await tool('pic16f1-build'), ['info', ...flags(folder, more), folder.uri.fsPath]);
    if (r.code !== 0) throw new Error((r.stderr || r.stdout).trim() || 'pic16f1-build failed');
    return JSON.parse(r.stdout);
}

async function makeTask(folder, command, more) {
    const definition = { type: 'pic16f1', command };
    if (more && more.debugger) definition.debugger = true;
    const execution = new vscode.ProcessExecution(await tool('pic16f1-build'),
        [command, ...flags(folder, more), folder.uri.fsPath], { cwd: folder.uri.fsPath });
    const task = new vscode.Task(definition, folder, more && more.debugger ? `${command} for the debugger` : command,
        'pic16f1', execution, '$pic16f1');
    if (command === 'build') task.group = vscode.TaskGroup.Build;
    if (command === 'clean') task.group = vscode.TaskGroup.Clean;
    task.presentationOptions = { reveal: vscode.TaskRevealKind.Silent, clear: true, showReuseMessage: false };
    return task;
}

async function runTask(task) {
    // Resolves with the exit code of the task's process.
    const running = await vscode.tasks.executeTask(task);
    return new Promise(resolve => {
        const listeners = [
            vscode.tasks.onDidEndTaskProcess(e => { if (e.execution === running) done(e.exitCode); }),
            vscode.tasks.onDidEndTask(e => { if (e.execution === running) setTimeout(() => done(undefined), 200); }),
        ];
        let finished = false;
        function done(code) {
            if (finished) return;
            finished = true;
            listeners.forEach(l => l.dispose());
            resolve(code);
        }
    });
}

async function build(folder, more) {
    // Builds the project as a task (its messages go to the Problems view).
    // Resolves with true if it built.
    await vscode.workspace.saveAll(false);
    const code = await runTask(await makeTask(folder, 'build', more));
    if (code === 0) {
        showSize(folder, more);
        return true;
    }
    sizeItem.hide();
    vscode.window.showErrorMessage('PIC16F1: the build failed.', 'Show').then(choice => {
        if (choice) vscode.commands.executeCommand('workbench.actions.view.problems');
    });
    return false;
}

async function showSize(folder, more) {
    try {
        const r = await run(await tool('pic16f1-build'), ['size', ...flags(folder, more), folder.uri.fsPath]);
        const m = /(\d+) words/.exec(r.stdout);
        if (r.code === 0 && m) {
            sizeItem.text = `$(chip) ${Number(m[1]).toLocaleString()} words`;
            sizeItem.tooltip = 'Flash the program takes, in 14-bit words. Click for the disassembly.';
            sizeItem.show();
        }
    } catch (e) { /* the size is a convenience */ }
}

async function refreshStatus() {
    const folder = currentFolder();
    const project = isProject(folder);
    vscode.commands.executeCommand('setContext', 'pic16f1.project', project);
    if (!project) {
        deviceItem.hide();
        sizeItem.hide();
        return;
    }
    try {
        const info = await projectInfo(folder);
        deviceItem.text = `$(circuit-board) ${info.device.toUpperCase()}${info.optimization ? '  ' + info.optimization : ''}`;
        deviceItem.tooltip = `PIC16F1 project "${info.name}" (${info.language}). Click to choose the device.`;
        deviceItem.show();
    } catch (e) {
        deviceItem.hide();
    }
}

// ----------------------------------------------------------------- commands

async function devices() {
    const include = path.join(await toolchainBin(), '..', 'include', 'pic16f1');
    return fs.readdirSync(include).filter(f => /^pic1[0-9a-z]+\.h$/.test(f)).map(f => f.slice(0, -2)).sort();
}

async function selectDevice() {
    const folder = needFolder();
    const all = await devices();
    const now = (await projectInfo(folder)).device;
    const picked = await vscode.window.showQuickPick(
        all.map(d => ({ label: d.toUpperCase(), id: d, description: d === now ? 'the project\'s now' : '' })),
        { placeHolder: 'The device the project is for' });
    if (!picked) return;
    await vscode.workspace.getConfiguration('pic16f1', folder.uri)
        .update('device', picked.id, vscode.ConfigurationTarget.WorkspaceFolder);
    sizeItem.hide();
    refreshStatus();
}

async function selectOptimization() {
    const folder = needFolder();
    const picked = await vscode.window.showQuickPick(
        OPTIMIZATIONS.map(o => ({ label: o.label || '(the Makefile\'s)', value: o.label, description: o.description })),
        { placeHolder: 'How the program is optimized' });
    if (!picked) return;
    await vscode.workspace.getConfiguration('pic16f1', folder.uri)
        .update('optimization', picked.value || undefined, vscode.ConfigurationTarget.WorkspaceFolder);
    refreshStatus();
}

async function newProject() {
    const bin = await toolchainBin();
    const language = await vscode.window.showQuickPick(LANGUAGES, { placeHolder: 'The language of the new project' });
    if (!language) return;
    const all = await devices();
    const device = await vscode.window.showQuickPick(
        ['pic16f1937', ...all.filter(d => d !== 'pic16f1937')].map(d => ({ label: d.toUpperCase(), id: d })),
        { placeHolder: 'The device' });
    if (!device) return;
    const parent = await vscode.window.showOpenDialog({
        canSelectFiles: false, canSelectFolders: true, canSelectMany: false,
        openLabel: 'Make the project in this folder', title: 'Where to make the project',
    });
    if (!parent) return;
    const name = await vscode.window.showInputBox({
        prompt: 'The project\'s name: a folder of this name is made', value: 'blink',
        validateInput: v => /^[A-Za-z0-9][A-Za-z0-9_-]*$/.test(v) ? undefined : 'Letters, digits, - and _',
    });
    if (!name) return;
    const target = path.join(parent[0].fsPath, name);
    const r = await run(path.join(bin, 'pic16f1-new-project' + EXE),
        ['--language', language.id, '--device', device.id, target]);
    if (r.code !== 0) throw new Error((r.stderr || r.stdout).trim() || 'pic16f1-new-project failed');
    output.appendLine(r.stdout.trim());
    await vscode.commands.executeCommand('vscode.openFolder', vscode.Uri.file(target),
        { forceNewWindow: !!(vscode.workspace.workspaceFolders || []).length });
}

async function showDisassembly() {
    const folder = needFolder();
    const r = await run(await tool('pic16f1-build'), ['list', ...flags(folder), folder.uri.fsPath]);
    if (r.code !== 0) throw new Error((r.stderr || r.stdout).trim() || 'no disassembly; build first');
    const document = await vscode.workspace.openTextDocument({ content: r.stdout });
    await vscode.window.showTextDocument(document, { preview: true });
}

async function about() {
    const bin = await toolchainBin();
    const version = (readText(path.join(bin, '..', 'VERSION')) || 'a checkout').trim();
    const clang = await run(path.join(bin, 'clang' + EXE), ['--version']);
    const choice = await vscode.window.showInformationMessage(
        `PIC16F1 toolchain ${version}, in ${path.dirname(bin)}. ${clang.stdout.split('\n')[0]}`, 'Open the folder');
    if (choice) vscode.env.openExternal(vscode.Uri.file(path.dirname(bin)));
}

// ---------------------------------------------------------------- debugging

function startServer(program, args, options, ready, seconds, what) {
    // Starts the simulator or OpenOCD and waits for the line that says it
    // is listening. Resolves with { process, match }.
    return new Promise((resolve, reject) => {
        let child;
        try {
            child = cp.spawn(program, args, Object.assign({ windowsHide: true }, options || {}));
        } catch (e) {
            reject(new Error(`cannot start ${what}: ${e.message}`));
            return;
        }
        let seen = '';
        let settled = false;
        const timer = setTimeout(() => finish(new Error(`${what} did not get ready in ${seconds} seconds:\n${seen.slice(-800)}`)), seconds * 1000);
        function finish(error, match) {
            if (settled) return;
            settled = true;
            clearTimeout(timer);
            if (error) {
                try { child.kill(); } catch (e) { /* gone already */ }
                reject(error);
            } else {
                resolve({ process: child, match });
            }
        }
        const take = data => {
            const text = data.toString();
            output.append(text);
            if (settled) return;
            seen += text;
            const m = ready.exec(seen);
            if (m) finish(undefined, m);
        };
        child.stdout.on('data', take);
        child.stderr.on('data', take);
        child.on('error', e => finish(new Error(`cannot start ${what} (${program}): ${e.message}`)));
        child.on('exit', code => finish(new Error(`${what} ended (${code}) before it was ready:\n${seen.slice(-800)}`)));
    });
}

async function startSimulator(elf, device) {
    const started = await startServer(await tool('pic16f1-sim'), ['gdb', '--device', device, '--port', '0', elf],
        undefined, /waiting for a debugger on port (\d+)/, 10, 'the simulator');
    return { process: started.process, port: Number(started.match[1]) };
}

async function startProbe(folder, elf) {
    const config = vscode.workspace.getConfiguration('pic16f1', folder && folder.uri);
    const dir = config.get('openocd');
    if (!dir)
        throw new Error('To debug on the part, set pic16f1.openocd to the directory of an OpenOCD that drives your probe.');
    const program = [path.join(dir, 'src', 'openocd' + EXE), path.join(dir, 'bin', 'openocd' + EXE)].find(p => fs.existsSync(p));
    if (!program) throw new Error(`No src/openocd or bin/openocd in ${dir} (the setting pic16f1.openocd).`);
    const scripts = [path.join(dir, 'tcl'), path.join(dir, 'share', 'openocd', 'scripts')].find(p => fs.existsSync(p));
    const target = config.get('openocdTarget');
    const cpu = path.basename(target, '.cfg') + '.cpu';
    const args = [
        ...(scripts ? ['-s', scripts] : []),
        '-f', config.get('openocdInterface'),
        '-c', `pk2icsp vdd ${config.get('vdd')}`, '-c', 'pk2icsp hvp on',
        '-f', target,
        // OpenOCD ends when the debugger leaves, so that the next session
        // programs and resets the part afresh.
        '-c', `${cpu} configure -event gdb-detach { shutdown }`,
        '-c', `init; pic16f1 load_debug {${elf}}; echo "pic16f1: ready for the debugger"`,
    ];
    output.appendLine(`${program} ${args.join(' ')}`);
    // OpenOCD opens its port before it has programmed the part; the
    // debugger must wait for the end of that.
    const started = await startServer(program, args, { cwd: dir }, /pic16f1: ready for the debugger/, 90, 'OpenOCD');
    return { process: started.process, port: 3333 };
}

class ConfigurationProvider {
    resolveDebugConfiguration(folder, config) {
        // F5 with no launch.json: the simulator.
        if (!config.type && !config.request && !config.name) {
            if (!isProject(folder || currentFolder())) return undefined;
            Object.assign(config, { type: 'pic16f1', request: 'launch', name: 'Debug on the simulator', target: 'simulator' });
        }
        return config;
    }

    async resolveDebugConfigurationWithSubstitutedVariables(folder, config) {
        try {
            folder = folder || needFolder();
            const probe = config.target === 'probe';
            const more = { debugger: probe, device: config.device };
            let elf = config.program;
            let device = config.device;
            if (!elf || !device) {
                const info = await projectInfo(folder, more);
                device = device || info.device;
                if (!elf) {
                    elf = info.elf;
                    // A program for the part differs from one for the
                    // simulator (-mdebugger), so each is built afresh.
                    if (config.build !== false && !await build(folder, more)) return undefined;
                }
            }
            if (!fs.existsSync(elf)) throw new Error(`There is no ${elf} to debug.`);
            const server = probe ? await startProbe(folder, elf) : await startSimulator(elf, device);
            const id = `pic16f1-${++serverCount}`;
            servers.set(id, server.process);
            const bin = await toolchainBin();
            const svd = path.join(bin, '..', 'share', 'pic16f1', 'svd', device + '.svd');
            const resolved = Object.assign({}, config, {
                request: 'attach',
                program: elf,
                'gdb-remote-hostname': '127.0.0.1',
                'gdb-remote-port': server.port,
                // Small reads: nothing here is 512 bytes long, and every
                // byte read from a running part takes time.
                initCommands: ['settings set target.process.memory-cache-line-size 32', ...(config.initCommands || [])],
                // Run to main: the debugger continues from the reset vector
                // once it is set up, and this stops it there.
                postRunCommands: [...(config.stopAtMain === false ? [] : ['breakpoint set --one-shot true --name main']),
                    ...(config.postRunCommands || [])],
                __pic16f1Server: id,
            });
            if (probe) resolved.timeout = 60;
            if (!resolved.svdPath && fs.existsSync(svd)) resolved.svdPath = svd;
            return resolved;
        } catch (e) {
            report(e);
            return undefined;
        }
    }
}

class AdapterFactory {
    async createDebugAdapterDescriptor() {
        return new vscode.DebugAdapterExecutable(await tool('lldb-dap'), []);
    }
}

function endServer(id) {
    const child = servers.get(id);
    if (!child) return;
    servers.delete(id);
    // (OpenOCD shuts itself down when the debugger detaches; give it the
    // moment that takes before insisting.)
    setTimeout(() => { try { child.kill(); } catch (e) { /* gone already */ } }, 500);
}

async function debug(target) {
    const folder = needFolder();
    await vscode.debug.startDebugging(folder, {
        type: 'pic16f1', request: 'launch', target,
        name: target === 'probe' ? 'Debug on the part (probe)' : 'Debug on the simulator',
    });
}

// --------------------------------------------------------------- activation

function command(name, body) {
    context.subscriptions.push(vscode.commands.registerCommand(name, async (...args) => {
        try {
            return await body(...args);
        } catch (e) {
            report(e);
            return undefined;
        }
    }));
}

async function activate(extensionContext) {
    context = extensionContext;
    output = vscode.window.createOutputChannel('PIC16F1');
    deviceItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 50);
    deviceItem.command = 'pic16f1.selectDevice';
    sizeItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 49);
    sizeItem.command = 'pic16f1.showDisassembly';
    context.subscriptions.push(output, deviceItem, sizeItem);

    command('pic16f1.newProject', newProject);
    command('pic16f1.build', async () => build(needFolder()));
    command('pic16f1.clean', async () => { sizeItem.hide(); return runTask(await makeTask(needFolder(), 'clean')); });
    command('pic16f1.debugSimulator', () => debug('simulator'));
    command('pic16f1.debugProbe', () => debug('probe'));
    command('pic16f1.selectDevice', selectDevice);
    command('pic16f1.selectOptimization', selectOptimization);
    command('pic16f1.showDisassembly', showDisassembly);
    command('pic16f1.about', about);

    context.subscriptions.push(
        vscode.tasks.registerTaskProvider('pic16f1', {
            async provideTasks() {
                const tasks = [];
                for (const folder of (vscode.workspace.workspaceFolders || []).filter(isProject)) {
                    try {
                        tasks.push(await makeTask(folder, 'build'), await makeTask(folder, 'clean'));
                    } catch (e) { /* no toolchain: no tasks */ }
                }
                return tasks;
            },
            async resolveTask(task) {
                const folder = task.scope && task.scope.uri ? task.scope : currentFolder();
                if (!folder || !['build', 'clean'].includes(task.definition.command)) return undefined;
                const made = await makeTask(folder, task.definition.command, { debugger: task.definition.debugger });
                return new vscode.Task(task.definition, folder, task.name, 'pic16f1', made.execution, '$pic16f1');
            },
        }),
        vscode.debug.registerDebugConfigurationProvider('pic16f1', new ConfigurationProvider()),
        vscode.debug.registerDebugAdapterDescriptorFactory('pic16f1', new AdapterFactory()),
        vscode.debug.onDidTerminateDebugSession(session => endServer(session.configuration.__pic16f1Server)),
        vscode.window.onDidChangeActiveTextEditor(refreshStatus),
        vscode.workspace.onDidChangeWorkspaceFolders(refreshStatus),
        vscode.workspace.onDidChangeConfiguration(e => { if (e.affectsConfiguration('pic16f1')) refreshStatus(); }),
        vscode.workspace.onDidSaveTextDocument(d => { if (/Makefile$|Cargo\.toml$/.test(d.fileName)) refreshStatus(); }),
    );

    // The toolchain: unpacked the first time (and after an update of the
    // extension), then put on the PATH of this window's terminals, where
    // pic16f1-cc, pic16f1-build and make then work as they are.
    try {
        const bin = await toolchainBin(true);
        context.environmentVariableCollection.description = 'The PIC16F1 toolchain\'s programs';
        context.environmentVariableCollection.prepend('PATH', bin + path.delimiter);
    } catch (e) {
        report(e);
    }
    refreshStatus();
    return { toolchainBin, build, projectInfo, servers };
}

// (If the window goes without deactivating: a simulator left behind is a
// stray process, an OpenOCD left behind holds the probe.)
process.on('exit', () => deactivate());

function deactivate() {
    for (const child of servers.values()) {
        try { child.kill(); } catch (e) { /* gone already */ }
    }
    servers.clear();
}

module.exports = { activate, deactivate };
