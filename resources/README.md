# Generated native resources

Run `npm run build:native` with Zig 0.14.1 to create `BridgeTemplate.vst3`
and `BridgeProbe.exe` here. They are project-owned bridge binaries, compiled
for Windows x64, and are intentionally not committed.

`compatibility.json` is an empty initial cache. Local runs populate the user's
profile instead. Do not commit machine-specific validation logs or vendor DLLs.
