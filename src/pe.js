// SPDX-License-Identifier: MIT
'use strict';
const fs = require('node:fs');
class PE {
  constructor(data) {
    this.data = data;
    if (data.length < 64 || data.subarray(0, 2).toString() !== 'MZ') throw Error('Not a PE image');
    const pe = this.u32(0x3c);
    if (!this.bytes(pe, 4).equals(Buffer.from('PE\0\0'))) throw Error('Invalid PE header');
    this.machine = this.u16(pe + 4);
    const count = this.u16(pe + 6), optsize = this.u16(pe + 20), opt = pe + 24, magic = this.u16(opt);
    if (![0x10b, 0x20b].includes(magic)) throw Error('Unknown PE format');
    this.is64 = magic === 0x20b;
    this.imageBase = this.is64 ? Number(this.bytes(opt + 24, 8).readBigUInt64LE()) : this.u32(opt + 28);
    this.headers = this.u32(opt + 60);
    const directories = opt + (this.is64 ? 112 : 96), ndirs = Math.min(this.u32(opt + (this.is64 ? 108 : 92)), 16);
    this.dirs = [];
    for (let i = 0; i < ndirs; i++) {
      if (directories + i * 8 + 8 > opt + optsize) throw Error('Invalid PE directories');
      this.dirs.push([this.u32(directories + i * 8), this.u32(directories + i * 8 + 4)]);
    }
    if (count > 96) throw Error('Too many PE sections');
    this.sections = [];
    for (let i = 0; i < count; i++) {
      const s = opt + optsize + i * 40; this.bytes(s, 40);
      this.sections.push([this.u32(s + 12), this.u32(s + 8), this.u32(s + 20), this.u32(s + 16)]);
    }
  }
  bytes(p, n) {
    if (!Number.isSafeInteger(p) || p < 0 || n < 0 || p + n > this.data.length) throw Error('Truncated PE image');
    return this.data.subarray(p, p + n);
  }
  u16(p) { return this.bytes(p, 2).readUInt16LE(); }
  u32(p) { return this.bytes(p, 4).readUInt32LE(); }
  offset(rva) {
    if (rva >= 0 && rva < this.headers && rva < this.data.length) return rva;
    for (const [va, virtual, raw, size] of this.sections) {
      if (rva >= va && rva < va + Math.max(virtual, size)) {
        const delta = rva - va;
        if (delta >= size) throw Error('PE RVA points outside file data');
        this.bytes(raw + delta, 1); return raw + delta;
      }
    }
    throw Error('Unmapped PE RVA');
  }
  string(rva) {
    const o = this.offset(rva), end = this.data.indexOf(0, o);
    if (end < 0 || end >= o + 4096) throw Error('Unterminated PE string');
    return this.data.subarray(o, end).toString('latin1');
  }
  imports() {
    const result = [];
    for (const [index, step, nameoffset] of [[1, 20, 12], [13, 32, 4]]) {
      const [rva, size] = this.dirs[index] || [0, 0]; if (!rva) continue;
      const o = this.offset(rva), limit = Math.min(Math.floor(size / step) + 1, 4096);
      let terminated = false;
      for (let i = 0; i < limit; i++) {
        const pos = o + i * step, desc = this.bytes(pos, step);
        if (!desc.some(x => x !== 0)) { terminated = true; break; }
        let name = this.u32(pos + nameoffset);
        if (index === 13 && !(this.u32(pos) & 1)) name -= this.imageBase;
        if (name) result.push([this.string(name), index === 1 ? 'import' : 'delay-import']);
      }
      if (!terminated) throw Error('Unbounded PE import table');
    }
    return result;
  }
  exports() {
    const [rva] = this.dirs[0] || [0]; if (!rva) return [];
    const o = this.offset(rva), count = this.u32(o + 24);
    if (count > 100000) throw Error('Excessive PE export count');
    if (!count) return [];
    const names = this.offset(this.u32(o + 32));
    return Array.from({ length: count }, (_, i) => this.string(this.u32(names + i * 4)));
  }
}
// Decode only bounded printable literals, including unaligned UTF-16 strings.
function literals(data, min = 5, max = 1024) {
  const latin = data.toString('latin1'), result = [];
  for (const m of latin.matchAll(new RegExp(`[\\x20-\\x7e]{${min},${max}}`, 'g'))) result.push(m[0].trim());
  for (const m of latin.matchAll(new RegExp(`(?:[\\x20-\\x7e]\\x00){${min},${max}}`, 'g'))) result.push(m[0].replaceAll('\0', '').trim());
  return result;
}
const cache = new Map();
function inspect(file, details = false) {
  const stat = fs.statSync(file, { bigint: true });
  if (stat.size > 512n * 1024n * 1024n) throw Error('PE image exceeds 512 MiB limit');
  const key = `${file}\0${stat.mtimeNs}\0${stat.size}`;
  let value = cache.get(key);
  if (!value || (details && !value.literals)) {
    const data = fs.readFileSync(file), p = new PE(data);
    value = { machine: p.machine, imports: p.imports(), exports: p.exports() };
    if (details) {
      value.literals = literals(data);
      value.dlls = [...new Set(value.literals.filter(s => /^[A-Za-z0-9_ !()+.,\\/\-]+\.dll$/i.test(s) && !s.startsWith('.') && !s.startsWith('_x64.'))) ].sort((a, b) => a.toLowerCase() < b.toLowerCase() ? -1 : a.toLowerCase() > b.toLowerCase() ? 1 : 0);
      const profiles = require('./profiles.json');
      value.symbols = [['ildsp_x64.dll', profiles.ILDSP_SYMBOLS], ['elastique_x64.dll', profiles.ELASTIQUE_SYMBOLS]].flatMap(([dll, symbols]) => symbols.filter(s => data.includes(Buffer.from(s)) || data.includes(Buffer.from(s, 'utf16le'))).map(s => [dll, s]));
    }
    if (cache.size >= 512) cache.delete(cache.keys().next().value);
    cache.set(key, value);
  }
  return value;
}
module.exports = { PE, inspect, literals };
