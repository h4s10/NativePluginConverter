// SPDX-License-Identifier: MIT
'use strict';
const fs = require('node:fs'), path = require('node:path'), os = require('node:os');
function image({ exports = [], imports = [], dynamic = [], machine = 0x8664 } = {}) {
  const d = Buffer.alloc(0x4000); d.write('MZ'); d.writeUInt32LE(0x80, 0x3c); d.write('PE\0\0', 0x80); d.writeUInt16LE(machine, 0x84); d.writeUInt16LE(1, 0x86); d.writeUInt16LE(240, 0x94);
  const opt = 0x98; d.writeUInt16LE(0x20b, opt); d.writeBigUInt64LE(0x180000000n, opt + 24); d.writeUInt32LE(0x400, opt + 60); d.writeUInt32LE(16, opt + 108);
  const sec = opt + 240; d.write('.rdata', sec); [0x3000, 0x1000, 0x3000, 0x400].forEach((v, i) => d.writeUInt32LE(v, sec + 8 + i * 4));
  const off = r => r - 0x1000 + 0x400; let cursor = 0x1800;
  const str = s => { const r = cursor; const b = Buffer.from(s + '\0'); b.copy(d, off(r)); cursor += b.length; return r; };
  if (exports.length) { d.writeUInt32LE(0x1000, opt + 112); d.writeUInt32LE(40, opt + 116); d.writeUInt32LE(exports.length, off(0x1000) + 24); d.writeUInt32LE(0x1100, off(0x1000) + 32); exports.forEach((s, i) => d.writeUInt32LE(str(s), off(0x1100) + 4 * i)); }
  if (imports.length) { d.writeUInt32LE(0x1200, opt + 120); d.writeUInt32LE((imports.length + 1) * 20, opt + 124); imports.forEach((s, i) => d.writeUInt32LE(str(s), off(0x1200) + i * 20 + 12)); }
  dynamic.forEach(str); return d;
}
function fixture(t) {
  const base = fs.mkdtempSync(path.join(os.tmpdir(), 'fl-electron-test-')), root = path.join(base, 'FL'), dir = 'Plugins/Fruity/Effects/Test FX', main = dir + '/Test FX_x64.dll';
  t.after(() => fs.rmSync(base, { recursive: true, force: true }));
  const write = (name, data) => { const p = path.join(root, name); fs.mkdirSync(path.dirname(p), { recursive: true }); fs.writeFileSync(p, data); return p; };
  write(main, image({ exports: ['CreatePlugInstance'], imports: ['kernel32.dll'], dynamic: ['QuickFontCache.dll', 'unused.dll'] }));
  write('Shared/QuickFontCache_x64.dll', image({ imports: ['freetype_x64.dll'] })); write('Shared/QuickFontCache.dll', image({ machine: 0x14c })); write('Shared/freetype_x64.dll', image()); write('Shared/unused.dll', image()); write('Shared/ILTools_x64.dll', image({ exports: ['CreatePluginPathManagerInstance'] })); write(dir + '/Artwork/skin.bmp', 'asset');
  const template = path.join(base, 'template.vst3'); fs.writeFileSync(template, image({ exports: ['GetPluginFactory'] }));
  return { base, root, dir, main, write, template };
}
module.exports = { image, fixture };
