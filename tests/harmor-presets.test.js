// SPDX-License-Identifier: MIT
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const { harmorFactoryPaths } = require('../src/preset-paths');
const { fixture, image } = require('./fixtures');
const core = require('../src/core');
function sized(data) { const n = Buffer.alloc(4); n.writeUInt32LE(data.length); return Buffer.concat([n, data]); }
function event(id, data) { const bytes = [id]; let n = data.length; do { let b = n & 127; n >>>= 7; bytes.push(b | (n ? 128 : 0)); } while (n); return Buffer.concat([Buffer.from(bytes), data]); }
function fst(path, name = 'Harmor', version = 16) {
  const top = Buffer.alloc(8); top.writeUInt32LE(version); top.writeUInt32LE(7, 4);
  const text = Buffer.from(path), state = Buffer.concat([top, sized(Buffer.concat([Buffer.from([0, 0, text.length]), text])), sized(Buffer.alloc(0)), Buffer.from('L\0')]);
  const events = Buffer.concat([event(201, Buffer.from(name + '\0')), event(213, state)]), header = Buffer.alloc(22);
  header.write('FLhd'); header.writeUInt32LE(6, 4); header[8] = 0x30; header.write('FLdt', 14); header.writeUInt32LE(events.length, 18);
  return Buffer.concat([header, events]);
}
test('Harmor reads the part-bounded shortstring despite a printable following global byte', () => {
  const path = '%FLStudioFactoryData%\\Data\\Patches\\Packs\\Legacy\\STR_Anty_A3.wav';
  for (const version of [11, 12, 16, 17, 0x130003]) assert.deepEqual(harmorFactoryPaths(fst(path, 'Harmor', version)), [path]);
  assert.deepEqual(harmorFactoryPaths(fst(path, 'Other')), []);
  assert.deepEqual(harmorFactoryPaths(fst(path, 'Harmor', 99)), []);
  assert.deepEqual(harmorFactoryPaths(fst(path).subarray(0, -1)), []);
  const malformed = fst(path); malformed[malformed.indexOf(Buffer.from(path)) - 1] = 1;
  assert.deepEqual(harmorFactoryPaths(malformed), []);
});
test('Harmor includes its exact external sample plus SG closure and fonts, without unused decoders', t => {
  const f = fixture(t), e = { ...core.discover(f.root)[0], name: 'Harmor', kind: 'instrument' };
  const reference = '%FLStudioFactoryData%\\Data\\Patches\\Packs\\Legacy\\STR_Anty_A3.wav';
  f.write('Data/Patches/Plugin presets/Generators/Harmor/Pad/Atlas.fst', fst(reference));
  f.write('Data/Patches/Packs/Legacy/STR_Anty_A3.wav', 'selected');
  f.write('Data/Patches/Packs/Legacy/Unused.wav', 'not selected');
  f.write('Shared/SG_x64.dll', image({ exports: ['CreatePlugInstance'], imports: ['sg_helper.dll'], dynamic: ['Cuprum.ttf'] }));
  f.write('Shared/sg_helper.dll', image()); f.write('Shared/Artwork/Fonts/Cuprum.ttf', 'font');
  f.write('Shared/wavpackdll_x64.dll', image()); f.write('Shared/oggio_x64.dll', image());
  const p = core.plan(f.root, e), names = p.files.map(f => f.path);
  assert.equal(p.status, 'experimental');
  assert(names.includes('Data/Patches/Packs/Legacy/STR_Anty_A3.wav'));
  assert(names.includes('Shared/SG_x64.dll')); assert(names.includes('Shared/sg_helper.dll')); assert(names.includes('Shared/Artwork/Fonts/Cuprum.ttf'));
  assert(!names.includes('Data/Patches/Packs/Legacy/Unused.wav')); assert(!names.includes('Shared/wavpackdll_x64.dll')); assert(!names.includes('Shared/oggio_x64.dll'));
  assert(p.dependency_order.indexOf('Shared/sg_helper.dll') < p.dependency_order.indexOf('Shared/SG_x64.dll'));
});
test('Harmor rejects missing or x86 SG helper while effects do not acquire it', t => {
  const f = fixture(t), original = core.discover(f.root)[0], e = { ...original, name: 'Harmor', kind: 'instrument' };
  assert.equal(core.plan(f.root, e).status, 'blocked');
  f.write('Shared/SG_x64.dll', image({ machine: 0x14c }));
  assert.equal(core.plan(f.root, e).status, 'blocked');
  assert.equal(core.plan(f.root, original).status, 'experimental');
});
