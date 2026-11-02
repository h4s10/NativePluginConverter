// SPDX-License-Identifier: MIT
'use strict';
const fs = require('node:fs'), path = require('node:path'), crypto = require('node:crypto');
const core = require('./core'), validation = require('./validation'), { packagePlugin } = require('./package');
const tick = () => new Promise(resolve => setImmediate(resolve));
class Controller {
  constructor({ resources, output, source = '', records = {}, onChange = () => {}, onSourceSelected = () => {} }) {
    this.resources = resources; this.records = records; this.onChange = onChange; this.onSourceSelected = onSourceSelected;
    this.state = { source, output, entries: [], plans: {}, results: [], validation: {}, job: null, template_ready: fs.existsSync(path.join(resources, 'BridgeTemplate.vst3')), platform: process.platform, can_validate: process.platform === 'win32' && process.arch === 'x64', version: core.VERSION };
    this.root = ''; this.abort = null;
  }
  changed() { this.onChange(this.state); }
  selected(ids) {
    if (!Array.isArray(ids) || !ids.length || ids.length > 512 || ids.some(x => typeof x !== 'string')) throw Error('Выберите хотя бы один плагин.');
    const entries = new Map(this.state.entries.map(e => [e.id, e]));
    if (ids.some(id => !entries.has(id))) throw Error('Список изменился. Выполните поиск плагинов заново.');
    return [...new Set(ids)].map(id => entries.get(id));
  }
  start(kind, total, work) {
    if (this.state.job?.status === 'running') throw Error('Дождитесь завершения текущей операции.');
    this.abort = new AbortController();
    const signal = this.abort.signal;
    this.state.job = { id: crypto.randomUUID(), kind, total, done: 0, status: 'running', message: 'Подготовка…', errors: [] };
    this.changed();
    this.pending = (async () => {
      await tick();
      try { await work(signal); signal.throwIfAborted(); this.state.job.status = 'complete'; this.state.job.message = this.state.job.errors.length ? 'Завершено с замечаниями' : 'Готово'; }
      catch (e) { this.state.job.status = signal.aborted ? 'cancelled' : 'failed'; this.state.job.message = signal.aborted ? 'Операция отменена' : e.message; if (!signal.aborted) this.state.job.errors.push(e.message); }
      finally { this.changed(); }
    })();
    return { job_id: this.state.job.id };
  }
  progress(message) { this.state.job.message = message; this.changed(); }
  cancel() { this.abort?.abort(); return {}; }
  request(kind, data = {}) {
    if (!data || typeof data !== 'object' || Array.isArray(data)) throw Error('Некорректный запрос.');
    if (kind === 'cancel') return this.cancel();
    if (!['scan', 'plan', 'validate', 'build'].includes(kind)) throw Error('Неизвестная операция.');
    if (this.state.job?.status === 'running') throw Error('Дождитесь завершения текущей операции.');
    if (kind === 'scan') {
      if (typeof data.root !== 'string' || !data.root.trim() || data.root.length > 32768) throw Error('Укажите папку FL Studio.');
      const root = core.rootPath(data.root);
      return this.start(kind, 1, async signal => {
        this.progress('Ищем плагины Windows x64…');
        const entries = core.discover(root), checks = {};
        for (const e of entries) { await tick(); signal.throwIfAborted(); const record = validation.lookup(root, e, this.records); if (record) checks[e.id] = record; }
        await this.onSourceSelected(root);
        this.root = root;
        Object.assign(this.state, { source: root, entries, plans: {}, validation: checks }); this.state.job.done = 1;
      });
    }
    const entries = this.selected(data.ids);
    if (kind === 'validate' && !this.state.can_validate) throw Error('Проверка запуска доступна только в Windows x64. Архивы можно собирать на этой системе.');
    let output;
    if (kind === 'build') {
      if (typeof data.output !== 'string' || !data.output.trim() || data.output.length > 32768) throw Error('Укажите папку для ZIP-архивов.');
      output = core.expand(data.output);
      if (fs.existsSync(output) && !fs.statSync(output).isDirectory()) throw Error('Путь для архивов должен указывать на папку.');
      if (core.under(this.root, output)) throw Error('Сохраняйте архивы вне папки FL Studio.');
    }
    return this.start(kind, entries.length, async signal => {
      const resolver = new core.Resolver(this.root);
      if (kind === 'build') { this.state.results = []; this.state.output = output; }
      for (const [index, entry] of entries.entries()) {
        await tick(); signal.throwIfAborted(); this.progress(entry.name + ': ' + ({ plan: 'анализ библиотек и ресурсов', validate: 'проверка запуска', build: 'сборка ZIP' })[kind]);
        try {
          if (kind === 'plan') this.state.plans[entry.id] = core.plan(this.root, entry, { resolver });
          if (kind === 'validate') {
            const r = await validation.validate(this.root, entry, this.resources, { resolver, signal, progress: m => this.progress(entry.name + ': ' + m) });
            this.state.validation[entry.id] = r;
            if (r.native_sha256) this.records[entry.path] = r;
            if (['failed', 'timeout'].includes(r.status)) this.state.job.errors.push(entry.name + ': ' + r.message);
          }
          if (kind === 'build') {
            // Recheck cached identity at build time; source files may have changed after scan.
            let check = this.state.validation[entry.id];
            if (check?.native_sha256) check = validation.lookup(this.root, entry, { [entry.path]: check });
            const result = await packagePlugin(this.root, entry, output, path.join(this.resources, 'BridgeTemplate.vst3'), { resolver, validation: check, signal });
            const { manifest, ...summary } = result;
            this.state.results.push({ ...summary, plugin: entry.name, ok: true });
          }
        } catch (e) {
          if (signal.aborted) throw e;
          this.state.job.errors.push(entry.name + ': ' + e.message);
          if (kind === 'build') this.state.results.push({ ok: false, plugin: entry.name, error: e.message });
          if (kind === 'validate') this.state.validation[entry.id] = { status: 'failed', message: e.message };
        }
        this.state.job.done = index + 1; this.changed();
      }
    });
  }
}
module.exports = { Controller };
