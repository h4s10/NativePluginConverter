// SPDX-License-Identifier: MIT
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const { fixture, image } = require('./fixtures');
const core = require('../src/core');
const names = planned => planned.files.map(f => f.path);
function convolver(t) {
  const f = fixture(t), entry = { ...core.discover(f.root)[0], name: 'Fruity Convolver' };
  const preset = (name, data) => f.write('Data/Patches/Plugin presets/Effects/Fruity Convolver/' + name + '.fst', data);
  return { ...f, entry, preset };
}
test('length-prefixed FLStudioData WV reference selects only its impulse and decoder closure', t => {
  const f = convolver(t);
  f.preset('Blur', '\x016%FLStudioData%\\Patches\\Impulses\\Special\\White Noise.wv\0');
  f.write('Data/Patches/Impulses/Special/White Noise.wv', 'selected impulse');
  f.write('Data/Patches/Impulses/Special/Unused.wv', 'unrelated impulse');
  f.write('Shared/wavpackdll_x64.dll', image({ imports: ['wavpack_support.dll'] }));
  f.write('Shared/wavpack_support.dll', image());
  f.write('Shared/wavpackdll.dll', image({ machine: 0x14c }));
  const p = core.plan(f.root, f.entry), files = names(p);
  assert.equal(p.status, 'experimental');
  assert(files.includes('Data/Patches/Impulses/Special/White Noise.wv'));
  assert(!files.includes('Data/Patches/Impulses/Special/Unused.wv'));
  assert(files.includes('Shared/wavpackdll_x64.dll'));
  assert(files.includes('Shared/wavpack_support.dll'));
  assert(!files.includes('Shared/wavpackdll.dll'));
  assert(p.dependency_order.indexOf('Shared/wavpack_support.dll') < p.dependency_order.indexOf('Shared/wavpackdll_x64.dll'));
});
test('UTF16 factory token inside literal resolves case-insensitive exact source path', t => {
  const f = convolver(t);
  f.preset('Factory', Buffer.from('prefix%FLStudioFactoryData%\\data\\patches\\packs\\selected.wav\0', 'utf16le'));
  f.write('Data/Patches/Packs/Selected.wav', 'selected sample');
  f.write('Shared/wavpackdll_x64.dll', image());
  const p = core.plan(f.root, f.entry), files = names(p);
  assert(files.includes('Data/Patches/Packs/Selected.wav'));
  assert(!files.includes('Shared/wavpackdll_x64.dll'));
});
test('ambiguous Convolver USERPROFILE paths warn with counts without guessing a replacement', t => {
  const f = convolver(t), legacy = '%USERPROFILE%\\Dropbox\\Impulses\\Cabinet.wv\0';
  f.preset('Legacy A', legacy); f.preset('Legacy B', legacy);
  f.write('Data/Patches/Impulses/Cabinet.wv', 'first candidate');
  f.write('Data/Patches/Impulses/Another/Cabinet.wv', 'ambiguous second candidate');
  f.write('Shared/wavpackdll_x64.dll', image());
  const p = core.plan(f.root, f.entry), warning = p.issues.find(i => i.code === 'legacy-preset-paths');
  assert.equal(warning.severity, 'warning'); assert.match(warning.message, /^2 пресетов содержат 1 ссылок/);
  assert(!names(p).includes('Data/Patches/Impulses/Cabinet.wv'));
  assert(!names(p).includes('Data/Patches/Impulses/Another/Cabinet.wv'));
  assert(!names(p).includes('Shared/wavpackdll_x64.dll'));
});
test('WV decoder requirement is scoped to selected Convolver assets', t => {
  const f = convolver(t);
  f.preset('Missing', '%FLStudioData%\\Patches\\Impulses\\Missing.wv\0');
  f.write('Shared/wavpackdll_x64.dll', image());
  const p = core.plan(f.root, f.entry);
  assert(!names(p).includes('Shared/wavpackdll_x64.dll'));
  assert(p.issues.some(i => i.code === 'missing-factory-samples'));
  f.write('Data/Patches/Plugin presets/Effects/Test FX/Other.fst', '%FLStudioData%\\Patches\\Impulses\\Present.wv\0');
  f.write('Data/Patches/Impulses/Present.wv', 'data');
  const generic = core.plan(f.root, { ...f.entry, name: 'Test FX' });
  assert(names(generic).includes('Data/Patches/Impulses/Present.wv'));
  assert(!names(generic).includes('Shared/wavpackdll_x64.dll'));
});
test('missing or x86-only WavPack decoder blocks selected Convolver WV presets', t => {
  const f = convolver(t);
  f.preset('Required', '%FLStudioData%\\Patches\\Impulses\\Required.wv\0');
  f.write('Data/Patches/Impulses/Required.wv', 'data');
  f.write('Shared/wavpackdll_x64.dll', image({ machine: 0x14c }));
  const p = core.plan(f.root, f.entry);
  assert.equal(p.status, 'blocked'); assert(p.issues.some(i => i.code === 'missing-preset-decoder'));
});
test('token extraction rejects path traversal and non-asset suffixes', t => {
  const f = convolver(t);
  f.preset('Bad', '%FLStudioData%\\..\\Secret.wav\0%FLStudioFactoryData%\\Data\\Patches\\Impulses\\Evil.wav.exe\0');
  f.write('Secret.wav', 'not a legitimate resource reference');
  f.write('Data/Patches/Impulses/Evil.wav', 'not the referenced extension');
  const p = core.plan(f.root, f.entry);
  assert(!names(p).includes('Secret.wav'));
  assert(!names(p).includes('Data/Patches/Impulses/Evil.wav'));
});
test('preset folder matching preserves actual spelling while ignoring plugin-name case', t => {
  const f = fixture(t), entry = { ...core.discover(f.root)[0], name: 'LuxeVerb' };
  f.write('Data/Patches/Plugin presets/Effects/lUxEvErB/Default.fst', 'factory preset');
  f.write('Data/Patches/Plugin presets/Effects/LuxeVerb Extra/Other.fst', 'another plugin');
  const p = core.plan(f.root, entry);
  assert(names(p).includes('Data/Patches/Plugin presets/Effects/lUxEvErB/Default.fst'));
  assert(!names(p).some(p => p.includes('LuxeVerb Extra')));
});

