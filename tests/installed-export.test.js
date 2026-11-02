// SPDX-License-Identifier: MIT
'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),zlib=require('node:zlib');
const {fixture}=require('./fixtures'),core=require('../src/core'),{packagePlugin}=require('../src/package'),{saveInstallation}=require('../src/installation');
function unzip(file){
 const b=fs.readFileSync(file),end=b.lastIndexOf(Buffer.from('504b0506','hex'));assert(end>=0);
 const files=new Map();let off=b.readUInt32LE(end+16);
 for(let n=0;n<b.readUInt16LE(end+10);n++){
  assert.equal(b.readUInt32LE(off),0x02014b50);const method=b.readUInt16LE(off+10),size=b.readUInt32LE(off+20),len=b.readUInt16LE(off+28),extra=b.readUInt16LE(off+30),comment=b.readUInt16LE(off+32),local=b.readUInt32LE(off+42);
  const name=b.subarray(off+46,off+46+len).toString(),start=local+30+b.readUInt16LE(local+26)+b.readUInt16LE(local+28),data=b.subarray(start,start+size);
  files.set(name,method===8?zlib.inflateRawSync(data):data);off+=46+len+extra+comment;
 }
 return files;
}
test('installed export contains only project bridge/notices/config and never vendor bytes or machine paths',async t=>{
 const f=fixture(t);const marker='PRIVATE_VENDOR_ASSET_MUST_NOT_BE_IN_ARCHIVE';f.write(f.dir+'/Artwork/secret.bmp',marker);f.write('Data/Patches/Plugin presets/Effects/Test FX/Factory.fst',marker);
 const plugin=core.discover(f.root)[0],before=fs.readFileSync(path.join(f.root,f.main));
 const result=await packagePlugin(f.root,plugin,path.join(f.base,'out'),f.template,{validation:{status:'passed',log:f.root}});
 const entries=unzip(result.path),prefix='Test FX FL Bridge.vst3/Contents/x86_64-win/';
 assert.equal(entries.size,9);
 assert.deepEqual([...entries.keys()].sort(),[prefix+'Test FX FL Bridge.vst3',prefix+'bridge.ini','LICENSES/NativePluginConverter-MIT.txt','LICENSES/Steinberg-VST3.txt','LICENSES/LLVM-libcxx.txt','LICENSES/mingw-w64.txt','SOURCE.txt','manifest.json','README_RU.txt'].sort());
 for(const [name,data] of entries){assert(!/\.dll$|\.fst$|\.bmp$|\.wav$|\.ttf$/i.test(name));assert(!data.includes(Buffer.from(marker)));assert(!data.includes(Buffer.from(f.root)));}
 const ini=entries.get(prefix+'bridge.ini').toString();assert.match(ini,/source = installed/);assert.match(ini,/dll = Plugins\/Fruity\//);assert.match(ini,/\nroot = \n/);assert(!ini.includes('= FL/'));
 assert.match(entries.get('LICENSES/NativePluginConverter-MIT.txt').toString(),/^MIT License/);
 const manifest=JSON.parse(entries.get('manifest.json'));assert.equal(manifest.mode,'installed');assert.equal(manifest.format,2);assert(!manifest.validation.log);assert.equal(manifest.uid,core.uid(plugin.path));
 assert.deepEqual(fs.readFileSync(path.join(f.root,f.main)),before);
});
test('installed configuration rejects traversal and non-Windows/line-injected overrides',t=>{
 const f=fixture(t),p=core.plan(f.root,core.discover(f.root)[0]);
 assert.match(core.config(p,undefined,'D:\\Audio\\FL Studio'),/root = D:\\Audio\\FL Studio/);
 for(const root of ['/unix/path','relative','C:\\FL\n[plugin]','C:\\FL\0bad'])assert.throws(()=>core.config(p,undefined,root));
 assert.throws(()=>core.config({...p,plugin:{...p.plugin,path:'../outside.dll'}}));
 assert.throws(()=>core.config({...p,dependency_order:['../outside.dll']}));
});
test('selected Windows installation is written only to local user profile',t=>{
 const f=fixture(t),local=path.join(f.base,'profile');
 const file=saveInstallation('D:\\Audio\\FL Studio',local);assert.equal(fs.readFileSync(file,'utf8'),'D:\\Audio\\FL Studio\n');
 saveInstallation('E:\\FL Studio',local);assert.equal(fs.readFileSync(file,'utf8'),'E:\\FL Studio\n');
 assert.throws(()=>saveInstallation('relative',local));assert.throws(()=>saveInstallation('C:\\FL\nextra',local));
 assert.equal(fs.readFileSync(file,'utf8'),'E:\\FL Studio\n');assert(!fs.existsSync(file+'.tmp'));
});
