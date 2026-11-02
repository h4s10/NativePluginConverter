// SPDX-License-Identifier: MIT
'use strict';
// Developer integration harness: exercises the real window, preload, IPC and worker.
const { app, BrowserWindow } = require('electron');
const fs = require('node:fs'), path = require('node:path'), assert = require('node:assert/strict');
const root = process.env.FL_TEST_ROOT, out = process.env.FL_TEST_OUTPUT;
if (!root || !out) throw Error('FL_TEST_ROOT and FL_TEST_OUTPUT are required');
app.setPath('userData', path.join(out, 'profile'));
require('../src/main');
const sleep = n => new Promise(resolve => setTimeout(resolve, n));
(async () => {
  await app.whenReady();
  let win;
  let ready = false;
  for (let i = 0; i < 300; i++) {
    win = BrowserWindow.getAllWindows()[0];
    if (win && win.webContents.getURL() === 'flbridge://app/index.html' && !win.webContents.isLoading()) {
      try { ready = await win.webContents.executeJavaScript("document.readyState === 'complete' && !!document.querySelector('#validate') && !!window.converter"); if (ready) { await win.webContents.executeJavaScript('window.converter.state()'); break; } } catch { ready = false; }
    }
    await sleep(100);
  }
  assert(ready, 'Converter page/preload/worker did not become ready: ' + win?.webContents.getURL());
  const js = async s => { try { return await win.webContents.executeJavaScript(s); } catch(e) { console.error('Failed UI action:', s); throw e; } };
  assert.equal(win.getTitle(), 'NativePluginConverter');
  assert.equal(await js("document.querySelector('.brand').textContent"), 'NativePluginConverter');
  assert.equal(await js("/FL Studio|Windows/.test(document.querySelector('.hero').textContent)"), false);
  const prefs = win.webContents.getLastWebPreferences();
  assert.equal(prefs.contextIsolation, true); assert.equal(prefs.nodeIntegration, false); assert.equal(prefs.sandbox, true);
  assert.equal(await js('typeof require'), 'undefined');
  assert.equal(await js("document.querySelector('#validate').disabled"), true);
  const wait = async previousId => {
    for (let i = 0; i < 900; i++) { const s = await js('window.converter.state()'); if (s.job && s.job.id !== previousId && s.job.status !== 'running') { if (s.job.errors.length) throw Error(s.job.errors.join('; ')); return s; } await sleep(100); }
    throw Error('Job timed out');
  };
  await sleep(1700);
  fs.writeFileSync(path.join(out, 'ui-empty.png'), (await win.webContents.capturePage()).toPNG());
  await js("document.querySelector('#settings').click(); if (!document.querySelector('#settingsDialog').open) throw Error('Settings did not open')");
  await sleep(300);
  fs.writeFileSync(path.join(out, 'ui-settings.png'), (await win.webContents.capturePage()).toPNG());
  await js(`document.querySelector('#source').value=${JSON.stringify(root)}; document.querySelector('#scan').click()`);
  let s = await wait(); assert.equal(s.entries.length, 140);
  await sleep(1700);
  assert.equal(await js("document.querySelector('#status')"), null);
  assert.equal(await js("[...document.querySelectorAll('#plugins .plug:not(.disabled) .badge')].length"), 0);
  assert.equal(await js("[...document.querySelectorAll('#plugins .plug')].filter(e=>e.querySelector('.name').textContent==='Fruity Wrapper').every(e=>e.querySelector('.badge')?.textContent==='Недоступен' && e.querySelector('input').disabled)"), true);
  // Navigate categories and selections through the visible controls.
  await js("document.querySelector('[data-kind=instrument]').click()");
  assert.equal(await js("document.querySelector('#libraryTitle').textContent"), 'Инструменты');
  await js("document.querySelector('[data-kind=effect]').click();document.querySelector('#search').value='Distructor';document.querySelector('#search').dispatchEvent(new Event('input'));document.querySelector('#plugins input').click()");
  assert.equal(await js("document.querySelector('#selectedCount').textContent"), '1');
  await js("document.querySelector('#names [data-remove]').click()");
  assert.equal(await js("document.querySelector('#build').disabled"), true);
  await js("document.querySelector('[data-kind=all]').click();document.querySelector('#search').value='';document.querySelector('#search').dispatchEvent(new Event('input'))");
  await sleep(300);
  fs.writeFileSync(path.join(out, 'ui-library.png'), (await win.webContents.capturePage()).toPNG());
  let previousId = s.job.id;
  await js("document.querySelector('#search').value='Distructor';document.querySelector('#search').dispatchEvent(new Event('input'));document.querySelector('#plugins input').click();document.querySelector('#detailsButton').click();document.querySelector('#plan').click()");
  s = await wait(previousId); const e = s.entries.find(x => x.name === 'Distructor'); assert(s.plans[e.id].files.filter(x => x.path.endsWith('.fst')).length >= 47);
  await sleep(1700);
  await js("document.querySelector('#detailsDialog [data-dismiss]').click();document.querySelector('#search').value='';document.querySelector('#search').dispatchEvent(new Event('input'))");
  await sleep(300);
  fs.writeFileSync(path.join(out, 'ui-selected.png'), (await win.webContents.capturePage()).toPNG());
  await js(`(() => {for (const name of ['Harmor','Sawer']) {const label=[...document.querySelectorAll('#plugins .plug')].find(x=>x.querySelector('.name').textContent===name);label.querySelector('input').click()} document.querySelector('#plugins').scrollTop=0})()`);
  assert.equal(await js("document.querySelector('#selectedCount').textContent"), '3');
  await sleep(300);
  fs.writeFileSync(path.join(out, 'ui-multiple.png'), (await win.webContents.capturePage()).toPNG());
  await js(`(() => {for (const name of ['Harmor','Sawer']) {const label=[...document.querySelectorAll('#plugins .plug')].find(x=>x.querySelector('.name').textContent===name);label.querySelector('input').click()}})()`);
  win.setSize(850, 650); await sleep(500);
  fs.writeFileSync(path.join(out, 'ui-compact.png'), (await win.webContents.capturePage()).toPNG());
  const geometry = await js(`({width: innerWidth, scroll: document.documentElement.scrollWidth, build: (()=>{const r=document.querySelector('#build').getBoundingClientRect();return {right:r.right,bottom:r.bottom}})(), height:innerHeight})`);
  assert(geometry.scroll <= geometry.width, 'Horizontal overflow');
  assert(geometry.build.right <= geometry.width && geometry.build.bottom <= geometry.height, 'Export button is clipped');
  win.setSize(1240, 820); await sleep(500);
  previousId = s.job.id;
  await js(`document.querySelector('#output').value=${JSON.stringify(out)}; document.querySelector('#build').click()`);
  s = await wait(previousId); assert(s.results[0]?.ok, JSON.stringify(s.results)); assert(fs.existsSync(s.results[0].path));
  // Renderer polling can be throttled while macOS moves/resizes the window.
  for (let i=0; i<100 && !await js("document.querySelector('#resultsDialog').open"); i++) await sleep(100);
  assert.equal(await js("document.querySelector('#resultsDialog').open"), true);
  await sleep(300);
  const screenshot = await win.webContents.capturePage(); fs.writeFileSync(path.join(out, 'electron-ui.png'), screenshot.toPNG());
  fs.writeFileSync(path.join(out, 'smoke.json'), JSON.stringify({ platform: process.platform, arch: process.arch, electron: process.versions.electron, entries: s.entries.length, presetCount: s.plans[e.id].files.filter(x => x.path.endsWith('.fst')).length, result: s.results[0], sandbox: prefs.sandbox, contextIsolation: prefs.contextIsolation, nodeIntegration: prefs.nodeIntegration }, null, 2));
  console.log('ELECTRON_SMOKE_OK', s.results[0].path); app.quit();
})().catch(e => { console.error(e); app.exit(1); });
