// SPDX-License-Identifier: MIT
'use strict';
const $ = id => document.getElementById(id);
let state = { entries: [], plans: {}, results: [] }, selected = new Set(), category = 'all';
let lastJob = '', lastState = '', toastTimer, first = true;
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const icon = name => `<svg aria-hidden="true"><use href="#i-${name}"/></svg>`;
const bytes = n => { n = Number(n) || 0; return n >= 1073741824 ? (n / 1073741824).toFixed(2) + ' ГБ' : n >= 1048576 ? (n / 1048576).toFixed(1) + ' МБ' : n >= 1024 ? (n / 1024).toFixed(0) + ' КБ' : n + ' Б'; };
const reason = s => String(s).replace('Plugin-local resource', 'Ресурс плагина').replace('Selected FL native plugin', 'Оригинальный плагин').replace('dynamic-string: ', 'Загрузка библиотек: ').replace('PE import: ', 'Зависимость: ').replace('Factory preset: ', 'Заводской пресет: ');
const unsupported = e => ['unsupported', 'blocked', 'incompatible', 'x86'].includes(e.status) || e.architecture === 'x86';
const check = e => state.validation?.[e.id];
const failed = e => ['failed', 'timeout'].includes(check(e)?.status);
const verified = e => check(e)?.status === 'passed';
const busy = () => state.job?.status === 'running';
const kindName = e => e.kind === 'effect' ? 'Эффект' : e.kind === 'instrument' ? 'Инструмент' : 'Компонент';
const artwork = e => `<span class="plugin-art tone-${[...e.name].reduce((n, c) => n + c.charCodeAt(0), 0) % 5}">${icon(e.kind === 'instrument' ? 'keys' : 'sliders')}</span>`;
function toast(message) { $('toast').textContent = message; $('toast').classList.add('show'); clearTimeout(toastTimer); toastTimer = setTimeout(() => $('toast').classList.remove('show'), 8000); }
function openDialog(id) { document.querySelectorAll('dialog[open]').forEach(d => d.close()); $(id).showModal(); }
async function api(method, payload = {}) {
  switch (method) {
    case 'state': return window.converter.state();
    case 'scan': return window.converter.scan(payload.root);
    case 'plan': return window.converter.plan(payload.ids);
    case 'validate': return window.converter.validate(payload.ids);
    case 'build': return window.converter.build(payload.ids, payload.output);
    case 'pick': return window.converter.pick(payload.purpose);
    case 'reveal': return window.converter.reveal();
    case 'cancel': return window.converter.cancel();
    default: throw Error('Неизвестная операция');
  }
}
async function action(method, payload) { try { await api(method, payload); await refresh(); return true; } catch (e) { toast(e.message); return false; } }
function visible() {
  const q = $('search').value.trim().toLocaleLowerCase();
  return state.entries.filter(e => (!q || e.name.toLocaleLowerCase().includes(q)) && (category === 'all' || e.kind === category));
}
function renderList() {
  const focusedId = document.activeElement?.dataset?.id;
  const entries = visible();
  $('count').textContent = state.entries.length ? entries.length + ' в коллекции' : 'Добавьте свою коллекцию';
  $('allCount').textContent = state.entries.length;
  $('instrumentCount').textContent = state.entries.filter(e => e.kind === 'instrument').length;
  $('effectCount').textContent = state.entries.filter(e => e.kind === 'effect').length;
  $('libraryTitle').textContent = { all: 'Все плагины', instrument: 'Инструменты', effect: 'Эффекты' }[category];
  document.querySelectorAll('[data-kind]').forEach(b => { b.classList.toggle('active', b.dataset.kind === category); b.setAttribute('aria-pressed', String(b.dataset.kind === category)); });
  if (!entries.length) {
    $('plugins').innerHTML = `<div class="empty"><div class="symbol">${icon(state.entries.length ? 'search' : 'folder')}</div><strong>${state.entries.length ? 'Ничего не найдено' : 'Ваша коллекция начинается здесь'}</strong><p>${state.entries.length ? 'Попробуйте другое название или измените фильтры.' : 'Добавьте библиотеку — мы найдём инструменты и эффекты.'}</p>${state.entries.length ? '' : '<button id="addLibrary" class="primary">Добавить библиотеку ' + icon('arrow') + '</button>'}</div>`;
    if ($('addLibrary')) $('addLibrary').onclick = () => openDialog('settingsDialog');
  } else {
    $('plugins').innerHTML = entries.map(e => `<label class="plug ${selected.has(e.id) ? 'selected' : ''} ${unsupported(e) ? 'disabled' : ''}" title="${esc(unsupported(e) ? (e.issues || []).map(x => typeof x === 'string' ? x : x.message).join('\n') || 'Этот компонент пока не поддерживается мостом.' : e.name)}"><input type="checkbox" aria-label="${esc('Выбрать ' + e.name)}" data-id="${esc(e.id)}" ${selected.has(e.id) ? 'checked' : ''} ${unsupported(e) || busy() ? 'disabled' : ''}>${artwork(e)}<div class="grow"><div class="name">${esc(e.name)}</div><div class="meta">${kindName(e)}</div></div>${unsupported(e) ? '<span class="badge unavailable">Недоступен</span>' : ''}</label>`).join('');
    $('plugins').querySelectorAll('input').forEach(x => x.onchange = () => { x.checked ? selected.add(x.dataset.id) : selected.delete(x.dataset.id); render(); });
  }
  if (focusedId) [...$('plugins').querySelectorAll('input')].find(x => x.dataset.id === focusedId)?.focus({ preventScroll: true });
  const eligible = entries.filter(e => !unsupported(e));
  $('selectAll').checked = eligible.length > 0 && eligible.every(e => selected.has(e.id));
  $('selectAll').indeterminate = eligible.some(e => selected.has(e.id)) && !$('selectAll').checked;
  $('selectAll').disabled = busy() || !eligible.length;
  $('clear').disabled = busy() || !selected.size;
}
function renderSelection() {
  const entries = state.entries.filter(e => selected.has(e.id));
  const plans = entries.map(e => state.plans[e.id]).filter(Boolean);
  $('selectedCount').textContent = entries.length;
  $('selectionEmpty').classList.toggle('hidden', !!entries.length);
  $('selectionInfo').classList.toggle('hidden', !entries.length);
  $('names').innerHTML = entries.map(e => `<div class="selected-item">${artwork(e)}<div class="grow"><strong>${esc(e.name)}</strong><small>${kindName(e)}</small></div><button class="icon-button" data-remove="${esc(e.id)}" aria-label="${esc('Убрать ' + e.name)}" ${busy() ? 'disabled' : ''}>${icon('close')}</button></div>`).join('');
  $('names').querySelectorAll('[data-remove]').forEach(b => b.onclick = () => { selected.delete(b.dataset.remove); render(); });
  $('archiveCount').textContent = entries.length + ' ZIP';
  $('plan').disabled = busy() || !entries.length;
  $('validate').disabled = busy() || !entries.length || !state.can_validate;
  $('detailsButton').disabled = !entries.length;
  $('platformNote').textContent = state.can_validate ? 'Проверка запускается в отдельном тестовом хосте. Совместимость с вашей DAW может отличаться.' : 'Проверка запуска доступна в Windows x64. Здесь можно посмотреть состав и создать архив.';
  const issues = [];
  if (!state.template_ready) issues.push('Модуль конвертации не найден. Распакуйте приложение целиком.');
  entries.filter(failed).forEach(e => issues.push(e.name + ': проверка запуска не пройдена. Подробности — в «Зависимости и проверка».'));
  plans.filter(p => ['blocked', 'unsupported'].includes(p.status)).forEach(p => issues.push((p.plugin?.name || 'Плагин') + ': не хватает ресурсов или компонент не поддерживается. Откройте зависимости.'));
  $('selectionIssues').innerHTML = issues.map(s => '<div class="notice error">' + esc(s) + '</div>').join('');
  $('build').disabled = busy() || !entries.length || !state.template_ready || entries.some(failed) || plans.some(p => ['blocked', 'unsupported'].includes(p.status));
  $('build').innerHTML = '<span>' + (busy() && state.job.kind === 'build' ? 'Собираем…' : entries.length > 1 ? 'Создать архивы · ' + entries.length : 'Создать архив') + '</span>' + icon('arrow');
  let html = entries.length ? '' : '<p class="hint">Сначала выберите плагины в библиотеке.</p>';
  entries.forEach(e => {
    const c = check(e);
    if (!c) return;
    html += `<div class="notice ${c.status === 'passed' ? 'success' : failed(e) ? 'error' : ''}">${esc(e.name)}: ${esc(c.message || (c.status === 'passed' ? 'Базовая проверка пройдена' : 'Проверка не выполнена'))}</div>`;
    if (failed(e) && c.log) html += '<details><summary>Журнал проверки</summary><pre>' + esc(c.log) + '</pre></details>';
  });
  if (entries.some(e => !verified(e))) html += '<p class="hint">Есть плагины без проверки запуска. После установки проверьте их работу в своей DAW.</p>';
  plans.forEach(p => {
    const files = p.files || [];
    html += `<details><summary>${esc(p.plugin?.name || 'Файлы в установке')} · ${files.length} файлов в установке · не копируются</summary>${(p.issues || []).length ? '<div class="checks">' + p.issues.map(i => '<p>' + esc(typeof i === 'string' ? i : i.message) + '</p>').join('') + '</div>' : ''}<div class="files">${files.map(f => `<div class="file"><div><div class="path">${esc(f.path)}</div><small>${esc((f.reasons || []).map(reason).join(' · '))}</small></div><span class="size">${bytes(f.size)}</span></div>`).join('')}</div></details>`;
  });
  // Keep expanded resource lists open while a background operation updates progress.
  const expanded = [...$('planDetails').querySelectorAll('details')].map(d => d.open);
  $('planDetails').innerHTML = html;
  $('planDetails').querySelectorAll('details').forEach((d, i) => { d.open = !!expanded[i]; });
}
function renderProgress() {
  const j = state.job;
  $('progress').classList.toggle('active', busy());
  $('footerTarget').classList.toggle('hidden', busy());
  $('footerDot').style.background = j?.status === 'failed' || j?.errors?.length ? 'var(--red)' : busy() ? 'var(--accent)' : '';
  $('footerText').textContent = busy() ? ({ scan: 'Ищем плагины…', plan: 'Проверяем файлы установки…', validate: 'Проверяем запуск…', build: 'Собираем архивы…' }[j.kind] || 'Подготовка…') : j?.status === 'cancelled' ? 'Операция отменена' : j?.status === 'failed' || j?.errors?.length ? 'Не удалось завершить всё. Откройте подробности.' : state.entries.length ? 'Коллекция готова · выберите плагины для экспорта' : 'Добавьте библиотеку, чтобы начать';
  if (j) {
    $('progressCount').textContent = j.done + ' / ' + j.total;
    $('progressBar').style.width = (j.total ? Math.round(100 * j.done / j.total) : 0) + '%';
    if (j.status !== 'running' && lastJob !== j.id) {
      lastJob = j.id;
      if (j.errors?.length) toast(j.errors.join('\n'));
      if (j.kind === 'build' && state.results?.length) openDialog('resultsDialog');
    }
  }
  ['scan', 'pickSource', 'pickOutput', 'source', 'output'].forEach(id => $(id).disabled = busy());
  $('cancel').disabled = !busy();
}
function renderResults() {
  const results = state.results || [], good = results.filter(r => r.ok);
  $('resultCount').textContent = good.length ? '· ' + good.length : '';
  $('readyCard').classList.toggle('hidden', !good.length);
  $('readyText').textContent = good.length === 1 ? 'Архив готов' : 'Готово архивов: ' + good.length;
  $('results').innerHTML = results.length ? results.map(r => `<div class="result ${r.ok ? '' : 'error'}"><strong>${r.ok ? '✓' : '!'} ${esc(r.plugin)}</strong><p>${r.ok ? esc(r.path) : esc(r.error)}</p>${r.ok ? '<span class="result-size">' + bytes(r.size) + ' · ZIP</span>' : ''}</div>`).join('') : '<div class="empty"><div class="symbol">' + icon('box') + '</div><strong>Здесь будут ваши архивы</strong><p>Выберите плагины и создайте первый ZIP.</p></div>';
  $('reveal').disabled = !good.length;
  document.querySelector('.install-hint').classList.toggle('hidden', !good.length);
}
function render() {
  $('sourceName').textContent = state.source ? state.source.split(/[\\/]/).filter(Boolean).pop() : 'Выбрать папку';
  $('sourceShortcut').title = state.source || 'Настроить библиотеку';
  $('output').title = $('output').value;
  renderList(); renderSelection(); renderResults(); renderProgress();
}
async function refresh() {
  try {
    const next = await api('state'), serialized = JSON.stringify(next);
    if (serialized === lastState) return;
    lastState = serialized; state = next;
    const ids = new Set(state.entries.map(e => e.id));
    selected = new Set([...selected].filter(id => ids.has(id)));
    if (first) { $('source').value = state.source || ''; $('output').value = state.output || ''; first = false; }
    render();
  } catch (e) { if (!first) toast(e.message); }
}
$('scan').onclick = async () => { if (await action('scan', { root: $('source').value })) $('settingsDialog').close(); };
$('source').onkeydown = e => { if (e.key === 'Enter' && !busy()) $('scan').click(); };
$('plan').onclick = () => action('plan', { ids: [...selected] });
$('validate').onclick = () => action('validate', { ids: [...selected] });
$('build').onclick = () => action('build', { ids: [...selected], output: $('output').value });
$('reveal').onclick = () => action('reveal');
$('search').oninput = renderList;
document.querySelectorAll('[data-kind]').forEach(b => b.onclick = () => { category = b.dataset.kind; renderList(); });
$('clear').onclick = () => { selected.clear(); render(); };
$('selectAll').onchange = e => { visible().filter(x => !unsupported(x)).forEach(x => e.target.checked ? selected.add(x.id) : selected.delete(x.id)); render(); };
async function pick(purpose) { try { const r = await api('pick', { purpose }); if (r.path) { const input = $(purpose === 'output' ? 'output' : 'source'); input.value = r.path; input.title = r.path; } } catch (e) { toast(e.message); } }
$('pickSource').onclick = () => pick('source'); $('pickOutput').onclick = () => pick('output');
$('cancel').onclick = () => action('cancel');
['settings', 'sourceShortcut'].forEach(id => $(id).onclick = () => openDialog('settingsDialog'));
['showResults', 'openReady'].forEach(id => $(id).onclick = () => openDialog('resultsDialog'));
$('detailsButton').onclick = () => openDialog('detailsDialog');
document.querySelectorAll('[data-dismiss]').forEach(b => b.onclick = () => b.closest('dialog').close());
$('output').oninput = () => { $('output').title = $('output').value; };
refresh(); setInterval(refresh, 1500);
