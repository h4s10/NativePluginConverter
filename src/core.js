// SPDX-License-Identifier: MIT
'use strict';
const fs = require('node:fs'), path = require('node:path'), os = require('node:os'), crypto = require('node:crypto');
const { inspect, literals } = require('./pe');
const { harmorFactoryPaths } = require('./preset-paths');
const P = require('./profiles.json');
const VERSION = '0.8.0', BRIDGE_VERSION = '0.6.1', X64 = 0x8664;
const lower = s => s.toLowerCase(), cmp = (a, b) => a < b ? -1 : a > b ? 1 : 0;
const sort = values => values.sort((a, b) => cmp(lower(a), lower(b)));
function expand(s) { return path.resolve(s.startsWith('~/') ? path.join(os.homedir(), s.slice(2)) : s); }
function rootPath(s) { const p = fs.realpathSync(expand(s)); if (!fs.statSync(p).isDirectory()) throw Error('Папка FL Studio не найдена.'); return p; }
function under(root, p) { const r = path.relative(root, p); return r === '' || (!r.startsWith('..' + path.sep) && r !== '..' && !path.isAbsolute(r)); }
function inside(root, p) { const real = fs.realpathSync(p); if (!under(root, real)) throw Error('Path is outside the selected FL Studio folder'); return real; }
function rel(root, p) { return path.relative(root, inside(root, p)).split(path.sep).join('/'); }
function safeRelative(s) { return typeof s === 'string' && s.length > 0 && !s.includes('\0') && !s.includes('\\') && !s.includes(':') && !s.startsWith('/') && !s.split('/').some(x => x === '..'); }
function issue(severity, code, message, file) { return { severity, code, message, ...(file ? { path: file } : {}) }; }
function filesBelow(dir) {
  if (!fs.existsSync(dir) || fs.lstatSync(dir).isSymbolicLink()) return [];
  const out = [];
  for (const e of fs.readdirSync(dir, { withFileTypes: true }).sort((a, b) => cmp(a.name, b.name))) {
    const p = path.join(dir, e.name);
    if (e.isSymbolicLink()) continue;
    if (e.isDirectory()) out.push(...filesBelow(p)); else if (e.isFile()) out.push(p);
  }
  return out;
}
function uid(relative) {
  const hash = crypto.createHash('sha1').update(Buffer.from('d1c2b8ae0c3b44db942ef055a21530ea', 'hex')).update(lower(relative)).digest().subarray(0, 16);
  hash[6] = (hash[6] & 15) | 0x50; hash[8] = (hash[8] & 63) | 0x80;
  return hash.toString('hex').toUpperCase();
}
function discover(source) {
  const root = rootPath(source), base = path.join(root, 'Plugins', 'Fruity'), entries = [];
  if (!fs.existsSync(base)) throw Error('В папке нет Plugins/Fruity. Выберите корень Windows-версии FL Studio.');
  for (const [category, kind] of [['Generators', 'instrument'], ['Effects', 'effect']]) {
    const folder = path.join(base, category); if (!fs.existsSync(folder)) continue;
    for (const d of fs.readdirSync(folder, { withFileTypes: true }).sort((a, b) => cmp(lower(a.name), lower(b.name)))) {
      if (!d.isDirectory() || d.isSymbolicLink()) continue;
      const dir = inside(root, path.join(folder, d.name)), candidates = [], issues = [];
      for (const f of fs.readdirSync(dir, { withFileTypes: true }).sort((a, b) => cmp(lower(a.name), lower(b.name)))) {
        if (!f.isFile() || !lower(f.name).endsWith('.dll')) continue;
        const file = path.join(dir, f.name);
        try { const meta = inspect(file); if (meta.exports.includes('CreatePlugInstance')) candidates.push({ file, meta }); }
        catch (e) { issues.push(issue('warning', 'unreadable-pe', e.message, rel(root, file))); }
      }
      const expected = lower(d.name + '_x64.dll');
      const rank = c => [c.meta.machine !== X64, lower(path.basename(c.file)) !== expected, lower(path.basename(c.file, '.dll')).includes('eng'), lower(path.basename(c.file))];
      candidates.sort((a, b) => compareTuple(rank(a), rank(b)));
      let relative, architecture, status;
      if (candidates.length) {
        const { file, meta } = candidates[0]; relative = rel(root, file); architecture = meta.machine === X64 ? 'x64' : '0x' + meta.machine.toString(16); status = 'experimental';
        if (meta.machine !== X64) { status = 'unsupported'; issues.push(issue('error', 'architecture', 'Нужна DLL Windows x64.')); }
      } else { relative = rel(root, dir); architecture = 'unknown'; status = 'unsupported'; issues.push(issue('error', 'no-entrypoint', 'Нет DLL с экспортом CreatePlugInstance.')); }
      const reason = lower(d.name).startsWith('vfx ') ? 'MIDI-маршрутизация Patcher/VFX не реализована этим аудиомостом.' : P.UNSUPPORTED_NAMES[lower(d.name)];
      if (reason) { status = 'unsupported'; issues.push(issue('error', 'host-service', reason)); }
      entries.push({ id: crypto.createHash('sha256').update(lower(relative)).digest('hex').slice(0, 20), name: d.name, kind, path: relative, status, issues, architecture });
    }
  }
  return entries.sort((a, b) => compareTuple([a.kind, lower(a.name)], [b.kind, lower(b.name)]));
}
function compareTuple(a, b) { for (let i = 0; i < a.length; i++) { const c = cmp(a[i], b[i]); if (c) return c; } return 0; }
class Resolver {
  constructor(root) {
    this.root = rootPath(root); this.byname = new Map(); this.byrelative = new Map();
    for (const file of filesBelow(this.root)) {
      this.byrelative.set(lower(rel(this.root, file)), file);
      if (lower(file).endsWith('.dll')) { const key = lower(path.basename(file)); if (!this.byname.has(key)) this.byname.set(key, []); this.byname.get(key).push(file); }
    }
  }
  find(name, source, preferX64 = false, pluginDir = path.dirname(source)) {
    name = name.replaceAll('\\', '/');
    if (/^\/[^/]+$/.test(name)) name = name.slice(1);
    if (!safeRelative(name)) return null;
    const base = path.posix.basename(name), names = [base], possible = [];
    if (preferX64 && !lower(base).endsWith('_x64.dll')) names.unshift(base.slice(0, -4) + '_x64.dll');
    for (const [i, n] of names.entries()) for (const f of this.byname.get(lower(n)) || []) {
      try { if (inspect(f).machine !== X64) continue; } catch { continue; }
      const relative = rel(this.root, f), same = path.dirname(f) === path.dirname(source), owned = under(pluginDir, f);
      if (['engine.dll', 'engine_x64.dll'].includes(lower(n)) && !(same || owned)) continue;
      possible.push({ f, priority: [i, !same, !owned, !lower(relative).endsWith(lower(name)), !relative.startsWith('Shared/'), relative.length, lower(relative)] });
    }
    possible.sort((a, b) => compareTuple(a.priority, b.priority)); return possible[0]?.f || null;
  }
}
function system(name) { const base = lower(path.posix.basename(name.replaceAll('\\', '/'))); return P.SYSTEM.includes(base) || P.SYSTEM.includes(base.replace(/\.dll$/, '')) || /^(api-ms-win-|ext-ms-win-)/.test(base); }
function validateEntry(root, plugin) {
  if (!plugin || !safeRelative(plugin.path)) throw Error('Invalid plugin path');
  const parts = plugin.path.split('/');
  if (parts.length < 4 || parts[0] !== 'Plugins' || parts[1] !== 'Fruity' || !['Generators', 'Effects'].includes(parts[2])) throw Error('Not an FL native plugin path');
  return inside(root, path.join(root, plugin.path));
}
function plan(source, plugin, options = {}) {
  const root = rootPath(source), main = validateEntry(root, plugin), issues = [...(plugin.issues || [])], files = new Map(), order = [], visiting = new Set(), optional = new Map();
  const resolver = options.resolver || new Resolver(root), id = uid(plugin.path);
  if (!fs.statSync(main).isFile() || plugin.status === 'unsupported') return { plugin, files: [], size: 0, dll_count: 0, issues, optional: [], uid: id, dependency_order: [], status: 'unsupported' };
  const meta = inspect(main);
  if (meta.machine !== X64 || !meta.exports.includes('CreatePlugInstance')) throw Error('Selected image is not an x64 FL native plugin');
  const pluginDir = path.dirname(main), selected = new Set(options.optionalDlls || []), runtime = new Set(options.runtimeModules || []), pname = lower(plugin.name);
  const ownedDirs = [pluginDir], resourceDirs = [pluginDir], embedded = [], extras = [];
  for (const relative of P.EMBEDDED_PLUGIN_PROFILES[pname] || []) {
    const child = resolver.byrelative.get(lower(relative));
    if (!child) { issues.push(issue('error', 'missing-embedded-plugin', `${plugin.name} использует встроенный плагин ${relative}; файл отсутствует.`)); continue; }
    const m = inspect(child);
    if (m.machine !== X64 || !m.exports.includes('CreatePlugInstance')) { issues.push(issue('error', 'invalid-embedded-plugin', `Встроенный плагин ${relative} должен быть нативной DLL x64.`)); continue; }
    embedded.push(child); ownedDirs.push(path.dirname(child)); resourceDirs.push(path.dirname(child));
  }
  for (const [relative, reason] of P.REQUIRED_MODULE_PROFILES[pname] || []) {
    const module = resolver.byrelative.get(lower(relative));
    if (!module) issues.push(issue('error', 'missing-profile-module', `${plugin.name} требует ${relative}; библиотека отсутствует.`));
    else if (inspect(module).machine !== X64) issues.push(issue('error', 'invalid-profile-module', `${relative} должна быть DLL x64.`));
    else { extras.push([module, reason]); if (relative.startsWith('Plugins/Fruity/')) ownedDirs.push(path.dirname(module)); }
  }
  function add(file, reason, role) {
    if (fs.lstatSync(file).isSymbolicLink()) throw Error('Symbolic links are not packaged');
    const relative = rel(root, file);
    if (!files.has(relative)) files.set(relative, { path: relative, size: fs.statSync(file).size, reasons: [], role });
    const f = files.get(relative); if (!f.reasons.includes(reason)) f.reasons.push(reason);
  }
  function walk(file, reason) {
    const relative = rel(root, file); add(file, reason, file === main ? 'plugin' : 'dependency');
    if (visiting.has(relative)) return; visiting.add(relative);
    const m = inspect(file, true), owner = ownedDirs.find(d => under(d, file)) || pluginDir;
    const refs = [...m.imports, ...m.dlls.map(n => [n, 'dynamic-string'])];
    if (under(owner, file)) for (const [dep, symbol] of m.symbols) {
      const opt = symbol === 'WaveformColouring_CreateInstance' || (dep === 'elastique_x64.dll' && !P.ELASTIQUE_REQUIRED_PLUGINS.includes(pname));
      refs.push([dep, (opt ? 'optional-symbol: ' : 'verified-symbol: ') + symbol]);
    }
    const seen = new Set();
    for (const [name, evidence] of refs) {
      const key = lower(name), seenKey = evidence.includes('-symbol: ') ? key + '\0' + evidence : key;
      if (seen.has(seenKey)) continue; seen.add(seenKey);
      const base = lower(path.posix.basename(name.replaceAll('\\', '/')));
      if (system(name)) continue;
      if (P.HOST_ONLY.includes(base)) { if (evidence !== 'dynamic-string') issues.push(issue('error', 'requires-fl-host', `${name}: требуется движок FL Studio.`, relative)); continue; }
      const dep = resolver.find(name, file, evidence === 'dynamic-string', owner);
      if (dep === file) continue;
      let required = (evidence !== 'dynamic-string' && !evidence.startsWith('optional-symbol: ')) || P.RUNTIME.includes(base) || (dep && under(owner, dep));
      if (P.DSP.includes(base) && lower(path.basename(file)) !== 'quickfontcache_x64.dll' && !evidence.startsWith('optional-symbol: ')) required = true;
      const profileReason = P.RUNTIME_REQUIRED_REFERENCES[pname]?.[base]; if (profileReason) required = true;
      if (dep && (selected.has(rel(root, dep)) || runtime.has(rel(root, dep)))) required = true;
      if (!required) { optional.set(key + '\0' + relative, { name, source: relative, evidence, ...(dep ? { path: rel(root, dep) } : {}) }); continue; }
      if (!dep) { issues.push(issue(evidence === 'dynamic-string' && !profileReason ? 'warning' : 'error', 'missing-dependency', `Не найдена x64-библиотека ${name} (${evidence}).`, relative)); continue; }
      if (evidence.startsWith('verified-symbol: ')) {
        const symbol = evidence.slice('verified-symbol: '.length);
        if (!inspect(dep).exports.includes(symbol)) issues.push(issue('error', 'missing-dynamic-export', `${path.basename(dep)} не экспортирует ${symbol}; версии плагина и библиотеки несовместимы.`, relative));
      }
      walk(dep, `${evidence}: ${relative} → ${name}${profileReason ? ' — ' + profileReason : ''}`);
    }
    order.push(relative);
  }
  const hostPaths = resolver.find('ILTools_x64.dll', main, false, pluginDir);
  if (!hostPaths) issues.push(issue('error', 'missing-host-service', 'Не найдена Shared/ILTools_x64.dll: она нужна мосту для нативного менеджера путей FL Studio.'));
  else { if (!inspect(hostPaths).exports.includes('CreatePluginPathManagerInstance')) issues.push(issue('error', 'missing-host-export', 'ILTools_x64.dll не содержит CreatePluginPathManagerInstance; версия не поддерживается.')); walk(hostPaths, 'Bridge host service: native FL path manager'); }
  walk(main, 'Selected FL native plugin');
  for (const child of embedded) walk(child, `Observed ${plugin.name} runtime: embedded native ${path.basename(path.dirname(child))} plugin`);
  for (const [module, reason] of extras) walk(module, reason);
  const helperSeen = new Set();
  function walkHelper(file, reason) {
    const relative = rel(root, file); if (helperSeen.has(relative)) return; helperSeen.add(relative);
    const m = inspect(file);
    if (m.machine !== 0x14c) { issues.push(issue('error', 'invalid-child-architecture', `${relative} должен быть оригинальным x86-модулем дочернего процесса.`)); return; }
    add(file, reason, 'bridge-helper');
    for (const [name, evidence] of m.imports) {
      if (system(name)) continue;
      const candidates = (resolver.byname.get(lower(path.posix.basename(name.replaceAll('\\', '/')))) || []).filter(f => { try { return inspect(f).machine === 0x14c; } catch { return false; } });
      candidates.sort((a, b) => compareTuple([path.dirname(a) !== path.dirname(file), path.dirname(a) !== path.join(root, 'Shared'), lower(rel(root, a))], [path.dirname(b) !== path.dirname(file), path.dirname(b) !== path.join(root, 'Shared'), lower(rel(root, b))]));
      if (!candidates.length) issues.push(issue('error', 'missing-child-dependency', `Для x86-процесса не найдена ${name}.`, relative));
      else walkHelper(candidates[0], `x86 ${evidence}: ${relative} → ${name}`);
    }
  }
  for (const relative of P.CHILD_PROCESS_PROFILES[pname] || []) {
    const helper = resolver.byrelative.get(lower(relative));
    if (!helper) issues.push(issue('error', 'missing-child-payload', `Для дочернего x86-процесса ${plugin.name} отсутствует ${relative}.`));
    else walkHelper(helper, `Original x86 child-process payload for ${plugin.name}; not preloaded into x64 host`);
  }
  if (pname === 'edison') issues.push(issue('warning', 'unverified-audio-restore', 'В пакет включён оригинальный x86 AudioRestore и его мост; запуск функции очистки AudioRestore ещё требует отдельной проверки.'));
  for (const relative of sort([...new Set([...runtime, ...selected])])) {
    if (!safeRelative(relative)) throw Error('Invalid dependency path');
    const f = inside(root, path.join(root, relative));
    if (!fs.statSync(f).isFile() || !lower(f).endsWith('.dll')) throw Error('Selected dependency is not a DLL');
    if (inspect(f).machine !== X64) throw Error('Selected dependency is not x64');
    if (P.HOST_ONLY.includes(lower(path.basename(f)))) throw Error('FL host engine cannot be packaged as a plugin dependency');
    walk(f, runtime.has(relative) ? 'Runtime observation' : 'User-selected optional feature');
  }
  for (const dir of resourceDirs) for (const asset of filesBelow(dir)) {
    if (path.basename(asset).startsWith('.') || ['.dll', '.exe', '.pdb', '.ilk', '.lib', '.obj'].includes(lower(path.extname(asset)))) continue;
    add(asset, 'Plugin-local resource: ' + path.basename(dir), 'asset');
  }
  const fonts = new Map();
  for (const [relative, file] of resolver.byrelative) if ((relative.startsWith('shared/artwork/fonts/') || relative.startsWith('artwork/fonts/')) && ['.ttf', '.otf', '.ilfont'].includes(lower(path.extname(file)))) {
    const key = lower(path.basename(file)); if (!fonts.has(key)) fonts.set(key, []); fonts.get(key).push(file);
  }
  for (const f of [...files.values()]) {
    if (['asset', 'bridge-helper'].includes(f.role)) continue;
    for (const literal of inspect(path.join(root, f.path), true).literals) if (/^[A-Za-z0-9_ !()+.,\\/\-]+\.(ttf|otf|ilfont)$/i.test(literal)) {
      for (const font of fonts.get(lower(path.posix.basename(literal.replaceAll('\\', '/')))) || []) add(font, 'Font filename in ' + f.path, 'asset');
    }
  }
  const presets = path.join(root, 'Data', 'Patches', 'Plugin presets', plugin.kind === 'instrument' ? 'Generators' : 'Effects', plugin.name);
  const presetPrefix = lower(path.relative(root, presets).split(path.sep).join('/')) + '/';
  const presetFiles = [...resolver.byrelative].filter(([r, f]) => r.startsWith(presetPrefix) && !path.basename(f).startsWith('.') && !['.dll', '.exe'].includes(lower(path.extname(f)))).map(([, f]) => f).sort();
  for (const f of presetFiles) add(f, 'Matching factory presets', 'asset');
  if (pname === 'flex') for (const f of filesBelow(path.join(root, 'Data/Patches/Packs/FLEX'))) {
    if (!path.basename(f).startsWith('.') && !['.dll', '.exe'].includes(lower(path.extname(f)))) add(f, 'FLEX factory sound bank', 'asset');
  }
  const missing = new Set(), legacyPaths = new Set(), legacyPresets = new Set(), relocatedPaths = new Set(), relocatedPresets = new Set();
  // Only Convolver has a verified native relocation path for the legacy factory
  // IR references. Search its Impulses tree exclusively and reject duplicate names.
  const impulseNames = new Map();
  if (pname === 'fruity convolver') for (const [relative, file] of resolver.byrelative) {
    if (!relative.startsWith('data/patches/impulses/')) continue;
    const parts = relative.slice('data/patches/impulses/'.length).split('/');
    if (parts.length > 17 || parts.some(p => p.startsWith('.'))) continue;
    const name = lower(path.basename(file));
    if (!impulseNames.has(name)) impulseNames.set(name, []);
    impulseNames.get(name).push(file);
  }
  // Native streams can put a printable length byte before an ASCII/UTF-16 path.
  // Resolve explicit FL data tokens inside bounded literals, never copy a whole
  // sample tree or follow arbitrary environment/absolute paths from a preset.
  const resourceExtension = /\.(?:wav|wv|ogg|flac|aiff?|sf2|wti|dat|dwp|bmp|png|jpg)(?=$|[\s"'<>;,)]|%)/gi;
  for (const f of presetFiles) {
    if (!['.fst', '.dwp', '.ini', '.json', '.xml', '.txt'].includes(lower(path.extname(f))) || fs.statSync(f).size > 32 * 1024 * 1024) continue;
    const data = fs.readFileSync(f), references = literals(data, 8, 4096);
    if (pname === 'harmor' && lower(path.extname(f)) === '.fst') references.push(...harmorFactoryPaths(data));
    for (const s of references) {
      const tokens = [...s.matchAll(/%(FLStudioFactoryData|FLStudioData|USERPROFILE)%/gi)];
      for (let i = 0; i < tokens.length; ++i) {
        const token = tokens[i], suffix = s.slice(token.index + token[0].length, tokens[i + 1]?.index ?? s.length);
        if (lower(token[1]) === 'userprofile') {
          const legacy = token[0] + suffix, relative = suffix.replaceAll('\\', '/').replace(/^\/+/, '');
          const endsWithAsset = [...suffix.matchAll(resourceExtension)].some(m => m.index + m[0].length === suffix.length);
          const matches = endsWithAsset && safeRelative(relative) ? impulseNames.get(lower(path.posix.basename(relative))) || [] : [];
          if (matches.length === 1) {
            add(matches[0], 'Convolver legacy impulse: unique basename within Data/Patches/Impulses for ' + legacy + ' in ' + rel(root, f), 'asset');
            relocatedPaths.add(legacy); relocatedPresets.add(rel(root, f));
          } else { legacyPaths.add(legacy); legacyPresets.add(rel(root, f)); }
          continue;
        }
        const base = lower(token[1]) === 'flstudiodata' ? 'Data/' : '';
        const candidates = [...suffix.matchAll(resourceExtension)].map(m => base + suffix.slice(0, m.index + m[0].length).replaceAll('\\', '/').replace(/^\/+/, '')).filter(safeRelative);
        // Prefer the longest existing path: a filename may itself contain an
        // extension-looking component. Missing references remain explicit.
        const match = candidates.reverse().map(r => [r, resolver.byrelative.get(lower(r))]).find(([, asset]) => asset);
        if (match) add(match[1], 'Factory-data path in ' + rel(root, f), 'asset');
        else if (candidates.length) missing.add(candidates[0]);
      }
    }
  }
  if (missing.size) issues.push(issue('warning', 'missing-factory-samples', `${missing.size} файлов из заводских пресетов отсутствуют в исходной папке; эти пресеты могут требовать отдельные банки.`));
  if (legacyPaths.size) issues.push(issue('warning', 'legacy-preset-paths', `${legacyPresets.size} пресетов содержат ${legacyPaths.size} ссылок на старые пути %USERPROFILE% без однозначного сопоставления. Эти пути не перенаправлены; соответствующие семплы нужно выбрать вручную в плагине.`));
  if (relocatedPaths.size) issues.push(issue('info', 'relocated-preset-paths', `Для ${relocatedPresets.size} пресетов Convolver сопоставлено ${relocatedPaths.size} старых путей %USERPROFILE%: точное имя файла однозначно найдено внутри Data/Patches/Impulses. Эти IR найдены в установке для восстановления пути при загрузке пресета.`));
  if (pname === 'fruity convolver' && [...files.values()].some(f => f.role === 'asset' && lower(f.path).endsWith('.wv'))) {
    const decoder = resolver.find('wavpackdll_x64.dll', main, false, pluginDir);
    if (decoder) walk(decoder, 'Preset asset decoder: selected Convolver WavPack impulse');
    else issues.push(issue('error', 'missing-preset-decoder', 'Для выбранных импульсов Convolver в формате WV нужна wavpackdll_x64.dll; библиотека отсутствует.'));
  }
  const list = [...files.values()].sort((a, b) => cmp(lower(a.path), lower(b.path)));
  return { plugin, files: list, size: list.reduce((n, f) => n + f.size, 0), dll_count: list.filter(f => lower(f.path).endsWith('.dll')).length, issues, optional: [...optional.values()].sort((a, b) => compareTuple([lower(a.name), a.source], [lower(b.name), b.source])), uid: id, dependency_order: order.filter(f => f !== plugin.path), status: issues.some(i => i.severity === 'error') ? 'blocked' : 'experimental' };
}
function config(planned, validation, installationRoot = '') {
  const p = planned.plugin;
  const fields = { name: p.name, source: 'installed', dll: p.path, kind: p.kind, uid: planned.uid, validated: validation?.status || 'unverified' };
  if (!safeRelative(p.path) || !p.path.toLowerCase().startsWith('plugins/fruity/')) throw Error('Invalid installed plugin path');
  if (installationRoot && !/^[a-z]:[\\/]/i.test(installationRoot)) throw Error('Installation root must be an absolute Windows drive path');
  for (const value of [...Object.values(fields), installationRoot]) if (/[\r\n\0]/.test(value)) throw Error('Invalid INI value');
  return '[plugin]\n' + Object.entries(fields).map(([k, v]) => `${k} = ${v}\n`).join('') + '\n[installation]\n; Leave empty to use the installation selected in NativePluginConverter or auto-detect.\nroot = ' + installationRoot + '\n\n[dependencies]\ncount = ' + planned.dependency_order.length + '\n' + planned.dependency_order.map((r, i) => {
    if (!safeRelative(r) || /[\r\n\0]/.test(r)) throw Error('Invalid INI path'); return `dll${i} = ${r}\n`;
  }).join('') + '\n';
}
function safeName(name) { let s = name.replace(/[<>:"/\\|?*\x00-\x1f]/g, '_').replace(/^[ .]+|[ .]+$/g, '').slice(0, 100); if (!s || /^(con|prn|aux|nul|com[1-9]|lpt[1-9])$/i.test(s)) s = 'FL_' + s; return s; }
module.exports = { VERSION, BRIDGE_VERSION, X64, discover, Resolver, plan, config, safeName, inside, rel, under, safeRelative, filesBelow, uid, expand, rootPath };
