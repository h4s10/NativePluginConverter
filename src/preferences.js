// SPDX-License-Identifier: MIT
'use strict';
const fs = require('node:fs'), path = require('node:path');
// The rename must preserve the old source/output paths and native check cache.
// Copy only our JSON preferences, never Chromium caches or an existing new profile.
function migratePreferences(destination, appData) {
  for (const name of ['settings.json', 'checks.json']) {
    const target = path.join(destination, name);
    if (fs.existsSync(target)) continue;
    for (const legacy of ['FL Plugin Converter', 'fl-plugin-converter']) {
      try {
        const source = path.join(appData, legacy, name);
        const data = fs.readFileSync(source, 'utf8');
        const parsed = JSON.parse(data);
        if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) continue;
        fs.writeFileSync(target, data, { flag: 'wx' });
        break;
      } catch { /* Missing/invalid preferences or read-only profile: use defaults. */ }
    }
  }
}
module.exports = { migratePreferences };
