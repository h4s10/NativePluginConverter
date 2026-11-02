// SPDX-License-Identifier: MIT
'use strict';
const { contextBridge, ipcRenderer } = require('electron');
const invoke = (kind, data) => ipcRenderer.invoke('converter', kind, data);
contextBridge.exposeInMainWorld('converter', Object.freeze({
  state: () => invoke('state'),
  scan: root => invoke('scan', { root }),
  plan: ids => invoke('plan', { ids }),
  validate: ids => invoke('validate', { ids }),
  build: (ids, output) => invoke('build', { ids, output }),
  pick: purpose => invoke('pick', { purpose }),
  reveal: () => invoke('reveal'),
  cancel: () => invoke('cancel'),
  close: () => invoke('close'),
}));
