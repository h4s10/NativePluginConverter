// SPDX-License-Identifier: MIT
'use strict';
const fs = require('node:fs'), path = require('node:path'), os = require('node:os'), crypto = require('node:crypto');
const { spawn } = require('node:child_process');
const core = require('./core');
const cache = new Map();
function digest(file) {
  const s = fs.statSync(file, { bigint: true }), key = `${file}\0${s.mtimeNs}\0${s.size}`;
  if (cache.has(key)) return cache.get(key);
  const h = crypto.createHash('sha256'), fd = fs.openSync(file, 'r'), b = Buffer.alloc(1024 * 1024);
  try { let n; while ((n = fs.readSync(fd, b)) > 0) h.update(b.subarray(0, n)); } finally { fs.closeSync(fd); }
  const result = h.digest('hex'); if (cache.size > 2048) cache.clear(); cache.set(key, result); return result;
}
function lookup(root, plugin, records) {
  try {
    root = core.rootPath(root);
    const r = records[plugin.path];
    if (!r || r.bridge_version !== core.BRIDGE_VERSION || !r.dependency_hashes || r.native_sha256 !== digest(core.inside(root, path.join(root, plugin.path)))) return null;
    for (const [relative, hash] of Object.entries(r.dependency_hashes)) if (!core.safeRelative(relative) || digest(core.inside(root, path.join(root, relative))) !== hash) return null;
    return r;
  } catch { return null; }
}
function fixture(file) {
  const b = Buffer.alloc(44 + 44100 * 4);
  b.write('RIFF'); b.writeUInt32LE(b.length - 8, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(2, 22); b.writeUInt32LE(44100, 24); b.writeUInt32LE(176400, 28); b.writeUInt16LE(4, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(b.length - 44, 40);
  for (let i = 0; i < 44100; i++) { const v = Math.round(6000 * Math.sin(i * 2 * Math.PI * 440 / 44100)); b.writeInt16LE(v, 44 + i * 4); b.writeInt16LE(v, 46 + i * 4); }
  fs.writeFileSync(file, b);
}
function runProbe(exe, args, signal) {
  return new Promise((resolve, reject) => {
    signal?.throwIfAborted();
    const child = spawn(exe, args, { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'], shell: false });
    let stdout = '', stderr = '', timeout = false;
    const kill = () => child.kill('SIGKILL');
    const timer = setTimeout(() => { timeout = true; kill(); }, 40000);
    signal?.addEventListener('abort', kill, { once: true });
    child.stdout.on('data', b => { stdout = (stdout + b.toString('utf8')).slice(-24000); });
    child.stderr.on('data', b => { stderr = (stderr + b.toString('utf8')).slice(-12000); });
    child.once('error', reject);
    child.once('close', code => { clearTimeout(timer); signal?.removeEventListener('abort', kill); if (signal?.aborted) reject(Error('Операция отменена.')); else resolve({ stdout, stderr, code, timeout }); });
  });
}
async function validate(root, plugin, resources, options = {}) {
  if (process.platform !== 'win32' || process.arch !== 'x64') return { status: 'not-run', message: 'Проверка запуска доступна в Windows x64. Сборка ZIP доступна на всех платформах.' };
  const p = core.plan(root, plugin, options);
  if (['blocked', 'unsupported'].includes(p.status)) return { status: 'failed', message: p.issues.filter(x => x.severity === 'error').map(x => x.message).join('; ') };
  const exe = path.join(resources, 'BridgeProbe.exe');
  const result = { bridge_version: core.BRIDGE_VERSION, native_sha256: digest(path.join(root, plugin.path)), environment: 'Windows x64', scope: 'create, parameters, render, state, editor lifecycle; not full feature/DAW validation', template_sha256: digest(path.join(resources, 'BridgeTemplate.vst3')), probe_sha256: digest(exe), dependency_hashes: Object.fromEntries(p.files.filter(f => f.role !== 'asset').map(f => [f.path, digest(path.join(root, f.path))])) };
  const stage = fs.mkdtempSync(path.join(os.tmpdir(), 'flbridge-check-'));
  try {
    options.signal?.throwIfAborted();
    const ini = path.join(stage, 'bridge.ini'), wav = path.join(stage, 'test.wav');
    // The probe reads the selected installation directly. No vendor files are staged.
    fs.writeFileSync(ini, core.config(p, undefined, core.rootPath(root))); fixture(wav);
    options.progress?.('Проверка запуска, звука и сохранения');
    const run = await runProbe(exe, [ini, wav], options.signal);
    let observed = {}; try { observed = JSON.parse(run.stdout); } catch { /* Native crash may leave partial output. */ }
    const status = run.timeout || run.code === 124 ? 'timeout' : run.code === 0 && observed.completed ? 'passed' : 'failed';
    const silent = status === 'passed' && plugin.kind === 'instrument' && !(observed.energy > 0);
    return { ...result, status, message: status === 'passed' ? (silent ? 'Базовая проверка пройдена; звук на тестовой ноте не подтверждён.' : 'Базовая проверка пройдена') : (observed.error ? 'Плагин не прошёл проверку: ' + String(observed.error).slice(0, 1000) : 'Плагин не прошёл проверку; подробности в отчёте.'), observed, log: run.stderr, stdout_partial: run.stdout, exit_code: run.code, preloaded_dependencies: p.dependency_order };
  } finally { await fs.promises.rm(stage, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 }); }
}
module.exports = { digest, lookup, validate, fixture };
