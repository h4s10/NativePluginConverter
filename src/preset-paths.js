// SPDX-License-Identifier: MIT
'use strict';
// Harmor's resynthesis sample is a one-byte-sized UTF-8 string at the end of
// each LE32-sized part. A following global byte may be printable, so generic
// binary string scanning alone can mistake e.g. 'sample.wavL' for a filename.
function harmorFactoryPaths(file) {
  if (!Buffer.isBuffer(file) || file.length < 22 || file.length > 256 * 1024 * 1024 || file.toString('ascii', 0, 4) !== 'FLhd' || file.readUInt32LE(4) !== 6 || ![0x20, 0x30].includes(file[8]) || file[9] || file.toString('ascii', 14, 18) !== 'FLdt' || file.readUInt32LE(18) !== file.length - 22) return [];
  let position = 22, count = 0, name, state;
  while (position < file.length) {
    if (++count > 100000) return [];
    const id = file[position++]; let length;
    if (id < 192) length = 1 << Math.floor(id / 64);
    else {
      length = 0; let complete = false;
      for (let i = 0; i < 5; i++) {
        if (position >= file.length) return [];
        const b = file[position++]; if (i === 4 && (b & 0xf0)) return [];
        length += (b & 127) * (2 ** (7 * i));
        if (!(b & 128)) { if (i && !(b & 127)) return []; complete = true; break; }
      }
      if (!complete) return [];
    }
    if (length > file.length - position) return [];
    if (id === 201) {
      if (name !== undefined || !length || file[position + length - 1]) return [];
      const bytes = file.subarray(position, position + length);
      name = bytes.length >= 4 && bytes.length % 2 === 0 && bytes[bytes.length - 2] === 0 ? bytes.toString('utf16le').slice(0, -1) : bytes.toString('utf8').slice(0, -1);
    }
    if (id === 213) { if (state || length > 128 * 1024 * 1024) return []; state = file.subarray(position, position + length); }
    position += length;
  }
  if (name?.toLowerCase() !== 'harmor' || !state || state.length < 16 || ![11, 12, 16, 17, 0x130003].includes(state.readUInt32LE(0)) || state.readUInt32LE(4) !== 7) return [];
  const paths = []; position = 8;
  for (let part = 0; part < 2; part++) {
    if (state.length - position < 4) return [];
    const length = state.readUInt32LE(position); position += 4;
    if (length > state.length - position) return [];
    const end = position + length;
    for (const token of ['%FLStudioData%', '%FLStudioFactoryData%']) {
      const at = state.indexOf(token, position);
      if (at < position || at >= end) continue;
      if (at === position || end - at > 255 || state[at - 1] !== end - at) return [];
      let text; try { text = new TextDecoder('utf-8', { fatal: true }).decode(state.subarray(at, end)); } catch { return []; }
      if (/[\x00-\x1f\x7f]/.test(text)) return [];
      paths.push(text);
    }
    position = end;
  }
  return paths;
}
module.exports = { harmorFactoryPaths };
