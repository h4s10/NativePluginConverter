// SPDX-License-Identifier: MIT
'use strict';
const { parentPort, workerData } = require('node:worker_threads');
const fs = require('node:fs'), path = require('node:path');
const { Controller } = require('./controller');
let records = {};
try { records = JSON.parse(fs.readFileSync(path.join(workerData.resources, 'compatibility.json'), 'utf8')); } catch { /* Cache is optional. */ }
try { Object.assign(records, JSON.parse(fs.readFileSync(workerData.cacheFile, 'utf8'))); } catch { /* First launch. */ }
const app = new Controller({ ...workerData, records, onSourceSelected: root => {
  if (process.platform === 'win32') require('./installation').saveInstallation(root, process.env.LOCALAPPDATA);
}, onChange: state => {
  parentPort.postMessage({ type: 'state', state });
  if (state.job?.kind === 'validate' && state.job.status !== 'running') {
    try { fs.writeFileSync(workerData.cacheFile + '.tmp', JSON.stringify(app.records)); fs.renameSync(workerData.cacheFile + '.tmp', workerData.cacheFile); } catch { /* Cache failure must not invalidate a completed check. */ }
  }
} });
parentPort.on('message', ({ id, kind, data }) => {
  try { const result = app.request(kind, data); parentPort.postMessage({ id, result }); }
  catch (e) { parentPort.postMessage({ id, error: e.message }); }
});
app.changed();
