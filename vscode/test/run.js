#!/usr/bin/env node
// Runs test/suite.js inside VS Code against a new project.
//
//   node vscode/test/run.js --toolchain <bin directory> [--extension <dir>] [--language c|c++|zig|rust]
//
// PIC16F1_TEST_TARGET=probe debugs on the part instead of the simulator: it
// drives the probe and programs the board (a PIC16F1937 on the probe that
// pic16f1-new-project found the OpenOCD of).
//
// The toolchain is one to test with (a packaged tree's bin, or
// toolchain/bin of a checkout). The extension is this directory unless
// another is named (an unpacked VSIX, to test what will be installed; with
// no --toolchain it then installs the toolchain it carries, into
// $PIC16F1_HOME if that is set, else .pic16f1 of the home directory). VS Code is $VSCODE_EXECUTABLE,
// or the one installed (macOS), or one that @vscode/test-electron
// downloads (CI: npm install --no-save @vscode/test-electron first).
'use strict';
const cp = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

async function findCode() {
    if (process.env.VSCODE_EXECUTABLE) return process.env.VSCODE_EXECUTABLE;
    for (const mac of ['Code', 'Electron'].map(n => `/Applications/Visual Studio Code.app/Contents/MacOS/${n}`))
        if (process.platform === 'darwin' && fs.existsSync(mac)) return mac;
    const { downloadAndUnzipVSCode } = require('@vscode/test-electron');
    return downloadAndUnzipVSCode('stable');
}

async function main() {
    const args = process.argv.slice(2);
    const option = name => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : undefined; };
    const toolchain = option('--toolchain') && path.resolve(option('--toolchain'));
    const extension = path.resolve(option('--extension') || path.join(__dirname, '..'));
    const language = option('--language') || 'c';
    const exe = process.platform === 'win32' ? '.exe' : '';
    const work = fs.mkdtempSync(path.join(os.tmpdir(), 'pic16f1-vscode-'));
    const project = path.join(work, 'project');
    const log = path.join(work, 'test.log');
    const env = Object.assign({}, process.env, { PIC16F1_TEST_LOG: log, PIC16F1_TEST_LANGUAGE: language });
    let bin = toolchain;
    if (toolchain) {
        env.PIC16F1_TOOLCHAIN_BIN = toolchain;
    } else {
        // The project is made with the toolchain the extension will
        // install; until then, with its template by hand is not possible,
        // so unpack the archive it carries the way the extension will.
        delete env.PIC16F1_TOOLCHAIN_BIN;
        const carried = path.join(extension, 'toolchain');
        const archive = fs.readdirSync(carried).find(f => /\.(tar\.gz|zip)$/.test(f));
        const tar = process.platform === 'win32' ? path.join(process.env.SystemRoot, 'System32', 'tar.exe') : 'tar';
        cp.execFileSync(tar, ['-xf', path.join(carried, archive), '-C', work]);
        bin = path.join(work, 'pic16f1-toolchain', 'bin');
    }
    cp.execFileSync(path.join(bin, 'pic16f1-new-project' + exe), ['--language', language, project], { stdio: 'inherit' });

    const code = await findCode();
    const run = cp.spawnSync(code, [
        project,
        `--extensionDevelopmentPath=${extension}`,
        `--extensionTestsPath=${path.join(__dirname, 'suite.js')}`,
        `--user-data-dir=${path.join(work, 'user')}`,
        `--extensions-dir=${path.join(work, 'extensions')}`,
        '--disable-workspace-trust', '--skip-welcome', '--skip-release-notes', '--disable-updates', '--no-sandbox',
        '--disable-gpu',
    ], { env, stdio: 'inherit', timeout: 240000, killSignal: 'SIGKILL' });
    if (fs.existsSync(log)) process.stdout.write(fs.readFileSync(log, 'utf8'));
    const ok = run.status === 0 && fs.existsSync(log) && /extension ok/.test(fs.readFileSync(log, 'utf8'));
    if (!ok) console.error(`extension test failed (VS Code exit status ${run.status}); files kept in ${work}`);
    else fs.rmSync(work, { recursive: true, force: true });
    process.exit(ok ? 0 : 1);
}

main().catch(e => { console.error(e); process.exit(1); });
