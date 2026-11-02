// SPDX-License-Identifier: MIT
'use strict';
const test=require('node:test'), assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),os=require('node:os');
const {migratePreferences}=require('../src/preferences');
test('brand migration preserves old preferences without overwriting the new profile',t=>{
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'npc-preferences-'));t.after(()=>fs.rmSync(root,{recursive:true,force:true}));
 const legacy=path.join(root,'FL Plugin Converter'),dest=path.join(root,'NativePluginConverter');fs.mkdirSync(legacy);fs.mkdirSync(dest);
 const settings=JSON.stringify({source:'C:\\FL Studio',output:'D:\\Exports',bounds:{width:1240,height:820}});
 fs.writeFileSync(path.join(legacy,'settings.json'),settings);fs.writeFileSync(path.join(legacy,'checks.json'),'{"plugin":{"status":"passed"}}');
 migratePreferences(dest,root);assert.equal(fs.readFileSync(path.join(dest,'settings.json'),'utf8'),settings);
 assert.equal(JSON.parse(fs.readFileSync(path.join(dest,'checks.json'))).plugin.status,'passed');
 fs.writeFileSync(path.join(dest,'settings.json'),'{"output":"new"}');migratePreferences(dest,root);
 assert.equal(JSON.parse(fs.readFileSync(path.join(dest,'settings.json'))).output,'new');
 assert.equal(fs.readFileSync(path.join(legacy,'settings.json'),'utf8'),settings);
});
test('invalid legacy JSON falls back to prior package-name profile',t=>{
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'npc-preferences-'));t.after(()=>fs.rmSync(root,{recursive:true,force:true}));
 for(const dir of ['FL Plugin Converter','fl-plugin-converter','NativePluginConverter'])fs.mkdirSync(path.join(root,dir));
 fs.writeFileSync(path.join(root,'FL Plugin Converter/settings.json'),'bad json');fs.writeFileSync(path.join(root,'fl-plugin-converter/settings.json'),'{"source":"legacy"}');
 migratePreferences(path.join(root,'NativePluginConverter'),root);
 assert.equal(JSON.parse(fs.readFileSync(path.join(root,'NativePluginConverter/settings.json'))).source,'legacy');
});