test('Convolver legacy lookup selects a unique case-insensitive basename only inside Impulses', t => {
  const f = convolver(t);
  f.preset('Legacy', '%USERPROFILE%\\Dropbox\\Impulses\\CABINET.WV\0');
  f.write('Data/Patches/Impulses/Guitar Cabinet/Cabinet.wv', 'factory IR');
  f.write('Plugins/Fruity/Effects/Distructor/module/impulses/Cabinet.wv', 'outside the permitted tree');
  f.write('Data/Patches/Impulses/Guitar Cabinet/Unreferenced.wv', 'unrelated IR');
  f.write('Shared/wavpackdll_x64.dll', image());
  const p = core.plan(f.root, f.entry), files = names(p);
  assert(files.includes('Data/Patches/Impulses/Guitar Cabinet/Cabinet.wv'));
  assert(files.includes('Shared/wavpackdll_x64.dll'));
  assert(!files.includes('Plugins/Fruity/Effects/Distructor/module/impulses/Cabinet.wv'));
  assert(!files.includes('Data/Patches/Impulses/Guitar Cabinet/Unreferenced.wv'));
  assert(!p.issues.some(i => i.code === 'legacy-preset-paths'));
  assert.equal(p.issues.find(i => i.code === 'relocated-preset-paths').severity, 'info');
  assert(p.files.find(f => f.path.endsWith('/Cabinet.wv')).reasons.some(r => r.includes('unique basename within Data/Patches/Impulses')));
});
test('Convolver legacy lookup does not search other plugin folders or accept traversal', t => {
  const f = convolver(t);
  f.preset('Outside', '%USERPROFILE%\\Dropbox\\Outside.wv\0');
  f.preset('Traversal', '%USERPROFILE%\\..\\Cabinet.wv\0');
  f.write('Plugins/Fruity/Effects/Distructor/module/impulses/Outside.wv', 'not a valid source');
  f.write('Data/Patches/Impulses/Cabinet.wv', 'traversal must not map here');
  const p = core.plan(f.root, f.entry);
  assert(!names(p).includes('Plugins/Fruity/Effects/Distructor/module/impulses/Outside.wv'));
  assert(!names(p).includes('Data/Patches/Impulses/Cabinet.wv'));
  assert.match(p.issues.find(i => i.code === 'legacy-preset-paths').message, /^2 пресетов содержат 2 ссылок/);
});
test('other plugins retain USERPROFILE warnings even with a unique Impulses basename', t => {
  const f = fixture(t), entry = core.discover(f.root)[0];
  f.write('Data/Patches/Plugin presets/Effects/Test FX/Legacy.fst', '%USERPROFILE%\\Dropbox\\Cabinet.wv\0');
  f.write('Data/Patches/Impulses/Cabinet.wv', 'no generic fallback');
  const p = core.plan(f.root, entry);
  assert(!names(p).includes('Data/Patches/Impulses/Cabinet.wv'));
  assert(p.issues.some(i => i.code === 'legacy-preset-paths'));
  assert(!p.issues.some(i => i.code === 'relocated-preset-paths'));
});

test('legacy Impulses lookup matches native depth16 and hidden-entry boundaries', t => {
  const f = convolver(t), within = 'Data/Patches/Impulses/' + 'a/'.repeat(16) + 'Cabinet.wv';
  f.preset('Legacy', '%USERPROFILE%\\Dropbox\\Cabinet.wv\0');
  f.write(within, 'only reachable candidate');
  f.write('Data/Patches/Impulses/' + 'a/'.repeat(17) + 'Cabinet.wv', 'beyond native depth');
  f.write('Data/Patches/Impulses/.hidden/Cabinet.wv', 'ignored by native traversal');
  f.write('Shared/wavpackdll_x64.dll', image());
  const p = core.plan(f.root, f.entry);
  assert(names(p).includes(within));
  assert.equal(p.files.filter(f => f.path.endsWith('/Cabinet.wv')).length, 1);
  assert(!p.issues.some(i => i.code === 'legacy-preset-paths'));
});
