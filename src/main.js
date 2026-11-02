// SPDX-License-Identifier: MIT
'use strict';
const { app, BrowserWindow, ipcMain, dialog, shell, Menu, protocol, net } = require('electron');
const { Worker } = require('node:worker_threads');
const fs = require('node:fs'), path = require('node:path'), { pathToFileURL } = require('node:url');
protocol.registerSchemesAsPrivileged([{ scheme: 'flbridge', privileges: { standard: true, secure: true, supportFetchAPI: true } }]);
app.setName('NativePluginConverter');
let window, worker, state, settings = {}, quitting = false, sequence = 0;
const pending = new Map(), ui = path.join(__dirname, '..', 'ui');
const workerCall = (kind, data = {}) => new Promise((resolve, reject) => { const id = ++sequence; pending.set(id, { resolve, reject }); worker.postMessage({ id, kind, data }); });
function defaultSource() {
  if (process.platform !== 'win32') return '';
  try { const base = path.join(process.env.ProgramFiles || 'C:\\Program Files', 'Image-Line'); return fs.readdirSync(base).filter(s => s.startsWith('FL Studio') && fs.existsSync(path.join(base, s, 'Plugins'))).sort().map(s => path.join(base, s)).pop() || ''; } catch { return ''; }
}
function saveSettings() {
  try { fs.writeFileSync(path.join(app.getPath('userData'), 'settings.json'), JSON.stringify({ source: state?.source || settings.source || '', output: state?.output || settings.output || '', bounds: window?.getBounds() || settings.bounds })); } catch { /* Read-only profile should not prevent conversion. */ }
}
async function shutdown() {
  if (quitting) return; quitting = true; saveSettings();
  if (state?.job?.status === 'running') {
    await workerCall('cancel').catch(() => {});
    await new Promise(resolve => { const started = Date.now(); const timer = setInterval(() => { if (state?.job?.status !== 'running' || Date.now() - started > 45000) { clearInterval(timer); resolve(); } }, 100); });
  }
  await worker?.terminate(); app.quit();
}
async function createWindow() {
  const b = settings.bounds;
  window = new BrowserWindow({ width: b?.width || 1240, height: b?.height || 820, minWidth: 850, minHeight: 650, backgroundColor: '#e8deeb', title: 'NativePluginConverter', show: false, autoHideMenuBar: true,
    webPreferences: { preload: path.join(__dirname, 'preload.js'), nodeIntegration: false, contextIsolation: true, sandbox: true, webSecurity: true, spellcheck: false } });
  window.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  window.webContents.on('will-navigate', e => e.preventDefault());
  window.webContents.on('will-attach-webview', e => e.preventDefault());
  window.webContents.session.setPermissionRequestHandler((_wc, _permission, cb) => cb(false));
  window.webContents.session.setPermissionCheckHandler(() => false);
  window.webContents.session.on('will-download', e => e.preventDefault());
  window.on('close', e => { if (!quitting) { e.preventDefault(); shutdown(); } });
  window.once('ready-to-show', () => window.show());
  await window.loadURL('flbridge://app/index.html');
}
app.whenReady().then(async () => {
  fs.mkdirSync(app.getPath('userData'), { recursive: true });
  // Import only app settings/checks; retain custom test profiles and never overwrite new preferences.
  if (['NativePluginConverter', 'native-plugin-converter'].includes(path.basename(app.getPath('userData')))) {
    require('./preferences').migratePreferences(app.getPath('userData'), app.getPath('appData'));
  }
  try { settings = JSON.parse(fs.readFileSync(path.join(app.getPath('userData'), 'settings.json'), 'utf8')); } catch { /* First launch. */ }
  protocol.handle('flbridge', request => {
    const url = new URL(request.url), name = url.pathname.slice(1);
    if (url.host !== 'app' || !['index.html', 'renderer.js', 'style.css'].includes(name)) return new Response('Not found', { status: 404 });
    return net.fetch(pathToFileURL(path.join(ui, name)).href);
  });
  const resources = app.isPackaged ? path.join(process.resourcesPath, 'resources') : path.join(__dirname, '..', 'resources');
  worker = new Worker(path.join(__dirname, 'worker.js'), { workerData: { resources, cacheFile: path.join(app.getPath('userData'), 'checks.json'), output: settings.output || path.join(app.getPath('downloads'), 'NativePluginConverter Exports'), source: settings.source || defaultSource() } });
  worker.on('message', message => {
    if (message.type === 'state') { state = message.state; return; }
    const call = pending.get(message.id); if (!call) return; pending.delete(message.id);
    if (message.error) call.reject(Error(message.error)); else call.resolve(message.result);
  });
  worker.on('error', e => { for (const p of pending.values()) p.reject(e); pending.clear(); if (state?.job) { state.job.status = 'failed'; state.job.message = 'Ошибка обработки: ' + e.message; } dialog.showErrorBox('NativePluginConverter', e.message); });
  ipcMain.handle('converter', async (event, kind, data = {}) => {
    if (event.sender !== window?.webContents || event.senderFrame !== window.webContents.mainFrame || event.senderFrame.url !== 'flbridge://app/index.html') throw Error('Untrusted sender');
    if (typeof kind !== 'string' || !data || typeof data !== 'object' || JSON.stringify(data).length > 1024 * 1024) throw Error('Invalid request');
    if (kind === 'state') { if (!state) throw Error('Приложение запускается…'); return state; }
    if (kind === 'pick') {
      if (state?.job?.status === 'running') throw Error('Дождитесь завершения текущей операции.');
      if (!['source', 'output'].includes(data.purpose)) throw Error('Invalid folder purpose');
      const result = await dialog.showOpenDialog(window, { title: data.purpose === 'source' ? 'Папка Windows-версии FL Studio' : 'Куда сохранить архивы', defaultPath: state?.[data.purpose], properties: ['openDirectory', ...(data.purpose === 'output' ? ['createDirectory'] : [])] });
      return { path: result.canceled ? null : result.filePaths[0] };
    }
    if (kind === 'reveal') { const result = state?.results.find(r => r.ok && fs.existsSync(r.path)); if (result) shell.showItemInFolder(result.path); return {}; }
    if (kind === 'close') { setImmediate(shutdown); return {}; }
    if (!['scan', 'plan', 'validate', 'build', 'cancel'].includes(kind)) throw Error('Unknown operation');
    return workerCall(kind, data);
  });
  Menu.setApplicationMenu(Menu.buildFromTemplate([
    ...(process.platform === 'darwin' ? [{ label: app.name, submenu: [{ role: 'about' }, { type: 'separator' }, { role: 'hide' }, { role: 'hideOthers' }, { role: 'unhide' }, { type: 'separator' }, { role: 'quit' }] }] : []),
    { label: 'Правка', submenu: [{ role: 'undo' }, { role: 'redo' }, { type: 'separator' }, { role: 'cut' }, { role: 'copy' }, { role: 'paste' }, { role: 'selectAll' }] },
    { label: 'Справка', submenu: [{ label: 'О NativePluginConverter', click: async () => {
      const result = await dialog.showMessageBox(window, { type: 'info', title: app.name, message: app.name + ' ' + app.getVersion(),
        detail: 'MIT License. Можно использовать, изменять и распространять с сохранением лицензии и уведомления об авторских правах. Без каких-либо гарантий.\n\nИсходники: https://github.com/h4s10/NativePluginConverter\nСторонние компоненты сохраняют собственные лицензии.', buttons: ['Закрыть', 'Текст лицензии'], defaultId: 0, cancelId: 0 });
      if (result.response === 1) await shell.openPath(app.isPackaged ? path.join(process.resourcesPath, 'LICENSE') : path.join(__dirname, '..', 'LICENSE'));
    } }] },
    { label: 'Вид', submenu: [{ role: 'resetZoom' }, { role: 'zoomIn' }, { role: 'zoomOut' }, { type: 'separator' }, { role: 'togglefullscreen' }] }
  ]));
  await createWindow();
});
app.on('before-quit', e => { if (!quitting) { e.preventDefault(); shutdown(); } });
app.on('window-all-closed', () => { if (!quitting) shutdown(); });
