// SPDX-License-Identifier: MIT
'use strict';
const fs = require('node:fs'), path = require('node:path'), crypto = require('node:crypto');
const { Transform } = require('node:stream');
const { pipeline } = require('node:stream/promises');
const yazl = require('yazl');
const core = require('./core'), { inspect } = require('./pe');
const fixed = { mtime: new Date('2026-01-01T00:00:00Z'), mode: 0o100644, compressionLevel: 6 };
async function createZip(destination, entries, signal) {
  const zip = new yazl.ZipFile();
  let active;
  const output = fs.createWriteStream(destination, { flags: 'wx' });
  const done = pipeline(zip.outputStream, output, { signal });
  zip.on('error', e => zip.outputStream.destroy(e));
  done.catch(e => { active?.destroy(e); });
  try {
    for (const entry of entries) {
      signal?.throwIfAborted();
      if (!core.safeRelative(entry.name)) throw Error('Invalid ZIP entry');
      if (entry.file) {
        const before = fs.statSync(entry.file, { bigint: true }), hash = crypto.createHash('sha256');
        active = fs.createReadStream(entry.file);
        const transform = new Transform({ transform(chunk, _, cb) { hash.update(chunk); cb(null, chunk); } });
        zip.addReadStream(transform, entry.name, { ...fixed, size: Number(before.size) });
        await pipeline(active, transform, { signal });
        const after = fs.statSync(entry.file, { bigint: true });
        if (before.size !== after.size || before.mtimeNs !== after.mtimeNs) throw Error('Source changed during packaging: ' + entry.file);
        entry.hashed?.(hash.digest('hex'));
      } else {
        const value = typeof entry.data === 'function' ? entry.data() : entry.data;
        zip.addBuffer(Buffer.isBuffer(value) ? value : Buffer.from(value), entry.name, fixed);
      }
    }
    zip.end(); await done;
  } catch (e) {
    active?.destroy(); zip.outputStream.destroy(e); output.destroy(); await done.catch(() => {});
    fs.rmSync(destination, { force: true }); throw e;
  }
}
async function packagePlugin(source, plugin, output, template, options = {}) {
  const root = core.rootPath(source), p = core.plan(root, plugin, options), validation = options.validation;
  if (['blocked', 'unsupported'].includes(p.status)) throw Error('Этот плагин пока нельзя упаковать: ' + p.issues.filter(i => i.severity === 'error').map(i => i.message).join('; '));
  if (['failed', 'timeout'].includes(validation?.status)) throw Error('Плагин не прошёл проверку запуска. Повторите проверку в Windows после исправления зависимостей.');
  const meta = inspect(template);
  if (meta.machine !== core.X64 || !meta.exports.includes('GetPluginFactory')) throw Error('Шаблон VST3 x64 не найден или повреждён.');
  const out = core.expand(output); fs.mkdirSync(out, { recursive: true });
  if (core.under(root, fs.realpathSync(out))) throw Error('Выберите папку для архивов вне исходной папки FL Studio.');
  const name = core.safeName(plugin.name) + ' FL Bridge', bundle = name + '.vst3', prefix = bundle + '/Contents/x86_64-win/';
  const manifest = {
    format: 2, mode: 'installed', converter_version: core.VERSION, bridge_version: core.BRIDGE_VERSION,
    plugin: { name: plugin.name, path: plugin.path, kind: plugin.kind }, uid: p.uid,
    requirements: { installation: 'FL Studio for Windows x64', dependencies: p.dependency_order, resources: p.files.filter(f => f.role === 'asset').map(f => f.path) },
    // Detailed probe logs contain local paths; keep them in the user's profile.
    validation: { status: validation?.status || 'not-run' },
    limitations: ['Requires the user-owned Windows x64 FL Studio installation on the target computer.', 'No vendor binaries, presets, samples or fonts are included.'], sha256: {}
  };
  const legalRoot = path.join(__dirname, '..');
  const payload = [
    { name: prefix + name + '.vst3', file: template },
    { name: 'LICENSES/NativePluginConverter-MIT.txt', file: path.join(legalRoot, 'LICENSE') },
    ...['Steinberg-VST3.txt', 'LLVM-libcxx.txt', 'mingw-w64.txt'].map(name => ({ name: 'LICENSES/' + name, file: path.join(legalRoot, 'LICENSES', name) }))
  ];
  for (const entry of payload) entry.hashed = digest => { manifest.sha256[entry.name] = digest; };
  const readme = `${plugin.name} — FL Bridge, Windows x64 VST3\n\nЭто только мост. DLL, пресеты, шрифты и сэмплы FL Studio в ZIP не включены.\nНа компьютере с DAW нужна собственная установленная FL Studio для Windows x64\nс этим плагином и его ресурсами. После удаления FL Studio мост работать не будет.\n\nЗакройте DAW и скопируйте папку ${bundle} целиком в\nC:\\Program Files\\Common Files\\VST3\\\nЗатем выполните сканирование плагинов в DAW.\n\nПоиск установки (по приоритету):\n1. installation.root в bridge.ini рядом с внутренним .vst3-модулем;\n2. переменная среды NPC_FL_STUDIO_ROOT;\n3. папка, выбранная в NativePluginConverter на этом компьютере;\n4. единственная подходящая установка в Program Files/Image-Line.\n\nДля нестандартной папки откройте NativePluginConverter в Windows и найдите\nплагины в нужной установке. Путь сохраняется локально, не в ZIP.\nМожно вместо этого указать полный путь в [installation] root в bridge.ini.\nЕсли найдено несколько установок, выберите одну явно. После изменения пути\nперезапустите DAW. Не копируйте DLL в папку моста.\n\nАрхивы, собранные на macOS/Linux, также предназначены для Windows x64.\nЗаводские пресеты читаются из выбранной установки FL Studio; выбранное\nсостояние сохраняется с проектом DAW. Обновление FL Studio может изменить\nсовместимость. Лицензия MIT относится к собственному коду моста, а не к FL Studio.\n`;
  const entries = [...payload,
    { name: prefix + 'bridge.ini', data: core.config(p, validation) },
    { name: 'SOURCE.txt', data: 'NativePluginConverter: https://github.com/h4s10/NativePluginConverter\nConverter ' + core.VERSION + '; bridge ' + core.BRIDGE_VERSION + '.\nProject-owned code: MIT. Third-party notices are in LICENSES/.\n' },
    { name: 'manifest.json', data: () => JSON.stringify(manifest, null, 2) },
    { name: 'README_RU.txt', data: readme }
  ];
  const temp = path.join(out, '.flbridge-' + crypto.randomUUID() + '.zip.tmp');
  let dest;
  try {
    await createZip(temp, entries, options.signal); options.signal?.throwIfAborted();
    for (let i = 1; ; i++) {
      dest = path.join(out, name + (i === 1 ? '' : '-' + i) + '.zip');
      try { fs.closeSync(fs.openSync(dest, 'wx')); break; } catch (e) { if (e.code !== 'EEXIST') throw e; }
    }
    try { fs.renameSync(temp, dest); } catch (e) { fs.rmSync(dest, { force: true }); throw e; }
    return { path: dest, name: path.basename(dest), size: fs.statSync(dest).size, files_count: entries.length, manifest };
  } finally { fs.rmSync(temp, { force: true }); }
}
module.exports = { createZip, packagePlugin };
