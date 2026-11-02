// SPDX-License-Identifier: MIT
'use strict';
// Check the files Git would publish, not ignored local build outputs.
const fs = require('node:fs'), path = require('node:path'), { execFileSync } = require('node:child_process');
const root = path.resolve(__dirname, '..');
const files = [...new Set(execFileSync('git', ['ls-files', '--cached', '--others', '--exclude-standard', '-z'], { cwd: root, encoding: 'utf8' }).split('\0').filter(Boolean))];
const problems = [];
// Only these reviewed captures of our own UI are permitted as binary documentation.
const screenshots = new Set(['docs/images/library.png', 'docs/images/export.png']);
for (const file of files) {
  const full = path.join(root, file); if (!fs.existsSync(full)) continue;
  if (fs.lstatSync(full).isSymbolicLink()) { problems.push(file + ': symlinks are not part of this source distribution'); continue; }
  if (/(^|\/)(node_modules|dist|build|work|verification|Plugins|Shared|Data)(\/|$)|\.(dll|exe|vst3|zip|fst|wav|aif|aiff|mp3|ogg|wv|ttf|otf)$/i.test(file)) problems.push(file + ': generated or vendor asset');
  const content = fs.readFileSync(full);
  if (screenshots.has(file)) {
    const png = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
    if (content.length > 2 * 1024 * 1024 || !content.subarray(0, 8).equals(png) || !content.subarray(-8).equals(Buffer.from([73, 69, 78, 68, 174, 66, 96, 130]))) problems.push(file + ': invalid or oversized documentation PNG');
    continue;
  }
  if (content.length > 2 * 1024 * 1024 || content.includes(0)) problems.push(file + ': unexpected binary or large file');
  const text = content.toString('utf8');
  if (/(?:\/Users\/|\/home\/)[\w.-]+\//.test(text)) problems.push(file + ': personal machine path');
  if (/-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----/.test(text)) problems.push(file + ': private key');
}
if (problems.length) { console.error(problems.join('\n')); process.exitCode = 1; }
else console.log('Source publication check passed: ' + files.length + ' files; no build/vendor payloads or personal paths in text; approved UI screenshots allowed.');
