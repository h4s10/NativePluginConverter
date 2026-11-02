// SPDX-License-Identifier: MIT
'use strict';
const fs = require('node:fs'), path = require('node:path');
function saveInstallation(root, localAppData) {
  if (!/^[a-z]:[\\/]/i.test(root) || /[\r\n\0]/.test(root)) throw Error('Выберите локальную папку FL Studio на диске Windows.');
  if (!localAppData) throw Error('Не найдена локальная папка профиля Windows.');
  const directory = path.join(localAppData, 'NativePluginConverter');
  fs.mkdirSync(directory, { recursive: true });
  const destination = path.join(directory, 'fl-studio-path.txt'), temp = destination + '.tmp';
  try { fs.writeFileSync(temp, root + '\n', 'utf8'); fs.renameSync(temp, destination); }
  finally { fs.rmSync(temp, { force: true }); }
  return destination;
}
module.exports = { saveInstallation };
