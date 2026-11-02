// SPDX-License-Identifier: MIT
'use strict';
const path = require('node:path'), fs = require('node:fs');
(async () => {
  const { packager } = await import('@electron/packager');
  const platform = process.argv[2] || process.platform, arch = process.argv[3] || process.arch;
  if (!['win32', 'darwin', 'linux'].includes(platform) || !['x64', 'arm64'].includes(arch)) throw Error('Unsupported desktop target');
  const root = path.resolve(__dirname, '..');
  for (const name of ['BridgeTemplate.vst3', 'BridgeProbe.exe']) {
    if (!fs.existsSync(path.join(root, 'resources', name))) throw Error('Missing native resources. Run npm run build:native first.');
  }
  const buildTemp = fs.mkdtempSync(path.join(require('node:os').tmpdir(), 'fl-converter-build-'));
  let paths;
  try { paths = await packager({ tmpdir: buildTemp, dir: root, name: 'NativePluginConverter', executableName: 'NativePluginConverter', appBundleId: 'local.nativepluginconverter.desktop', appVersion: require('../package.json').version, platform, arch, out: path.join(root, 'dist'), overwrite: true, asar: true, prune: true,
    ignore: [/^\/dist(?:\/|$)/, /^\/tests(?:\/|$)/, /^\/scripts(?:\/|$)/, /^\/resources(?:\/|$)/, /^\/native(?:\/|$)/, /^\/verification(?:\/|$)/, /^\/build(?:\/|$)/, /^\/\.git(?:\/|$)/, /^\/\.github(?:\/|$)/, /\.log$/],
    extraResource: [path.join(root, 'resources'), path.join(root, 'LICENSE'), path.join(root, 'LICENSES'), path.join(root, 'THIRD_PARTY_NOTICES.md')],
    // Developer ID / Windows signing certificates can be supplied by the owner later.
    ...(platform === 'darwin' ? { darwinDarkModeSupport: true } : {})
  }); } finally { fs.rmSync(buildTemp, { recursive: true, force: true }); }
  for (const p of paths) {
    if (platform === 'darwin') require('node:child_process').execFileSync('codesign', ['--force', '--deep', '--sign', '-', path.join(p, 'NativePluginConverter.app')], { stdio: 'inherit' });
    console.log(p);
  }
})().catch(e => { console.error(e); process.exitCode = 1; });
