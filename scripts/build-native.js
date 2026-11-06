// SPDX-License-Identifier: MIT
'use strict';
// Build the project-owned Windows x64 bridge, never vendor plugin DLLs.
const fs = require('node:fs'), path = require('node:path');
const { execFileSync } = require('node:child_process');
const { inspect } = require('../src/pe');
const root = path.resolve(__dirname, '..'), zig = process.env.ZIG || 'zig';
const build = path.join(root, 'build', 'native'), resources = path.join(root, 'resources');
const tests = process.argv.includes('--tests');
const hostCheck = process.argv.includes('--fl-host-check');
function run(args) { execFileSync(zig, args, { cwd: root, stdio: 'inherit' }); }
try {
  const version = execFileSync(zig, ['version'], { encoding: 'utf8' }).trim();
  if (version !== '0.14.1') throw Error('Use Zig 0.14.1 (found ' + version + '). Set ZIG to its executable if it is not on PATH.');
  fs.mkdirSync(build, { recursive: true }); fs.mkdirSync(resources, { recursive: true });
  const common = ['c++', '-target', 'x86_64-windows-gnu', '-std=c++17', '-DUNICODE', '-D_UNICODE', '-Inative/third_party'];
  const libs = ['-luser32', '-lgdi32', '-lshell32', '-lcomctl32', '-luuid', '-lcomdlg32', '-lole32'];
  if (hostCheck) {
    run([...common, '-O1', '-municode', '-Wl,--subsystem,windows', 'native/tests/fl_studio_host_probe.cpp', ...libs, '-o', path.join(build, 'FLHostCheck.exe')]);
    console.log('Optional FL Studio host diagnostic built: build/native/FLHostCheck.exe. This does not enable Edison support.');
  } else if (tests) {
    for (const [name, define] of [['FontFixture.dll', 'NPC_FONT_FIXTURE'], ['FontOwnerA.dll', 'NPC_FONT_OWNER'], ['FontOwnerB.dll', 'NPC_FONT_OWNER']]) {
      run([...common, '-O1', '-shared', '-D' + define, 'native/tests/installed_fonts_test.cpp', ...libs, '-o', path.join(build, name)]);
    }
    for (const [source, name] of [['generic_adapter_test.cpp', 'AdapterTest.exe'], ['channel_engine_test.cpp', 'ChannelTest.exe'], ['license_policy_test.cpp', 'LicensePolicyTest.exe'], ['installed_config_test.cpp', 'InstalledConfigTest.exe'], ['startup_diagnostics_test.cpp', 'StartupDiagnosticsTest.exe'], ['installed_fonts_test.cpp', 'InstalledFontsTest.exe']]) {
      run([...common, '-O1', 'native/tests/' + source, ...libs, '-o', path.join(build, name)]);
    }
    if (process.platform === 'win32' && process.arch === 'x64') {
      for (const [name, args] of [['AdapterTest.exe', ['instrument']], ['AdapterTest.exe', ['effect']], ['ChannelTest.exe', []], ['LicensePolicyTest.exe', []], ['InstalledConfigTest.exe', []], ['StartupDiagnosticsTest.exe', []], ['InstalledFontsTest.exe', []]]) {
        execFileSync(path.join(build, name), args, { cwd: build, stdio: 'inherit' });
      }
    } else console.log('Windows test executables compiled in build/native. Run them on Windows x64; no native tests were executed on this host.');
  } else {
    run([...common, '-O2', '-shared', 'native/bridge/vst3.cpp', 'native/bridge/engine.cpp', ...libs, '-o', path.join(build, 'BridgeTemplate.vst3')]);
    run([...common, '-O1', '-municode', 'native/bridge/probe.cpp', 'native/bridge/engine.cpp', ...libs, '-o', path.join(build, 'BridgeProbe.exe')]);
    for (const name of ['BridgeTemplate.vst3', 'BridgeProbe.exe']) {
      const meta = inspect(path.join(build, name));
      if (meta.machine !== 0x8664 || (name.endsWith('.vst3') && !meta.exports.includes('GetPluginFactory'))) throw Error('Invalid Windows x64 output: ' + name);
    }
    for (const name of ['BridgeTemplate.vst3', 'BridgeProbe.exe']) fs.copyFileSync(path.join(build, name), path.join(resources, name));
    console.log('Windows x64 bridge and probe built in resources/.');
  }
} catch (e) { console.error(e.code === 'ENOENT' ? 'Zig was not found. Install Zig 0.14.1 or set ZIG to its executable.' : e.message); process.exitCode = 1; }
